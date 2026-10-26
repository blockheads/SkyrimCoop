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
#include <Services/SteamService.h>
#include <Services/TransportService.h>
#include <World.h>

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
    m_partyReload = false;
    m_brought.reset();
    if (!m_bridge.IsAtMainMenu())
    {
        // Its quest items belong to its own world's quests.
        m_brought = m_bridge.ReadPlayer();
        m_brought->Items.RemoveByFilter([](const Inventory::Entry& acEntry) { return acEntry.IsQuestItem; });
    }
    spdlog::info("JoinService: joining {} as {}, {}", acEndpoint, PlayerKey(),
                 m_brought ? std::format("bringing {} (level {})", m_brought->Name, m_brought->Level) : "from the main menu");
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
    spdlog::error("JoinService: couldn't join ({}): {}", aStatus.Reason, aStatus.Message);
    const bool cConnected = m_world.GetTransport().IsConnected();
    m_step = Step::kIdle;
    m_status = std::move(aStatus);
    if (cConnected)
        m_world.GetTransport().Close();
    m_dispatcher.trigger(SessionNoticeEvent{"Couldn't join: " + m_status.Message});
}

void JoinService::OnUpdate(const UpdateEvent& acEvent) noexcept
{
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
    if (acResponse.Type == kWorldFollows && m_step != Step::kJoining)
    {
        spdlog::info("JoinService: the host's gate passed; receiving its save");
        m_reached = true;
        To(Step::kReceiving, JoinStatus::State::kReceivingWorld);
        return;
    }
    if (acResponse.Type != kAccepted)
        Fail(m_world.GetTransport().GetJoinStatus().Current == JoinStatus::State::kRefused ? m_world.GetTransport().GetJoinStatus()
                                                                                        : JoinRefusal::Failed("transfer_failed"));
}

void JoinService::OnSaveChunk(const NotifySaveChunk& acChunk) noexcept
{
    if (m_step != Step::kReceiving)
        return;
    if (!m_save.Add(acChunk))
        return Fail("transfer_failed");
    m_waited = 0.0;
    m_status.Progress = m_save.Progress();
    if (!m_save.Complete())
        return;

    auto files = SaveTransfer::Unpack(m_save.Data());
    spdlog::info("JoinService: the host's save is here ({} bytes packed, {} and {} unpacked)", m_save.Data().size(),
                 files ? files->Game.size() : 0, files ? files->CoSave.size() : 0);
    m_save = {};
    if (!files)
        return Fail("transfer_failed");
    // Our connection goes while the save loads; the join after it is a new one.
    To(Step::kLoading, JoinStatus::State::kLoading);
    m_world.GetTransport().Close();
    if (!m_bridge.WriteAndLoad(kJoinSave, *files))
        Fail("transfer_failed");
}

void JoinService::OnGameLoaded(const GameLoadedEvent&) noexcept
{
    if (m_step == Step::kLoading)
        Arrive();
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
    m_bridge.ApplyPlayer(*cpCharacter);
    m_brought.reset();

    To(Step::kJoining, JoinStatus::State::kConnecting);
    m_reached = false;
    if (!m_world.GetTransport().Connect(m_endpoint))
        Fail("unreachable");
}

void JoinService::OnConnected(const ConnectedEvent&) noexcept
{
    m_joinedCharacter = m_bridge.ReadPlayer();
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
    spdlog::info("JoinService: joined");
    m_step = Step::kIdle;
    m_status = {};
}

void JoinService::OnDisconnected(const DisconnectedEvent& acEvent) noexcept
{
    using enum DisconnectedEvent::Cause;
    if (m_step == Step::kIdle)
    {
        if (acEvent.WasGuest && acEvent.Why == kKicked)
        {
            m_status = JoinRefusal::Ended("kicked");
            m_dispatcher.trigger(SessionNoticeEvent{m_status.Message});
        }
        else if (acEvent.WasGuest && acEvent.Why == kLost)
            StartReconnect();
        return;
    }
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
}

void JoinService::OnPartyReload(const NotifyPartyReload&) noexcept
{
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
