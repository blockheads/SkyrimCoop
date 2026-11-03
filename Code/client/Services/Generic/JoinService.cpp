#include <Services/JoinService.h>

#include <Events/GameLoadFailedEvent.h>
#include <Messages/DeathMessages.h>

#include <Bridges/IJoinBridge.h>
#include <Events/ConnectedEvent.h>
#include <Events/DisconnectedEvent.h>
#include <Events/GameLoadedEvent.h>
#include <Events/SessionNoticeEvent.h>
#include <Events/UpdateEvent.h>
#include <Messages/AuthenticationResponse.h>
#include <Messages/JoinMessages.h>
#include <Services/Host/JoinRefusal.h>
#include <Services/SaveService.h>
#include <Services/SessionService.h>
#include <Services/SteamService.h>
#include <Services/TransportService.h>
#include <World.h>
#include <Diagnostics/Profile.h>

namespace
{
using Clock = std::chrono::steady_clock;
double Since(Clock::time_point aStart) noexcept
{
    return std::chrono::duration<double, std::milli>(Clock::now() - aStart).count();
}
}

JoinService::JoinService(World& aWorld, entt::dispatcher& aDispatcher, IJoinBridge& aBridge) noexcept
    : m_world(aWorld)
    , m_dispatcher(aDispatcher)
    , m_bridge(aBridge)
{
    Listen<UpdateEvent, &JoinService::OnUpdate>(m_connections, aDispatcher, this);
    Listen<AuthenticationResponse, &JoinService::OnAuthentication>(m_connections, aDispatcher, this);
    Listen<NotifySaveChunk, &JoinService::OnSaveChunk>(m_connections, aDispatcher, this);
    Listen<ConnectedEvent, &JoinService::OnConnected>(m_connections, aDispatcher, this);
    Listen<DisconnectedEvent, &JoinService::OnDisconnected>(m_connections, aDispatcher, this);
    Listen<GameLoadFailedEvent, &JoinService::OnGameLoadFailed>(m_connections, aDispatcher, this);
    Listen<GameLoadedEvent, &JoinService::OnGameLoaded>(m_connections, aDispatcher, this);
    Listen<NotifyPartyReload, &JoinService::OnPartyReload>(m_connections, aDispatcher, this);
}

std::string JoinService::PlayerKey() const noexcept
{
    if (!m_playerKey.empty())
        return m_playerKey;
    if (const auto* pSteam = m_world.ctx().find<SteamService>())
        if (const auto cId = pSteam->JoinedAs())
            return std::format("steam:{}", cId);
    return m_bridge.PlayerId();
}

const JoinStatus& JoinService::GetStatus() const noexcept
{
    return m_status.Current == JoinStatus::State::kIdle ? m_world.GetTransport().GetJoinStatus() : m_status;
}

bool JoinService::Join(const std::string& acEndpoint) noexcept
{
    if (m_creatingCharacter)
        return false;
    // A game hosting alone (a new game or a save of its own) steps down to join.
    if (auto* pSteam = m_world.ctx().find<SteamService>(); pSteam && !IsJoining() && !pSteam->StepDown())
    {
        spdlog::warn("JoinService: no join: friends are in this game's session");
        return false;
    }
    if (IsJoining() || m_world.GetTransport().IsConnected())
    {
        spdlog::warn("JoinService: no join (joining {}, connected {})", IsJoining(), m_world.GetTransport().IsConnected());
        return false;
    }
    m_metrics = {};
    m_metrics.Outcome = "joining";
    m_joinStarted = Clock::now();
    m_partyReload = false;
    m_playerKey.clear();
    m_playerKey = PlayerKey();
    m_fromMainMenu = m_bridge.IsAtMainMenu();
    m_brought = m_createdCharacter;
    if (!m_fromMainMenu && !m_brought)
    {
        // Its quest items belong to its own world's quests.
        m_brought = m_bridge.ReadPlayer();
        m_brought->Knowledge = m_bridge.ReadKnowledge();
        m_brought->Items.RemoveByFilter([](const Inventory::Entry& acEntry) { return acEntry.IsQuestItem; });
    }
    spdlog::info("JoinService: joining {} as {}, {}", acEndpoint, PlayerKey(),
                 m_brought ? std::format("bringing {} (level {})", m_brought->Name, m_brought->Level) : "from the main menu");
    const auto cCacheStart = Clock::now();
    m_cache.Clear();
    if (const auto cPrevious = m_bridge.ReadSave(kJoinSave))
        m_cache.Load(*cPrevious);
    m_metrics.CacheMs = Since(cCacheStart);
    m_metrics.AdvertisedBlocks = m_cache.Hashes().size();
    m_endpoint = acEndpoint;
    m_save = {};
    m_reached = false;
    To(Step::kConnecting, JoinStatus::State::kConnecting);
    if (!m_world.GetTransport().Connect(acEndpoint))
    {
        Fail("unreachable");
        return false;
    }
    return true;
}

