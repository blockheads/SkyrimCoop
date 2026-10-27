#include <Services/CharacterService.h>
#include <Services/CopyClock.h>
#include <Services/TransportService.h>
#include <Services/SessionService.h>

#include <Components.h>

#include <Events/ActorAddedEvent.h>
#include <Events/ActorRemovedEvent.h>
#include <Events/UpdateEvent.h>
#include <Events/ConnectedEvent.h>
#include <Events/DisconnectedEvent.h>
#include <Events/MountEvent.h>
#include <Events/DismountEvent.h>
#include <Events/AiStateChangeEvent.h>
#include <Events/PlayerLooksChangeEvent.h>
#include <Events/PlayerDeathEvent.h>
#include <Events/AddExperienceEvent.h>
#include <Events/FactionsChangeEvent.h>
#include <Events/ReferenceJumpEvent.h>
#include <Events/DialogueEvent.h>
#include <Events/SubtitleEvent.h>

#include <Structs/ActionEvent.h>
#include <Structs/GridCellCoords.h>
#include <Messages/AnnounceCharacterRequest.h>
#include <Messages/NotifyReferencesMove.h>
#include <Messages/ReferencesMoveRequest.h>
#include <Messages/NotifyCharacterSpawn.h>
#include <Messages/RequestFactionsChanges.h>
#include <Messages/NotifyFactionsChanges.h>
#include <Messages/NotifyRemoveCharacter.h>
#include <Messages/RequestRemoveCharacter.h>
#include <Messages/Relayed.h>
#include <Messages/RequestRespawn.h>
#include <Messages/EnterCellRequest.h>
#include <Messages/NotifyRespawn.h>
#include <Events/PlayerArrivedEvent.h>
#include <Services/TravelService.h>
#include <Services/AiStateService.h>
#include <Services/EffectVisualsService.h>

#include <Bridges/IActorBridge.h>
#include <Bridges/IFormBridge.h>
#include <Bridges/IPlayerBridge.h>

#include <Systems/InterpolationSystem.h>
#include <Systems/AnimationSystem.h>
#include <Systems/CacheSystem.h>
#include <Systems/FaceGenSystem.h>

#include <World.h>
#include <SessionIds.h>
#include <Services/HostService.h>
#include <FormIds.h>

namespace
{
// TESNPC change flags the player's copies are made from: race, sex and face (NPC_ChangeForm 0x2000000, 0x1000000, 0x800).
constexpr uint32_t kPlayerLooksFlags = 0x3000800;
} // namespace

CharacterService::CharacterService(World& aWorld, entt::dispatcher& aDispatcher, TransportService& aTransport,
                                   IActorBridge& aActorBridge, IFormBridge& aFormBridge, IPlayerBridge& aPlayerBridge) noexcept
    : m_world(aWorld)
    , m_dispatcher(aDispatcher)
    , m_transport(aTransport)
    , m_actorBridge(aActorBridge)
    , m_formBridge(aFormBridge)
    , m_playerBridge(aPlayerBridge)
{
    Listen<ActorAddedEvent, &CharacterService::OnActorAdded>(m_connections, m_dispatcher, this);
    Listen<ActorRemovedEvent, &CharacterService::OnActorRemoved>(m_connections, m_dispatcher, this);

    Listen<UpdateEvent, &CharacterService::OnUpdate>(m_connections, m_dispatcher, this);
    Listen<ActionEvent, &CharacterService::OnActionEvent>(m_connections, m_dispatcher, this);

    Listen<ConnectedEvent, &CharacterService::OnConnected>(m_connections, m_dispatcher, this);
    Listen<DisconnectedEvent, &CharacterService::OnDisconnected>(m_connections, m_dispatcher, this);

    Listen<NotifyCharacterSpawn, &CharacterService::OnCharacterSpawn>(m_connections, m_dispatcher, this);
    Listen<NotifyReferencesMove, &CharacterService::OnReferencesMoveRequest>(m_connections, m_dispatcher, this);
    Listen<NotifyFactionsChanges, &CharacterService::OnFactionsChanges>(m_connections, m_dispatcher, this);
    Listen<FactionsChangeEvent, &CharacterService::OnFactionsChange>(m_connections, m_dispatcher, this);
    Listen<ReferenceJumpEvent, &CharacterService::OnReferenceJump>(m_connections, m_dispatcher, this);
    Listen<NotifyRemoveCharacter, &CharacterService::OnRemoveCharacter>(m_connections, m_dispatcher, this);

    Listen<MountEvent, &CharacterService::OnMountEvent>(m_connections, m_dispatcher, this);
    Listen<NotifyMount, &CharacterService::OnNotifyMount>(m_connections, m_dispatcher, this);
    Listen<DismountEvent, &CharacterService::OnDismountEvent>(m_connections, m_dispatcher, this);
    Listen<NotifyDismount, &CharacterService::OnNotifyDismount>(m_connections, m_dispatcher, this);

    Listen<NotifyRespawn, &CharacterService::OnNotifyRespawn>(m_connections, m_dispatcher, this);
    Listen<PlayerArrivedEvent, &CharacterService::OnPlayerArrived>(m_connections, m_dispatcher, this);
    Listen<PlayerLooksChangeEvent, &CharacterService::OnPlayerLooksChange>(m_connections, m_dispatcher, this);
    Listen<PlayerRevivedEvent, &CharacterService::OnPlayerRevived>(m_connections, m_dispatcher, this);

    Listen<AddExperienceEvent, &CharacterService::OnAddExperienceEvent>(m_connections, m_dispatcher, this);
    Listen<NotifySyncExperience, &CharacterService::OnNotifySyncExperience>(m_connections, m_dispatcher, this);

    Listen<DialogueEvent, &CharacterService::OnDialogueEvent>(m_connections, m_dispatcher, this);
    Listen<NotifyDialogue, &CharacterService::OnNotifyDialogue>(m_connections, m_dispatcher, this);

    Listen<SubtitleEvent, &CharacterService::OnSubtitleEvent>(m_connections, m_dispatcher, this);
    Listen<NotifySubtitle, &CharacterService::OnNotifySubtitle>(m_connections, m_dispatcher, this);

}

