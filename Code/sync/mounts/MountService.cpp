// Riding, boarding and getting off.
#include <mounts/MountService.h>
#include <characters/CharacterService.h>
#include <characters/ClaimService.h>
#include <core/TransportService.h>

#include <core/FormIdComponent.h>
#include <core/LocalComponent.h>
#include <core/RemoteComponent.h>
#include <core/PlayerComponent.h>

#include <mounts/MountEvent.h>
#include <mounts/DismountEvent.h>
#include <mounts/BoardEvent.h>

#include <core/CopyRecords.h>
#include <Structs/RidePart.h>
#include <animation/ActorStateRules.h>

#include <ports/IActorBridge.h>
#include <ports/IFormBridge.h>
#include <ports/IPlayerBridge.h>


#include <core/World.h>
#include <core/SessionIds.h>
#include <core/FormIds.h>

#include <Messages/Relayed.h>
#include <animation/GraphStream.h>
#include <characters/CharacterInternal.h>

MountService::MountService(World& aWorld, entt::dispatcher& aDispatcher, TransportService& aTransport, IActorBridge& aActorBridge,
                           IPlayerBridge& aPlayerBridge, CharacterService& aCharacters) noexcept
    : m_world(aWorld)
    , m_transport(aTransport)
    , m_actorBridge(aActorBridge)
    , m_playerBridge(aPlayerBridge)
    , m_characters(aCharacters)
{
    m_world.ctx().at<CopyRecords>().Register(
        RecordKind::kRide, [this](uint32_t aId, const std::string& acBytes) { return ApplyRide(aId, acBytes); },
        [this](uint32_t aFormId) -> std::optional<std::string> {
            if (aFormId != m_playerBridge.GetPlayerFormId() || !m_boardedMount)
                return std::nullopt;
            return ToPartBytes(RidePart{m_world.GetFrameTick(), m_boardedMount});
        });
    Listen<MountEvent, &MountService::OnMountEvent>(m_connections, aDispatcher, this);
    Listen<NotifyMount, &MountService::OnNotifyMount>(m_connections, aDispatcher, this);
    Listen<NotifyMountRefused, &MountService::OnNotifyMountRefused>(m_connections, aDispatcher, this);
    Listen<NotifyClaimRefused, &MountService::OnRideRefused>(m_connections, aDispatcher, this);
    Listen<BoardEvent, &MountService::OnBoardEvent>(m_connections, aDispatcher, this);
    Listen<NotifyBoard, &MountService::OnNotifyBoard>(m_connections, aDispatcher, this);
    Listen<DismountEvent, &MountService::OnDismountEvent>(m_connections, aDispatcher, this);
    Listen<NotifyDismount, &MountService::OnNotifyDismount>(m_connections, aDispatcher, this);
    Listen<NotifyUnboard, &MountService::OnNotifyUnboard>(m_connections, aDispatcher, this);
}

bool MountService::IsPartyMount(uint32_t aSessionId) const noexcept
{
    return m_partyMounts.contains(aSessionId);
}

void MountService::ApplyDueRides() noexcept
{
    // Each due part waits again in ApplyRide; one whose record went is dropped.
    for (const uint32_t cId : std::exchange(m_dueRides, {}))
        m_world.ctx().at<CopyRecords>().ApplyAgain(cId, RecordKind::kRide);
}

void MountService::ReleaseAll() noexcept
{
    const auto cMounts = m_mountPersistence;
    for (const auto& [cFormId, cPersistent] : cMounts)
        ReleaseMount(cFormId);
}

void MountService::Clear() noexcept
{
    m_riddenMount = 0;
    m_partyMounts.clear();
    m_pendingRides.clear();
    m_boardedMount = 0;
    m_boardingMount = 0;
    m_dueRides.clear();
    m_drivers.clear();
}

// A late joiner hears who rides what right after the spawns, which can still be loading.
void MountService::LinkPendingRides(uint32_t aSessionId) noexcept
{
    std::vector<NotifyMount> rides;
    for (const auto& [cMount, cRider] : m_pendingRides)
        if (cMount == aSessionId || cRider == aSessionId)
        {
            auto& ride = rides.emplace_back();
            ride.MountId = cMount;
            ride.RiderId = cRider;
        }
    for (const auto& cRide : rides)
        OnNotifyMount(cRide);
}

