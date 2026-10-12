#include <Services/CharacterService.h>
#include <Services/QuestService.h>
#include <Services/TransportService.h>

#include <Components.h>

#include <Events/ActorAddedEvent.h>
#include <Events/ActorRemovedEvent.h>
#include <Events/UpdateEvent.h>
#include <Events/ConnectedEvent.h>
#include <Events/DisconnectedEvent.h>
#include <Events/MountEvent.h>
#include <Events/InitPackageEvent.h>
#include <Events/BeastFormChangeEvent.h>
#include <Events/AddExperienceEvent.h>
#include <Events/DialogueEvent.h>
#include <Events/SubtitleEvent.h>
#include <Events/MoveActorEvent.h>

#include <Structs/ActionEvent.h>
#include <Structs/GridCellCoords.h>
#include <Messages/CancelAssignmentRequest.h>
#include <Messages/AssignCharacterRequest.h>
#include <Messages/AssignCharacterResponse.h>
#include <Messages/ServerReferencesMoveRequest.h>
#include <Messages/ClientReferencesMoveRequest.h>
#include <Messages/CharacterSpawnRequest.h>
#include <Messages/RequestFactionsChanges.h>
#include <Messages/NotifyFactionsChanges.h>
#include <Messages/NotifyRemoveCharacter.h>
#include <Messages/RequestRemoveCharacter.h>
#include <Messages/NotifySpawnData.h>
#include <Messages/MountRequest.h>
#include <Messages/NotifyMount.h>
#include <Messages/NewPackageRequest.h>
#include <Messages/NotifyNewPackage.h>
#include <Messages/RequestRespawn.h>
#include <Messages/EnterCellRequest.h>
#include <Messages/NotifyRespawn.h>
#include <Messages/SyncExperienceRequest.h>
#include <Messages/NotifySyncExperience.h>
#include <Messages/DialogueRequest.h>
#include <Messages/NotifyDialogue.h>
#include <Messages/SubtitleRequest.h>
#include <Messages/NotifySubtitle.h>
#include <Messages/NotifyActorTeleport.h>
#include <Messages/NotifyRelinquishControl.h>

#include <Bridges/IActorBridge.h>
#include <Bridges/IFormBridge.h>
#include <Bridges/IPlayerBridge.h>

#include <Systems/InterpolationSystem.h>
#include <Systems/AnimationSystem.h>
#include <Systems/CacheSystem.h>
#include <Systems/FaceGenSystem.h>

#include <World.h>

CharacterService::CharacterService(World& aWorld, entt::dispatcher& aDispatcher, TransportService& aTransport,
                                   IActorBridge& aActorBridge, IFormBridge& aFormBridge, IPlayerBridge& aPlayerBridge) noexcept
    : m_world(aWorld)
    , m_dispatcher(aDispatcher)
    , m_transport(aTransport)
    , m_actorBridge(aActorBridge)
    , m_formBridge(aFormBridge)
    , m_playerBridge(aPlayerBridge)
{
    m_referenceAddedConnection = m_dispatcher.sink<ActorAddedEvent>().connect<&CharacterService::OnActorAdded>(this);
    m_referenceRemovedConnection = m_dispatcher.sink<ActorRemovedEvent>().connect<&CharacterService::OnActorRemoved>(this);

    m_updateConnection = m_dispatcher.sink<UpdateEvent>().connect<&CharacterService::OnUpdate>(this);
    m_actionConnection = m_dispatcher.sink<ActionEvent>().connect<&CharacterService::OnActionEvent>(this);

    m_connectedConnection = m_dispatcher.sink<ConnectedEvent>().connect<&CharacterService::OnConnected>(this);
    m_disconnectedConnection = m_dispatcher.sink<DisconnectedEvent>().connect<&CharacterService::OnDisconnected>(this);

    m_assignCharacterConnection = m_dispatcher.sink<AssignCharacterResponse>().connect<&CharacterService::OnAssignCharacter>(this);
    m_characterSpawnConnection = m_dispatcher.sink<CharacterSpawnRequest>().connect<&CharacterService::OnCharacterSpawn>(this);
    m_referenceMovementSnapshotConnection = m_dispatcher.sink<ServerReferencesMoveRequest>().connect<&CharacterService::OnReferencesMoveRequest>(this);
    m_factionsConnection = m_dispatcher.sink<NotifyFactionsChanges>().connect<&CharacterService::OnFactionsChanges>(this);
    m_removeCharacterConnection = m_dispatcher.sink<NotifyRemoveCharacter>().connect<&CharacterService::OnRemoveCharacter>(this);
    m_remoteSpawnDataReceivedConnection = m_dispatcher.sink<NotifySpawnData>().connect<&CharacterService::OnRemoteSpawnDataReceived>(this);

    m_mountConnection = m_dispatcher.sink<MountEvent>().connect<&CharacterService::OnMountEvent>(this);
    m_notifyMountConnection = m_dispatcher.sink<NotifyMount>().connect<&CharacterService::OnNotifyMount>(this);

    m_initPackageConnection = m_dispatcher.sink<InitPackageEvent>().connect<&CharacterService::OnInitPackageEvent>(this);
    m_newPackageConnection = m_dispatcher.sink<NotifyNewPackage>().connect<&CharacterService::OnNotifyNewPackage>(this);

    m_notifyRespawnConnection = m_dispatcher.sink<NotifyRespawn>().connect<&CharacterService::OnNotifyRespawn>(this);
    m_beastFormChangeConnection = m_dispatcher.sink<BeastFormChangeEvent>().connect<&CharacterService::OnBeastFormChange>(this);

    m_addExperienceEventConnection = m_dispatcher.sink<AddExperienceEvent>().connect<&CharacterService::OnAddExperienceEvent>(this);
    m_syncExperienceConnection = m_dispatcher.sink<NotifySyncExperience>().connect<&CharacterService::OnNotifySyncExperience>(this);

    m_dialogueEventConnection = m_dispatcher.sink<DialogueEvent>().connect<&CharacterService::OnDialogueEvent>(this);
    m_dialogueSyncConnection = m_dispatcher.sink<NotifyDialogue>().connect<&CharacterService::OnNotifyDialogue>(this);

    m_subtitleEventConnection = m_dispatcher.sink<SubtitleEvent>().connect<&CharacterService::OnSubtitleEvent>(this);
    m_subtitleSyncConnection = m_dispatcher.sink<NotifySubtitle>().connect<&CharacterService::OnNotifySubtitle>(this);

    m_actorTeleportConnection = m_dispatcher.sink<NotifyActorTeleport>().connect<&CharacterService::OnNotifyActorTeleport>(this);

    m_relinquishConnection = m_dispatcher.sink<NotifyRelinquishControl>().connect<&CharacterService::OnNotifyRelinquishControl>(this);

}

void CharacterService::DeleteRemoteEntityComponents(entt::entity aEntity) const noexcept
{
    m_world.remove<FaceGenComponent, InterpolationComponent, RemoteAnimationComponent, RemoteComponent, CacheComponent, WaitingFor3D, PlayerComponent>(aEntity);
}

bool CharacterService::TakeOwnership(const uint32_t acFormId, const uint32_t acServerId, const entt::entity acEntity) const noexcept
{
    if (!m_actorBridge.ActorExists(acFormId))
    {
        spdlog::error("Cannot find actor to take control over, form id: {:X}, server id: {:X}", acFormId, acServerId);
        return false;
    }

    if (m_actorBridge.IsRemotePlayer(acFormId))
    {
        spdlog::error("Cannot take control over remote player actor, form id: {:X}, server id: {:X}", acFormId, acServerId);
        return false;
    }

    if (m_actorBridge.IsPlayerSummon(acFormId))
    {
        spdlog::error("Cannot take control over remote player summon, form id: {:X}, server id: {:X}", acFormId, acServerId);
        return false;
    }

    m_actorBridge.SetActorRemote(acFormId, false);

    // TODO(cosideci): this should be done differently.
    // Send an ownership claim request, and have the server broadcast the result.
    // Only then should components be added or removed.
    m_world.emplace_or_replace<LocalComponent>(acEntity, acServerId);
    m_world.emplace_or_replace<LocalAnimationComponent>(acEntity);
    DeleteRemoteEntityComponents(acEntity);

    return true;
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
        m_actorBridge.SetActorRemote(acEvent.FormId, true);
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

    auto& formIdComponent = view.get<FormIdComponent>(cId);
    CancelServerAssignment(*entityIt, formIdComponent.Id);

    if (m_world.all_of<FormIdComponent>(cId))
        m_world.remove<FormIdComponent>(cId);

    if (m_world.orphan(cId))
        m_world.destroy(cId);

    spdlog::info("Actor removed, form id: {:X}", acEvent.FormId);
}