void JoinService::Cancel(const std::string& acReason) noexcept
{
    m_bridge.CancelCharacterCreation();
    m_step = Step::kIdle;
    m_playerKey.clear();
    m_save = {};
    m_cache.Clear();
    m_brought.reset();
    m_joinedCharacter.reset();
    m_partyReload = false;
    m_tries = 0;
    m_status = acReason.empty() ? JoinStatus{} : JoinRefusal::Ended(acReason);
}

void JoinService::To(Step aStep, JoinStatus::State aState) noexcept
{
    m_step = aStep;
    m_status = {aState};
    m_waited = 0.0;
}

void JoinService::Fail(const char* acpReason) noexcept
{
    Fail(JoinRefusal::Failed(acpReason));
}

void JoinService::Fail(JoinStatus aStatus) noexcept
{
    m_metrics.Outcome = aStatus.Reason;
    m_metrics.TotalMs = Since(m_joinStarted);
    m_cache.Clear();
    m_save = {};
    spdlog::error("JoinService: couldn't join ({}): {}", aStatus.Reason, aStatus.Message);
    m_bridge.CancelCharacterCreation();
    const bool cConnected = m_world.GetTransport().IsConnected();
    m_step = Step::kIdle;
    m_status = std::move(aStatus);
    if (cConnected)
        m_world.GetTransport().Close();
    m_dispatcher.trigger(SessionNoticeEvent{"Couldn't join: " + m_status.Message});
}

void JoinService::OnUpdate(const UpdateEvent& acEvent) noexcept
{
    PROFILE_ZONE("JoinService::OnUpdate");
    const auto cCreation = m_bridge.PollCharacterCreation();
    using Creation = IJoinBridge::Creation;
    m_creatingCharacter = cCreation == Creation::kStarting || cCreation == Creation::kCustomizing || cCreation == Creation::kFinalizing;
    if (m_step == Step::kCreating)
    {
        if (cCreation == Creation::kComplete)
            return FinishCreation();
        if (cCreation == Creation::kFailed || cCreation == Creation::kIdle)
            return Fail("creation_failed");
        if (cCreation == Creation::kCustomizing)
        {
            m_waited = 0.0; // there is no deadline on choosing a face or a name
            return;
        }
        m_waited += acEvent.Delta;
        if (m_waited > kLoadSeconds)
            Fail("creation_failed");
        return;
    }
    if (m_step == Step::kIdle)
        return;
    m_waited += acEvent.Delta;
    if (m_step == Step::kConnecting && m_world.GetTransport().IsConnected())
    {
        m_reached = true;
        To(Step::kChecking, JoinStatus::State::kChecking);
    }
    switch (m_step)
    {
    case Step::kConnecting:
    case Step::kChecking:
    case Step::kJoining:
        if (m_waited > (m_partyReload ? kLoadSeconds : kConnectSeconds))
            Fail(m_reached ? "lost_connection" : "unreachable");
        break;
    case Step::kReceiving:
        if (m_waited > kChunkSeconds)
            Fail("transfer_failed");
        break;
    case Step::kPlacing:
        if (m_bridge.PlaceBesideHost())
            JoinSession();
        else if (m_waited > kConnectSeconds)
            Fail("placement_failed");
        break;
    case Step::kLoading:
        if (m_waited > kLoadSeconds)
            Fail("transfer_failed");
        break;
    case Step::kReconnectWait:
        if (m_waited > kRetrySeconds)
            Reconnect();
        break;
    case Step::kReconnecting:
        if (m_waited > kReconnectSeconds)
        {
            Retry();
            m_world.GetTransport().Close();
        }
        break;
    default: break;
    }
}