void MountService::OnMountEvent(const MountEvent& acEvent) noexcept
{
    const auto& actors = m_world.GetActorIndex();
    const auto cRiderEntity = actors.ByForm(acEvent.RiderID);
    if (cRiderEntity == entt::null)
    {
        spdlog::warn("Rider not found, form id: {:X}", acEvent.RiderID);
        return;
    }

    // Ours only: a copy getting on its mount replays its owner's.
    const auto* cpRider = m_world.try_get<LocalComponent>(cRiderEntity);
    if (!cpRider)
        return;

    const entt::entity cMountEntity = actors.ByForm(acEvent.MountID);
    if (cMountEntity == entt::null)
    {
        spdlog::warn("Mount not found, form id: {:X}", acEvent.MountID);
        return;
    }

    std::optional<uint32_t> mountSessionIdRes = Utils::GetSessionId(cMountEntity);
    if (!mountSessionIdRes.has_value())
    {
        spdlog::error("{}: failed to find session id", __FUNCTION__);
        return;
    }

    // A ride is a claim on its horse (HostCharacters' rule): granted, it is ours to simulate (ClaimService).
    const uint32_t cMountId = mountSessionIdRes.value();
    ClaimRequest request;
    request.Kind = Payloads::Claim::kRide;
    request.ForId = cpRider->Id;
    request.Members = {{cMountId, true, false}};
    if (acEvent.RiderID == m_playerBridge.GetPlayerFormId())
    {
        m_riddenMount = cMountId;
        m_partyMounts.insert(cMountId);
        m_drivers[cMountId] = cpRider->Id;
        KeepMount(acEvent.MountID);
    }
    m_transport.Send(request);
}

void MountService::OnNotifyMount(const NotifyMount& acMessage) noexcept
{
    m_pendingRides.erase(acMessage.MountId);
    m_drivers[acMessage.MountId] = acMessage.RiderId;
    if (m_world.GetActorIndex().BySession<PlayerComponent>(acMessage.RiderId) != entt::null)
        m_partyMounts.insert(acMessage.MountId);
    // Our player got on it: the host's claim gave the mount to this game (ClaimService, just before). Not asked for: a
    // handover (HostCharacters::HandOver).
    const bool cHandedOver = m_riddenMount != acMessage.MountId;
    const auto& actors = m_world.GetActorIndex();
    if (actors.BySession<LocalComponent>(acMessage.RiderId) != entt::null)
    {
        const auto cMount = actors.BySession<LocalComponent, FormIdComponent>(acMessage.MountId);
        if (cMount != entt::null && m_world.ctx().at<ClaimService>().Holds(acMessage.MountId))
        {
            m_riddenMount = acMessage.MountId;
            KeepMount(m_world.get<FormIdComponent>(cMount).Id);
            // A passenger the driver left: ours to steer. Off it already (its ride's end still on its way, or never
            // heard): handed back at once. Our own ride's answer can come before the get-on links our player.
            const uint32_t cHorse = m_world.get<FormIdComponent>(cMount).Id;
            if (m_boardedMount)
                LeaveBoard();
            if (cHandedOver)
            {
                if (m_actorBridge.GetActorMount(m_playerBridge.GetPlayerFormId()) == cHorse)
                    m_playerBridge.PlayerTakeReins(cHorse);
                else
                    OnDismountEvent(DismountEvent{m_playerBridge.GetPlayerFormId(), cHorse});
            }
        }
        return;
    }

    const auto cRider = actors.BySession<RemoteComponent, FormIdComponent>(acMessage.RiderId);
    if (cRider == entt::null)
    {
        spdlog::info("Rider {:X} of {:X} has no copy yet", acMessage.RiderId, acMessage.MountId);
        m_pendingRides[acMessage.MountId] = acMessage.RiderId;
        return;
    }

    const uint32_t cRiderFormId = m_world.get<FormIdComponent>(cRider).Id;

    uint32_t mountFormId = 0;
    if (const auto cMount = actors.BySession<FormIdComponent>(acMessage.MountId); cMount != entt::null)
    {
        mountFormId = m_world.get<FormIdComponent>(cMount).Id;
        if (m_partyMounts.contains(acMessage.MountId))
            KeepMount(mountFormId);
    }

    spdlog::info("Ride {:X} on {:X}: its copy {:X} on {:X}, sit {}", acMessage.RiderId, acMessage.MountId, cRiderFormId, mountFormId,
                 m_actorBridge.GetActorSitState(cRiderFormId));
    if (!mountFormId)
        m_pendingRides[acMessage.MountId] = acMessage.RiderId;
    else if (cRiderFormId && m_actorBridge.GetActorMount(cRiderFormId) == mountFormId)
        m_actorBridge.ActorHoldMount(cRiderFormId, mountFormId); // a passenger driving now, seated already
    else if (cRiderFormId)
        m_actorBridge.ActorPutOnMount(cRiderFormId, mountFormId);
}

