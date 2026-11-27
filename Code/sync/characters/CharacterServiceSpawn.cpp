// CharacterService: spawning copies, announcing this game's actors, looks and respawns.
#include <characters/NpcSnapshot.h>
#include <characters/ActorPools.h>
#include <characters/CharacterService.h>
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

#include <characters/ActorAddedEvent.h>
#include <core/PlayerLooksChangeEvent.h>
#include <characters/ActorHeadUpdateEvent.h>

#include <Messages/AnnounceCharacterRequest.h>
#include <Messages/NotifyCharacterSpawn.h>
#include <Messages/RequestRespawn.h>
#include <Messages/NotifyRespawn.h>
#include <Messages/NotifyPlayerLevel.h>
#include <core/CopyRecords.h>
#include <magic/EffectVisualsService.h>

#include <ports/IActorBridge.h>
#include <ports/IFormBridge.h>
#include <ports/IPlayerBridge.h>

#include <movement/SnapshotAge.h>
#include <movement/InterpolationSystem.h>
#include <animation/AnimationSystem.h>

#include <core/World.h>
#include <core/SessionIds.h>
#include <core/FormIds.h>

#include <Messages/Relayed.h>
#include <travel/TravelService.h>
#include <core/EngineNames.h>
#include <core/Hosting.h>
#include <characters/CharacterInternal.h>
#include <mounts/MountService.h>

void CharacterService::SpawnPending(uint32_t aFormId) noexcept
{
    const auto it = std::ranges::find_if(m_pendingSpawns, [&](const auto& acPending) { return ToGame(acPending.second.FormId) == aFormId; });
    if (it == m_pendingSpawns.end())
        return;
    const NotifyCharacterSpawn cSpawn = std::move(it->second);
    m_pendingSpawns.erase(it);
    OnCharacterSpawn(cSpawn);
}