void JoinService::OnAuthentication(const AuthenticationResponse& acResponse) noexcept
{
    using enum AuthenticationResponse::ResponseType;
    if (m_step != Step::kConnecting && m_step != Step::kChecking && m_step != Step::kReceiving && m_step != Step::kJoining)
        return;
    if (acResponse.Type == kNoCharacter && WantsWorld() && m_fromMainMenu && !m_brought)
        return BeginCreation();
    if (acResponse.Type == kWorldFollows && m_step != Step::kJoining)
    {
        spdlog::info("JoinService: the host's gate passed; receiving its save");
        m_reached = true;
        m_worldRequested = Clock::now();
        m_metrics.CheckMs = Since(m_joinStarted) - m_metrics.CacheMs - m_metrics.CreationMs;
        To(Step::kReceiving, JoinStatus::State::kReceivingWorld);
        return;
    }
    if (acResponse.Type != kAccepted)
        Fail(m_world.GetTransport().GetJoinStatus().Current == JoinStatus::State::kRefused ? m_world.GetTransport().GetJoinStatus()
                                                                                        : JoinRefusal::Failed("transfer_failed"));
}

void JoinService::BeginCreation() noexcept
{
    m_creationStarted = Clock::now();
    To(Step::kCreating, JoinStatus::State::kCreatingCharacter);
    m_creatingCharacter = true;
    // The refused transport closes; Steam's identity, lobby and tunnel stay alive.
    m_world.GetTransport().Close();
    if (!m_bridge.BeginCharacterCreation())
    {
        m_creatingCharacter = false;
        return Fail("creation_unavailable");
    }
    spdlog::info("JoinService: creating a character for {} at {}", PlayerKey(), m_endpoint);
}

void JoinService::FinishCreation() noexcept
{
    m_metrics.CreationMs += Since(m_creationStarted);
    m_creatingCharacter = false;
    m_createdCharacter = m_bridge.ReadPlayer();
    m_createdCharacter->Knowledge = m_bridge.ReadKnowledge();
    m_createdCharacter->Items.RemoveByFilter([](const Inventory::Entry& acEntry) { return acEntry.IsQuestItem; });
    m_brought = m_createdCharacter;
    m_fromMainMenu = false;
    m_reached = false;
    m_save = {};
    To(Step::kConnecting, JoinStatus::State::kConnecting);
    spdlog::info("JoinService: {} finished creation; resuming join to {}", m_brought->Name, m_endpoint);
    if (!m_world.GetTransport().Connect(m_endpoint))
        Fail("unreachable");
}

void JoinService::CreationHostLost() noexcept
{
    if (m_step == Step::kCreating)
        Fail("lost_connection");
}

