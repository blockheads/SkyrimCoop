// CharacterService: where an actor is put when its owner changes, and a copy's 3D arriving.
#include <characters/CharacterService.h>
#include <core/TransportService.h>
#include <core/SessionService.h>

#include <animation/LocalAnimationComponent.h>
#include <animation/RemoteAnimationComponent.h>
#include <core/FormIdComponent.h>
#include <movement/InterpolationComponent.h>
#include <core/LocalComponent.h>
#include <core/RemoteComponent.h>
#include <characters/WaitingFor3D.h>
#include <characters/OwnerPlaceComponent.h>
#include <characters/HeldCopyComponent.h>

#include <characters/Copy3DEvent.h>
#include <characters/CopyHeldEvent.h>
#include <animation/ReplayState.h>
#include <characters/ReferenceJumpEvent.h>

#include <Structs/ActionEvent.h>
#include <Messages/NotifyReferencesMove.h>
#include <Messages/ReferencesMoveRequest.h>
#include <physics/BodyService.h>

#include <ports/IActorBridge.h>
#include <ports/IFormBridge.h>
#include <ports/IPlayerBridge.h>

#include <movement/SnapshotAge.h>
#include <movement/CopyStep.h>
#include <movement/InterpolationSystem.h>
#include <animation/AnimationSystem.h>

#include <core/World.h>
#include <core/SessionIds.h>
#include <core/FormIds.h>

#include <Messages/Relayed.h>
#include <animation/GraphStream.h>
#include <travel/TravelService.h>
#include <characters/CharacterInternal.h>








void CharacterService::PlaceAt(uint32_t aFormId, const ActorWhereabouts& acWhereabouts) const noexcept
{
    if (acWhereabouts.Where != ActorWhereabouts::State::kPlaced)
        return;
    if (const uint32_t cCellId = m_world.GetTravelService().CellAt(acWhereabouts.WorldSpaceId, acWhereabouts.CellId, acWhereabouts.Position))
    {
        m_actorBridge.MoveActorTo(aFormId, cCellId, acWhereabouts.Position.x, acWhereabouts.Position.y, acWhereabouts.Position.z);
        m_actorBridge.SetActorRotation(aFormId, 0.f, acWhereabouts.Heading);
    }
}