// The ride of one of this game's actors ended (any path): the host takes its mount back.
// A boarding refused (HostCharacters::OnBoard), or a ride (OnRideRefused): ours gets off, the horse stays a copy here.
void MountService::OnRideRefused(const NotifyClaimRefused& acMessage) noexcept
{
    if (acMessage.Kind != Payloads::Claim::kRide || acMessage.Members.empty() || acMessage.ForId != m_characters.CharacterId())
        return;
    NotifyMountRefused refused;
    refused.RiderId = acMessage.ForId;
    refused.MountId = acMessage.Members[0].ActorId;
    OnNotifyMountRefused(refused);
}

void MountService::OnNotifyMountRefused(const NotifyMountRefused& acMessage) noexcept
{
    spdlog::info("Ride on {:X} refused (riding {:X}, aboard {:X}): our player gets off", acMessage.MountId, m_riddenMount, m_boardedMount);
    if (m_boardingMount == acMessage.MountId)
    {
        m_boardingMount = 0; // asked only: never seated
        return;
    }
    if (m_riddenMount == acMessage.MountId)
        m_riddenMount = 0;
    else if (m_boardedMount == acMessage.MountId)
        LeaveBoard();
    else
        return;
    m_playerBridge.PlayerStopRiding();
}

void MountService::OnBoardEvent(const BoardEvent& acEvent) noexcept
{
    if (acEvent.RiderId != m_playerBridge.GetPlayerFormId() || m_riddenMount || m_boardedMount || m_boardingMount || !m_characters.CharacterId())
    {
        spdlog::info("Board {:X}: not now (riding {:X}, aboard {:X})", acEvent.MountId, m_riddenMount, m_boardedMount);
        return;
    }
    if (const auto cEntity = m_world.GetActorIndex().ByForm<RemoteComponent>(acEvent.MountId); cEntity != entt::null)
    {
        const uint32_t cMount = m_world.get<RemoteComponent>(cEntity).Id;
        // Only a horse a party player drives: an NPC's stays refused, as in Skyrim (37864).
        if (!m_drivers.contains(cMount) || !m_partyMounts.contains(cMount))
        {
            spdlog::info("Board {:X}: no party player drives it", acEvent.MountId);
            return;
        }
        // Seated once the host says yes (NotifyBoard): nothing to undo on a refusal.
        spdlog::info("Board {:X}: asking the host", acEvent.MountId);
        m_boardingMount = cMount;
        BoardRequest request;
        request.RiderId = m_characters.CharacterId();
        request.MountId = cMount;
        m_transport.Send(request);
        return;
    }
    spdlog::info("Board {:X}: not a copy here", acEvent.MountId);
}

// The host gave our player a seat: seated at once by the engine's quick get-on (PutActorOnMountQuick, no paired
// clip: player-mount.md), its copies by its Ride part.
void MountService::OnNotifyBoard(const NotifyBoard& acMessage) noexcept
{
    if (acMessage.RiderId != m_characters.CharacterId() || acMessage.MountId != std::exchange(m_boardingMount, 0u))
        return;
    const uint32_t cMount = m_characters.FormOfSession(acMessage.MountId);
    // On another horse meanwhile (its own ride): the seat goes back, PutOnMount would take it off that one.
    if (!cMount || m_riddenMount || m_playerBridge.IsPlayerMounted() || !m_playerBridge.PlayerSitOnMountQuick(cMount))
    {
        spdlog::info("Board {:X}: riding {:X}, or the engine's quick get-on refused (41530)", cMount, m_riddenMount);
        UnboardRequest unboard;
        unboard.RiderId = m_characters.CharacterId();
        unboard.MountId = acMessage.MountId;
        m_transport.Send(unboard);
        return;
    }
    spdlog::info("Board {:X}: on as a passenger", cMount);
    m_boardedMount = acMessage.MountId;
    m_world.ctx().at<CopyRecords>().Declare(m_characters.CharacterId(), RecordKind::kRide, ToPartBytes(RidePart{m_world.GetFrameTick(), m_boardedMount}));
}

