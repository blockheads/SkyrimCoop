#include <characters/NpcSnapshot.h>
#include <animation/GraphStream.h>
#include <characters/ActorPools.h>
#include <characters/CharacterService.h>
#include <characters/ClaimService.h>
#include <core/MainLoopTasks.h>
#include <actorstate/ActorValueService.h>
#include <core/TransportService.h>
#include <core/SessionService.h>

#include <animation/LocalAnimationComponent.h>
#include <animation/RemoteAnimationComponent.h>
#include <core/FormIdComponent.h>
#include <movement/InterpolationComponent.h>
#include <core/LocalComponent.h>
#include <core/RemoteComponent.h>
#include <characters/FaceGenComponent.h>
#include <characters/WaitingFor3D.h>
#include <core/PlayerComponent.h>
#include <animation/SpawnGraphStateComponent.h>
#include <characters/OwnerPlaceComponent.h>
#include <characters/HeldCopyComponent.h>

#include <characters/Copy3DEvent.h>
#include <characters/ActorAddedEvent.h>
#include <characters/ActorRemovedEvent.h>
#include <core/UpdateEvent.h>
#include <core/ConnectedEvent.h>
#include <characters/CopyHeldEvent.h>
#include <core/DisconnectedEvent.h>
#include <characters/MountEvent.h>
#include <characters/DismountEvent.h>
#include <characters/BoardEvent.h>
#include <animation/ReplayState.h>
#include <actorstate/AiStateChangeEvent.h>
#include <core/PlayerLooksChangeEvent.h>
#include <characters/ActorHeadUpdateEvent.h>
#include <core/PlayerDeathEvent.h>
#include <characters/ReferenceJumpEvent.h>
#include <characters/ReferenceLoadedEvent.h>
#include <characters/NpcHeldEvent.h>

#include <Structs/ActionEvent.h>
#include <Structs/GridCellCoords.h>
#include <Messages/AnnounceCharacterRequest.h>
#include <Messages/NotifyReferencesMove.h>
#include <Messages/ReferencesMoveRequest.h>
#include <Messages/NotifyCharacterSpawn.h>
#include <Messages/NotifyRemoveCharacter.h>
#include <Messages/RequestRemoveCharacter.h>
#include <Messages/Relayed.h>
#include <Messages/RequestRespawn.h>
#include <Messages/EnterCellRequest.h>
#include <Messages/NotifyRespawn.h>
#include <core/EngineNames.h>
#include <Messages/NotifyPlayerLevel.h>
#include <core/PlayerArrivedEvent.h>
#include <travel/TravelService.h>
#include <core/CopyRecords.h>
#include <Structs/RidePart.h>
#include <animation/ActorStateRules.h>
#include <items/WornResetEvent.h>
#include <physics/BodyService.h>
#include <magic/EffectVisualsService.h>

#include <ports/IActorBridge.h>
#include <ports/IFormBridge.h>
#include <ports/IPlayerBridge.h>

#include <movement/SnapshotAge.h>
#include <movement/CopyStep.h>
#include <movement/InterpolationSystem.h>
#include <animation/AnimationSystem.h>

#include <core/World.h>
#include <core/SessionIds.h>
#include <core/Hosting.h>
#include <core/FormIds.h>
#include <core/Profile.h>

#include <characters/CharacterInternal.h>


