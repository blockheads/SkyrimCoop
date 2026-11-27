// CharacterService: a player's looks and level on its copies.
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

void CharacterService::OnNotifyRespawn(const NotifyRespawn& acMessage) noexcept
{
    if (!acMessage.AppearanceBuffer.empty())
    {
        m_pendingLooks[acMessage.ActorId] = acMessage;
        QueuePendingLooks();
        return;
    }
    m_pendingLooks.erase(acMessage.ActorId);
    const auto cEntity = m_world.GetActorIndex().BySession<FormIdComponent, RemoteComponent>(acMessage.ActorId);
    if (cEntity == entt::null)
    {
        spdlog::error("Actor to respawn not found in: {:X}", acMessage.ActorId);
        return;
    }

    RespawnCopy(cEntity, acMessage.ActorId);
}

void CharacterService::QueuePendingLooks() noexcept
{
    if (m_pendingLooks.empty() || m_looksQueued)
        return;
    m_looksQueued = true;
    m_mainTasks.Queue([this] {
        m_looksQueued = false;
        ApplyPendingLooks();
    });
}

void CharacterService::ApplyPendingLooks() noexcept
{
    for (auto it = m_pendingLooks.begin(); it != m_pendingLooks.end();)
    {
        const uint32_t cFormId = Utils::GetFormIdBySessionId(it->first);
        const auto& cLooks = it->second;
        if (!cFormId || !m_actorBridge.ApplyActorLooks(cFormId, cLooks.AppearanceBuffer, cLooks.ChangeFlags, cLooks.Weight))
        {
            ++it;
            continue;
        }
        m_world.ctx().at<ActorValueService>().RestoreCopyValues(it->first);
        // A different race has a different variable layout; derive it from the rebuilt graph.
        m_actorBridge.SetAnimationGraphDescriptorHash(cFormId, 0);
        if (const auto cEntity = m_world.GetActorIndex().ByForm<RemoteComponent>(cFormId); cEntity != entt::null)
        {
            m_world.remove<FaceGenComponent>(cEntity);
            KeepFaceTints(m_world, cEntity, cLooks.FaceTints);
            PaintFace(cEntity, cFormId);
            if (const auto* cpPlayer = m_world.try_get<PlayerComponent>(cEntity))
                SetCopyLevel(cFormId, cpPlayer->Id);
        }
        it = m_pendingLooks.erase(it);
    }
}

void CharacterService::SetCopyLevel(uint32_t aFormId, uint32_t aPlayerId) const noexcept
{
    const auto& cPlayers = m_world.ctx().at<SessionService>().GetPlayers();
    if (const auto it = cPlayers.find(aPlayerId); it != cPlayers.end())
        m_actorBridge.SetActorBaseLevel(aFormId, it->second.Level);
}

void CharacterService::OnPlayerLevel(const NotifyPlayerLevel& acMessage) const noexcept
{
    for (auto&& [cEntity, form, player, copy] : m_world.view<FormIdComponent, PlayerComponent, RemoteComponent>().each())
        if (player.Id == acMessage.PlayerId)
            m_actorBridge.SetActorBaseLevel(form.Id, acMessage.NewLevel);
}

void CharacterService::PaintFace(entt::entity aEntity, uint32_t aFormId) noexcept
{
    auto* pFace = m_world.try_get<FaceGenComponent>(aEntity);
    if (!pFace || (pFace->Painted && m_actorBridge.IsActorFacePainted(aFormId)))
        return;
    pFace->Painted = m_actorBridge.PaintActorFace(aFormId, pFace->FaceTints);
    pFace->Paints += pFace->Painted;
}

void CharacterService::OnActorHeadUpdate(const ActorHeadUpdateEvent& acEvent) noexcept
{
    if (const auto cEntity = m_world.GetActorIndex().ByForm<FaceGenComponent>(acEvent.FormId); cEntity != entt::null)
        PaintFace(cEntity, acEvent.FormId);
}

void CharacterService::OnPlayerLooksChange(const PlayerLooksChangeEvent&) const noexcept
{
    const auto cPlayer = m_world.GetActorIndex().ByForm<LocalComponent>(0x14);
    // Not announced yet: its announcement carries the looks.
    if (cPlayer == entt::null || !m_transport.IsOnline())
        return;

    RequestRespawn request;
    request.ActorId = m_world.get<LocalComponent>(cPlayer).Id;
    m_actorBridge.MarkActorNpcChanged(0x14, kPlayerLooksFlags);
    request.ChangeFlags = m_actorBridge.SerializeActorNpc(0x14, request.AppearanceBuffer);
    request.FaceTints = m_playerBridge.GetPlayerFaceTints();
    request.Weight = m_actorBridge.GetActorBaseWeight(0x14);
    const auto cState = ReadLiveState(0x14);
    request.HasState = true;
    request.LatestAction = cState.LatestAction;
    request.GraphState = cState.GraphState;
    request.Data = cState.Data;

    spdlog::info("Player looks changed: our copies take race {:X}", m_actorBridge.GetActorRaceFormId(0x14));
    m_transport.Send(request);
}

void CharacterService::OnPlayerRevived(const PlayerRevivedEvent&) const noexcept
{
    if (!m_transport.IsOnline())
        return;
    const auto cPlayer = m_world.GetActorIndex().ByForm<LocalComponent>(0x14);
    if (cPlayer == entt::null)
        return;

    const auto state = ReadLiveState(0x14);
    RequestRespawn request;
    request.ActorId = m_world.get<LocalComponent>(cPlayer).Id;
    request.HasState = true;
    request.LatestAction = state.LatestAction;
    request.GraphState = state.GraphState;
    request.Data = state.Data;
    m_transport.Send(request);
}

