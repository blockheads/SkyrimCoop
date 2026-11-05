#include "EngineCalls.h"
#include <Actor.h>
#include <Combat/CombatBehavior.h>
#include <AI/AITimer.h>
#include <Combat/CombatController.h>
#include <Combat/CombatInventory.h>
#include <Combat/CombatGroup.h>
#include <Forms/ActorValueInfo.h>
#include <Games/Primitives.h>
#include <VersionDb.h>
#include <Havok/Raycast.h>

namespace TestHarness::EngineCalls
{
std::optional<CombatRead> ReadCombat(uint32_t aActorId)
{
    auto* pActor = Cast<Actor>(TESForm::GetById(aActorId));
    if (!pActor)
        return {};
    CombatRead read;
    read.State = pActor->GetOwnCombatState();
    const auto form = [](uint32_t aHandle) {
        const auto* pRef = TESObjectREFR::GetByHandle(aHandle);
        return pRef ? pRef->formID : 0;
    };
    read.ActorTarget = form(pActor->combatHandle);
    const auto* pTarget = pActor->GetCombatTarget();
    read.PapyrusTarget = pTarget ? pTarget->formID : 0;
    auto* pController = pActor->pCombatController;
    if (!pController)
        return read;
    read.ControllerTarget = form(pController->targetHandle);
    read.Inactive = pController->inactive;
    read.Ignoring = pController->ignoringCombat;
    read.Stopped = pController->stoppedCombat;
    read.LowMovementDelta = pController->unk4C;
    read.Areas = pController->areas.length;
    if (auto* pTarget = Cast<Actor>(TESObjectREFR::GetByHandle(pController->targetHandle)))
    {
        using TPredicted = void(Actor*, float, float*);
        POINTER_SKYRIMSE(TPredicted, s_predicted, 47309);
        read.TargetPosition = {pTarget->position.x, pTarget->position.y, pTarget->position.z};
        s_predicted.Get()(pTarget, 0.5f, read.TargetPredicted.data());
        using TMargin = void(Actor*, Actor*, float*, float*);
        POINTER_SKYRIMSE(TMargin, s_margin, 50643);
        s_margin.Get()(pActor, pTarget, &read.Margin, &read.MarginMax);
        using TReach = float(Actor*, Actor*);
        POINTER_SKYRIMSE(TReach, s_reach, 47336);
        read.TargetReach = s_reach.Get()(pTarget, pActor);
        if (auto* pMover = pTarget->movementDrivenControl)
        {
            // Movement controller slot 3: an interface by its type id (412229: IMovementQueryPathFollowing).
            POINTER_SKYRIMSE(void, s_pathFollowing, 412229);
            using TQuery = void*(void*, const void*);
            auto* pQuery = reinterpret_cast<TQuery*>((*reinterpret_cast<void***>(pMover))[3]);
            if (const auto* pFollowing = static_cast<const MovementQueryPathFollowing*>(pQuery(pMover, s_pathFollowing.Get())))
            {
                read.TargetPathState = pFollowing->state;
                read.TargetPathPoints = pFollowing->pPath ? pFollowing->pPath->pointCount : 0;
            }
        }
    }
    if (const auto* pInventory = pController->pInventory)
    {
        read.CloseRange = pInventory->closeRange;
        read.Reach = pInventory->reach;
    }
    if (const auto* pState = pController->pState)
    {
        read.SinceAttacked = AITimer::GetAITime() - pState->lastAttackedTime;
        read.Fleeing = pState->isFleeing;
        read.Detected = pState->isDetected;
        read.TargetLost = pState->targetLost;
        read.Unreachable = pState->unreachableLocations.length;
    }
    if (const auto* pBehavior = pController->pBehaviorController)
    {
        const auto path = [&read](const CombatBehaviorThread* apThread) {
            if (!apThread)
                return;
            std::string nodes;
            for (uint32_t i = 0; i < apThread->frameCount; ++i)
                if (const auto* pNode = apThread->Frames()[i].pNode)
                    nodes += std::string(nodes.empty() ? "" : " > ") + (pNode->name.data ? pNode->name.data : "?");
            read.Behavior.push_back(std::move(nodes));
        };
        path(pBehavior->pDefaultThread);
        for (const auto* pThread : pBehavior->activeThreads)
            if (pThread != pBehavior->pDefaultThread)
                path(pThread);
    }
    auto* pGroup = pController->pCombatGroup;
    if (!pGroup)
        return read;
    std::vector<CombatCandidateRead> handles;
    {
        BSReadLockGuard guard(pGroup->lock);
        for (const auto& cTarget : pGroup->targets)
            handles.push_back({cTarget.targetHandle, cTarget.detectLevel, cTarget.attackerCount,
                               uint16_t(cTarget.flags), cTarget.stealthPoints});
    }
    for (auto candidate : handles)
    {
        candidate.FormId = form(candidate.FormId);
        if (auto* pTarget = Cast<Actor>(TESForm::GetById(candidate.FormId)))
        {
            using TLineOfSight = bool(Actor*, TESObjectREFR*, bool&);
            POINTER_SKYRIMSE(TLineOfSight, s_lineOfSight, 53829);
            bool unused = false;
            candidate.LineOfSight = s_lineOfSight.Get()(pActor, pTarget, unused);
            candidate.ClearPath = GameRaycast::HasClearPath(pActor, pTarget);
            candidate.Invisible = pTarget->GetActorValue(ActorValueInfo::kInvisibility) > 0;
            candidate.Sneaking = pTarget->actorState.IsSneaking();
            using TLight = float(Actor*);
            POINTER_SKYRIMSE(TLight, s_light, 37775);
            using TProcessLight = float(void*);
            POINTER_SKYRIMSE(TProcessLight, s_processLight, 39509);
            using TDetect = int32_t(Actor*, Actor*, uint32_t);
            POINTER_SKYRIMSE(TDetect, s_detect, 37764);
            candidate.Light = s_light.Get()(pTarget);
            candidate.RawLight = pTarget->currentProcess ? s_processLight.Get()(pTarget->currentProcess) : -1.f;
            candidate.RequestedDetection = s_detect.Get()(pActor, pTarget, 3);
        }
        read.Candidates.push_back(candidate);
    }
    return read;
}
} // namespace TestHarness::EngineCalls
