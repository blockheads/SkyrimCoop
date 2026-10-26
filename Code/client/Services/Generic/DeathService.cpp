#include <Services/DeathService.h>

#include <Bridges/IActorBridge.h>
#include <Bridges/IPlayerBridge.h>
#include <Components.h>
#include <Events/ActorDeathChangeEvent.h>
#include <Events/ActorRemovedEvent.h>
#include <Events/AiStateChangeEvent.h>
#include <Events/ConnectedEvent.h>
#include <Events/DisconnectedEvent.h>
#include <Events/PlayerArrivedEvent.h>
#include <Events/ReferenceLoadedEvent.h>
#include <Events/UpdateEvent.h>
#include <Messages/NotifyCharacterSpawn.h>
#include <Messages/NotifyPartyTravel.h>
#include <Messages/NotifySettingsChange.h>
#include <Services/SessionService.h>
#include <Services/TransportService.h>
#include <Services/TravelService.h>
#include <SessionIds.h>
#include <World.h>

#include <algorithm>
#include <cmath>

DeathService::DeathService(World& aWorld, entt::dispatcher& aDispatcher, TransportService& aTransport, IPlayerBridge& aPlayerBridge,
                           IActorBridge& aActorBridge) noexcept
    : m_world(aWorld)
    , m_dispatcher(aDispatcher)
    , m_transport(aTransport)
    , m_playerBridge(aPlayerBridge)
    , m_actorBridge(aActorBridge)
{
    Listen<UpdateEvent, &DeathService::OnUpdate>(m_connections, m_dispatcher, this);
    Listen<ConnectedEvent, &DeathService::OnConnected>(m_connections, m_dispatcher, this);
    Listen<DisconnectedEvent, &DeathService::OnDisconnected>(m_connections, m_dispatcher, this);
    Listen<NotifySettingsChange, &DeathService::OnSettings>(m_connections, m_dispatcher, this);
    Listen<ActorDeathChangeEvent, &DeathService::OnPlayerDeath>(m_connections, m_dispatcher, this);
    Listen<NotifyPlayerRespawn, &DeathService::OnRespawn>(m_connections, m_dispatcher, this);
    Listen<NotifyPartyTravel, &DeathService::OnPartyTravel>(m_connections, m_dispatcher, this);
    Listen<PlayerArrivedEvent, &DeathService::OnRespawnArrival>(m_connections, m_dispatcher, this);
    Listen<NotifyPartyDeath, &DeathService::OnPartyDeath>(m_connections, m_dispatcher, this);
    Listen<SpectateCycleEvent, &DeathService::OnCycle>(m_connections, m_dispatcher, this);
    Listen<ReferenceLoadedEvent, &DeathService::OnReferenceLoaded>(m_connections, m_dispatcher, this);
    Listen<ActorRemovedEvent, &DeathService::OnRemoved>(m_connections, m_dispatcher, this);
    Listen<NotifyCharacterSpawn, &DeathService::OnCopySpawn>(m_connections, m_dispatcher, this);
    Listen<SpectatorRefreshEvent, &DeathService::OnScreenChanged>(m_connections, m_dispatcher, this);
}

DeathService::~DeathService() noexcept
{
    StandUp();
    m_playerBridge.HoldPlayerDeathReload(false);
}

void DeathService::StandUp() noexcept
{
    m_down = m_dying = false;
    m_spectating = m_shown = 0;
    m_playerBridge.SetSpectatorTarget(0);
    m_playerBridge.HoldDeathControls(false);
}

void DeathService::OnUpdate(const UpdateEvent& acEvent) noexcept
{
    if (!m_transport.IsOnline())
        return;
    // Once its character has a session id: the host learns its death state (and combat state, for the recovery timer),
    // and a player already down when it joined goes down here too.
    if (!m_announced && Utils::GetSessionIdByFormId(0x14))
    {
        m_announced = true;
        SendDeathState();
        m_dispatcher.trigger(AiStateChangeEvent{0x14});
        if (!m_playerBridge.IsPlayerAlive())
            GoDown(false);
    }
    m_remaining = std::max(0.0, m_remaining - acEvent.Delta);
}

void DeathService::OnConnected(const ConnectedEvent& acEvent) noexcept
{
    m_characterId = acEvent.CharacterId;
    m_announced = false;
}

void DeathService::OnSettings(const NotifySettingsChange&) noexcept
{
    // In a session a dead player waits for the party's respawn or wipe instead of loading the last save.
    m_playerBridge.HoldPlayerDeathReload(true);
}

void DeathService::OnDisconnected(const DisconnectedEvent&) noexcept
{
    StandUp();
    m_playerBridge.HoldPlayerDeathReload(false);
    m_characterId = m_generation = 0;
    m_announced = false;
    m_respawn.reset();
    m_party = {};
    m_remaining = 0.0;
}

void DeathService::OnPlayerDeath(const ActorDeathChangeEvent& acEvent) noexcept
{
    if (acEvent.FormId != 0x14 || acEvent.Resurrected)
        return;
    if (!m_down)
        GoDown(!acEvent.Final);
    else if (acEvent.Final && m_dying)
    {
        // Dead: the first standing player is watched from here.
        m_dying = false;
        ChooseSpectator();
    }
}