CharacterService::CharacterService(World& aWorld, entt::dispatcher& aDispatcher, TransportService& aTransport,
                                   IActorBridge& aActorBridge, IFormBridge& aFormBridge, IPlayerBridge& aPlayerBridge) noexcept
    : m_world(aWorld)
    , m_dispatcher(aDispatcher)
    , m_transport(aTransport)
    , m_actorBridge(aActorBridge)
    , m_formBridge(aFormBridge)
    , m_playerBridge(aPlayerBridge)
{
    m_world.ctx().at<CopyRecords>().Register(
        RecordKind::kRide, [this](uint32_t aId, const std::string& acBytes) { return ApplyRide(aId, acBytes); },
        [this](uint32_t aFormId) -> std::optional<std::string> {
            if (aFormId != m_playerBridge.GetPlayerFormId() || !m_boardedMount)
                return std::nullopt;
            return ToPartBytes(RidePart{m_world.GetFrameTick(), m_boardedMount});
        });
    Listen<ActorAddedEvent, &CharacterService::OnActorAdded>(m_connections, m_dispatcher, this);
    Listen<ActorRemovedEvent, &CharacterService::OnActorRemoved>(m_connections, m_dispatcher, this);

    Listen<UpdateEvent, &CharacterService::OnUpdate>(m_connections, m_dispatcher, this);
    Listen<ActionEvent, &CharacterService::OnActionEvent>(m_connections, m_dispatcher, this);

    Listen<ConnectedEvent, &CharacterService::OnConnected>(m_connections, m_dispatcher, this);
    Listen<DisconnectedEvent, &CharacterService::OnDisconnected>(m_connections, m_dispatcher, this);

    Listen<NotifyCharacterSpawn, &CharacterService::OnCharacterSpawn>(m_connections, m_dispatcher, this);
    Listen<NotifyReferencesMove, &CharacterService::OnReferencesMoveRequest>(m_connections, m_dispatcher, this);
    Listen<ReferenceJumpEvent, &CharacterService::OnReferenceJump>(m_connections, m_dispatcher, this);
    Listen<ReferenceLoadedEvent, &CharacterService::OnReferenceLoaded>(m_connections, m_dispatcher, this);
    Listen<NpcHeldEvent, &CharacterService::OnNpcHeld>(m_connections, m_dispatcher, this);
    Listen<NotifyRemoveCharacter, &CharacterService::OnRemoveCharacter>(m_connections, m_dispatcher, this);

    Listen<MountEvent, &CharacterService::OnMountEvent>(m_connections, m_dispatcher, this);
    Listen<NotifyMount, &CharacterService::OnNotifyMount>(m_connections, m_dispatcher, this);
    Listen<NotifyMountRefused, &CharacterService::OnNotifyMountRefused>(m_connections, m_dispatcher, this);
    Listen<NotifyClaimRefused, &CharacterService::OnRideRefused>(m_connections, m_dispatcher, this);
    Listen<BoardEvent, &CharacterService::OnBoardEvent>(m_connections, m_dispatcher, this);
    Listen<NotifyBoard, &CharacterService::OnNotifyBoard>(m_connections, m_dispatcher, this);
    Listen<NotifyActorName, &CharacterService::OnNotifyActorName>(m_connections, m_dispatcher, this);
    Listen<DismountEvent, &CharacterService::OnDismountEvent>(m_connections, m_dispatcher, this);
    Listen<NotifyDismount, &CharacterService::OnNotifyDismount>(m_connections, m_dispatcher, this);
    Listen<NotifyUnboard, &CharacterService::OnNotifyUnboard>(m_connections, m_dispatcher, this);

    Listen<NotifyRespawn, &CharacterService::OnNotifyRespawn>(m_connections, m_dispatcher, this);
    Listen<PlayerArrivedEvent, &CharacterService::OnPlayerArrived>(m_connections, m_dispatcher, this);
    Listen<NotifyPlayerLevel, &CharacterService::OnPlayerLevel>(m_connections, m_dispatcher, this);
    Listen<PlayerLooksChangeEvent, &CharacterService::OnPlayerLooksChange>(m_connections, m_dispatcher, this);
    Listen<ActorHeadUpdateEvent, &CharacterService::OnActorHeadUpdate>(m_connections, m_dispatcher, this);
    Listen<PlayerRevivedEvent, &CharacterService::OnPlayerRevived>(m_connections, m_dispatcher, this);

    Listen<&CharacterService::OnStreamedRemoved>(m_connections, m_world.on_destroy<LocalComponent>(), this);
    Listen<&CharacterService::OnStreamedRemoved>(m_connections, m_world.on_destroy<RemoteComponent>(), this);
    Listen<NotifyGraphOps, &CharacterService::OnGraphOps>(m_connections, m_dispatcher, this);
    Listen<NotifyGraphSync, &CharacterService::OnGraphSync>(m_connections, m_dispatcher, this);
    Listen<NotifyGraphSyncPoint, &CharacterService::OnGraphSyncPoint>(m_connections, m_dispatcher, this);

}