void CharacterService::DeleteRemoteEntityComponents(entt::entity aEntity) const noexcept
{
    m_world.remove<FaceGenComponent, InterpolationComponent, RemoteAnimationComponent, RemoteComponent, CacheComponent, WaitingFor3D, PlayerComponent,
                   OwnerPlaceComponent>(aEntity);
}

bool CharacterService::TakeOwnership(const uint32_t acFormId, const uint32_t acSessionId, const entt::entity acEntity) const noexcept
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
    InterpolationSystem::Setup(m_world, aEntity);
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
    RunActorDiscovery();
    RunPresence();
    RunLocalUpdates();
    RunRemoteUpdates();
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
    if (m_world.GetHostService().IsHosting())
        for (const uint32_t cFormId : m_actorBridge.GetLoadedActorFormIds())
            loaded.insert(cFormId);

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

    // Go through all the forms that were previously detected
    auto view = m_world.view<FormIdComponent>();
    std::vector<entt::entity> entities(view.begin(), view.end());
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

void CharacterService::OnDisconnected(const DisconnectedEvent& acDisconnectedEvent) noexcept
{
    // The session's copies go: the ones we spawned are deleted, the game's own actors are its own again.
    DropCopies([](uint32_t) { return true; });
    const auto cHeld = m_world.view<HeldCopyComponent>();
    for (const auto cEntity : std::vector<entt::entity>(cHeld.begin(), cHeld.end()))
    {
        if (const auto* pFormId = m_world.try_get<FormIdComponent>(cEntity); pFormId && m_actorBridge.IsActorCopy(pFormId->Id))
            m_actorBridge.SetActorCopy(pFormId->Id, false);
        m_world.remove<HeldCopyComponent, FormIdComponent>(cEntity);
        if (m_world.orphan(cEntity))
            m_world.destroy(cEntity);
    }
    m_world.clear<LocalComponent>();
    m_playerId = 0;
    m_characterId = 0;
    m_riddenMount = 0;
}

CharacterService::~CharacterService() noexcept
{
    m_playerBridge.StopTeleportEvents();
}