void JoinService::OnSaveChunk(const NotifySaveChunk& acChunk) noexcept
{
    PROFILE_ZONE("Receive world block");
    if (m_step != Step::kReceiving)
        return;
    if (m_metrics.Chunks == 0)
    {
        m_metrics.WaitForWorldMs = Since(m_worldRequested);
        m_transferStarted = Clock::now();
    }
    const auto cDecodeStart = Clock::now();
    if (!m_save.Add(acChunk, &m_cache))
        return Fail("transfer_failed");
    m_metrics.DecodeMs += Since(cDecodeStart);
    ++m_metrics.Chunks;
    m_metrics.WorldBytes = acChunk.Total;
    m_metrics.PayloadBytes += acChunk.Data.size();
    if (acChunk.Data.empty())
    {
        ++m_metrics.CachedChunks;
        m_metrics.ReusedBytes += acChunk.RawSize;
    }
    m_waited = 0.0;
    m_status.Progress = m_save.Progress();
    if (!m_save.Complete())
        return;

    const auto cUnpackStart = Clock::now();
    auto files = SaveTransfer::Unpack(m_save.Data());
    m_metrics.DecodeMs += Since(cUnpackStart);
    m_metrics.TransferMs = std::max(0.0, Since(m_transferStarted) - m_metrics.DecodeMs);
    spdlog::info("JoinService: the host's save is here ({} bytes packed, {} and {} unpacked)", m_save.Data().size(),
                 files ? files->Game.size() : 0, files ? files->CoSave.size() : 0);
    m_save = {};
    m_cache.Clear();
    if (!files)
        return Fail("transfer_failed");
    // Our connection goes while the save loads; the join after it is a new one.
    To(Step::kLoading, JoinStatus::State::kLoading);
    m_world.GetTransport().Close();
    m_loadStarted = Clock::now();
    const bool cWritten = m_bridge.WriteAndLoad(kJoinSave, *files);
    m_metrics.WriteMs = Since(m_loadStarted);
    m_loadStarted = Clock::now();
    if (!cWritten)
        Fail("transfer_failed");
}

void JoinService::OnGameLoaded(const GameLoadedEvent&) noexcept
{
    if (m_step == Step::kLoading)
    {
        m_metrics.LoadMs = Since(m_loadStarted);
        Arrive();
    }
}

void JoinService::Arrive() noexcept
{
    const std::string cKey = PlayerKey();
    const PlayerRecord* cpKept = m_world.ctx().at<SaveService>().GetRecords().Find(cKey);
    const PlayerRecord* cpCharacter = cpKept ? cpKept : (m_brought ? &*m_brought : nullptr);
    if (!cpCharacter)
        return Fail("no_character");
    spdlog::info("JoinService: {} plays {} (level {}), {}", cKey, cpCharacter->Name, cpCharacter->Level,
                 cpKept ? "kept in the host's save" : "brought from its own game");
    PlayerRecord character = *cpCharacter;
    m_world.ctx().at<SaveService>().GetRecords().ApplyStorySpells(character);
    const auto cApplyStart = Clock::now();
    m_bridge.ApplyPlayer(character);
    m_bridge.ApplyKnowledge(character.Knowledge);
    m_metrics.ApplyMs = Since(cApplyStart);
    m_brought.reset();
    // A new character, or a kept one without a place (it would stand on the save's player, the host's own spot).
    if (!cpKept || !character.HasPlace())
    {
        To(Step::kPlacing, JoinStatus::State::kCreatingCharacter);
        return;
    }
    JoinSession();
}

void JoinService::JoinSession() noexcept
{
    To(Step::kJoining, JoinStatus::State::kConnecting);
    m_reached = false;
    if (!m_world.GetTransport().Connect(m_endpoint))
        Fail("unreachable");
}

void JoinService::OnConnected(const ConnectedEvent&) noexcept
{
    m_createdCharacter.reset();
    m_joinedCharacter = m_bridge.ReadPlayer();
    m_joinedCharacter->Knowledge = m_bridge.ReadKnowledge();
    m_partyReload = false;
    if (m_step == Step::kReconnecting)
    {
        spdlog::info("JoinService: reconnected (try {})", m_tries);
        m_step = Step::kIdle;
        m_status = {};
        m_dispatcher.trigger(SessionNoticeEvent{"Reconnected to the host."});
        return;
    }
    // A new session: how the last one ended is over.
    if (m_step == Step::kIdle)
        m_status = {};
    if (m_step != Step::kJoining)
        return;
    m_metrics.Outcome = "joined";
    m_metrics.TotalMs = Since(m_joinStarted);
    spdlog::info("JoinService: joined in {:.0f} ms: cache {:.0f}, wait {:.0f}, transfer {:.0f}, decode {:.0f}, write {:.0f}, load {:.0f}, character {:.0f}; {} payload bytes, {} raw bytes reused",
                 m_metrics.TotalMs, m_metrics.CacheMs, m_metrics.WaitForWorldMs, m_metrics.TransferMs,
                 m_metrics.DecodeMs, m_metrics.WriteMs, m_metrics.LoadMs, m_metrics.ApplyMs,
                 m_metrics.PayloadBytes, m_metrics.ReusedBytes);
    m_step = Step::kIdle;
    m_status = {};
}