void CharacterService::DeleteRemoteEntityComponents(entt::entity aEntity) const noexcept
{
    m_world.remove<FaceGenComponent, InterpolationComponent, RemoteAnimationComponent, RemoteComponent, WaitingFor3D, PlayerComponent,
                   OwnerPlaceComponent>(aEntity);
}

bool CharacterService::TakeOwnership(const uint32_t acFormId, const uint32_t acSessionId, const entt::entity acEntity, bool aHoldKill) const noexcept
{
    if (!m_actorBridge.ActorExists(acFormId))
    {
        spdlog::error("Cannot find actor to take control over, form id: {:X}, session id: {:X}", acFormId, acSessionId);
        return false;
    }

    if (m_actorBridge.IsOtherPlayer(acFormId))
    {
        spdlog::error("Cannot take control over remote player actor, form id: {:X}, session id: {:X}", acFormId, acSessionId);
        return false;
    }

    if (m_actorBridge.IsPlayerSummon(acFormId))
    {
        spdlog::error("Cannot take control over remote player summon, form id: {:X}, session id: {:X}", acFormId, acSessionId);
        return false;
    }

    m_actorBridge.SetActorCopy(acFormId, false);
    // Ours now: no longer a copy its owner left (FollowOwner's mark would drop it at a disconnect).
    m_world.remove<HeldCopyComponent>(acEntity);
    // A copy's scripted deferred kill (UpdateCopyAI) outlives the copy: no engine path ends one (kill-moves.md).
    m_actorBridge.ActorHoldKill(acFormId, aHoldKill);
    m_world.emplace_or_replace<LocalComponent>(acEntity, acSessionId);
    // Its copies hold the last owner's action and AI state until this game sends its own (the action as played here
    // while it was a copy: a mount's half of its rider's paired getting on).
    m_world.emplace_or_replace<LocalAnimationComponent>(acEntity).Append(ReadLiveState(acFormId).LatestAction);
    DeleteRemoteEntityComponents(acEntity);
    m_dispatcher.trigger(AiStateChangeEvent{acFormId});

    return true;
}

void CharacterService::GiveUpOwnership(entt::entity aEntity, uint32_t aSessionId, uint32_t aFormId) const noexcept
{
    if (m_world.all_of<LocalComponent>(aEntity))
    {
        m_world.remove<LocalAnimationComponent, LocalComponent>(aEntity);
        m_world.emplace_or_replace<RemoteComponent>(aEntity, aSessionId, aFormId);
    }
    m_actorBridge.SetActorCopy(aFormId, true);
    auto& interpolation = InterpolationSystem::Setup(m_world, aEntity);
    m_actorBridge.GetActorPosition(aFormId, interpolation.Position.x, interpolation.Position.y, interpolation.Position.z);
    AnimationSystem::Setup(m_world, aEntity);
}

void CharacterService::DeleteTempActor(const uint32_t aFormId) noexcept
{
    // Temporary actors have form IDs in the FF000000+ range
    if ((aFormId & 0xFF000000) == 0xFF000000)
    {
        // Deletion is routed through World bridge in the main client compilation unit
        // This static method is called from OnRemoveCharacter which has bridge access,
        // but for now we rely on the direct call path via SkyrimBridge in the non-services build.
        // TODO: thread bridge through or make this non-static
        spdlog::info("\tRequesting delete of actor {:X} (temp ID range)", aFormId);
    }
}