void CharacterService::OnCharacterSpawn(const NotifyCharacterSpawn& acMessage) noexcept
{
    if (acMessage.FormId != GameId{})
    {
        const uint32_t cFormId = ToGame(acMessage.FormId);
        if (cFormId && (!m_actorBridge.ActorExists(cFormId) || m_actorBridge.IsActorStillLoading(cFormId)))
        {
            m_pendingSpawns[acMessage.SessionId] = acMessage;
            return;
        }
    }
    m_pendingSpawns.erase(acMessage.SessionId);
    const auto& actors = m_world.GetActorIndex();
    if (const auto cCopy = actors.BySession<RemoteComponent>(acMessage.SessionId); cCopy != entt::null)
    {
        RefreshCopy(cCopy, acMessage);
        m_world.ctx().at<MountService>().LinkPendingRides(acMessage.SessionId);
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

            actorFormId = m_actorBridge.SpawnActorFromNpc(cNpcId);
        }
        else
        {
            // Players and npcs with temporary ref ids and base ids (usually random events)
            actorFormId = m_actorBridge.CreateCustomActor(acMessage.AppearanceBuffer, acMessage.ChangeFlags, acMessage.Weight);
            m_actorBridge.SetupActorFaceGen(actorFormId);
            KeepFaceTints(m_world, *entity, acMessage.FaceTints);
            PaintFace(*entity, actorFormId);
        }
    }
    else
    {
        const uint32_t cActorId = ToGame(acMessage.FormId);

        if (actors.ByForm<LocalComponent>(cActorId) != entt::null)
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

        if (const auto cHeld = actors.ByForm(cActorId); cHeld != entt::null)
            entity = cHeld;
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
    if (!ApplyNpcSnapshot(actorFormId, acMessage))
        return;

    // A held copy hidden without its owner (GuestCopies) is shown once it is where its owner is, below.
    const auto* cpHeld = m_world.try_get<HeldCopyComponent>(*entity);
    const bool cWasHidden = cpHeld && cpHeld->Hidden;
    m_world.remove<HeldCopyComponent>(*entity);
    if (!cWasHidden && m_actorBridge.IsActorDisabled(actorFormId))
    {
        spdlog::warn("Disabled actor is being re-enabled: {:X}", actorFormId);
        m_actorBridge.EnableActor(actorFormId);
    }

    // Where the copy goes: an NPC from the game's data (FormId set) to the cell its owner reported, even if
    // the player has left it since (outdoors with no cell, one the engine made: the cell at its position in the
    // player's worldspace); a created actor (a player's copy) to the player's cell.
    uint32_t cellId = m_playerBridge.GetPlayerParentCellFormId();
    if (acMessage.FormId != GameId{})
    {
        if (const uint32_t cOwnerCellId = ToGame(acMessage.CellId))
            cellId = cOwnerCellId;
        else if (const auto cWorldSpace = ToSession(m_playerBridge.GetPlayerWorldSpaceId()); cWorldSpace && *cWorldSpace != GameId{})
        {
            if (const uint32_t cOutdoors = m_world.GetTravelService().CellAt(*cWorldSpace, {}, acMessage.Position))
                cellId = cOutdoors;
        }
    }
    m_actorBridge.MoveActorTo(actorFormId, cellId, acMessage.Position.x, acMessage.Position.y, acMessage.Position.z);
    m_actorBridge.SetActorRotation(actorFormId, acMessage.Rotation.x, acMessage.Rotation.y);
    // Enabled where its owner is (kept persistent, GuestCopies: enabled in a cell not loaded, 37501 frees no other).
    if (cWasHidden)
    {
        m_actorBridge.SetActorHidden(actorFormId, false);
        m_actorBridge.EnableActor(actorFormId);
    }

    m_actorBridge.SetActorValues(actorFormId, acMessage.InitialActorValues);

    m_actorBridge.SetActorPlayer(actorFormId, acMessage.IsPlayer);
    if (acMessage.IsPlayer)
    {
        // Lives as long as the player is in the session and travels with the party, never with its cell
        // (docs/design/copy-contract.md, "Players' copies").
        m_actorBridge.SetActorPersistent(actorFormId);
        m_actorBridge.SetActorIgnoreFriendlyHit(actorFormId, true);
        m_actorBridge.SetActorNotOnStealthMeter(actorFormId, true); // never a witness on this player's eye (stealth.md)
        m_actorBridge.SetActorPlayerFaction(actorFormId);
        m_world.emplace_or_replace<PlayerComponent>(*entity, acMessage.PlayerId);
        SetCopyLevel(actorFormId, acMessage.PlayerId);
    }

    const bool cIsDead = m_actorBridge.IsActorDead(actorFormId);
    if (cIsDead != acMessage.IsDead)
        acMessage.IsDead ? m_actorBridge.KillActor(actorFormId) : m_actorBridge.RespawnActor(actorFormId);

    // A summon's commander here: the caster's game sees its own player (summons.md).
    if (acMessage.CommanderId)
    {
        if (const uint32_t cCommander = Utils::GetFormIdBySessionId(acMessage.CommanderId))
            m_actorBridge.SetActorCommandingActor(actorFormId, cCommander);
        else
            m_pendingCommanders[acMessage.SessionId] = acMessage.CommanderId; // its caster's copy comes later (R5)
    }

    m_world.emplace_or_replace<RemoteComponent>(*entity, acMessage.SessionId, actorFormId);
    // Link the engine actor now: DiscoveryService (removed in ec7d02d1) used to do this when the
    // actor entered a loaded cell, via ActorAddedEvent -> OnActorAdded. Without the link,
    // RunRemoteUpdates has no actor to apply interpolated movement to.
    m_world.emplace_or_replace<FormIdComponent>(*entity, actorFormId);

    auto& interpolationComponent = InterpolationSystem::Setup(m_world, *entity);
    interpolationComponent.Position = acMessage.Position;
    interpolationComponent.RestartTick = SnapshotAge::Cutoff(acMessage.Tick, m_transport.GetTick(), 0);
    m_world.emplace_or_replace<OwnerPlaceComponent>(*entity, acMessage.CellId, GameId{}, std::chrono::steady_clock::now());

    AnimationSystem::Setup(m_world, *entity);

    m_world.emplace_or_replace<WaitingFor3D>(*entity, acMessage);

    auto& remoteAnimationComponent = m_world.get<RemoteAnimationComponent>(*entity);
    remoteAnimationComponent.TimePoints.push_back(acMessage.LatestAction);
    if (!acMessage.GraphState.empty())
        m_world.emplace_or_replace<SpawnGraphStateComponent>(*entity, acMessage.GraphState);
    m_world.ctx().at<MountService>().LinkPendingRides(acMessage.SessionId);
    LinkPendingCommanders(acMessage.SessionId);
    SessionIdHere(acMessage.SessionId);
}