void JoinService::OnDisconnected(const DisconnectedEvent& acEvent) noexcept
{
    using enum DisconnectedEvent::Cause;
    if (m_world.GetSessionService().IsEnding())
        return;
    if (m_step == Step::kIdle)
    {
        if (acEvent.WasGuest && acEvent.Why == kHostLeft)
            m_world.GetSessionService().End("host_left");
        else if (acEvent.WasGuest && acEvent.Why == kKicked)
        {
            m_status = JoinRefusal::Ended("kicked");
            m_dispatcher.trigger(SessionNoticeEvent{m_status.Message});
            m_world.GetSessionService().End("kicked");
        }
        else if (acEvent.WasGuest && acEvent.Why == kLost)
            StartReconnect();
        return;
    }
    // The refusal's connection is deliberately closed before the bundled start runs.
    if (m_step == Step::kCreating && acEvent.Why == kLeft)
        return;
    // Loading runs with no connection (ours closed on purpose); between tries there is none.
    if (m_step == Step::kLoading || m_step == Step::kReconnectWait)
        return;
    const JoinStatus& cTransport = m_world.GetTransport().GetJoinStatus();
    if (cTransport.Current == JoinStatus::State::kRefused)
        return Fail(cTransport);
    if (m_step == Step::kReconnecting)
        return Retry();
    Fail(m_reached || m_step == Step::kReceiving ? "lost_connection" : "unreachable");
}

void JoinService::StartReconnect() noexcept
{
    m_endpoint = m_world.GetTransport().GetEndpoint();
    m_brought.reset();
    m_tries = 0;
    spdlog::warn("JoinService: lost the host; reconnecting to {}", m_endpoint);
    m_dispatcher.trigger(SessionNoticeEvent{"Lost connection to the host. Reconnecting..."});
    To(Step::kReconnectWait, JoinStatus::State::kReconnecting);
}

void JoinService::Reconnect() noexcept
{
    ++m_tries;
    To(Step::kReconnecting, JoinStatus::State::kReconnecting);
    spdlog::info("JoinService: reconnecting, try {} of {}", m_tries, kReconnectTries);
    // A Steam session needs its tunnel again first; it dials through Redial.
    if (auto* pSteam = m_world.ctx().find<SteamService>(); pSteam && pSteam->Rejoin())
        return;
    if (!m_world.GetTransport().Connect(m_endpoint))
        Retry();
}

bool JoinService::Redial(const std::string& acEndpoint) noexcept
{
    return m_step == Step::kReconnecting && m_world.GetTransport().Connect(acEndpoint);
}

void JoinService::Retry() noexcept
{
    if (m_tries < kReconnectTries)
        return To(Step::kReconnectWait, JoinStatus::State::kReconnecting);
    spdlog::error("JoinService: couldn't reconnect after {} tries", m_tries);
    if (auto* pSteam = m_world.ctx().find<SteamService>())
        pSteam->EndRejoin();
    m_step = Step::kIdle;
    m_status = JoinRefusal::Ended("lost_connection");
    m_dispatcher.trigger(SessionNoticeEvent{"Couldn't reconnect to the host."});
    m_world.GetSessionService().End("lost_connection");
}

void JoinService::OnPartyReload(const NotifyPartyReload&) noexcept
{
    m_metrics = {};
    m_metrics.Outcome = "party_reload";
    m_joinStarted = Clock::now();
    m_worldRequested = m_joinStarted;
    m_partyReload = true;
    m_endpoint = m_world.GetTransport().GetEndpoint();
    if (!m_brought)
        m_brought = m_joinedCharacter;
    m_save = {};
    To(Step::kReceiving, JoinStatus::State::kReceivingWorld);
}

void JoinService::OnGameLoadFailed(const GameLoadFailedEvent& acEvent) noexcept
{
    if (m_step == Step::kLoading && acEvent.Name == kJoinSave)
        Fail("transfer_failed");
}