void CharacterService::OnActorAdded(const ActorAddedEvent& acEvent) noexcept
{
    if (acEvent.FormId == 0x14)
    {
        m_actorBridge.SetActorPlayer(acEvent.FormId, true);
    }

    entt::entity entity;

    const auto view = m_world.view<RemoteComponent>();
    const auto it = std::find_if(
        std::begin(view), std::end(view),
        [&acEvent, view](entt::entity entity)
        {
            auto& remoteComponent = view.get<RemoteComponent>(entity);
            return remoteComponent.CachedRefId == acEvent.FormId;
        });

    if (it != std::end(view))
    {
        m_actorBridge.SetActorCopy(acEvent.FormId, true);
        entity = *it;
    }
    else
        entity = m_world.create();

    m_world.emplace_or_replace<FormIdComponent>(entity, acEvent.FormId);

    ProcessNewEntity(entity);
}

void CharacterService::OnActorRemoved(const ActorRemovedEvent& acEvent) noexcept
{
    auto view = m_world.view<FormIdComponent>();
    const auto entityIt = std::find_if(view.begin(), view.end(), [view, formId = acEvent.FormId](auto aEntity) { return view.get<FormIdComponent>(aEntity).Id == formId; });

    if (entityIt == view.end())
    {
        spdlog::error("Actor to remove not found in form ids map {:X}", acEvent.FormId);
        return;
    }

    const auto cId = *entityIt;
    if (const auto cSession = Utils::GetSessionId(cId); cSession && m_partyMounts.contains(*cSession))
        return;
    // Another player's copy left the loaded area with our travel: it travels with the party, not its cell.
    if (m_world.all_of<RemoteComponent, PlayerComponent>(cId) && m_actorBridge.IsActorCopy(acEvent.FormId))
        return;

    auto& formIdComponent = view.get<FormIdComponent>(cId);
    ReleaseCharacter(*entityIt, formIdComponent.Id);

    if (m_world.all_of<FormIdComponent>(cId))
        m_world.remove<FormIdComponent>(cId);

    if (m_world.orphan(cId))
        m_world.destroy(cId);

    spdlog::info("Actor removed, form id: {:X}", acEvent.FormId);
}

void CharacterService::OnUpdate(const UpdateEvent& acUpdateEvent) noexcept
{
    PROFILE_ZONE("CharacterService::OnUpdate");
    QueuePendingLooks();
    RunActorDiscovery();
    RunPresence();
    RunLocalUpdates();
    RunRemoteUpdates();
    // Each due part waits again in ApplyRide; one whose record went is dropped.
    for (const uint32_t cId : std::exchange(m_dueRides, {}))
        m_world.ctx().at<CopyRecords>().ApplyAgain(cId, RecordKind::kRide);
}

// Dispatches ActorAddedEvent / ActorRemovedEvent as actors gain or lose loaded 3D, which drives
// announcements (replaces the actor half of DiscoveryService, removed in ec7d02d1).
// Everyone announces their own player. Only the host announces NPCs: under the party tether every
// player is in the host's cell, so the host owns every actor and guests only replicate them.
void CharacterService::RunActorDiscovery() noexcept
{
    std::unordered_set<uint32_t> loaded;
    const uint32_t cPlayerId = m_playerBridge.GetPlayerFormId();
    if (cPlayerId != 0 && m_actorBridge.ActorHas3D(cPlayerId))
        loaded.insert(cPlayerId);
    if (m_world.IsHosting())
        for (const uint32_t cFormId : m_actorBridge.GetLoadedActorFormIds())
            loaded.insert(cFormId);
    else if (m_transport.IsOnline())
        for (const uint32_t cFormId : m_actorBridge.GetLoadedActorFormIds())
            if (cFormId != cPlayerId && m_actorBridge.IsActorTemporary(cFormId) &&
                !m_actorBridge.IsActorCopy(cFormId) && !Utils::GetSessionIdByFormId(cFormId))
            {
                // The host creates (one-simulator.md piece 7): an actor this guest's engine made is refused.
                spdlog::warn("Actor discovery: {:X}, made by this guest's engine, refused (deleted)", cFormId);
                m_actorBridge.DeleteActor(cFormId);
            }

    // Removals first, as the original DiscoveryService did.
    std::vector<uint32_t> removed;
    for (const uint32_t cFormId : m_discoveredActors)
        if (!loaded.count(cFormId))
            removed.push_back(cFormId);
    for (const uint32_t cFormId : removed)
    {
        m_discoveredActors.erase(cFormId);
        spdlog::debug("Actor discovery: {:X} removed", cFormId);
        m_dispatcher.trigger(ActorRemovedEvent(cFormId));
    }

    for (const uint32_t cFormId : loaded)
    {
        if (!m_discoveredActors.insert(cFormId).second)
            continue;
        spdlog::debug("Actor discovery: {:X} added", cFormId);
        m_dispatcher.trigger(ActorAddedEvent(cFormId));
    }
}