// Our player is no passenger now (off, or driving it): its copies stop being seated by its Ride part.
void MountService::LeaveBoard() noexcept
{
    m_boardedMount = 0;
    if (m_characters.CharacterId())
        m_world.ctx().at<CopyRecords>().Declare(m_characters.CharacterId(), RecordKind::kRide, ToPartBytes(RidePart{m_world.GetFrameTick(), 0}));
}

// A passenger's copy seated on its horse once both are here (R5) and its stream reached its owner's quick get-on;
// Ride{0} leaves getting off to NotifyUnboard.
std::vector<uint32_t> MountService::ApplyRide(uint32_t aId, const std::string& acBytes) noexcept
{
    m_dueRides.erase(aId);
    RidePart part;
    const uint32_t cRider = m_characters.FormOfSession(aId);
    if (!FromPartBytes(acBytes, part) || !part.Mount || !cRider || !m_actorBridge.IsActorCopy(cRider))
        return {};
    const uint32_t cMount = m_characters.FormOfSession(part.Mount);
    if (!cMount)
        return {part.Mount};
    // After its driver: the horse's one slot names the first rider linked (12074), so a joiner hearing the
    // passenger's spawn before the ride would have the horse name the passenger.
    const auto cDriver = m_drivers.find(part.Mount);
    if (cDriver == m_drivers.end() || m_actorBridge.GetActorMount(m_characters.FormOfSession(cDriver->second)) != cMount)
    {
        m_dueRides.insert(aId);
        return {};
    }
    // Linked at once: its capsule collides with nothing until seated (GettingOnMount); unlinked, stepped toward its
    // owner on the saddle, it pushed the horse (player-mount.md).
    if (m_actorBridge.GetActorMount(cRider) != cMount)
        m_actorBridge.ActorPutOnMount(cRider, cMount);
    const auto cPlayback = m_world.GetGraphStreams().StreamPlayback(cRider);
    if (!ActorStateRules::Due(cPlayback.has_value(), cPlayback.value_or(0), part.Tick))
    {
        m_dueRides.insert(aId); // sat on the saddle before its graph's HorseEnterInstant, it stood there (the duo)
        return {};
    }
    // Seated by 41533 even at sit state 3 (its stream's HorseEnterInstant): SeatRider puts the REFRSyncController
    // (20318) that carries its reference; without it the reference stayed where it got on (the duo).
    m_actorBridge.ActorSeatOnMount(cRider, cMount);
    return {};
}

void MountService::OnNotifyUnboard(const NotifyUnboard& acMessage) noexcept
{
    // Its owner is off a horse that stays ridden: its copy gets off too, if no exit clip took it off already.
    const uint32_t cRider = m_characters.FormOfSession(acMessage.RiderId);
    const uint32_t cMount = m_characters.FormOfSession(acMessage.MountId);
    spdlog::info("Unboard {:X}: its copy on {:X}, sit state {}", cRider, m_actorBridge.GetActorMount(cRider), m_actorBridge.GetActorSitState(cRider));
    if (cRider && m_actorBridge.IsActorCopy(cRider) && m_actorBridge.GetActorMount(cRider) == cMount)
        m_actorBridge.ActorEndRide(cRider);
}

void MountService::KeepDriverOn(const DismountEvent& acEvent) noexcept
{
    if (!acEvent.MountId)
        return;
    const uint32_t cMount = Utils::GetSessionIdByFormId(acEvent.MountId).value_or(0);
    const auto cDriver = m_drivers.find(cMount);
    if (cDriver == m_drivers.end())
        return;
    const uint32_t cDriverForm = m_characters.FormOfSession(cDriver->second);
    if (!cDriverForm || cDriverForm == acEvent.RiderId || m_actorBridge.GetActorMount(cDriverForm) != acEvent.MountId)
        return;
    m_actorBridge.ActorHoldMount(cDriverForm, acEvent.MountId);
    // A copy's exit on a horse our player drives now: the horse was left in that pair, which nobody here ends.
    if (acEvent.Copy && cDriverForm == m_playerBridge.GetPlayerFormId())
        m_actorBridge.ActorEndMountPair(acEvent.MountId);
}