void CharacterService::SessionIdHere(uint32_t aSessionId) const noexcept
{
    if (auto* pRecords = m_world.ctx().find<CopyRecords>())
        pRecords->OnSessionId(aSessionId);
}

// A summon spawned before its commander's copy: commanded once that copy is here.
void CharacterService::LinkPendingCommanders(uint32_t aSessionId) noexcept
{
    std::erase_if(m_pendingCommanders, [&](const auto& acPending) {
        const auto& [cSummon, cCommander] = acPending;
        if (cCommander != aSessionId)
            return false;
        const uint32_t cSummonId = Utils::GetFormIdBySessionId(cSummon);
        const uint32_t cCommanderId = Utils::GetFormIdBySessionId(cCommander);
        if (cSummonId && cCommanderId)
            m_actorBridge.SetActorCommandingActor(cSummonId, cCommanderId);
        return true;
    });
}

bool CharacterService::ApplyNpcSnapshot(uint32_t aFormId, const NotifyCharacterSpawn& acMessage) const noexcept
{
    if (acMessage.BaseId == GameId{})
        return true;
    const uint32_t cBaseId = ToGame(acMessage.BaseId);
    bool applied = cBaseId != 0;
    if (applied)
    {
        switch (NpcSnapshot::ChooseBaseApply(m_actorBridge.IsActorLeveled(aFormId), m_actorBridge.IsActorNpcTemporary(aFormId),
                                             m_actorBridge.GetActorNpcTemplateBaseFormId(aFormId), cBaseId))
        {
        case NpcSnapshot::BaseApply::Keep: break;
        case NpcSnapshot::BaseApply::Relevel: applied = m_actorBridge.RelevelActor(aFormId, cBaseId); break;
        case NpcSnapshot::BaseApply::Swap: applied = m_actorBridge.SetActorBaseFormId(aFormId, cBaseId); break;
        }
    }
    if (!applied)
    {
        spdlog::error("Failed to apply owner's NPC base {:X}:{:X} to actor {:X}", acMessage.BaseId.ModId,
                      acMessage.BaseId.BaseId, aFormId);
        return false;
    }
    return true;
}

void CharacterService::OnNotifyActorName(const NotifyActorName& acMessage) const noexcept
{
    // The host's quest named this game's own actor (a horse its player rides): its name part carries it on.
    const uint32_t cFormId = Utils::GetFormIdBySessionId(acMessage.ActorId);
    if (cFormId && !m_actorBridge.IsActorCopy(cFormId) && !acMessage.Name.empty() && !SameName(m_actorBridge.GetActorDisplayName(cFormId), acMessage.Name))
        m_actorBridge.SetActorDisplayName(cFormId, acMessage.Name);
}