void CharacterService::OnCharacterSpawn(const NotifyCharacterSpawn& acMessage) const noexcept
{
    auto remoteView = m_world.view<RemoteComponent>();
    const auto remoteItor = std::find_if(std::begin(remoteView), std::end(remoteView), [remoteView, Id = acMessage.SessionId](auto entity) { return remoteView.get<RemoteComponent>(entity).Id == Id; });

    if (remoteItor != std::end(remoteView))
    {
        RefreshCopy(*remoteItor, acMessage);
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
            const auto cNpcId = ToGame(acMessage.BaseId);
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
            actorFormId = m_actorBridge.CreateCustomActor(acMessage.AppearanceBuffer, acMessage.ChangeFlags, acMessage.Weight);
            m_actorBridge.SetupActorFaceGen(actorFormId);
            FaceGenSystem::Setup(m_world, *entity, acMessage.FaceTints);
        }
    }
    else
    {
        const uint32_t cActorId = ToGame(acMessage.FormId);

        auto ownView = m_world.view<FormIdComponent, LocalComponent>();
        const auto ownItor = std::find_if(std::begin(ownView), std::end(ownView), [ownView, cActorId](auto entity) { return ownView.get<FormIdComponent>(entity).Id == cActorId; });

        if (ownItor != std::end(ownView))
        {
            spdlog::info("Character with form id {:X} is this game's already.", cActorId);
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
        spdlog::error("Actor object {:X} could not be created.", acMessage.SessionId);
        return;
    }

    spdlog::info("NotifyCharacterSpawn, session id: {:X}, form id: {:X}", acMessage.SessionId, actorFormId);

    m_actorBridge.SetActorCopy(actorFormId, true);
    if (!ApplyPlacedBase(actorFormId, acMessage))
        return;

    if (m_actorBridge.IsActorDisabled(actorFormId))
    {
        spdlog::warn("Disabled actor is being re-enabled: {:X}", actorFormId);
        m_actorBridge.EnableActor(actorFormId);
    }

    // Where the copy goes: an NPC from the game's data (FormId set) to the cell its owner reported, even if
    // the player has left it since; a created actor (a player's copy) to the player's cell.
    uint32_t cellId = m_playerBridge.GetPlayerParentCellFormId();
    if (acMessage.FormId != GameId{})
    {
        if (const uint32_t cOwnerCellId = ToGame(acMessage.CellId))
            cellId = cOwnerCellId;
    }
    m_actorBridge.MoveActorTo(actorFormId, cellId, acMessage.Position.x, acMessage.Position.y, acMessage.Position.z);
    m_actorBridge.SetActorRotation(actorFormId, acMessage.Rotation.x, acMessage.Rotation.y);

    m_actorBridge.SetActorValues(actorFormId, acMessage.InitialActorValues);

    m_actorBridge.SetActorPlayer(actorFormId, acMessage.IsPlayer);
    if (acMessage.IsPlayer)
    {
        // Lives as long as the player is in the session and travels with the party, never with its cell
        // (docs/design/copy-contract.md, "Players' copies").
        m_actorBridge.SetActorPersistent(actorFormId);
        m_actorBridge.SetActorIgnoreFriendlyHit(actorFormId, true);
        m_actorBridge.SetActorPlayerFaction(actorFormId);
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

    m_world.emplace_or_replace<RemoteComponent>(*entity, acMessage.SessionId, actorFormId);
    m_world.remove<HeldCopyComponent>(*entity);
    // Link the engine actor now: DiscoveryService (removed in ec7d02d1) used to do this when the
    // actor entered a loaded cell, via ActorAddedEvent -> OnActorAdded. Without the link,
    // RunRemoteUpdates has no actor to apply interpolated movement to.
    m_world.emplace_or_replace<FormIdComponent>(*entity, actorFormId);

    auto& interpolationComponent = InterpolationSystem::Setup(m_world, *entity);
    interpolationComponent.Position = acMessage.Position;
    m_world.emplace_or_replace<OwnerPlaceComponent>(*entity, acMessage.CellId, GameId{}, std::chrono::steady_clock::now());

    AnimationSystem::Setup(m_world, *entity);

    m_world.emplace_or_replace<WaitingFor3D>(*entity, acMessage);

    auto& remoteAnimationComponent = m_world.get<RemoteAnimationComponent>(*entity);
    remoteAnimationComponent.TimePoints.push_back(acMessage.LatestAction);
    if (!acMessage.GraphState.empty())
        m_world.emplace_or_replace<SpawnGraphStateComponent>(*entity, acMessage.GraphState);
}

bool CharacterService::ApplyPlacedBase(uint32_t aFormId, const NotifyCharacterSpawn& acMessage) const noexcept
{
    if (acMessage.FormId == GameId{} || acMessage.BaseId == GameId{})
        return true;
    const uint32_t cBaseId = ToGame(acMessage.BaseId);
    if (cBaseId && m_actorBridge.SetActorBaseFormId(aFormId, cBaseId))
        return true;
    spdlog::error("Failed to apply owner's NPC base {:X}:{:X} to placed actor {:X}", acMessage.BaseId.ModId,
                  acMessage.BaseId.BaseId, aFormId);
    return false;
}

void CharacterService::RefreshCopy(entt::entity aEntity, const NotifyCharacterSpawn& acMessage) const noexcept
{
    const auto* pFormId = m_world.try_get<FormIdComponent>(aEntity);
    const uint32_t cOldBase = pFormId ? m_actorBridge.GetActorBaseFormId(pFormId->Id) : 0;
    if (pFormId && !ApplyPlacedBase(pFormId->Id, acMessage))
        return;
    const bool cBaseChanged = pFormId && cOldBase != m_actorBridge.GetActorBaseFormId(pFormId->Id);
    if (cBaseChanged)
        m_world.emplace_or_replace<WaitingFor3D>(aEntity, acMessage);

    // The owner's arrival: it is there now (RunPresence moves the copy), as the spawn would have it.
    if (auto* pInterpolation = m_world.try_get<InterpolationComponent>(aEntity))
    {
        pInterpolation->TimePoints.clear();
        pInterpolation->Position = acMessage.Position;
    }
    auto& place = m_world.get_or_emplace<OwnerPlaceComponent>(aEntity);
    place.CellId = acMessage.CellId;
    place.LastMove = {};
    if (auto* pWaitingFor3D = m_world.try_get<WaitingFor3D>(aEntity))
        pWaitingFor3D->SpawnRequest = acMessage;
    if (pFormId && (cBaseChanged || !m_actorBridge.ActorHas3D(pFormId->Id)) && !acMessage.GraphState.empty())
        m_world.emplace_or_replace<SpawnGraphStateComponent>(aEntity, acMessage.GraphState);
    spdlog::info("Character with remote id {:X} is already spawned: refreshed from the new spawn (cell {:X})", acMessage.SessionId,
                 acMessage.CellId.BaseId);
}

void CharacterService::OnReferencesMoveRequest(const NotifyReferencesMove& acMessage) const noexcept
{
    auto view = m_world.view<RemoteComponent, InterpolationComponent, RemoteAnimationComponent>();

    for (const auto& [sessionId, update] : acMessage.Updates)
    {
        auto itor = std::find_if(std::begin(view), std::end(view), [sessionId = sessionId, view](entt::entity entity) { return view.get<RemoteComponent>(entity).Id == sessionId; });

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
        point.Lying = movement.Lying;

        // Put there at once, or into another interior or worldspace: the copy starts there (RunPresence), never
        // sweeps across.
        bool teleported = movement.Teleported;
        if (movement.CellId)
        {
            auto& place = m_world.get_or_emplace<OwnerPlaceComponent>(*itor);
            teleported |= place.CellId && (movement.WorldSpaceId != place.WorldSpaceId || (!movement.WorldSpaceId && movement.CellId != place.CellId));
            place.CellId = movement.CellId;
            place.WorldSpaceId = movement.WorldSpaceId;
        }
        if (teleported)
        {
            interpolationComponent.TimePoints.clear();
            interpolationComponent.Position = movement.Position;
        }
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

        ActionEvent action = acActionEvent;
        AnimationSystem::TargetToNetwork(action);
        localComponent.Append(action);
    }
}

void CharacterService::OnFactionsChanges(const NotifyFactionsChanges& acEvent) const noexcept
{
    // Copies have no CacheComponent (only actors this game owns do, CharacterService::ProcessNewEntity).
    auto view = m_world.view<RemoteComponent, FormIdComponent>();

    for (const auto& [id, factions] : acEvent.Changes)
    {
        const auto itor = std::find_if(std::begin(view), std::end(view), [id = id, view](entt::entity entity) { return view.get<RemoteComponent>(entity).Id == id; });

        if (itor != std::end(view))
        {
            const uint32_t cFormId = view.get<FormIdComponent>(*itor).Id;
            if (auto* pCache = m_world.try_get<CacheComponent>(*itor))
                pCache->FactionsContent = factions;
            m_actorBridge.SetActorFactions(cFormId, factions);
        }
    }
}

void CharacterService::OnRemoveCharacter(const NotifyRemoveCharacter& acMessage) const noexcept
{
    auto view = m_world.view<RemoteComponent>();

    const auto itor = std::find_if(std::begin(view), std::end(view), [id = acMessage.SessionId, view](entt::entity entity) { return view.get<RemoteComponent>(entity).Id == id; });

    if (itor == std::end(view))
        return;

    const entt::entity cEntity = *itor;
    const auto* pFormIdComponent = m_world.try_get<FormIdComponent>(cEntity);
    const uint32_t cFormId = pFormIdComponent ? pFormIdComponent->Id : 0;
    // A copy we spawned (temporary reference) goes with its owner's actor. Checking only for a temporary *base*
    // kept copies of real NPC bases (e.g. a placed chicken) as frozen leftovers.
    bool deleted = false;
    if (cFormId && (m_actorBridge.IsActorTemporary(cFormId) || m_actorBridge.IsActorNpcTemporary(cFormId)))
    {
        m_actorBridge.DeleteActor(cFormId);
        deleted = true;
        spdlog::info("\tDeleted actor {:X}", cFormId);
    }
    else if (cFormId && m_actorBridge.IsActorCopy(cFormId))
        deleted = FollowOwner(cEntity, cFormId, acMessage.Whereabouts);

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

bool CharacterService::FollowOwner(entt::entity aEntity, uint32_t aFormId, const ActorWhereabouts& acWhereabouts) const noexcept
{
    // Still a copy: a guest never runs a host NPC (copy-contract.md, "When the owner's actor goes away").
    m_world.emplace_or_replace<HeldCopyComponent>(aEntity);
    switch (acWhereabouts.Where)
    {
    case ActorWhereabouts::State::kPlaced:
    {
        const uint32_t cCellId = m_world.GetTravelService().CellAt(acWhereabouts.WorldSpaceId, acWhereabouts.CellId, acWhereabouts.Position);
        if (!cCellId)
        {
            spdlog::warn("Copy {:X}: its owner's actor went to cell {:X} (worldspace {:X}), which this game lacks", aFormId,
                         acWhereabouts.CellId.BaseId, acWhereabouts.WorldSpaceId.BaseId);
            return false;
        }
        float x = 0.f, y = 0.f, z = 0.f;
        m_actorBridge.GetActorPosition(aFormId, x, y, z);
        const glm::vec3 cHere{x, y, z};
        if (m_actorBridge.GetActorParentCellFormId(aFormId) == cCellId && glm::distance(cHere, glm::vec3(acWhereabouts.Position)) < 64.f)
            return false;
        spdlog::info("Copy {:X} follows its owner's actor to cell {:X}", aFormId, cCellId);
        m_actorBridge.MoveActorTo(aFormId, cCellId, acWhereabouts.Position.x, acWhereabouts.Position.y, acWhereabouts.Position.z);
        m_actorBridge.SetActorRotation(aFormId, 0.f, acWhereabouts.Heading);
        return false;
    }
    case ActorWhereabouts::State::kDisabled:
    case ActorWhereabouts::State::kDeleted:
    {
        const bool cDelete = acWhereabouts.Where == ActorWhereabouts::State::kDeleted;
        spdlog::info("Copy {:X}: its owner's actor was {}", aFormId, cDelete ? "deleted" : "disabled");
        m_actorBridge.DisableActor(aFormId, cDelete);
        return cDelete;
    }
    default:
        return false;
    }
}

ActorWhereabouts CharacterService::ReadWhereabouts(uint32_t aFormId) const noexcept
{
    ActorWhereabouts where;
    if (!m_actorBridge.ActorExists(aFormId))
        return where;
    if (m_actorBridge.IsActorDeleted(aFormId))
        where.Where = ActorWhereabouts::State::kDeleted;
    else if (m_actorBridge.IsActorDisabled(aFormId))
        where.Where = ActorWhereabouts::State::kDisabled;
    else
    {
        const uint32_t cCellId = m_actorBridge.GetActorParentCellFormId(aFormId);
        const uint32_t cWorldSpaceId = m_actorBridge.GetActorWorldSpaceFormId(aFormId);
        const auto cCell = cCellId ? ToSession(cCellId) : std::nullopt;
        const auto cWorldSpace = cWorldSpaceId ? ToSession(cWorldSpaceId) : std::optional(GameId{});
        if (!cCell || !cWorldSpace)
            return {};
        where.CellId = *cCell;
        where.WorldSpaceId = *cWorldSpace;
        float x = 0.f, y = 0.f, z = 0.f;
        m_actorBridge.GetActorPosition(aFormId, x, y, z);
        where.Position = {x, y, z};
        m_actorBridge.GetActorRotation(aFormId, x, y, z);
        where.Heading = z;
        where.Where = ActorWhereabouts::State::kPlaced;
    }
    return where;
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

    RespawnCopy(*entityIt, acMessage.ActorId);
}

void CharacterService::RespawnCopy(entt::entity aEntity, uint32_t aSessionId) const noexcept
{
    ReleaseCharacter(aEntity, m_world.get<FormIdComponent>(aEntity).Id);

    m_world.remove<FormIdComponent, HeldCopyComponent>(aEntity);

    if (m_world.orphan(aEntity))
        m_world.destroy(aEntity);

    // Asked by a game that does not own it, the host answers with a fresh spawn (HostCharacters::OnRespawn).
    RequestRespawn request;
    request.ActorId = aSessionId;
    m_transport.Send(request);
}

void CharacterService::OnPlayerLooksChange(const PlayerLooksChangeEvent&) const noexcept
{
    auto view = m_world.view<FormIdComponent, LocalComponent>();
    const auto it = std::find_if(view.begin(), view.end(), [view](auto entity) { return view.get<FormIdComponent>(entity).Id == 0x14; });
    // Not announced yet: its announcement carries the looks.
    if (it == view.end() || !m_transport.IsOnline())
        return;

    RequestRespawn request;
    request.ActorId = view.get<LocalComponent>(*it).Id;
    m_actorBridge.MarkActorNpcChanged(0x14, kPlayerLooksFlags);
    request.ChangeFlags = m_actorBridge.SerializeActorNpc(0x14, request.AppearanceBuffer);
    request.FaceTints = m_playerBridge.GetPlayerFaceTints();
    request.Weight = m_actorBridge.GetActorBaseWeight(0x14);
    const auto cState = ReadLiveState(0x14);
    request.HasState = true;
    request.LatestAction = cState.LatestAction;
    request.GraphState = cState.GraphState;
    request.Data = cState.Data;

    spdlog::info("Player looks changed: our copies respawn as race {:X}", m_actorBridge.GetActorRaceFormId(0x14));
    m_transport.Send(request);
}

void CharacterService::OnPlayerRevived(const PlayerRevivedEvent&) const noexcept
{
    if (!m_transport.IsOnline())
        return;
    auto view = m_world.view<FormIdComponent, LocalComponent>();
    const auto it = std::ranges::find_if(view, [&view](auto aEntity) { return view.get<FormIdComponent>(aEntity).Id == 0x14; });
    if (it == view.end())
        return;

    const auto state = ReadLiveState(0x14);
    RequestRespawn request;
    request.ActorId = view.get<LocalComponent>(*it).Id;
    request.HasState = true;
    request.LatestAction = state.LatestAction;
    request.GraphState = state.GraphState;
    request.Data = state.Data;
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

    // Ours only: a copy getting on its mount replays its owner's.
    const auto* cpRider = m_world.try_get<LocalComponent>(*riderIt);
    if (!cpRider)
        return;

    const auto mountIt = std::find_if(std::begin(view), std::end(view), [id = acEvent.MountID, view](auto entity) { return view.get<FormIdComponent>(entity).Id == id; });

    if (mountIt == std::end(view))
    {
        spdlog::warn("Mount not found, form id: {:X}", acEvent.MountID);
        return;
    }

    const entt::entity cMountEntity = *mountIt;

    std::optional<uint32_t> mountSessionIdRes = Utils::GetSessionId(cMountEntity);
    if (!mountSessionIdRes.has_value())
    {
        spdlog::error("{}: failed to find session id", __FUNCTION__);
        return;
    }

    // The host decides who simulates the mount (HostCharacters::OnMount); its answer gives it to us.
    MountRequest request;
    request.MountId = mountSessionIdRes.value();
    request.RiderId = cpRider->Id;

    m_transport.Send(request);
}

void CharacterService::OnNotifyMount(const NotifyMount& acMessage) noexcept
{
    // Our player got on it: the host gave the mount to this game.
    for (const auto cRider : m_world.view<LocalComponent>())
    {
        if (m_world.get<LocalComponent>(cRider).Id != acMessage.RiderId)
            continue;
        for (const auto cMount : m_world.view<RemoteComponent, FormIdComponent>())
            if (m_world.get<RemoteComponent>(cMount).Id == acMessage.MountId &&
                TakeOwnership(m_world.get<FormIdComponent>(cMount).Id, acMessage.MountId, cMount))
                m_riddenMount = acMessage.MountId;
        return;
    }

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
    std::vector<entt::entity> entities(formView.begin(), formView.end());

    for (auto entity : entities)
    {
        std::optional<uint32_t> sessionIdRes = Utils::GetSessionId(entity);
        if (!sessionIdRes.has_value())
        {
            spdlog::error("{}: failed to find session id", __FUNCTION__);
            continue;
        }

        uint32_t sessionId = sessionIdRes.value();

        if (sessionId == acMessage.MountId)
        {
            mountFormId = m_world.get<FormIdComponent>(entity).Id;
            GiveUpOwnership(entity, acMessage.MountId, mountFormId);
            break;
        }
    }

    if (cRiderFormId && mountFormId)
        m_actorBridge.ActorPutOnMount(cRiderFormId, mountFormId);
}

void CharacterService::OnDismountEvent(const DismountEvent& acEvent) const noexcept
{
    if (!m_riddenMount || acEvent.RiderId != m_playerBridge.GetPlayerFormId())
        return;
    DismountRequest request;
    request.RiderId = m_characterId;
    request.MountId = m_riddenMount;
    m_transport.Send(request);
}

void CharacterService::OnNotifyDismount(const NotifyDismount& acMessage) noexcept
{
    for (const auto cEntity : m_world.view<FormIdComponent>())
    {
        if (Utils::GetSessionId(cEntity) != acMessage.MountId)
            continue;
        const uint32_t cFormId = m_world.get<FormIdComponent>(cEntity).Id;
        if (m_world.all_of<LocalComponent>(cEntity))
            GiveUpOwnership(cEntity, acMessage.MountId, cFormId);
        else if (m_world.GetHostService().IsHosting())
            TakeOwnership(cFormId, acMessage.MountId, cEntity);
        break;
    }
    if (m_riddenMount == acMessage.MountId)
        m_riddenMount = 0;
}

void CharacterService::OnAddExperienceEvent(const AddExperienceEvent& acEvent) noexcept
{
    if (!m_transport.IsConnected())
        return;
    SyncExperienceRequest message;
    message.Experience = acEvent.Experience;
    m_transport.Send(message);
    spdlog::debug("Sending over experience {}", message.Experience);
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

    auto view = m_world.view<FormIdComponent>();
    auto entityIt = std::find_if(view.begin(), view.end(), [view, formId = acEvent.ActorID](auto entity) { return view.get<FormIdComponent>(entity).Id == formId; });

    if (entityIt == view.end())
        return;

    auto sessionIdRes = Utils::GetSessionId(*entityIt);
    if (!sessionIdRes)
    {
        spdlog::error("{}: session id not found for form id {:X}", __FUNCTION__, acEvent.ActorID);
        return;
    }

    DialogueRequest request{};
    request.SessionId = sessionIdRes.value();
    request.SoundFilename = acEvent.VoiceFile;

    m_transport.Send(request);
}

void CharacterService::OnNotifyDialogue(const NotifyDialogue& acMessage) noexcept
{
    auto remoteView = m_world.view<RemoteComponent, FormIdComponent>();
    const auto remoteIt = std::find_if(std::begin(remoteView), std::end(remoteView), [remoteView, Id = acMessage.SessionId](auto entity) { return remoteView.get<RemoteComponent>(entity).Id == Id; });

    if (remoteIt == std::end(remoteView))
    {
        spdlog::warn("Actor for dialogue with remote id {:X} not found.", acMessage.SessionId);
        return;
    }

    auto formIdComponent = remoteView.get<FormIdComponent>(*remoteIt);
    // A copy not loaded yet has no process, which Actor::SpeakSound (37542) reads unchecked.
    if (!m_actorBridge.ActorHas3D(formIdComponent.Id))
        return;

    m_actorBridge.ActorStopCurrentDialogue(formIdComponent.Id);
    m_actorBridge.ActorSpeakSound(formIdComponent.Id, std::string(acMessage.SoundFilename.c_str()));
}

void CharacterService::OnSubtitleEvent(const SubtitleEvent& acEvent) noexcept
{
    if (!m_transport.IsConnected())
        return;

    auto view = m_world.view<FormIdComponent>();
    auto entityIt = std::find_if(view.begin(), view.end(), [view, formId = acEvent.SpeakerID](auto entity) { return view.get<FormIdComponent>(entity).Id == formId; });

    if (entityIt == view.end())
        return;

    auto sessionIdRes = Utils::GetSessionId(*entityIt);
    if (!sessionIdRes)
    {
        spdlog::error("{}: session id not found for form id {:X}", __FUNCTION__, acEvent.SpeakerID);
        return;
    }

    SubtitleRequest request{};
    request.SessionId = sessionIdRes.value();
    request.Text = acEvent.Text;
    request.TopicFormId = acEvent.TopicFormID;
    request.Force = acEvent.Force;

    m_transport.Send(request);
}

void CharacterService::OnNotifySubtitle(const NotifySubtitle& acMessage) noexcept
{
    auto remoteView = m_world.view<RemoteComponent, FormIdComponent>();
    const auto remoteIt = std::find_if(std::begin(remoteView), std::end(remoteView), [remoteView, Id = acMessage.SessionId](auto entity) { return remoteView.get<RemoteComponent>(entity).Id == Id; });

    if (remoteIt == std::end(remoteView))
    {
        spdlog::warn("Actor for dialogue with remote id {:X} not found.", acMessage.SessionId);
        return;
    }

    auto formIdComponent = remoteView.get<FormIdComponent>(*remoteIt);

    // The owner's subtitle text over the copy (its voice comes through NotifyDialogue).
    m_actorBridge.ActorShowSubtitle(formIdComponent.Id, std::string(acMessage.Text.c_str()), acMessage.TopicFormId, acMessage.Force);
}

void CharacterService::MoveActor(uint32_t aFormId, const GameId& acWorldSpaceId, const GameId& acCellId, const Vector3_NetQuantize& acPosition) const noexcept
{
    const uint32_t cCellFormId = m_world.GetTravelService().CellAt(acWorldSpaceId, acCellId, acPosition);
    if (!cCellFormId)
    {
        spdlog::error("{}: : failed to fetch cell to teleport, actor: {:X}, worldspace: {:X}, cell: {:X}, position: {}, {}, {}", __FUNCTION__, aFormId, acWorldSpaceId.BaseId, acCellId.BaseId, acPosition.x, acPosition.y, acPosition.z);
        return;
    }

    m_actorBridge.MoveActorTo(aFormId, cCellFormId, acPosition.x, acPosition.y, acPosition.z);
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
        spdlog::info("New entity remotely managed, form id: {:X}, session id: {:X}", formIdComponent.Id, pRemoteComponent->Id);
        return;
    }

    if (m_world.any_of<RemoteComponent, LocalComponent>(aEntity))
        return;

    m_actorBridge.SetupActorCache(formIdComponent.Id, static_cast<uint32_t>(entt::to_integral(aEntity)));

    AnnounceCharacter(aEntity);
}

std::optional<AnnounceCharacterRequest> CharacterService::BuildAnnouncement(const entt::entity aEntity) const noexcept
{
    const auto& formIdComponent = m_world.get<FormIdComponent>(aEntity);

    if (!m_actorBridge.ActorExists(formIdComponent.Id))
        return std::nullopt;

    // The actor needs an NPC base form. (Not the *template* base: that only exists for temporary
    // NPCs, so checking it here skipped the player and every placed NPC.)
    if (!m_actorBridge.GetActorBaseFormId(formIdComponent.Id))
    {
        spdlog::warn("AnnounceCharacter: no base form for {:X}", formIdComponent.Id);
        return std::nullopt;
    }

    AnnounceCharacterRequest message{};

    // A created actor goes by its created id: the spawn rebuilds it from its base (NotifyCharacterSpawn).
    const auto cReference = ToSessionOrCreated(formIdComponent.Id);
    if (!cReference)
        return std::nullopt;
    message.ReferenceId = *cReference;

    const auto cCell = ToSession(m_actorBridge.GetActorParentCellFormId(formIdComponent.Id));
    if (!cCell)
        return std::nullopt;
    message.CellId = *cCell;

    if (const uint32_t cWorldSpaceId = m_actorBridge.GetActorWorldSpaceFormId(formIdComponent.Id))
    {
        const auto cWorldSpace = ToSession(cWorldSpaceId);
        if (!cWorldSpace)
            return std::nullopt;
        message.WorldSpaceId = *cWorldSpace;
    }

    const auto cState = ReadLiveState(formIdComponent.Id);
    message.Position = cState.Position;
    message.Rotation = cState.Rotation;
    message.CurrentActorData = cState.Data;
    message.LatestAction = cState.LatestAction;
    message.GraphState = cState.GraphState;
    if (const auto* pAiState = m_world.ctx().find<AiStateService>())
        message.AiState = pAiState->ReadState(formIdComponent.Id);
    if (const auto* pEffects = m_world.ctx().find<EffectVisualsService>())
        message.EffectIds = pEffects->ReadEffects(formIdComponent.Id);

    // Serialize the base form
    const auto isPlayer = (formIdComponent.Id == 0x14);

    if (isPlayer)
    {
        m_actorBridge.MarkActorNpcChanged(formIdComponent.Id, kPlayerLooksFlags);
    }

    const auto changeFlags = m_actorBridge.SerializeActorNpc(formIdComponent.Id, message.AppearanceBuffer);
    message.ChangeFlags = changeFlags;
    message.Weight = m_actorBridge.GetActorBaseWeight(formIdComponent.Id);

    if (isPlayer)
    {
        message.FaceTints = m_playerBridge.GetPlayerFaceTints();
    }

    message.FactionsContent = m_actorBridge.GetActorFactions(formIdComponent.Id);
    message.IsDragon = m_actorBridge.IsActorDragon(formIdComponent.Id);
    message.IsMount = m_actorBridge.IsActorMount(formIdComponent.Id);

    const uint32_t cCommandingActor = m_actorBridge.GetActorCommandingActorFormId(formIdComponent.Id);
    message.IsPlayerSummon = (cCommandingActor == 0x14);

    if (!isPlayer)
    {
        // Placed references can roll different leveled NPCs in each game too. Send the owner's
        // chosen base (a temporary base's template), for both placed and created copies.
        uint32_t cNpcTemplateId = m_actorBridge.GetActorNpcTemplateBaseFormId(formIdComponent.Id);
        if (!cNpcTemplateId && !m_actorBridge.IsActorNpcTemporary(formIdComponent.Id))
            cNpcTemplateId = m_actorBridge.GetActorBaseFormId(formIdComponent.Id);
        if (cNpcTemplateId)
        {
            const auto cTemplate = ToSession(cNpcTemplateId);
            if (!cTemplate)
                return std::nullopt;
            message.FormId = *cTemplate;
        }
    }

    return message;
}

void CharacterService::AnnounceCharacter(const entt::entity aEntity) const noexcept
{
    if (!m_transport.IsOnline())
        return;

    const uint32_t cFormId = m_world.get<FormIdComponent>(aEntity).Id;
    const bool cIsPlayer = cFormId == m_playerBridge.GetPlayerFormId();
    auto message = BuildAnnouncement(aEntity);
    if (!message)
        return;
    // The id the host gave it: the player's from the join, the host's game's other actors' from its rules.
    message->SessionId = cIsPlayer ? m_characterId : m_world.GetHostService().NewCharacterId();
    if (!message->SessionId)
        return;

    m_world.emplace_or_replace<LocalComponent>(aEntity, message->SessionId);
    // Its actions from here on follow the graph state just sent.
    (void)m_world.get_or_emplace<LocalAnimationComponent>(aEntity);
    m_actorBridge.SetActorCopy(cFormId, false);
    if (cIsPlayer)
        m_world.emplace_or_replace<PlayerComponent>(aEntity, m_playerId);

    spdlog::info("Announced {:X} as {:X}", cFormId, message->SessionId);
    m_transport.Send(*message);
}

// What another game needs to spawn a copy of one of our actors, built from its announcement: a game reference
// keeps its id, a player or temporary actor is rebuilt from its appearance and base.
std::vector<NotifyCharacterSpawn> CharacterService::BuildSpawnRequests(uint32_t aExcludedSessionId) const noexcept
{
    std::vector<NotifyCharacterSpawn> spawns;

    // Ours, read from the engine as it is now.
    for (const auto cEntity : m_world.view<FormIdComponent, LocalComponent>())
    {
        const uint32_t cSessionId = m_world.get<LocalComponent>(cEntity).Id;
        if (cSessionId == aExcludedSessionId)
            continue;
        // An NPC whose cell just unloaded is gone (discovery drops it on its next update).
        const uint32_t cFormId = m_world.get<FormIdComponent>(cEntity).Id;
        if (cFormId != m_playerBridge.GetPlayerFormId() && !m_actorBridge.ActorHas3D(cFormId))
            continue;
        if (const auto cAssignment = BuildAnnouncement(cEntity))
            spawns.push_back(NotifyCharacterSpawn::FromAnnouncement(*cAssignment, cSessionId, m_transport.GetLocalPlayerId()));
    }

    return spawns;
}

bool CharacterService::ReadCopy(uint32_t aSessionId, NotifyCharacterSpawn& aSpawn) const noexcept
{
    for (const auto cEntity : m_world.view<FormIdComponent, RemoteComponent>())
    {
        if (m_world.get<RemoteComponent>(cEntity).Id != aSessionId)
            continue;
        const uint32_t cFormId = m_world.get<FormIdComponent>(cEntity).Id;
        if (!m_actorBridge.ActorExists(cFormId))
            return false;
        const auto cState = ReadLiveState(cFormId);
        aSpawn.Position = cState.Position;
        aSpawn.Rotation = cState.Rotation;
        aSpawn.InventoryContent = cState.Data.InitialInventory;
        aSpawn.IsDead = cState.Data.IsDead;
        aSpawn.IsWeaponDrawn = cState.Data.IsWeaponDrawn;
        aSpawn.LatestAction = cState.LatestAction;
        aSpawn.GraphState = cState.GraphState;
        return true;
    }
    return false;
}

void CharacterService::ReleaseCharacter(const entt::entity aEntity, const uint32_t aFormId) const noexcept
{
    if (m_world.all_of<RemoteComponent>(aEntity))
    {
        // Only while it is still our copy: a deleted copy's form id can belong to a new reference already.
        if (m_actorBridge.IsActorCopy(aFormId))
        {
            if (m_actorBridge.IsActorTemporary(aFormId) || m_actorBridge.IsActorNpcTemporary(aFormId))
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

        m_world.remove<LocalAnimationComponent, LocalComponent>(aEntity);
        return;
    }
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
    AnimationSystem::TargetToNetwork(state.LatestAction);
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

void CharacterService::RunLocalUpdates() noexcept
{
    constexpr auto cDelayBetweenSnapshots = 100ms;

    const auto now = std::chrono::steady_clock::now();
    if (now - m_lastSnapshot < cDelayBetweenSnapshots)
        return;

    m_lastSnapshot = now;

    ReferencesMoveRequest message;
    message.Tick = m_transport.GetTick();

    auto animatedLocalView = m_world.view<LocalComponent, LocalAnimationComponent, FormIdComponent>();

    for (auto entity : animatedLocalView)
    {
        auto& localComponent = animatedLocalView.get<LocalComponent>(entity);
        auto& animationComponent = animatedLocalView.get<LocalAnimationComponent>(entity);
        auto& formIdComponent = animatedLocalView.get<FormIdComponent>(entity);

        AnimationSystem::Serialize(m_world, message, localComponent, animationComponent, formIdComponent);
        const auto it = message.Updates.find(localComponent.Id);
        if (m_jumped.erase(formIdComponent.Id) && it != message.Updates.end())
            it->second.UpdatedMovement.Teleported = true;
        if (it != message.Updates.end())
            it->second.UpdatedMovement.Lying = m_actorBridge.IsLyingInRagdoll(formIdComponent.Id);
    }
    m_jumped.clear(); // jumps of actors that are not ours

    m_transport.Send(message);
}

void CharacterService::RunRemoteUpdates() noexcept
{
    // Behind the owners' time, to have points to interpolate between; a copy's ragdoll plays on the same clock.
    const auto currentTick = m_transport.GetTick();
    const auto tick = currentTick > kCopyDelayMs ? currentTick - kCopyDelayMs : 0;

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
            // Its owner lying, or the copy itself: its own ragdoll places it. Up again, it is eased from where its
            // engine stood it onto its owner's place (physics-sync.md, "A copy's ragdoll, steered").
            auto& ease = interpolationComponent.Ease;
            const bool cOwnerLies = tick >= second.Tick ? second.Lying : first.Lying;
            const bool cPlace = !cOwnerLies && !m_actorBridge.IsLyingInRagdoll(pFormIdComponent->Id);
            if (!cPlace)
                ease.Held = true;
            else if (std::exchange(ease.Held, false))
            {
                glm::vec3 own{}, ownRotation{};
                m_actorBridge.GetActorPosition(pFormIdComponent->Id, own.x, own.y, own.z);
                m_actorBridge.GetActorRotation(pFormIdComponent->Id, ownRotation.x, ownRotation.y, ownRotation.z);
                ease.Begin(own - position, ownRotation.z - rotation.z, currentTick);
            }
            if (cPlace)
                ease.Apply(currentTick, position, rotation.z);
            const float cPosition[3]{position.x, position.y, position.z};
            const float cRotation[3]{rotation.x, rotation.y, rotation.z};
            m_actorBridge.ApplyRemoteMovement(pFormIdComponent->Id, cPosition, cRotation, second.Direction, *pVariables, cPlace);
        }
    }

    auto animatedView = m_world.view<RemoteComponent, RemoteAnimationComponent, FormIdComponent>();

    for (auto entity : animatedView)
    {
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

    std::vector<entt::entity> toRemove;
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

void CharacterService::OnReferenceJump(const ReferenceJumpEvent& acEvent) noexcept
{
    m_jumped.insert(acEvent.FormId);
}

void CharacterService::OnFactionsChange(const FactionsChangeEvent& acEvent) const noexcept
{
    if (!m_transport.IsConnected())
        return;
    auto view = m_world.view<LocalComponent, CacheComponent, FormIdComponent>();
    for (const auto cEntity : view)
    {
        if (view.get<FormIdComponent>(cEntity).Id != acEvent.ActorId)
            continue;
        auto& cacheComponent = view.get<CacheComponent>(cEntity);
        auto factions = m_actorBridge.GetActorFactions(acEvent.ActorId);
        if (cacheComponent.FactionsContent == factions)
            return;
        cacheComponent.FactionsContent = factions;

        RequestFactionsChanges message;
        message.Changes[view.get<LocalComponent>(cEntity).Id] = std::move(factions);
        m_transport.Send(message);
        return;
    }
}

// Tells the host, which sends what is here and, when the player led, takes the party along.
void CharacterService::OnPlayerArrived(const PlayerArrivedEvent& acEvent) noexcept
{
    const uint32_t cCellId = acEvent.CellId;
    const uint32_t cWorldSpaceId = acEvent.WorldSpaceId;

    // The other players' copies travel with the party (RunPresence moves each to its owner); the others stay with
    // their cell.
    std::unordered_set<uint32_t> playerCopies;
    for (const auto cEntity : m_world.view<RemoteComponent, PlayerComponent, FormIdComponent>())
    {
        playerCopies.insert(m_world.get<FormIdComponent>(cEntity).Id);
        // Moved at once, not after the spacing between two moves of the same copy.
        if (auto* pPlace = m_world.try_get<OwnerPlaceComponent>(cEntity))
            pPlace->LastMove = {};
    }
    DropCopies([&](uint32_t aFormId) {
        if (playerCopies.count(aFormId) && m_actorBridge.IsActorCopy(aFormId))
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
        const bool cCopyHere = place.WorldSpaceId
                                   ? m_actorBridge.GetActorWorldSpaceFormId(cFormId) == cPlayerWorldSpaceId
                                   : m_actorBridge.GetActorParentCellFormId(cFormId) == cPlayerCellId;
        if (cCopyHere && m_actorBridge.ActorHas3D(cFormId))
            continue;

        place.LastMove = cNow;
        Vector3_NetQuantize position;
        position = cPosition;
        spdlog::info("Copy {:X} is not where its owner is (cell {:X}, worldspace {:X}): moved there", cFormId, place.CellId.BaseId,
                     place.WorldSpaceId.BaseId);
        MoveActor(cFormId, place.WorldSpaceId, place.CellId, position);
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
            continue;
        if (m_world.all_of<FormIdComponent>(entity))
            m_world.remove<FormIdComponent>(entity);
        if (m_world.orphan(entity))
            m_world.destroy(entity);
    }
}