void DeathService::GoDown(bool aDying) noexcept
{
    if (!m_transport.IsOnline() || m_down || m_party.Wiped)
        return;
    m_down = true;
    ++m_generation;
    m_dying = aDying;
    m_playerBridge.HoldDeathControls(true);
    SendDeathState();
    ChooseSpectator();
}

void DeathService::OnRespawn(const NotifyPlayerRespawn& acMessage) noexcept
{
    if (!m_down || m_party.Wiped || acMessage.SessionId != m_characterId || acMessage.Generation != m_generation)
        return;
    auto& travel = m_world.GetTravelService();
    if (!travel.CellAt(acMessage.WorldSpaceId, acMessage.CellId, acMessage.Position))
        return;
    m_respawn = acMessage;
    travel.FollowTo(acMessage.WorldSpaceId, acMessage.CellId, acMessage.Position, acMessage.Heading, true);
}

void DeathService::OnPartyTravel(const NotifyPartyTravel& acMessage) noexcept
{
    // The party moved on before this game's player got there: it follows them there instead.
    if (!m_respawn)
        return;
    m_respawn->WorldSpaceId = acMessage.WorldSpaceId;
    m_respawn->CellId = acMessage.CellId;
    m_respawn->Position = acMessage.Position;
    m_respawn->Heading = acMessage.Heading;
}

void DeathService::OnRespawnArrival(const PlayerArrivedEvent& acEvent) noexcept
{
    if (!m_down || !m_respawn || m_party.Wiped || acEvent.How != EnterCellRequest::Travel::kTether)
        return;
    if (!m_world.GetTravelService().IsAroundPlayer(m_respawn->WorldSpaceId, m_respawn->CellId, m_respawn->Position))
        return;
    m_respawn.reset();
    m_playerBridge.RevivePlayer();
    StandUp();
    m_dispatcher.trigger(PlayerRevivedEvent{});
    SendDeathState();
}

void DeathService::SendDeathState() const noexcept
{
    if (!m_transport.IsOnline() || !m_characterId)
        return;
    PlayerDeathRequest request;
    request.SessionId = m_characterId;
    request.Generation = m_generation;
    request.Down = m_down;
    m_transport.Send(request);
}

void DeathService::OnPartyDeath(const NotifyPartyDeath& acMessage) noexcept
{
    m_party = acMessage;
    m_remaining = acMessage.RemainingMs / 1000.0;
    if (acMessage.Wiped)
    {
        m_spectating = m_shown = 0;
        m_playerBridge.SetSpectatorTarget(0);
    }
    ChooseSpectator();
}

void DeathService::OnCycle(const SpectateCycleEvent& acEvent) noexcept
{
    if (m_down && !m_dying && !m_playerBridge.IsScriptCameraHeld())
        ChooseSpectator(acEvent.Direction);
}

void DeathService::ChooseSpectator(int aDirection) noexcept
{
    if (!m_down || m_dying || m_party.Wiped)
        return;
    // The standing players whose copies are here, in session id order.
    std::vector<uint32_t> standing;
    for (const auto& cPlayer : m_party.Players)
    {
        const uint32_t cFormId = cPlayer.Down || cPlayer.SessionId == m_characterId ? 0 : Utils::GetFormIdBySessionId(cPlayer.SessionId);
        if (cFormId && m_actorBridge.ActorExists(cFormId) && m_actorBridge.ActorHas3D(cFormId))
            standing.push_back(cPlayer.SessionId);
    }
    std::ranges::sort(standing);
    uint32_t target = 0;
    if (!standing.empty())
    {
        const auto cAt = std::ranges::find(standing, m_spectating);
        const size_t cCount = standing.size();
        target = cAt == standing.end() ? standing.front()
                                       : standing[(static_cast<size_t>(cAt - standing.begin()) + cCount + aDirection) % cCount];
    }
    // A script's camera (a cutscene) wins: the choice is kept, shown once it gives the camera back.
    // A replaced copy keeps its session id under a new form id.
    const bool cSuspended = m_playerBridge.IsScriptCameraHeld();
    const uint32_t cShown = !cSuspended && target ? Utils::GetFormIdBySessionId(target) : 0;
    if (target != m_spectating || cShown != m_shown)
    {
        m_spectating = target;
        m_shown = cShown;
        m_playerBridge.SetSpectatorTarget(cShown);
    }
}

void DeathService::QueueRefresh() noexcept
{
    if (m_refreshQueued)
        return;
    m_refreshQueued = true;
    m_world.GetRunner().Queue([this] {
        m_refreshQueued = false;
        ChooseSpectator();
    });
}

std::string DeathService::SpectatingName() const noexcept
{
    const auto& cPlayers = m_world.GetSessionService().GetPlayers();
    for (auto&& [cEntity, cCopy, cPlayer] : m_world.view<RemoteComponent, PlayerComponent>().each())
        if (cCopy.Id == m_spectating)
            if (const auto it = cPlayers.find(cPlayer.Id); it != cPlayers.end())
                return it->second.Name;
    return {};
}

std::string DeathService::Status() const noexcept
{
    if (!m_party.Error.empty())
        return m_party.Error;
    if (m_party.Wiped)
        return "The party has fallen. Loading the previous save...";
    if (m_dying)
        return "You have fallen.";
    if (!m_spectating)
        return "Waiting for a player to spectate";
    if (m_party.Combat)
        return "Party in combat";
    return std::format("Respawning in {}s", static_cast<int>(std::ceil(m_remaining)));
}