void CharacterService::RefreshCopy(entt::entity aEntity, const NotifyCharacterSpawn& acMessage) const noexcept
{
    const auto* pFormId = m_world.try_get<FormIdComponent>(aEntity);
    const uint32_t cOldBase = pFormId ? m_actorBridge.GetActorBaseFormId(pFormId->Id) : 0;
    if (pFormId && !ApplyNpcSnapshot(pFormId->Id, acMessage))
        return;
    const bool cBaseChanged = pFormId && cOldBase != m_actorBridge.GetActorBaseFormId(pFormId->Id);
    if (cBaseChanged)
        m_world.emplace_or_replace<WaitingFor3D>(aEntity, acMessage);

    // The owner's arrival: it is there now (RunPresence moves the copy), as the spawn would have it.
    auto& place = m_world.get_or_emplace<OwnerPlaceComponent>(aEntity);
    auto* pInterpolation = m_world.try_get<InterpolationComponent>(aEntity);
    if (!pInterpolation || InterpolationSystem::Restart(*pInterpolation, acMessage.Position, acMessage.Tick, m_transport.GetTick()))
        place.CellId = acMessage.CellId;
    place.LastMove = {};
    if (auto* pWaitingFor3D = m_world.try_get<WaitingFor3D>(aEntity))
        pWaitingFor3D->SpawnRequest = acMessage;
    if (pFormId && (cBaseChanged || !m_actorBridge.ActorHas3D(pFormId->Id)) && !acMessage.GraphState.empty())
        m_world.emplace_or_replace<SpawnGraphStateComponent>(aEntity, acMessage.GraphState);
    spdlog::info("Character with remote id {:X} is already spawned: refreshed from the new spawn (cell {:X})", acMessage.SessionId,
                 acMessage.CellId.BaseId);
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
    message.Tick = m_world.GetFrameTick(); // ours; a copy's is its owner's (PlaceFromOwner)

    // A created actor goes by its created id: the spawn rebuilds it from its base (NotifyCharacterSpawn).
    const auto cReference = ToSessionOrCreated(formIdComponent.Id);
    if (!cReference)
    {
        spdlog::warn("AnnounceCharacter: {:X} has no session reference", formIdComponent.Id);
        return std::nullopt;
    }
    message.ReferenceId = *cReference;

    const auto cCell = ToSessionCell(m_actorBridge.GetActorParentCellFormId(formIdComponent.Id),
                                     m_actorBridge.GetActorWorldSpaceFormId(formIdComponent.Id));
    if (!cCell)
    {
        spdlog::warn("AnnounceCharacter: {:X}'s cell {:X} has no session id", formIdComponent.Id, m_actorBridge.GetActorParentCellFormId(formIdComponent.Id));
        return std::nullopt;
    }
    message.CellId = *cCell;

    if (const uint32_t cWorldSpaceId = m_actorBridge.GetActorWorldSpaceFormId(formIdComponent.Id))
    {
        const auto cWorldSpace = ToSession(cWorldSpaceId);
        if (!cWorldSpace)
        {
            spdlog::warn("AnnounceCharacter: {:X}'s worldspace {:X} has no session id", formIdComponent.Id, cWorldSpaceId);
            return std::nullopt;
        }
        message.WorldSpaceId = *cWorldSpace;
    }

    const auto cState = ReadLiveState(formIdComponent.Id);
    message.Position = cState.Position;
    message.Rotation = cState.Rotation;
    message.CurrentActorData = cState.Data;
    if (formIdComponent.Id == 0x14)
        message.PerksContent = m_actorBridge.GetPlayerPerks();
    message.LatestAction = cState.LatestAction;
    message.GraphState = cState.GraphState;
    if (const auto* pEffects = m_world.ctx().find<EffectVisualsService>())
    {
        message.EffectIds = pEffects->ReadEffects(formIdComponent.Id);
        message.HeldEffects = pEffects->ReadHeldIds(formIdComponent.Id);
    }

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

    message.IsDragon = m_actorBridge.IsActorDragon(formIdComponent.Id);
    message.IsMount = m_actorBridge.IsActorMount(formIdComponent.Id);

    if (const uint32_t cCommander = m_actorBridge.GetActorCommandingActorFormId(formIdComponent.Id))
        message.CommanderId = Utils::GetSessionIdByFormId(cCommander).value_or(0);

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
            {
                spdlog::warn("AnnounceCharacter: {:X}'s base {:X} has no session id", formIdComponent.Id, cNpcTemplateId);
                return std::nullopt;
            }
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
    message->SessionId = cIsPlayer ? m_characterId : m_world.GetHosting().NewCharacterId();
    if (!message->SessionId)
        return;

    m_world.emplace_or_replace<LocalComponent>(aEntity, message->SessionId);
    if (auto* pRecords = m_world.ctx().find<CopyRecords>())
    {
        message->Record = pRecords->ReadAll(message->SessionId, cFormId);
        pRecords->OnSessionId(message->SessionId);
    }
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

std::optional<NotifyCharacterSpawn> CharacterService::BuildCopySpawn(uint32_t aSessionId) const noexcept
{
    const auto cEntity = m_world.GetActorIndex().BySession<FormIdComponent, RemoteComponent>(aSessionId);
    if (cEntity == entt::null)
        return std::nullopt;
    const auto cAnnouncement = BuildAnnouncement(cEntity);
    if (!cAnnouncement)
        return std::nullopt;
    auto spawn = NotifyCharacterSpawn::FromAnnouncement(*cAnnouncement, aSessionId, 0);
    PlaceFromOwner(cEntity, spawn);
    return spawn;
}

void CharacterService::PlaceFromOwner(entt::entity aEntity, NotifyCharacterSpawn& aSpawn) const noexcept
{
    const auto* pInterpolation = m_world.try_get<InterpolationComponent>(aEntity);
    const auto* pPlace = m_world.try_get<OwnerPlaceComponent>(aEntity);
    aSpawn.Tick = pInterpolation ? pInterpolation->RestartTick : 0;
    if (pInterpolation && !pInterpolation->TimePoints.empty())
    {
        const auto& cNewest = pInterpolation->TimePoints.back();
        aSpawn.Tick = cNewest.Tick;
        aSpawn.Position = cNewest.Position;
        aSpawn.Rotation.x = cNewest.Rotation.x;
        aSpawn.Rotation.y = cNewest.Rotation.z;
    }
    else if (aSpawn.Tick)
        aSpawn.Position = pInterpolation->Position;
    if (aSpawn.Tick && pPlace && pPlace->CellId)
        aSpawn.CellId = pPlace->CellId;
}

bool CharacterService::ReadCopy(uint32_t aSessionId, NotifyCharacterSpawn& aSpawn) const noexcept
{
    const auto cEntity = m_world.GetActorIndex().BySession<FormIdComponent, RemoteComponent>(aSessionId);
    if (cEntity == entt::null)
        return false;
    const uint32_t cFormId = m_world.get<FormIdComponent>(cEntity).Id;
    if (!m_actorBridge.ActorExists(cFormId))
        return false;
    const auto cState = ReadLiveState(cFormId);
    aSpawn.Position = cState.Position;
    aSpawn.Rotation = cState.Rotation;
    PlaceFromOwner(cEntity, aSpawn);
    aSpawn.IsDead = cState.Data.IsDead;
    aSpawn.LatestAction = cState.LatestAction;
    aSpawn.GraphState = cState.GraphState;
    return true;
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
    // Its pools; a player's skills and the values its abilities hold off their base (a rejoin's copy has them before
    // any change is relayed; copy-contract.md). Never its dragon souls.
    const bool cPlayer = aFormId == m_playerBridge.GetPlayerFormId();
    for (int32_t index = 0; index < m_actorBridge.GetActorValueCount(); ++index)
    {
        const bool cPool = ActorPools::IsPool(index);
        const bool cSkill = index >= 6 && index <= 23; // kOneHanded to kEnchanting
        if (!cPool && (!cPlayer || index == static_cast<int32_t>(ActorValueService::kDragonSouls)))
            continue;
        const float cValue = m_actorBridge.GetActorValue(aFormId, index);
        const float cMax = m_actorBridge.GetActorPermanentValue(aFormId, index);
        if (!cPool && !cSkill && cValue == m_actorBridge.GetActorBaseValue(aFormId, index) && cMax == cValue)
            continue;
        actorData.InitialActorValues.ActorValuesList[index] = cValue;
        actorData.InitialActorValues.ActorMaxValuesList[index] = cMax;
        if (cPool)
            actorData.InitialActorValues.ActorTemporaryValuesList[index] = m_actorBridge.GetActorTemporaryValue(aFormId, index);
    }
    actorData.IsDead = m_actorBridge.IsActorDead(aFormId);

    return actorData;
}
