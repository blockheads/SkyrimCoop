#include "TestHarness/EngineCalls.h"
#include <Actor.h>
#include <Combat/CombatBehavior.h>
#include <AI/AITimer.h>
#include <Combat/CombatController.h>
#include <Combat/CombatInventory.h>
#include <Combat/CombatGroup.h>
#include <Forms/ActorValueInfo.h>
#include <Primitives.h>
#include <VersionDb.h>
#include <Havok/Raycast.h>

#include <cmath>

namespace TestHarness::EngineCalls
{
namespace
{
// The frames live in the thread while they fit (frameCapacity's top bit), else on the heap.
const CombatBehaviorThread::Frame* Frames(const CombatBehaviorThread& acThread) noexcept
{
    return (acThread.frameCapacity >> 31) ? acThread.localFrames : acThread.pHeapFrames;
}

const MovementQueryPathFollowing* PathFollowing(MovementController& aController, const void* apTypeId) noexcept
{
    return static_cast<const MovementQueryPathFollowing*>(aController.pVtable->QueryInterface(&aController, apTypeId));
}
} // namespace

std::optional<CombatRead> ReadCombat(uint32_t aActorId)
{
    auto* pActor = Cast<Actor>(TESForm::GetById(aActorId));
    if (!pActor)
        return {};
    CombatRead read;
    read.State = pActor->GetCombatState();
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
        if (auto* pMover = reinterpret_cast<MovementController*>(pTarget->movementDrivenControl))
        {
            POINTER_SKYRIMSE(void, s_pathFollowing, 412229); // IMovementQueryPathFollowing's type id
            if (const auto* pFollowing = PathFollowing(*pMover, s_pathFollowing.Get()))
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
                if (const auto* pNode = Frames(*apThread)[i].pNode)
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
            candidate.Light = s_light.Get()(pTarget);
            candidate.RawLight = pTarget->currentProcess ? s_processLight.Get()(pTarget->currentProcess) : -1.f;
        }
        read.Candidates.push_back(candidate);
    }
    return read;
}

std::optional<SightRead> ReadSight(uint32_t aDetectorId, uint32_t aTargetId)
{
    auto* pDetector = Cast<Actor>(TESForm::GetById(aDetectorId));
    auto* pTarget = Cast<Actor>(TESForm::GetById(aTargetId));
    if (!pDetector || !pTarget || !pDetector->currentProcess || !pTarget->currentProcess)
        return {};
    using TCalculateLos = uint32_t(Actor*, Actor*, float);
    using TLosLocation = float*(Actor*, float*, uint32_t);
    using TLosToPoint = NiAVObject*(Actor*, const float*, float*, float);
    using TAngle = float(Actor*, const float*);
    using TFindRef = TESObjectREFR*(NiAVObject*);
    using TDetection = void(Actor*, Actor*, int32_t*, bool*, bool*, uint32_t*, float*, float*, float*, float*);
    POINTER_SKYRIMSE(TCalculateLos, s_calculateLos, 37768);
    POINTER_SKYRIMSE(TLosLocation, s_losLocation, 37771);
    POINTER_SKYRIMSE(TLosToPoint, s_losToPoint, 37770);
    POINTER_SKYRIMSE(TAngle, s_angle, 37773);
    POINTER_SKYRIMSE(TFindRef, s_findRef, 19750);
    POINTER_SKYRIMSE(TDetection, s_detection, 37774);
    struct FloatSetting
    {
        void* pVtable;
        float Value;
    };
    POINTER_SKYRIMSE(FloatSetting, s_viewCone, 370394); // fDetectionViewCone

    SightRead read;
    if (const auto* pCombatTarget = TESObjectREFR::GetByHandle(pDetector->combatHandle))
        read.CombatTarget = pCombatTarget->formID;
    const float cTargetAt[3]{pTarget->position.x, pTarget->position.y, pTarget->position.z};
    read.Angle = s_angle.Get()(pDetector, cTargetAt) * 57.29578f;
    read.HalfCone = s_viewCone.Get()->Value * 0.5f;
    read.Distance = std::sqrt(std::pow(pTarget->position.x - pDetector->position.x, 2.f) +
                              std::pow(pTarget->position.y - pDetector->position.y, 2.f) +
                              std::pow(pTarget->position.z - pDetector->position.z, 2.f));
    read.Los = s_calculateLos.Get()(pDetector, pTarget, -1.f);
    uint32_t location{};
    float seenAt[3]{}, sound{}, sight{}, sneak{};
    s_detection.Get()(pDetector, pTarget, &read.Level, &read.InView, &read.LineOfSight, &location, seenAt, &sound, &sight, &sneak);
    read.Sound = sound;
    read.Sight = sight;
    read.Sneak = sneak;
    read.TargetNoise = pTarget->iActionValue;
    read.TargetNoiseTimer = pTarget->fTimerOnAction;

    float eye[3]{};
    s_losLocation.Get()(pDetector, eye, 1);
    for (const uint32_t cLocation : {3u, 2u, 4u})
    {
        SightRay ray;
        ray.Location = cLocation;
        s_losLocation.Get()(pTarget, ray.To.data(), cLocation);
        // A cone past a full turn: the cone is read once above, as 37768 does.
        if (NiAVObject* pHit = s_losToPoint.Get()(pDetector, ray.To.data(), ray.HitAt.data(), 7.f))
        {
            ray.Hit = true;
            ray.HitNode = pHit->name ? pHit->name : "";
            if (const auto* pRef = s_findRef.Get()(pHit))
                ray.HitReference = pRef->formID;
        }
        ray.Sees = !ray.Hit || ray.HitReference == pTarget->formID;
        if (const auto cCast = GameRaycast::CastRay(eye, ray.To.data(), 41, pDetector->formID, pDetector->parentCell); cCast && cCast->Hit)
        {
            ray.CastLayer = cCast->Layer;
            ray.CastReference = cCast->Reference;
        }
        read.Rays.push_back(std::move(ray));
    }
    return read;
}
} // namespace TestHarness::EngineCalls