void CharacterService::OnConnected(const ConnectedEvent& acConnectedEvent) noexcept
{
    m_playerId = acConnectedEvent.PlayerId;
    m_characterId = acConnectedEvent.CharacterId;
    m_world.GetCopyClock().Reset();

    // Go through all the forms that were previously detected
    auto view = m_world.view<FormIdComponent>();
    std::vector<entt::entity> entities(view.begin(), view.end());
    spdlog::debug("CharacterService::OnConnected: processing {} discovered entities", entities.size());

    for (auto entity : entities)
    {
        auto& formIdComponent = m_world.get<FormIdComponent>(entity);
        // The host's saved temporary NPCs belong to its world too; only guests discard their own.
        if (!m_world.IsHosting() && formIdComponent.Id >= 0xFF000000)
        {
            m_actorBridge.DeleteActor(formIdComponent.Id);
            continue;
        }

        ProcessNewEntity(entity);
    }
}

void CharacterService::OnDisconnected(const DisconnectedEvent& acDisconnectedEvent) noexcept
{
    m_pendingLooks.clear();
    m_pendingSpawns.clear();
    m_world.GetCopyClock().Reset();
    const auto cMounts = m_mountPersistence;
    for (const auto& [cFormId, cPersistent] : cMounts)
        ReleaseMount(cFormId);
    // The session's copies go: the ones we spawned are deleted, the game's own actors are its own again.
    DropCopies([](uint32_t) { return true; });
    const auto cHeld = m_world.view<HeldCopyComponent>();
    for (const auto cEntity : std::vector<entt::entity>(cHeld.begin(), cHeld.end()))
    {
        if (const auto* pFormId = m_world.try_get<FormIdComponent>(cEntity); pFormId && m_actorBridge.IsActorCopy(pFormId->Id))
        {
            m_actorBridge.SetActorCopy(pFormId->Id, false);
            const auto& cHeld = m_world.get<HeldCopyComponent>(cEntity);
            if (cHeld.Hidden)
                m_actorBridge.SetActorHidden(pFormId->Id, false);
            if (cHeld.DisabledByUs)
                m_actorBridge.EnableActor(pFormId->Id);
        }
        m_world.remove<HeldCopyComponent, FormIdComponent>(cEntity);
        if (m_world.orphan(cEntity))
            m_world.destroy(cEntity);
    }
    // Kept persistent while a copy (GuestCopies): its own flag back.
    const auto cKept = m_world.view<KeptCopyComponent>();
    for (const auto cEntity : std::vector<entt::entity>(cKept.begin(), cKept.end()))
    {
        if (const auto& cCopy = m_world.get<KeptCopyComponent>(cEntity); !cCopy.WasPersistent && m_actorBridge.ActorExists(cCopy.FormId))
            m_actorBridge.SetActorPersistent(cCopy.FormId, false);
        m_world.remove<KeptCopyComponent>(cEntity);
        if (m_world.orphan(cEntity))
            m_world.destroy(cEntity);
    }
    m_world.clear<LocalComponent>();
    m_playerId = 0;
    m_characterId = 0;
    m_riddenMount = 0;
    m_partyMounts.clear();
    m_pendingRides.clear();
    m_pendingCommanders.clear();
    m_boardedMount = 0;
    m_boardingMount = 0;
    m_dueRides.clear();
    m_drivers.clear();
}

CharacterService::~CharacterService() noexcept
{
    m_world.ctx().at<CopyRecords>().Register(RecordKind::kRide, {});
    m_playerBridge.StopTeleportEvents();
}