// The ride of one of this game's actors ended (any path): the host takes its mount back.
void MountService::OnDismountEvent(const DismountEvent& acEvent) noexcept
{
    KeepDriverOn(acEvent);
    if (acEvent.Copy)
        return;
    DismountRequest request;
    if (acEvent.RiderId == m_playerBridge.GetPlayerFormId())
    {
        if (m_boardedMount)
        {
            UnboardRequest unboard;
            unboard.RiderId = m_characters.CharacterId();
            unboard.MountId = m_boardedMount;
            LeaveBoard();
            m_transport.Send(unboard);
            return;
        }
        if (!m_riddenMount)
            return;
        request.RiderId = m_characters.CharacterId();
        request.MountId = std::exchange(m_riddenMount, 0u);
    }
    else
    {
        const auto& actors = m_world.GetActorIndex();
        if (const auto cRider = actors.ByForm<LocalComponent>(acEvent.RiderId); cRider != entt::null)
            request.RiderId = m_world.get<LocalComponent>(cRider).Id;
        if (const auto cMount = actors.ByForm<LocalComponent>(acEvent.MountId); cMount != entt::null)
            request.MountId = m_world.get<LocalComponent>(cMount).Id;
        if (!request.RiderId || !request.MountId)
            return;
    }
    // The mount goes back once its graph's next update is recorded (its exit pair's PairEnd is due with it), the
    // ride's end told first (mounts.md).
    m_world.ctx().at<ClaimService>().GiveBack(request.MountId, false, false, [this, request] { m_transport.Send(request); });
}

void MountService::OnNotifyDismount(const NotifyDismount& acMessage) noexcept
{
    m_partyMounts.erase(acMessage.MountId);
    m_pendingRides.erase(acMessage.MountId);
    m_drivers.erase(acMessage.MountId);
    if (m_boardedMount == acMessage.MountId)
        LeaveBoard();
    // Its owner's ride ended: a copy rider still seated gets off now (its owner's ride ended with no exit clip).
    if (const auto cRider = m_world.GetActorIndex().BySession<RemoteComponent, FormIdComponent>(acMessage.RiderId); cRider != entt::null)
        m_actorBridge.ActorEndRide(m_world.get<FormIdComponent>(cRider).Id);
    // Its owner (the host, given back: ClaimService) keeps it as it would.
    const uint32_t cMountFormId = m_characters.FormOfSession(acMessage.MountId);
    if (cMountFormId)
        ReleaseMount(cMountFormId);
    if (m_riddenMount == acMessage.MountId)
        m_riddenMount = 0;
    // Our player's ride ended by the host's word with our player still on it (this game could not take the horse:
    // HostClaims::OnEnded): off it too. Off already in a dismount of its own.
    if (acMessage.RiderId == m_characters.CharacterId() && cMountFormId && m_actorBridge.GetActorMount(m_playerBridge.GetPlayerFormId()) == cMountFormId)
        m_playerBridge.PlayerStopRiding();
}

void MountService::CarryMountTo(uint32_t aCellId, const glm::vec3& acPosition) noexcept
{
    if (!m_riddenMount)
        return;
    if (const auto cEntity = m_world.GetActorIndex().BySession<LocalComponent, FormIdComponent>(m_riddenMount); cEntity != entt::null)
    {
        const uint32_t cFormId = m_world.get<FormIdComponent>(cEntity).Id;
        m_actorBridge.MoveActorTo(cFormId, aCellId, acPosition.x, acPosition.y, acPosition.z);
    }
}

void MountService::KeepMount(uint32_t aFormId) noexcept
{
    const bool cWasPersistent = m_actorBridge.SetActorPersistent(aFormId);
    m_mountPersistence.try_emplace(aFormId, cWasPersistent);
}

void MountService::ReleaseMount(uint32_t aFormId) noexcept
{
    if (const auto it = m_mountPersistence.find(aFormId); it != m_mountPersistence.end())
    {
        m_actorBridge.SetActorPersistent(aFormId, it->second);
        m_mountPersistence.erase(it);
    }
}