void CharacterService::OnUpdate(const UpdateEvent& acUpdateEvent) noexcept
{
    RunActorDiscovery();
    RunArrivals();
    RunSpawnUpdates();
    RunLocalUpdates();
    RunFactionsUpdates();
    RunRemoteUpdates();
    RunExperienceUpdates();
}

// Dispatches ActorAddedEvent / ActorRemovedEvent as actors gain or lose loaded 3D, which drives
// server assignment (replaces the actor half of DiscoveryService, removed in ec7d02d1).
// Everyone announces their own player. Only the host announces NPCs: under the party tether
// (P2P_ARCHITECTURE.md, open question 4) every player is in the host's cell, so the host owns
// every actor and peers only replicate them.
void CharacterService::RunActorDiscovery() noexcept
{
    Set<uint32_t> loaded;
    const uint32_t cPlayerId = m_playerBridge.GetPlayerFormId();
    if (cPlayerId != 0 && m_actorBridge.ActorHas3D(cPlayerId))
        loaded.insert(cPlayerId);
    if (m_world.GetHostService().IsHosting())
        for (const uint32_t cFormId : m_actorBridge.GetLoadedActorFormIds())
            loaded.insert(cFormId);

    // Removals first, as the original DiscoveryService did.
    Vector<uint32_t> removed;
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

void CharacterService::OnConnected(const ConnectedEvent& acConnectedEvent) const noexcept
{
    // Go through all the forms that were previously detected
    auto view = m_world.view<FormIdComponent>(entt::exclude<ObjectComponent>);
    Vector<entt::entity> entities(view.begin(), view.end());
    spdlog::debug("CharacterService::OnConnected: processing {} discovered entities", entities.size());

    for (auto entity : entities)
    {
        auto& formIdComponent = m_world.get<FormIdComponent>(entity);
        // Delete all temporary actors on connect
        if (formIdComponent.Id > 0xFF000000)
        {
            m_actorBridge.DeleteActor(formIdComponent.Id);
            continue;
        }

        ProcessNewEntity(entity);
    }
}

void CharacterService::OnDisconnected(const DisconnectedEvent& acDisconnectedEvent) const noexcept
{
    auto remoteView = m_world.view<FormIdComponent, RemoteComponent>();
    for (auto entity : remoteView)
    {
        auto& formIdComponent = remoteView.get<FormIdComponent>(entity);

        if (!m_actorBridge.ActorExists(formIdComponent.Id))
            continue;

        if (m_actorBridge.IsRemotePlayer(formIdComponent.Id))
            m_actorBridge.DeleteActor(formIdComponent.Id);
        else
            m_actorBridge.SetActorRemote(formIdComponent.Id, false);
    }

    m_world.clear<WaitingForAssignmentComponent, LocalComponent, RemoteComponent>();
}

void CharacterService::OnAssignCharacter(const AssignCharacterResponse& acMessage) noexcept
{
    spdlog::info("Received for cookie {:X}, server id {:X}", acMessage.Cookie, acMessage.ServerId);

    auto view = m_world.view<WaitingForAssignmentComponent>();
    const auto itor = std::find_if(std::begin(view), std::end(view), [view, cookie = acMessage.Cookie](auto entity) { return view.get<WaitingForAssignmentComponent>(entity).Cookie == cookie; });

    if (itor == std::end(view))
    {
        spdlog::warn("Never found requested cookie: {}", acMessage.Cookie);
        return;
    }

    const auto cEntity = *itor;

    m_world.remove<WaitingForAssignmentComponent>(cEntity);

    const auto formIdComponent = m_world.try_get<FormIdComponent>(cEntity);
    if (!formIdComponent)
    {
        spdlog::error("{}: : form id component doesn't exist, cookie: {:X}", __FUNCTION__, acMessage.Cookie);
        return;
    }

    if (!m_actorBridge.ActorExists(formIdComponent->Id))
    {
        spdlog::error("{}: : actor not found, form id: {:X}", __FUNCTION__, formIdComponent->Id);
        m_world.destroy(cEntity);
        return;
    }

    // TODO: how could this possibly trigger?
    if (acMessage.PlayerId != 0)
        m_world.emplace_or_replace<PlayerComponent>(cEntity, acMessage.PlayerId);

    if (acMessage.Owner)
    {
        spdlog::info("Received local actor, form id: {:X}", formIdComponent->Id);

        m_world.emplace_or_replace<LocalComponent>(cEntity, acMessage.ServerId);
        m_world.emplace_or_replace<LocalAnimationComponent>(cEntity);

        m_actorBridge.SetActorRemote(formIdComponent->Id, false);

        // Nothing is re-sent: copies start in this actor's graph state (CharacterSpawnRequest::GraphState).
    }
    else
    {
        spdlog::info("Received remote actor, form id: {:X}, isweapondrawn: {}", formIdComponent->Id, acMessage.IsWeaponDrawn);

        m_world.emplace_or_replace<RemoteComponent>(cEntity, acMessage.ServerId, formIdComponent->Id);

        m_actorBridge.SetActorRemote(formIdComponent->Id, true);

        InterpolationSystem::Setup(m_world, cEntity);
        AnimationSystem::Setup(m_world, cEntity);

        m_actorBridge.SetActorValues(formIdComponent->Id, acMessage.AllActorValues);
        m_actorBridge.SetActorInventory(formIdComponent->Id, acMessage.CurrentInventory);

        const bool cIsDead = m_actorBridge.IsActorDead(formIdComponent->Id);
        if (cIsDead != acMessage.IsDead)
            acMessage.IsDead ? m_actorBridge.KillActor(formIdComponent->Id) : m_actorBridge.RespawnActor(formIdComponent->Id);

        m_world.emplace_or_replace<RemoteWeaponDrawComponent>(cEntity, acMessage.IsWeaponDrawn);

        MoveActor(formIdComponent->Id, acMessage.WorldSpaceId, acMessage.CellId, acMessage.Position);
    }
}

CharacterService::~CharacterService() noexcept
{
    m_playerBridge.StopTeleportEvents();
}

void CharacterService::OnCharacterSpawn(const CharacterSpawnRequest& acMessage) const noexcept
{
    auto remoteView = m_world.view<RemoteComponent>();
    const auto remoteItor = std::find_if(std::begin(remoteView), std::end(remoteView), [remoteView, Id = acMessage.ServerId](auto entity) { return remoteView.get<RemoteComponent>(entity).Id == Id; });

    if (remoteItor != std::end(remoteView))
    {
        spdlog::warn("Character with remote id {:X} is already spawned.", acMessage.ServerId);
        return;
    }

    uint32_t actorFormId = 0;

    std::optional<entt::entity> entity;

    // Custom forms
    if (acMessage.FormId == GameId{})
    {
        entity = m_world.create();

        if (acMessage.BaseId != GameId{})
        {
            const auto cNpcId = World::Get().GetModSystem().GetGameId(acMessage.BaseId);
            if (cNpcId == 0)
            {
                spdlog::error("Failed to retrieve NPC, it will not be spawned, possibly missing mod, base: {:X}:{:X}, form: {:X}:{:X}", acMessage.BaseId.BaseId, acMessage.BaseId.ModId, acMessage.FormId.BaseId, acMessage.FormId.ModId);
                return;
            }

            m_actorBridge.DeserializeActorNpc(cNpcId, acMessage.AppearanceBuffer, acMessage.ChangeFlags);
            actorFormId = m_actorBridge.SpawnActorFromNpc(cNpcId);
        }
        else
        {
            // Players and npcs with temporary ref ids and base ids (usually random events)
            actorFormId = m_actorBridge.CreateCustomActor(acMessage.AppearanceBuffer, acMessage.ChangeFlags);
            m_actorBridge.SetupActorFaceGen(actorFormId);
            FaceGenSystem::Setup(m_world, *entity, acMessage.FaceTints);
        }
    }
    else
    {
        const uint32_t cActorId = World::Get().GetModSystem().GetGameId(acMessage.FormId);

        auto waitingView = m_world.view<FormIdComponent, WaitingForAssignmentComponent>();
        const auto waitingItor = std::find_if(std::begin(waitingView), std::end(waitingView), [waitingView, cActorId](auto entity) { return waitingView.get<FormIdComponent>(entity).Id == cActorId; });

        if (waitingItor != std::end(waitingView))
        {
            spdlog::info("Character with form id {:X} already has a spawn request in progress.", cActorId);
            return;
        }

        if (!m_actorBridge.ActorExists(cActorId))
        {
            spdlog::error("Failed to retrieve Actor {:X}, it will not be spawned, possibly missing mod", cActorId);
            return;
        }

        actorFormId = cActorId;

        const auto view = m_world.view<FormIdComponent>();
        const auto itor = std::find_if(std::begin(view), std::end(view), [cActorId, view](entt::entity entity) { return view.get<FormIdComponent>(entity).Id == cActorId; });

        if (itor != std::end(view))
            entity = *itor;
        else
            entity = m_world.create();
    }

    if (!actorFormId)
    {
        spdlog::error("Actor object {:X} could not be created.", acMessage.ServerId);
        return;
    }

    spdlog::info("CharacterSpawnRequest, server id: {:X}, form id: {:X}", acMessage.ServerId, actorFormId);

    if (m_actorBridge.IsActorDisabled(actorFormId))
    {
        spdlog::warn("Disabled actor is being re-enabled: {:X}", actorFormId);
        m_actorBridge.EnableActor(actorFormId);
    }

    m_actorBridge.SetActorRemote(actorFormId, true);

    // Move to player cell
    const uint32_t cPlayerCellId = m_playerBridge.GetPlayerParentCellFormId();
    m_actorBridge.MoveActorTo(actorFormId, cPlayerCellId, acMessage.Position.x, acMessage.Position.y, acMessage.Position.z);
    m_actorBridge.SetActorRotation(actorFormId, acMessage.Rotation.x, acMessage.Rotation.y);

    m_actorBridge.SetActorValues(actorFormId, acMessage.InitialActorValues);

    m_actorBridge.SetActorPlayer(actorFormId, acMessage.IsPlayer);
    if (acMessage.IsPlayer)
    {
        m_actorBridge.SetActorIgnoreFriendlyHit(actorFormId, true);
        m_actorBridge.SetActorPlayerRespawnMode(actorFormId);
        m_world.emplace_or_replace<PlayerComponent>(*entity, acMessage.PlayerId);
    }

    const bool cIsDead = m_actorBridge.IsActorDead(actorFormId);
    if (cIsDead != acMessage.IsDead)
        acMessage.IsDead ? m_actorBridge.KillActor(actorFormId) : m_actorBridge.RespawnActor(actorFormId);

    spdlog::info("Spawn Request Is summon {}", acMessage.IsPlayerSummon);

    if (acMessage.IsPlayerSummon)
    {
        // Prevents remote summons agroing other players.
        const uint32_t cPlayerId = m_playerBridge.GetPlayerFormId();
        m_actorBridge.SetActorCommandingActor(actorFormId, cPlayerId);
    }

    auto& remoteComponent = m_world.emplace_or_replace<RemoteComponent>(*entity, acMessage.ServerId, actorFormId);
    // Link the engine actor now: DiscoveryService (removed in ec7d02d1) used to do this when the
    // actor entered a loaded cell, via ActorAddedEvent -> OnActorAdded. Without the link,
    // RunRemoteUpdates has no actor to apply interpolated movement to.
    m_world.emplace_or_replace<FormIdComponent>(*entity, actorFormId);

    auto& interpolationComponent = InterpolationSystem::Setup(m_world, *entity);
    interpolationComponent.Position = acMessage.Position;

    AnimationSystem::Setup(m_world, *entity);

    m_world.emplace_or_replace<WaitingFor3D>(*entity, acMessage);

    auto& remoteAnimationComponent = m_world.get<RemoteAnimationComponent>(*entity);
    remoteAnimationComponent.TimePoints.push_back(acMessage.LatestAction);
    if (!acMessage.GraphState.empty())
        m_world.emplace_or_replace<SpawnGraphStateComponent>(*entity, acMessage.GraphState);
}

void CharacterService::OnRemoteSpawnDataReceived(const NotifySpawnData& acMessage) noexcept
{
    auto view = m_world.view<FormIdComponent>(entt::exclude<ObjectComponent>);

    const auto itor = std::find_if(
        std::begin(view), std::end(view),
        [view, id = acMessage.Id](auto entity)
        {
            if (auto serverId = Utils::GetServerId(entity))
            {
                if (*serverId == id)
                    return true;
            }
            return false;
        });

    if (itor == std::end(view))
        return;

    if (auto* pWaitingFor3D = m_world.try_get<WaitingFor3D>(*itor))
    {
        pWaitingFor3D->SpawnRequest.InitialActorValues = acMessage.NewActorData.InitialActorValues;
        pWaitingFor3D->SpawnRequest.InventoryContent = acMessage.NewActorData.InitialInventory;
        pWaitingFor3D->SpawnRequest.IsDead = acMessage.NewActorData.IsDead;
        pWaitingFor3D->SpawnRequest.IsWeaponDrawn = acMessage.NewActorData.IsWeaponDrawn;
    }

    auto& formIdComponent = view.get<FormIdComponent>(*itor);

    if (!m_actorBridge.ActorExists(formIdComponent.Id))
        return;

    m_actorBridge.SetActorValues(formIdComponent.Id, acMessage.NewActorData.InitialActorValues);
    m_actorBridge.SetActorInventory(formIdComponent.Id, acMessage.NewActorData.InitialInventory);
    if (m_world.all_of<RemoteComponent>(*itor))
        m_world.emplace_or_replace<RemoteWeaponDrawComponent>(*itor, acMessage.NewActorData.IsWeaponDrawn);

    const bool cIsDead = m_actorBridge.IsActorDead(formIdComponent.Id);
    if (cIsDead != acMessage.NewActorData.IsDead)
        acMessage.NewActorData.IsDead ? m_actorBridge.KillActor(formIdComponent.Id) : m_actorBridge.RespawnActor(formIdComponent.Id);

    spdlog::info("Applied remote spawn data, actor form id: {:X}", formIdComponent.Id);
}

void CharacterService::OnReferencesMoveRequest(const ServerReferencesMoveRequest& acMessage) const noexcept
{
    auto view = m_world.view<RemoteComponent, InterpolationComponent, RemoteAnimationComponent>();

    for (const auto& [serverId, update] : acMessage.Updates)
    {
        auto itor = std::find_if(std::begin(view), std::end(view), [serverId = serverId, view](entt::entity entity) { return view.get<RemoteComponent>(entity).Id == serverId; });

        if (itor == std::end(view))
            continue;

        auto& interpolationComponent = view.get<InterpolationComponent>(*itor);
        auto& animationComponent = view.get<RemoteAnimationComponent>(*itor);
        const auto& movement = update.UpdatedMovement;

        InterpolationComponent::TimePoint point;
        point.Tick = acMessage.Tick;
        point.Position = movement.Position;
        point.Rotation = {movement.Rotation.x, 0.f, movement.Rotation.y};
        point.Variables = movement.Variables;
        point.Direction = movement.Direction;

        InterpolationSystem::AddPoint(interpolationComponent, point);

        for (const auto& action : update.ActionEvents)
        {
            animationComponent.TimePoints.push_back(action);
        }
    }
}

void CharacterService::OnActionEvent(const ActionEvent& acActionEvent) const noexcept
{
    auto view = m_world.view<LocalAnimationComponent, FormIdComponent>();

    const auto itor = std::find_if(std::begin(view), std::end(view), [id = acActionEvent.ActorId, view](entt::entity entity) { return view.get<FormIdComponent>(entity).Id == id; });

    if (itor != std::end(view))
    {
        auto& localComponent = view.get<LocalAnimationComponent>(*itor);

        localComponent.Append(acActionEvent);
    }
}

void CharacterService::OnFactionsChanges(const NotifyFactionsChanges& acEvent) const noexcept
{
    auto view = m_world.view<RemoteComponent, FormIdComponent, CacheComponent>();

    for (const auto& [id, factions] : acEvent.Changes)
    {
        const auto itor = std::find_if(std::begin(view), std::end(view), [id = id, view](entt::entity entity) { return view.get<RemoteComponent>(entity).Id == id; });

        if (itor != std::end(view))
        {
            auto& formIdComponent = view.get<FormIdComponent>(*itor);
            auto& cacheComponent = view.get<CacheComponent>(*itor);
            cacheComponent.FactionsContent = factions;

            m_actorBridge.SetActorFactions(formIdComponent.Id, cacheComponent.FactionsContent);
        }
    }
}

void CharacterService::OnRemoveCharacter(const NotifyRemoveCharacter& acMessage) const noexcept
{
    auto view = m_world.view<RemoteComponent>();

    const auto itor = std::find_if(std::begin(view), std::end(view), [id = acMessage.ServerId, view](entt::entity entity) { return view.get<RemoteComponent>(entity).Id == id; });

    if (itor != std::end(view))
    {
        const entt::entity cEntity = *itor;
        bool deleted = false;
        if (auto* pFormIdComponent = m_world.try_get<FormIdComponent>(cEntity))
        {
            const uint32_t cFormId = pFormIdComponent->Id;
            // A copy we spawned (temporary reference) goes with its owner's actor. Checking only for a
            // temporary *base* kept copies of real NPC bases (e.g. a placed chicken) as frozen leftovers.
            if (m_actorBridge.IsActorTemporary(cFormId) || m_actorBridge.IsActorNpcTemporary(cFormId))
            {
                m_actorBridge.DeleteActor(cFormId);
                deleted = true;
                spdlog::info("\tDeleted actor {:X}", cFormId);
            }
        }

        DeleteRemoteEntityComponents(cEntity);
        // The engine reuses a deleted copy's form id for the next one: an entity left holding it
        // shadowed the new copy in form id lookups (its hits were dropped). Peers never see actor
        // discovery remove a copy, so this is the only place it goes.
        if (deleted)
        {
            m_world.remove<FormIdComponent>(cEntity);
            if (m_world.orphan(cEntity))
                m_world.destroy(cEntity);
        }
    }
}

void CharacterService::OnNotifyRespawn(const NotifyRespawn& acMessage) const noexcept
{
    auto view = m_world.view<FormIdComponent, RemoteComponent>();
    const auto entityIt = std::find_if(view.begin(), view.end(), [view, id = acMessage.ActorId](auto aEntity) { return view.get<RemoteComponent>(aEntity).Id == id; });

    if (entityIt == view.end())
    {
        spdlog::error("Actor to respawn not found in: {:X}", acMessage.ActorId);
        return;
    }

    const auto cId = *entityIt;

    auto& formIdComponent = view.get<FormIdComponent>(cId);
    CancelServerAssignment(*entityIt, formIdComponent.Id);

    if (m_world.all_of<FormIdComponent>(cId))
        m_world.remove<FormIdComponent>(cId);

    if (m_world.orphan(cId))
        m_world.destroy(cId);

    RequestRespawn request;
    request.ActorId = acMessage.ActorId;

    m_transport.Send(request);
}

void CharacterService::OnBeastFormChange(const BeastFormChangeEvent& acEvent) const noexcept
{
    auto view = m_world.view<FormIdComponent>();

    const auto it = std::find_if(view.begin(), view.end(), [view](auto entity) { return view.get<FormIdComponent>(entity).Id == 0x14; });

    std::optional<uint32_t> serverIdRes = Utils::GetServerId(*it);
    if (!serverIdRes.has_value())
    {
        spdlog::error("{}: failed to find server id", __FUNCTION__);
        return;
    }

    uint32_t serverId = serverIdRes.value();

    RequestRespawn request;
    request.ActorId = serverId;

    // SerializeActorNpc returns the change flags bitmask
    request.ChangeFlags = m_actorBridge.SerializeActorNpc(0x14, request.AppearanceBuffer);

    m_transport.Send(request);
}

void CharacterService::OnMountEvent(const MountEvent& acEvent) const noexcept
{
    auto view = m_world.view<FormIdComponent>();

    const auto riderIt = std::find_if(std::begin(view), std::end(view), [id = acEvent.RiderID, view](auto entity) { return view.get<FormIdComponent>(entity).Id == id; });

    if (riderIt == std::end(view))
    {
        spdlog::warn("Rider not found, form id: {:X}", acEvent.RiderID);
        return;
    }

    const entt::entity cRiderEntity = *riderIt;

    std::optional<uint32_t> riderServerIdRes = Utils::GetServerId(cRiderEntity);
    if (!riderServerIdRes.has_value())
    {
        spdlog::error("{}: failed to find server id", __FUNCTION__);
        return;
    }

    const auto mountIt = std::find_if(std::begin(view), std::end(view), [id = acEvent.MountID, view](auto entity) { return view.get<FormIdComponent>(entity).Id == id; });

    if (mountIt == std::end(view))
    {
        spdlog::warn("Mount not found, form id: {:X}", acEvent.MountID);
        return;
    }

    const entt::entity cMountEntity = *mountIt;

    std::optional<uint32_t> mountServerIdRes = Utils::GetServerId(cMountEntity);
    if (!mountServerIdRes.has_value())
    {
        spdlog::error("{}: failed to find server id", __FUNCTION__);
        return;
    }

    if (m_world.try_get<RemoteComponent>(cMountEntity))
        TakeOwnership(acEvent.MountID, *mountServerIdRes, cMountEntity);

    MountRequest request;
    request.MountId = mountServerIdRes.value();
    request.RiderId = riderServerIdRes.value();

    m_transport.Send(request);
}

void CharacterService::OnNotifyMount(const NotifyMount& acMessage) const noexcept
{
    auto remoteView = m_world.view<RemoteComponent, FormIdComponent>();

    const auto riderIt = std::find_if(std::begin(remoteView), std::end(remoteView), [remoteView, Id = acMessage.RiderId](auto entity) { return remoteView.get<RemoteComponent>(entity).Id == Id; });

    if (riderIt == std::end(remoteView))
    {
        spdlog::warn("Rider with remote id {:X} not found.", acMessage.RiderId);
        return;
    }

    auto& riderFormIdComponent = remoteView.get<FormIdComponent>(*riderIt);
    const uint32_t cRiderFormId = riderFormIdComponent.Id;

    uint32_t mountFormId = 0;

    auto formView = m_world.view<FormIdComponent>();
    Vector<entt::entity> entities(formView.begin(), formView.end());

    // TODO(cosideci): remove this, cause of NotifyRelinquishControl?
    for (auto entity : entities)
    {
        std::optional<uint32_t> serverIdRes = Utils::GetServerId(entity);
        if (!serverIdRes.has_value())
        {
            spdlog::error("{}: failed to find server id", __FUNCTION__);
            continue;
        }

        uint32_t serverId = serverIdRes.value();

        if (serverId == acMessage.MountId)
        {
            auto& mountFormIdComponent = m_world.get<FormIdComponent>(entity);
            mountFormId = mountFormIdComponent.Id;

            if (m_world.all_of<LocalComponent>(entity))
            {
                m_world.remove<LocalAnimationComponent, LocalComponent>(entity);
                m_world.emplace_or_replace<RemoteComponent>(entity, acMessage.MountId, mountFormId);
            }

            m_actorBridge.SetActorRemote(mountFormId, true);

            InterpolationSystem::Setup(m_world, entity);
            AnimationSystem::Setup(m_world, entity);

            break;
        }
    }

    if (cRiderFormId && mountFormId)
        m_actorBridge.ActorInitiateMountPackage(cRiderFormId, mountFormId);
}

void CharacterService::OnInitPackageEvent(const InitPackageEvent& acEvent) const noexcept
{
    if (!m_transport.IsConnected())
        return;

    auto view = m_world.view<FormIdComponent>();

    const auto actorIt = std::find_if(std::begin(view), std::end(view), [id = acEvent.ActorId, view](auto entity) { return view.get<FormIdComponent>(entity).Id == id; });

    if (actorIt == std::end(view))
        return;

    const entt::entity cActorEntity = *actorIt;

    std::optional<uint32_t> actorServerIdRes = Utils::GetServerId(cActorEntity);
    if (!actorServerIdRes.has_value())
    {
        spdlog::error("{}: failed to find server id", __FUNCTION__);
        return;
    }

    NewPackageRequest request;
    request.ActorId = actorServerIdRes.value();
    if (!m_world.GetModSystem().GetServerModId(acEvent.PackageId, request.PackageId.ModId, request.PackageId.BaseId))
        return;

    m_transport.Send(request);
}

void CharacterService::OnNotifyNewPackage(const NotifyNewPackage& acMessage) const noexcept
{
    auto remoteView = m_world.view<RemoteComponent, FormIdComponent>();
    const auto remoteIt = std::find_if(std::begin(remoteView), std::end(remoteView), [remoteView, Id = acMessage.ActorId](auto entity) { return remoteView.get<RemoteComponent>(entity).Id == Id; });

    if (remoteIt == std::end(remoteView))
    {
        spdlog::warn("Actor for package with remote id {:X} not found.", acMessage.ActorId);
        return;
    }

    auto formIdComponent = remoteView.get<FormIdComponent>(*remoteIt);

    const uint32_t cPackageFormId = World::Get().GetModSystem().GetGameId(acMessage.PackageId);
    if (!cPackageFormId)
    {
        spdlog::warn("Actor package not found, base id: {:X}, mod id: {:X}", acMessage.PackageId.BaseId, acMessage.PackageId.ModId);
        return;
    }

    m_actorBridge.SetActorPackage(formIdComponent.Id, cPackageFormId);
}

void CharacterService::OnAddExperienceEvent(const AddExperienceEvent& acEvent) noexcept
{
    m_cachedExperience += acEvent.Experience;
}

void CharacterService::OnNotifySyncExperience(const NotifySyncExperience& acMessage) noexcept
{
    const int32_t cLastSkill = m_playerBridge.GetPlayerLastUsedCombatSkill();
    if (cLastSkill == -1)
        return;

    m_playerBridge.AddPlayerSkillExperience(cLastSkill, acMessage.Experience);
}

void CharacterService::OnDialogueEvent(const DialogueEvent& acEvent) noexcept
{
    if (!m_transport.IsConnected())
        return;

    auto view = m_world.view<FormIdComponent>(entt::exclude<ObjectComponent>);
    auto entityIt = std::find_if(view.begin(), view.end(), [view, formId = acEvent.ActorID](auto entity) { return view.get<FormIdComponent>(entity).Id == formId; });

    if (entityIt == view.end())
        return;

    auto serverIdRes = Utils::GetServerId(*entityIt);
    if (!serverIdRes)
    {
        spdlog::error("{}: server id not found for form id {:X}", __FUNCTION__, acEvent.ActorID);
        return;
    }

    DialogueRequest request{};
    request.ServerId = serverIdRes.value();
    request.SoundFilename = acEvent.VoiceFile;

    m_transport.Send(request);
}

void CharacterService::OnNotifyDialogue(const NotifyDialogue& acMessage) noexcept
{
    auto remoteView = m_world.view<RemoteComponent, FormIdComponent>();
    const auto remoteIt = std::find_if(std::begin(remoteView), std::end(remoteView), [remoteView, Id = acMessage.ServerId](auto entity) { return remoteView.get<RemoteComponent>(entity).Id == Id; });

    if (remoteIt == std::end(remoteView))
    {
        spdlog::warn("Actor for dialogue with remote id {:X} not found.", acMessage.ServerId);
        return;
    }

    auto formIdComponent = remoteView.get<FormIdComponent>(*remoteIt);

    m_actorBridge.ActorStopCurrentDialogue(formIdComponent.Id);
    m_actorBridge.ActorSpeakSound(formIdComponent.Id, std::string(acMessage.SoundFilename.c_str()));
}

void CharacterService::OnSubtitleEvent(const SubtitleEvent& acEvent) noexcept
{
    if (!m_transport.IsConnected())
        return;

    auto view = m_world.view<FormIdComponent>(entt::exclude<ObjectComponent>);
    auto entityIt = std::find_if(view.begin(), view.end(), [view, formId = acEvent.SpeakerID](auto entity) { return view.get<FormIdComponent>(entity).Id == formId; });

    if (entityIt == view.end())
        return;

    auto serverIdRes = Utils::GetServerId(*entityIt);
    if (!serverIdRes)
    {
        spdlog::error("{}: server id not found for form id {:X}", __FUNCTION__, acEvent.SpeakerID);
        return;
    }

    SubtitleRequest request{};
    request.ServerId = serverIdRes.value();
    request.Text = acEvent.Text;
    request.TopicFormId = acEvent.TopicFormID;

    m_transport.Send(request);
}

void CharacterService::OnNotifySubtitle(const NotifySubtitle& acMessage) noexcept
{
    auto remoteView = m_world.view<RemoteComponent, FormIdComponent>();
    const auto remoteIt = std::find_if(std::begin(remoteView), std::end(remoteView), [remoteView, Id = acMessage.ServerId](auto entity) { return remoteView.get<RemoteComponent>(entity).Id == Id; });

    if (remoteIt == std::end(remoteView))
    {
        spdlog::warn("Actor for dialogue with remote id {:X} not found.", acMessage.ServerId);
        return;
    }

    auto formIdComponent = remoteView.get<FormIdComponent>(*remoteIt);

    // SubtitleManager bridge integration: use actor bridge to speak a subtitle line
    // The topic form ID for Skyrim is not fully wired through the current bridge,
    // but the SubtitleManager call uses pInfo=nullptr fallback gracefully.
    m_actorBridge.ActorSpeakSound(formIdComponent.Id, std::string(acMessage.Text.c_str()));
}

void CharacterService::OnNotifyRelinquishControl(const NotifyRelinquishControl& acMessage) noexcept
{
    auto formView = m_world.view<FormIdComponent>();
    Vector<entt::entity> entities(formView.begin(), formView.end());

    // TODO(cosideci): this entity iteration shouldn't technically be necessary, just look for the local component
    for (auto entity : entities)
    {
        std::optional<uint32_t> serverIdRes = Utils::GetServerId(entity);
        if (!serverIdRes.has_value())
        {
            spdlog::error("{}: : failed to find server id for entity", __FUNCTION__);
            continue;
        }

        uint32_t serverId = serverIdRes.value();

        if (serverId == acMessage.ServerId)
        {
            auto& formIdComponent = m_world.get<FormIdComponent>(entity);

            if (m_world.all_of<LocalComponent>(entity))
            {
                m_world.remove<LocalAnimationComponent, LocalComponent>(entity);
                m_world.emplace_or_replace<RemoteComponent>(entity, acMessage.ServerId, formIdComponent.Id);
            }

            if (!m_actorBridge.ActorExists(formIdComponent.Id))
            {
                // Probably left the room and/or temporary.
                spdlog::info("{}: : no local Actor for serverId {:X} to relinquish", __FUNCTION__, serverId);
                continue;
            }

            m_actorBridge.SetActorRemote(formIdComponent.Id, true);

            InterpolationSystem::Setup(m_world, entity);
            AnimationSystem::Setup(m_world, entity);

            spdlog::info("{}: : relinquished control of actor {:X} with server id {:X}", __FUNCTION__, formIdComponent.Id, acMessage.ServerId);

            return;
        }
    }

    spdlog::error("Did not find actor to relinquish control of, server id {:X}", acMessage.ServerId);
}

void CharacterService::OnNotifyActorTeleport(const NotifyActorTeleport& acMessage) noexcept
{
    const uint32_t cActorId = World::Get().GetModSystem().GetGameId(acMessage.FormId);
    if (!m_actorBridge.ActorExists(cActorId))
    {
        spdlog::error("{}: : failed to retrieve actor to teleport.", __FUNCTION__);
        return;
    }

    MoveActor(cActorId, acMessage.WorldSpaceId, acMessage.CellId, acMessage.Position);

    spdlog::info("Successfully teleported actor, form id: {:X}, world space: {:X}, cell: {:X}, position: ({}, {}, {})", cActorId, acMessage.WorldSpaceId.BaseId, acMessage.CellId.BaseId, acMessage.Position.x, acMessage.Position.y, acMessage.Position.z);
}

void CharacterService::MoveActor(uint32_t aFormId, const GameId& acWorldSpaceId, const GameId& acCellId, const Vector3_NetQuantize& acPosition) const noexcept
{
    uint32_t cellFormId = 0;
    if (!acWorldSpaceId)
    {
        cellFormId = m_world.GetModSystem().GetGameId(acCellId);
    }
    else
    {
        const uint32_t cWorldSpaceId = m_world.GetModSystem().GetGameId(acWorldSpaceId);
        const GridCellCoords coordinates = GridCellCoords::CalculateGridCellCoords(acPosition);
        cellFormId = m_formBridge.LookupCellByCoords(cWorldSpaceId, coordinates.X, coordinates.Y);
    }

    if (!cellFormId)
    {
        spdlog::error("{}: : failed to fetch cell to teleport, actor: {:X}, worldspace: {:X}, cell: {:X}, position: {}, {}, {}", __FUNCTION__, aFormId, acWorldSpaceId.BaseId, acCellId.BaseId, acPosition.x, acPosition.y, acPosition.z);
        return;
    }

    m_actorBridge.MoveActorTo(aFormId, cellFormId, acPosition.x, acPosition.y, acPosition.z);
}

void CharacterService::ProcessNewEntity(entt::entity aEntity) const noexcept
{
    if (!m_transport.IsOnline())
        return;

    auto& formIdComponent = m_world.get<FormIdComponent>(aEntity);

    if (!m_actorBridge.ActorExists(formIdComponent.Id))
    {
        spdlog::warn("{}: : actor for new entity not found, form id: {:X}", __FUNCTION__, formIdComponent.Id);
        return;
    }

    if (auto* pRemoteComponent = m_world.try_get<RemoteComponent>(aEntity); pRemoteComponent)
    {
        spdlog::info("New entity remotely managed, form id: {:X}, server id: {:X}", formIdComponent.Id, pRemoteComponent->Id);
        return;
    }

    if (m_world.any_of<RemoteComponent, LocalComponent, WaitingForAssignmentComponent>(aEntity))
        return;

    m_actorBridge.SetupActorCache(formIdComponent.Id, static_cast<uint32_t>(entt::to_integral(aEntity)));

    RequestServerAssignment(aEntity);
}

std::optional<AssignCharacterRequest> CharacterService::BuildAssignment(const entt::entity aEntity) const noexcept
{
    const auto& formIdComponent = m_world.get<FormIdComponent>(aEntity);

    if (!m_actorBridge.ActorExists(formIdComponent.Id))
        return std::nullopt;

    // The actor needs an NPC base form. (Not the *template* base: that only exists for temporary
    // NPCs, so checking it here skipped the player and every placed NPC.)
    if (!m_actorBridge.GetActorBaseFormId(formIdComponent.Id))
    {
        spdlog::warn("RequestServerAssignment: no base form for {:X}", formIdComponent.Id);
        return std::nullopt;
    }

    AssignCharacterRequest message{};

    if (!m_world.GetModSystem().GetServerModId(formIdComponent.Id, message.ReferenceId))
    {
        spdlog::error("Server reference id not found for form id {:X}", formIdComponent.Id);
        return std::nullopt;
    }

    const uint32_t cParentCellId = m_actorBridge.GetActorParentCellFormId(formIdComponent.Id);
    if (!m_world.GetModSystem().GetServerModId(cParentCellId, message.CellId))
    {
        spdlog::error("Server cell id not found for cell id {:X}", cParentCellId);
        return std::nullopt;
    }

    const uint32_t cWorldSpaceId = m_actorBridge.GetActorWorldSpaceFormId(formIdComponent.Id);
    if (cWorldSpaceId)
    {
        if (!m_world.GetModSystem().GetServerModId(cWorldSpaceId, message.WorldSpaceId))
        {
            spdlog::error("Server worldspace id not found for {:X}", cWorldSpaceId);
            return std::nullopt;
        }
    }

    const auto cState = ReadLiveState(formIdComponent.Id);
    message.Position = cState.Position;
    message.Rotation = cState.Rotation;
    message.CurrentActorData = cState.Data;
    message.LatestAction = cState.LatestAction;
    message.GraphState = cState.GraphState;

    // Serialize the base form
    const auto isPlayer = (formIdComponent.Id == 0x14);
    const auto isTemporary = (formIdComponent.Id >= 0xFF000000);

    if (isPlayer)
    {
        m_actorBridge.MarkActorNpcChanged(formIdComponent.Id, 0x2000800);
    }

    const auto changeFlags = m_actorBridge.SerializeActorNpc(formIdComponent.Id, message.AppearanceBuffer);
    message.ChangeFlags = changeFlags;

    if (isPlayer)
    {
        message.FaceTints = m_playerBridge.GetPlayerFaceTints();
        message.QuestContent = m_playerBridge.GetPlayerQuestLog();
    }

    message.FactionsContent = m_actorBridge.GetActorFactions(formIdComponent.Id);
    message.IsDragon = m_actorBridge.IsActorDragon(formIdComponent.Id);
    message.IsMount = m_actorBridge.IsActorMount(formIdComponent.Id);

    const uint32_t cCommandingActor = m_actorBridge.GetActorCommandingActorFormId(formIdComponent.Id);
    message.IsPlayerSummon = (cCommandingActor == 0x14);

    if (isTemporary)
    {
        // The reference only exists in this game, so peers rebuild it from its NPC base. A
        // temporary base (leveled spawns) is described by its template; a real base (e.g. placeatme
        // of a game NPC) is sent as is. Without one the copy is a blank custom actor with the
        // default face and race.
        uint32_t cNpcTemplateId = m_actorBridge.GetActorNpcTemplateBaseFormId(formIdComponent.Id);
        if (!cNpcTemplateId && !m_actorBridge.IsActorNpcTemporary(formIdComponent.Id))
            cNpcTemplateId = m_actorBridge.GetActorBaseFormId(formIdComponent.Id);
        if (cNpcTemplateId && !m_world.GetModSystem().GetServerModId(cNpcTemplateId, message.FormId))
        {
            spdlog::error("Server NPC form id not found for form id {:X}", cNpcTemplateId);
            return std::nullopt;
        }
    }

    return message;
}

void CharacterService::RequestServerAssignment(const entt::entity aEntity) const noexcept
{
    if (!m_transport.IsOnline())
        return;

    static uint32_t sCookieSeed = 0;

    auto message = BuildAssignment(aEntity);
    if (!message)
        return;
    message->Cookie = sCookieSeed;

    spdlog::info("Request id: {:X}, cookie: {:X}, entity: {:X}", m_world.get<FormIdComponent>(aEntity).Id, sCookieSeed, to_integral(aEntity));

    if (m_transport.Send(*message))
    {
        m_world.emplace<WaitingForAssignmentComponent>(aEntity, sCookieSeed);

        sCookieSeed++;
    }
}

// What a peer needs to spawn a copy of one of our actors, as the embedded server built it from the
// assignment (CreateCharacter + Serialize): a game reference keeps its id, a player or temporary
// actor is rebuilt from its appearance and base.
Vector<CharacterSpawnRequest> CharacterService::BuildSpawnRequests(uint32_t aExcludedServerId) const noexcept
{
    Vector<CharacterSpawnRequest> spawns;

    // Ours, read from the engine as it is now.
    for (const auto cEntity : m_world.view<FormIdComponent, LocalComponent>())
    {
        const uint32_t cServerId = m_world.get<LocalComponent>(cEntity).Id;
        if (cServerId == aExcludedServerId)
            continue;
        if (const auto cAssignment = BuildAssignment(cEntity))
            spawns.push_back(CharacterSpawnRequest::FromAssignment(*cAssignment, cServerId, m_transport.GetLocalPlayerId()));
    }

    return spawns;
}

bool CharacterService::ReadCopy(uint32_t aServerId, CharacterSpawnRequest& aSpawn) const noexcept
{
    for (const auto cEntity : m_world.view<FormIdComponent, RemoteComponent>())
    {
        if (m_world.get<RemoteComponent>(cEntity).Id != aServerId)
            continue;
        const uint32_t cFormId = m_world.get<FormIdComponent>(cEntity).Id;
        if (!m_actorBridge.ActorExists(cFormId))
            return false;
        const auto cState = ReadLiveState(cFormId);
        aSpawn.Position = cState.Position;
        aSpawn.Rotation = cState.Rotation;
        aSpawn.InventoryContent = cState.Data.InitialInventory;
        aSpawn.InitialActorValues = cState.Data.InitialActorValues;
        aSpawn.IsDead = cState.Data.IsDead;
        aSpawn.IsWeaponDrawn = cState.Data.IsWeaponDrawn;
        aSpawn.LatestAction = cState.LatestAction;
        aSpawn.GraphState = cState.GraphState;
        return true;
    }
    return false;
}

void CharacterService::CancelServerAssignment(const entt::entity aEntity, const uint32_t aFormId) const noexcept
{
    if (m_world.all_of<RemoteComponent>(aEntity))
    {
        if (m_actorBridge.ActorExists(aFormId))
        {
            if (m_actorBridge.IsActorTemporary(aFormId) || m_actorBridge.IsActorNpcTemporary(aFormId))
            {
                spdlog::info("Temporary Remote Deleted {:X}", aFormId);
                m_actorBridge.DeleteActor(aFormId);
            }
            else
            {
                m_actorBridge.SetActorRemote(aFormId, false);
            }
        }

        DeleteRemoteEntityComponents(aEntity);

        return;
    }

    // We own it and stopped simulating it: the copies on other games must be removed too.
    if (const auto* pLocalComponent = m_world.try_get<LocalComponent>(aEntity))
    {
        RequestRemoveCharacter message;
        message.ServerId = pLocalComponent->Id;
        m_transport.Send(message);

        m_world.remove<LocalAnimationComponent, LocalComponent>(aEntity);
        return;
    }

    // In the event we were waiting for assignment, drop it
    if (m_world.all_of<WaitingForAssignmentComponent>(aEntity))
    {
        auto& waitingComponent = m_world.get<WaitingForAssignmentComponent>(aEntity);

        CancelAssignmentRequest message;
        message.Cookie = waitingComponent.Cookie;

        m_transport.Send(message);

        m_world.remove<WaitingForAssignmentComponent>(aEntity);
    }
}

uint32_t CharacterService::CreateCharacterForEntity(entt::entity aEntity) const noexcept
{
    auto* pWaitingFor3D = m_world.try_get<WaitingFor3D>(aEntity);
    auto* pInterpolationComponent = m_world.try_get<InterpolationComponent>(aEntity);

    if (!pWaitingFor3D || !pInterpolationComponent)
    {
        spdlog::error("{}: : could not find WaitingFor3D or InterpolationComponent", __FUNCTION__);
        return 0;
    }

    auto& acMessage = pWaitingFor3D->SpawnRequest;

    uint32_t actorFormId = 0;

    // Custom forms
    if (acMessage.FormId == GameId{})
    {
        if (acMessage.BaseId != GameId{})
        {
            const uint32_t cNpcId = World::Get().GetModSystem().GetGameId(acMessage.BaseId);
            if (cNpcId == 0)
            {
                spdlog::error("Failed to retrieve NPC, it will not be spawned, possibly missing mod");
                return 0;
            }

            m_actorBridge.DeserializeActorNpc(cNpcId, acMessage.AppearanceBuffer, acMessage.ChangeFlags);
            actorFormId = m_actorBridge.SpawnActorFromNpc(cNpcId);
        }
        else
        {
            actorFormId = m_actorBridge.CreateCustomActor(acMessage.AppearanceBuffer, acMessage.ChangeFlags);
            m_actorBridge.SetupActorFaceGen(actorFormId);
            FaceGenSystem::Setup(m_world, aEntity, acMessage.FaceTints);
        }
    }

    auto& remoteComponent = m_world.get<RemoteComponent>(aEntity);

    if (!actorFormId)
    {
        spdlog::error("{}: : could not spawn actor for remote server id {:X}.", __FUNCTION__, remoteComponent.Id);
        return 0;
    }

    m_actorBridge.SetActorRemote(actorFormId, true);

    const uint32_t cPlayerCellId = m_playerBridge.GetPlayerParentCellFormId();
    m_actorBridge.MoveActorTo(actorFormId, cPlayerCellId, pInterpolationComponent->Position.x, pInterpolationComponent->Position.y, pInterpolationComponent->Position.z);
    m_actorBridge.SetActorRotation(actorFormId, acMessage.Rotation.x, acMessage.Rotation.y);
    m_actorBridge.SetActorValues(actorFormId, acMessage.InitialActorValues);

    m_actorBridge.SetActorPlayer(actorFormId, acMessage.IsPlayer);
    if (acMessage.IsPlayer)
    {
        m_actorBridge.SetActorIgnoreFriendlyHit(actorFormId, true);
        m_actorBridge.SetActorPlayerRespawnMode(actorFormId);
        m_world.emplace_or_replace<PlayerComponent>(aEntity, acMessage.PlayerId);
    }

    const bool cIsDead = m_actorBridge.IsActorDead(actorFormId);
    if (cIsDead != acMessage.IsDead)
        acMessage.IsDead ? m_actorBridge.KillActor(actorFormId) : m_actorBridge.RespawnActor(actorFormId);

    spdlog::info("Spawned character for entity, server id: {:X}", remoteComponent.Id);

    return actorFormId;
}

CharacterService::ActorLiveState CharacterService::ReadLiveState(uint32_t aFormId) const noexcept
{
    ActorLiveState state;
    float x = 0.f, y = 0.f, z = 0.f;
    m_actorBridge.GetActorPosition(aFormId, x, y, z);
    state.Position = {x, y, z};
    m_actorBridge.GetActorRotation(aFormId, x, y, z);
    state.Rotation.x = x;
    state.Rotation.y = z;
    state.Data = BuildActorData(aFormId);
    state.LatestAction = m_actorBridge.GetActorLatestAnimation(aFormId);
    m_actorBridge.ActorSaveAnimationVariables(aFormId, state.LatestAction.Variables);
    state.GraphState = m_actorBridge.SaveActorGraphState(aFormId);
    return state;
}

ActorData CharacterService::BuildActorData(uint32_t aFormId) const noexcept
{
    ActorData actorData{};
    actorData.InitialActorValues = m_actorBridge.GetActorEssentialValues(aFormId);
    actorData.InitialInventory = m_actorBridge.GetActorInventory(aFormId);
    actorData.IsDead = m_actorBridge.IsActorDead(aFormId);
    actorData.IsWeaponDrawn = HandsUp(m_actorBridge.GetWeaponState(aFormId));

    return actorData;
}

void CharacterService::RunLocalUpdates() const noexcept
{
    static std::chrono::steady_clock::time_point lastSendTimePoint;
    constexpr auto cDelayBetweenSnapshots = 100ms;

    const auto now = std::chrono::steady_clock::now();
    if (now - lastSendTimePoint < cDelayBetweenSnapshots)
        return;

    lastSendTimePoint = now;

    ClientReferencesMoveRequest message;
    message.Tick = m_transport.GetTick();

    auto animatedLocalView = m_world.view<LocalComponent, LocalAnimationComponent, FormIdComponent>();

    for (auto entity : animatedLocalView)
    {
        auto& localComponent = animatedLocalView.get<LocalComponent>(entity);
        auto& animationComponent = animatedLocalView.get<LocalAnimationComponent>(entity);
        auto& formIdComponent = animatedLocalView.get<FormIdComponent>(entity);

        AnimationSystem::Serialize(m_world, message, localComponent, animationComponent, formIdComponent);
    }

    m_transport.Send(message);
}

void CharacterService::RunRemoteUpdates() noexcept
{
    // Delay by 300ms to let the interpolation system accumulate interpolation points
    const auto currentTick = m_transport.GetTick();
    const auto tick = currentTick > 300 ? currentTick - 300 : 0;

    // Interpolation has to keep running even if the actor is not in view, otherwise we will never know if we need to spawn it
    auto interpolatedEntities = m_world.view<RemoteComponent, InterpolationComponent>();

    static auto s_nextDiagnostic = std::chrono::steady_clock::now();
    const bool cLogDiagnostic = std::chrono::steady_clock::now() >= s_nextDiagnostic;
    if (cLogDiagnostic)
        s_nextDiagnostic = std::chrono::steady_clock::now() + std::chrono::seconds(2);

    for (auto entity : interpolatedEntities)
    {
        auto* pFormIdComponent = m_world.try_get<FormIdComponent>(entity);
        auto& interpolationComponent = interpolatedEntities.get<InterpolationComponent>(entity);

        auto& points = interpolationComponent.TimePoints;
        if (cLogDiagnostic)
            spdlog::debug("RunRemoteUpdates: entity {} formId {:X} points {} tick {} first {} last {}",
                          entt::to_integral(entity), pFormIdComponent ? pFormIdComponent->Id : 0, points.size(), tick,
                          points.empty() ? 0 : points.front().Tick, points.empty() ? 0 : points.back().Tick);
        if (points.size() < 2) continue;
        while (points.size() > 2 && tick > std::next(points.begin())->Tick)
            points.pop_front();
        const auto& first = points.front();
        const auto& second = *std::next(points.begin());
        glm::vec3 position{};
        glm::vec3 rotation{};
        if (!InterpolationSystem::Interpolate(first, second, tick, position, rotation))
            continue;
        interpolationComponent.Position = position;
        if (pFormIdComponent)
        {
            // The current snapshot's variables, never the next one's or older ones (graphs react to changes).
            const AnimationVariables* pVariables = &first.Variables;
            if (auto* pAnimation = m_world.try_get<RemoteAnimationComponent>(entity))
            {
                if (first.Tick > pAnimation->VariablesTick)
                {
                    pAnimation->Variables = first.Variables;
                    pAnimation->VariablesTick = first.Tick;
                }
                pVariables = &pAnimation->Variables;
            }
            const float cPosition[3]{position.x, position.y, position.z};
            const float cRotation[3]{rotation.x, rotation.y, rotation.z};
            m_actorBridge.ApplyRemoteMovement(pFormIdComponent->Id, cPosition, cRotation, second.Direction, *pVariables);
        }
    }

    auto animatedView = m_world.view<RemoteComponent, RemoteAnimationComponent, FormIdComponent>();

    for (auto entity : animatedView)
    {
        auto& animationComponent = animatedView.get<RemoteAnimationComponent>(entity);
        auto& formIdComponent = animatedView.get<FormIdComponent>(entity);

        m_actorBridge.ActorUpdateAnimation(formIdComponent.Id, tick);
    }

    auto facegenView = m_world.view<FormIdComponent, FaceGenComponent>();

    for (auto entity : facegenView)
    {
        auto& formIdComponent = facegenView.get<FormIdComponent>(entity);

        m_actorBridge.ActorUpdateFaceGen(formIdComponent.Id);
    }

    auto waitingView = m_world.view<FormIdComponent, WaitingFor3D>();

    Vector<entt::entity> toRemove;
    for (auto entity : waitingView)
    {
        auto& formIdComponent = waitingView.get<FormIdComponent>(entity);
        auto& waitingFor3D = waitingView.get<WaitingFor3D>(entity);

        if (!m_actorBridge.ActorHas3D(formIdComponent.Id))
            continue;

        m_actorBridge.SetActorInventory(formIdComponent.Id, waitingFor3D.SpawnRequest.InventoryContent);
        m_actorBridge.SetActorFactions(formIdComponent.Id, waitingFor3D.SpawnRequest.FactionsContent);
        m_actorBridge.ActorLoadAnimationVariables(formIdComponent.Id, waitingFor3D.SpawnRequest.LatestAction.Variables);
        if (m_world.all_of<RemoteComponent>(entity))
            m_world.emplace_or_replace<RemoteWeaponDrawComponent>(entity, waitingFor3D.SpawnRequest.IsWeaponDrawn);

        const bool cIsDead = m_actorBridge.IsActorDead(formIdComponent.Id);
        if (cIsDead != waitingFor3D.SpawnRequest.IsDead)
            waitingFor3D.SpawnRequest.IsDead ? m_actorBridge.KillActor(formIdComponent.Id) : m_actorBridge.RespawnActor(formIdComponent.Id);

        if (m_actorBridge.IsActorVampireLord(formIdComponent.Id))
            m_actorBridge.ActorFixVampireLordModel(formIdComponent.Id);

        toRemove.push_back(entity);

        spdlog::info("Applied 3D for actor, form id: {:X}", formIdComponent.Id);
    }

    for (auto entity : toRemove)
        m_world.remove<WaitingFor3D>(entity);
}

void CharacterService::RunFactionsUpdates() const noexcept
{
    static std::chrono::steady_clock::time_point lastSendTimePoint;
    constexpr auto cDelayBetweenSnapshots = 2000ms;

    const auto now = std::chrono::steady_clock::now();
    if (now - lastSendTimePoint < cDelayBetweenSnapshots)
        return;

    lastSendTimePoint = now;

    RequestFactionsChanges message;

    auto factionedActors = m_world.view<LocalComponent, CacheComponent, FormIdComponent>();
    for (auto entity : factionedActors)
    {
        auto& formIdComponent = factionedActors.get<FormIdComponent>(entity);
        auto& localComponent = factionedActors.get<LocalComponent>(entity);
        auto& cacheComponent = factionedActors.get<CacheComponent>(entity);

        auto factions = m_actorBridge.GetActorFactions(formIdComponent.Id);

        if (cacheComponent.FactionsContent == factions)
            continue;

        cacheComponent.FactionsContent = factions;

        // If not send the current factions and replace the cached factions
        message.Changes[localComponent.Id] = factions;
    }

    if (!message.Changes.empty())
        m_transport.Send(message);
}

void CharacterService::RunSpawnUpdates() const noexcept
{
    auto invisibleView = m_world.view<RemoteComponent, InterpolationComponent, RemoteAnimationComponent, WaitingFor3D>(entt::exclude<FormIdComponent>);
    Vector<entt::entity> entities(invisibleView.begin(), invisibleView.end());

    for (const auto entity : entities)
    {
        auto& remoteComponent = m_world.get<RemoteComponent>(entity);
        auto& interpolationComponent = m_world.get<InterpolationComponent>(entity);

        const uint32_t cPlayerWorldSpaceId = m_playerBridge.GetPlayerWorldSpaceId();
        if (cPlayerWorldSpaceId)
        {
            float characterX = interpolationComponent.Position.x;
            float characterY = interpolationComponent.Position.y;
            const auto characterCoords = GridCellCoords::CalculateGridCellCoords(characterX, characterY);

            float playerX = 0.f, playerY = 0.f, playerZ = 0.f;
            m_playerBridge.GetPlayerPosition(playerX, playerY, playerZ);
            const auto playerCoords = GridCellCoords::CalculateGridCellCoords(playerX, playerY);

            // TODO(cosideci): IsDragon probably shouldn't be straight up false here.
            if (GridCellCoords::IsCellInGridCell(characterCoords, playerCoords, false))
            {
                uint32_t actorFormId = remoteComponent.CachedRefId;
                if (!actorFormId || !m_actorBridge.ActorExists(actorFormId))
                {
                    actorFormId = CreateCharacterForEntity(entity);
                    if (!actorFormId)
                        continue;

                    remoteComponent.CachedRefId = actorFormId;
                }

                const uint32_t cPlayerCellId = m_playerBridge.GetPlayerParentCellFormId();
                m_actorBridge.MoveActorTo(actorFormId, cPlayerCellId, interpolationComponent.Position.x, interpolationComponent.Position.y, interpolationComponent.Position.z);
            }
        }
    }
}

// A coc teleports the player (PlayerCharacter::ConsumeTeleported; experimental: doors and fast
// travel are not validated yet), and the engine deletes the references of our copies with the old
// cell. Under the party tether everyone arrives where the host is: once in a cell again, our
// entities for the copies left behind are dropped (or the host's spawns would be refused as already
// spawned) and the host told, which sends what is here (HostCharacters::OnEnterCell); copies still
// here are kept, and their spawns refused. A teleport within the cell (moveto, setpos) keeps them
// all: respawned, they would lose their state. Walking and riding are no teleport. Runs here, not
// in the engine's event: the engine sends that mid-teleport with its cell locks held.
void CharacterService::RunArrivals() noexcept
{
    if (!m_transport.IsOnline())
    {
        m_arriving = false;
        return;
    }
    if (m_playerBridge.ConsumeTeleported())
        m_arriving = true;
    const uint32_t cCellId = m_playerBridge.GetPlayerParentCellFormId();
    if (!m_arriving || !cCellId)
        return;
    m_arriving = false;
    const uint32_t cWorldSpaceId = m_playerBridge.GetPlayerWorldSpaceId();

    DropCopies([&](uint32_t aFormId) {
        // Interiors: the same cell. Exteriors: the same worldspace, loaded around the player.
        const bool cHere = cWorldSpaceId ? m_actorBridge.GetActorWorldSpaceFormId(aFormId) == cWorldSpaceId
                                         : m_actorBridge.GetActorParentCellFormId(aFormId) == cCellId;
        return !m_actorBridge.ActorExists(aFormId) || !m_actorBridge.ActorHas3D(aFormId) || !cHere;
    });

    EnterCellRequest request;
    auto& modSystem = m_world.GetModSystem();
    if (!modSystem.GetServerModId(cCellId, request.CellId) || (cWorldSpaceId && !modSystem.GetServerModId(cWorldSpaceId, request.WorldSpaceId)))
    {
        spdlog::error("Arrived in cell {:X} (worldspace {:X}) the host cannot know: no mod id", cCellId, cWorldSpaceId);
        return;
    }
    float x, y, z;
    m_playerBridge.GetPlayerPosition(x, y, z);
    request.Position = glm::vec3(x, y, z);
    const auto cState = ReadLiveState(m_playerBridge.GetPlayerFormId());
    request.LatestAction = cState.LatestAction;
    request.GraphState = cState.GraphState;
    m_transport.Send(request);
    spdlog::info("Arrived in cell {:X} (worldspace {:X}): copies dropped, the host sends what is here", cCellId, cWorldSpaceId);
}

void CharacterService::DropCopies(const std::function<bool(uint32_t)>& acShouldDrop) noexcept
{
    auto remoteView = m_world.view<RemoteComponent>();
    const Vector<entt::entity> cCopies(remoteView.begin(), remoteView.end());
    for (const auto entity : cCopies)
    {
        const auto* pFormIdComponent = m_world.try_get<FormIdComponent>(entity);
        const uint32_t cFormId = pFormIdComponent ? pFormIdComponent->Id : 0;
        if (cFormId && !acShouldDrop(cFormId))
            continue;
        // Deletes what is left of a copy we spawned; the game's own references stop being copies.
        CancelServerAssignment(entity, cFormId);
        // Gone without discovery noticing: it must not report it removed later.
        m_discoveredActors.erase(cFormId);
        if (m_world.all_of<FormIdComponent>(entity))
            m_world.remove<FormIdComponent>(entity);
        if (m_world.orphan(entity))
            m_world.destroy(entity);
    }
}

void CharacterService::RunExperienceUpdates() noexcept
{
    static std::chrono::steady_clock::time_point lastSendTimePoint;
    constexpr auto cDelayBetweenSnapshots = 1000ms;

    const auto now = std::chrono::steady_clock::now();
    if (now - lastSendTimePoint < cDelayBetweenSnapshots)
        return;

    lastSendTimePoint = now;

    if (m_cachedExperience == 0.f)
        return;


    SyncExperienceRequest message;
    message.Experience = m_cachedExperience;

    m_cachedExperience = 0.f;

    m_transport.Send(message);

    spdlog::debug("Sending over experience {}", message.Experience);
}