void CharacterService::OnReferenceLoaded(const ReferenceLoadedEvent& acEvent) noexcept
{
    SpawnPending(acEvent.FormId);
}

// Done loading without 3D: its spawn makes it a copy, which loads it (Actor::ApplyHeld3D).
void CharacterService::OnNpcHeld(const NpcHeldEvent& acEvent) noexcept
{
    SpawnPending(acEvent.FormId);
}

void CharacterService::OnRemoveCharacter(const NotifyRemoveCharacter& acMessage) noexcept
{
    m_pendingLooks.erase(acMessage.SessionId);
    m_pendingSpawns.erase(acMessage.SessionId);
    auto view = m_world.view<RemoteComponent>();

    const auto itor = std::find_if(std::begin(view), std::end(view), [id = acMessage.SessionId, view](entt::entity entity) { return view.get<RemoteComponent>(entity).Id == id; });

    if (itor == std::end(view))
        return;

    const entt::entity cEntity = *itor;
    const auto* pFormIdComponent = m_world.try_get<FormIdComponent>(cEntity);
    const uint32_t cFormId = pFormIdComponent ? pFormIdComponent->Id : 0;
    // A copy we spawned (temporary reference) goes with its owner's actor. A placed one, even on a leveled
    // (temporary) base, is the plugin's: it follows its owner (FollowOwner).
    bool deleted = false;
    if (cFormId && m_actorBridge.IsActorTemporary(cFormId))
    {
        m_actorBridge.DeleteActor(cFormId);
        deleted = true;
        spdlog::info("\tDeleted actor {:X}", cFormId);
    }
    else if (cFormId && m_actorBridge.IsActorCopy(cFormId))
        FollowOwner(cEntity, cFormId, acMessage.Whereabouts, true);

    DeleteRemoteEntityComponents(cEntity);
    // The engine reuses a deleted copy's form id for the next one: an entity left holding it
    // shadowed the new copy in form id lookups (its hits were dropped). Peers never see actor
    // discovery remove a copy, so this is the only place it goes.
    if (deleted)
    {
        m_world.remove<FormIdComponent, HeldCopyComponent>(cEntity);
        if (m_world.orphan(cEntity))
            m_world.destroy(cEntity);
    }
}

uint32_t CharacterService::FormOfSession(uint32_t aSessionId) const noexcept
{
    for (const auto cEntity : m_world.view<FormIdComponent>())
        if (Utils::GetSessionId(cEntity) == aSessionId)
            return m_world.get<FormIdComponent>(cEntity).Id;
    return 0;
}

void CharacterService::ReleaseCharacter(const entt::entity aEntity, const uint32_t aFormId) const noexcept
{
    if (m_world.all_of<RemoteComponent>(aEntity))
    {
        // Only while it is still our copy: a deleted copy's form id can belong to a new reference already.
        if (m_actorBridge.IsActorCopy(aFormId))
        {
            if (m_actorBridge.IsActorTemporary(aFormId))
            {
                spdlog::info("Temporary Remote Deleted {:X}", aFormId);
                m_actorBridge.DeleteActor(aFormId);
            }
            else if (m_transport.IsOnline())
                m_world.emplace_or_replace<HeldCopyComponent>(aEntity); // stays a copy while the session lasts
            else
                m_actorBridge.SetActorCopy(aFormId, false);
        }

        DeleteRemoteEntityComponents(aEntity);

        return;
    }

    // We own it and stopped simulating it: the copies on other games must be removed too.
    if (const auto* pLocalComponent = m_world.try_get<LocalComponent>(aEntity))
    {
        RequestRemoveCharacter message;
        message.SessionId = pLocalComponent->Id;
        message.Whereabouts = ReadWhereabouts(aFormId);
        m_transport.Send(message);
        if (auto* pRecords = m_world.ctx().find<CopyRecords>())
            pRecords->Forget(pLocalComponent->Id);

        m_world.remove<LocalAnimationComponent, LocalComponent>(aEntity);
        return;
    }
}