void CharacterService::FollowOwner(entt::entity aEntity, uint32_t aFormId, const ActorWhereabouts& acWhereabouts, bool aOwnerGone) const noexcept
{
    // Still a copy: a guest never runs a host NPC (copy-contract.md, "When the owner's actor goes away").
    m_world.emplace_or_replace<HeldCopyComponent>(aEntity);
    // Before the move: kept and hidden, the engine neither loads nor frees it where it goes (copy-lifecycle.md).
    if (aOwnerGone)
        m_dispatcher.trigger(CopyHeldEvent{aEntity, aFormId,
                                           acWhereabouts.Where == ActorWhereabouts::State::kDisabled ||
                                               acWhereabouts.Where == ActorWhereabouts::State::kDeleted});
    switch (acWhereabouts.Where)
    {
    case ActorWhereabouts::State::kPlaced:
    {
        const uint32_t cCellId = m_world.GetTravelService().CellAt(acWhereabouts.WorldSpaceId, acWhereabouts.CellId, acWhereabouts.Position);
        if (!cCellId)
        {
            spdlog::warn("Copy {:X}: its owner's actor went to cell {:X} (worldspace {:X}), which this game lacks", aFormId,
                         acWhereabouts.CellId.BaseId, acWhereabouts.WorldSpaceId.BaseId);
            return;
        }
        float x = 0.f, y = 0.f, z = 0.f;
        m_actorBridge.GetActorPosition(aFormId, x, y, z);
        const glm::vec3 cHere{x, y, z};
        if (m_actorBridge.GetActorParentCellFormId(aFormId) != cCellId || glm::distance(cHere, glm::vec3(acWhereabouts.Position)) >= 64.f)
        {
            spdlog::info("Copy {:X} follows its owner's actor to cell {:X}", aFormId, cCellId);
            m_actorBridge.MoveActorTo(aFormId, cCellId, acWhereabouts.Position.x, acWhereabouts.Position.y, acWhereabouts.Position.z);
            m_actorBridge.SetActorRotation(aFormId, 0.f, acWhereabouts.Heading);
        }
        return;
    }
    case ActorWhereabouts::State::kDisabled:
    case ActorWhereabouts::State::kDeleted:
        // Disabled only: a guest deleting a plugin's reference crashed its next load (2026-10-30-deleted-placed-ref-load.md);
        // the host's save carries the deletion.
        spdlog::info("Copy {:X}: its owner's actor was {}", aFormId,
                     acWhereabouts.Where == ActorWhereabouts::State::kDeleted ? "deleted" : "disabled");
        m_actorBridge.DisableActor(aFormId, false);
        return;
    default:
        return;
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
        // A persistent reference outdoors out of the loaded cells has no parent cell, only its worldspace.
        if (!cCellId && !cWorldSpaceId)
            return {};
        const auto cCell = cCellId ? ToSessionCell(cCellId, cWorldSpaceId) : std::optional(GameId{});
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

void CharacterService::MoveActor(uint32_t aFormId, const GameId& acWorldSpaceId, const GameId& acCellId, const Vector3_NetQuantize& acPosition) const noexcept
{
    m_mainTasks.Queue([this, aFormId, acWorldSpaceId, acCellId, acPosition] {
        if (!m_transport.IsOnline() || !m_actorBridge.ActorExists(aFormId))
            return;
        const uint32_t cCellFormId = m_world.GetTravelService().CellAt(acWorldSpaceId, acCellId, acPosition);
        if (!cCellFormId)
        {
            spdlog::error("MoveActor: failed to fetch cell to teleport, actor: {:X}, worldspace: {:X}, cell: {:X}, position: {}, {}, {}", aFormId, acWorldSpaceId.BaseId, acCellId.BaseId, acPosition.x, acPosition.y, acPosition.z);
            return;
        }

        m_actorBridge.MoveActorTo(aFormId, cCellFormId, acPosition.x, acPosition.y, acPosition.z);
    });
}

// A copy whose 3D arrived: its spawn's variables and life, then its record's parts.
void CharacterService::RunCopy3D() noexcept
{
    auto waitingView = m_world.view<FormIdComponent, WaitingFor3D>();

    std::vector<entt::entity> toRemove;
    for (auto entity : waitingView)
    {
        auto& formIdComponent = waitingView.get<FormIdComponent>(entity);
        auto& waitingFor3D = waitingView.get<WaitingFor3D>(entity);

        if (!m_actorBridge.ActorHas3D(formIdComponent.Id))
            continue;

        m_actorBridge.ActorLoadAnimationVariables(formIdComponent.Id, waitingFor3D.SpawnRequest.LatestAction.Variables);

        const bool cIsDead = m_actorBridge.IsActorDead(formIdComponent.Id);
        if (cIsDead != waitingFor3D.SpawnRequest.IsDead)
            waitingFor3D.SpawnRequest.IsDead ? m_actorBridge.KillActor(formIdComponent.Id) : m_actorBridge.RespawnActor(formIdComponent.Id);

        toRemove.push_back(entity);
        if (const auto* pCopy = m_world.try_get<RemoteComponent>(entity))
            SessionIdHere(pCopy->Id);

        // After its owner's items (its record's part), as after the spawn's inventory before.
        if (m_actorBridge.IsActorVampireLord(formIdComponent.Id))
            m_actorBridge.ActorFixVampireLordModel(formIdComponent.Id);

        spdlog::info("Applied 3D for actor, form id: {:X}", formIdComponent.Id);
        m_dispatcher.trigger(Copy3DEvent{formIdComponent.Id});
    }

    for (auto entity : toRemove)
        m_world.remove<WaitingFor3D>(entity);
}