// Tells the host, which sends what is here and, when the player led, takes the party along.
void CharacterService::OnPlayerArrived(const PlayerArrivedEvent& acEvent) noexcept
{
    const uint32_t cCellId = acEvent.CellId;
    const uint32_t cWorldSpaceId = acEvent.WorldSpaceId;
    // MoveTo can unlink the mount interaction without calling Actor::Dismount.
    if ((m_riddenMount || m_boardedMount) && !m_playerBridge.IsPlayerMounted())
        OnDismountEvent(DismountEvent{m_playerBridge.GetPlayerFormId()});

    // Players and their ridden horses travel with the party; other copies stay with their cell.
    std::unordered_set<uint32_t> partyCopies;
    for (const auto cEntity : m_world.view<RemoteComponent, FormIdComponent>())
    {
        if (!m_world.all_of<PlayerComponent>(cEntity) && !m_partyMounts.contains(m_world.get<RemoteComponent>(cEntity).Id))
            continue;
        partyCopies.insert(m_world.get<FormIdComponent>(cEntity).Id);
        // Moved at once, not after the spacing between two moves of the same copy.
        if (auto* pPlace = m_world.try_get<OwnerPlaceComponent>(cEntity))
            pPlace->LastMove = {};
    }
    DropCopies([&](uint32_t aFormId) {
        if (partyCopies.count(aFormId) && m_actorBridge.IsActorCopy(aFormId))
            return false;
        // Interiors: the same cell. Exteriors: the same worldspace, loaded around the player.
        const bool cHere = cWorldSpaceId ? m_actorBridge.GetActorWorldSpaceFormId(aFormId) == cWorldSpaceId
                                         : m_actorBridge.GetActorParentCellFormId(aFormId) == cCellId;
        return !m_actorBridge.ActorExists(aFormId) || !m_actorBridge.ActorHas3D(aFormId) || !cHere;
    });

    EnterCellRequest request;
    const auto cCell = ToSession(cCellId);
    const auto cWorldSpace = cWorldSpaceId ? ToSession(cWorldSpaceId) : std::optional(GameId{});
    if (!cCell || !cWorldSpace)
        return;
    request.Tick = m_world.GetFrameTick();
    request.CellId = *cCell;
    request.WorldSpaceId = *cWorldSpace;
    float x, y, z;
    m_playerBridge.GetPlayerPosition(x, y, z);
    request.Position = glm::vec3(x, y, z);
    request.Heading = m_playerBridge.GetPlayerHeading();
    const auto cState = ReadLiveState(m_playerBridge.GetPlayerFormId());
    request.LatestAction = cState.LatestAction;
    request.GraphState = cState.GraphState;
    request.Data = cState.Data;
    request.How = acEvent.How;
    m_transport.Send(request);
    spdlog::info("Arrived in cell {:X} (worldspace {:X}), {}: copies dropped, the host sends what is here", cCellId,
                 cWorldSpaceId,
                 request.How == EnterCellRequest::Travel::kOwn      ? "leading the party"
                 : request.How == EnterCellRequest::Travel::kTether ? "following"
                                                                    : "from before joining");
}

// A copy belongs where its owner says it is. One whose owner is around us (our cell; outdoors our worldspace,
// within the loaded grid) but which is in another cell or has no 3D is moved there: a spawn placed it before
// its owner arrived, or a move the engine dropped. Each copy is moved at most every 2 s, the time 3D takes.
void CharacterService::RunPresence() noexcept
{
    constexpr auto cRetry = 2s;
    const uint32_t cPlayerCellId = m_playerBridge.GetPlayerParentCellFormId();
    if (!m_transport.IsOnline() || !cPlayerCellId)
        return;
    const uint32_t cPlayerWorldSpaceId = m_playerBridge.GetPlayerWorldSpaceId();
    const auto cNow = std::chrono::steady_clock::now();

    std::vector<std::pair<entt::entity, uint32_t>> gone;
    std::unordered_set<uint32_t> goneElsewhere;
    auto view = m_world.view<RemoteComponent, FormIdComponent, InterpolationComponent, OwnerPlaceComponent>();
    for (const auto entity : view)
    {
        auto& place = view.get<OwnerPlaceComponent>(entity);
        const uint32_t cFormId = view.get<FormIdComponent>(entity).Id;
        const auto& cPosition = view.get<InterpolationComponent>(entity).Position;
        const bool cOwnerAround =
            m_world.GetTravelService().IsAroundPlayer(place.WorldSpaceId, place.CellId, cPosition);
        // The engine deleted it (a temporary reference left in a cell it unloaded) and may already have given its
        // form id to a new reference: an actor there that is not a copy is a stranger, never ours to drive.
        if (!m_actorBridge.IsActorCopy(cFormId))
        {
            if (cOwnerAround)
                gone.emplace_back(entity, view.get<RemoteComponent>(entity).Id);
            else
                goneElsewhere.insert(cFormId);
            continue;
        }
        if (!cOwnerAround || cNow - place.LastMove < cRetry)
            continue;
        // Outdoors also in a cell attached here: in a detached one nothing moves it to the cell it stands in
        // (UpdateCellAttachment 37352 never ran for it) and it is below high process (37503).
        const bool cCopyHere = place.WorldSpaceId
                                   ? m_actorBridge.GetActorWorldSpaceFormId(cFormId) == cPlayerWorldSpaceId &&
                                         m_actorBridge.IsActorCellAttached(cFormId)
                                   : m_actorBridge.GetActorParentCellFormId(cFormId) == cPlayerCellId;
        if (cCopyHere && m_actorBridge.ActorHas3D(cFormId))
            continue;

        place.LastMove = cNow;
        Vector3_NetQuantize position;
        position = cPosition;
        spdlog::info("Copy {:X} is not where its owner is (cell {:X}, worldspace {:X}): moved there", cFormId, place.CellId.BaseId,
                     place.WorldSpaceId.BaseId);
        MoveActor(cFormId, place.WorldSpaceId, place.CellId, position);
        // The move rebuilds the copy's equipment and drops its hand spells (copy-hand-spells.md): it wears its part again.
        m_world.GetRunner().Trigger(WornResetEvent{cFormId});
    }

    for (const auto& [cEntity, cSessionId] : gone)
    {
        spdlog::info("Copy of {:X} is gone where its owner is: spawned again", cSessionId);
        RespawnCopy(cEntity, cSessionId);
    }
    // Its owner is elsewhere: the host sends it again when they meet (an arrival).
    if (!goneElsewhere.empty())
    {
        for (const uint32_t cFormId : goneElsewhere)
            spdlog::info("Copy {:X} is gone where its owner is not: dropped", cFormId);
        DropCopies([&](uint32_t aFormId) { return goneElsewhere.count(aFormId) != 0; });
    }
}

void CharacterService::DropCopies(const std::function<bool(uint32_t)>& acShouldDrop) noexcept
{
    auto remoteView = m_world.view<RemoteComponent>();
    const std::vector<entt::entity> cCopies(remoteView.begin(), remoteView.end());
    for (const auto entity : cCopies)
    {
        const auto* pFormIdComponent = m_world.try_get<FormIdComponent>(entity);
        const uint32_t cFormId = pFormIdComponent ? pFormIdComponent->Id : 0;
        if (cFormId && !acShouldDrop(cFormId))
            continue;
        // Deletes what is left of a copy we spawned; the game's own references stop being copies.
        ReleaseCharacter(entity, cFormId);
        // Gone without discovery noticing: it must not report it removed later.
        m_discoveredActors.erase(cFormId);
        if (m_world.all_of<HeldCopyComponent>(entity))
        {
            m_dispatcher.trigger(CopyHeldEvent{entity, cFormId, m_actorBridge.IsActorDisabled(cFormId)});
            continue;
        }
        if (m_world.all_of<FormIdComponent>(entity))
            m_world.remove<FormIdComponent>(entity);
        if (m_world.orphan(entity))
            m_world.destroy(entity);
    }
}
