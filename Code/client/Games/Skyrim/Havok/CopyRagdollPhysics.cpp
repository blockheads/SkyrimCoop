#include <Actor.h>
#include <Games/ActorExtension.h>
#include <Havok/BShkbAnimationGraph.h>
#include <Havok/Ragdoll.h>
#include <HookSwitch.h>
#include <Hooks/FunctionHook.h>
#include <Hooks/Initializer.h>
#include <Physics/BodyPrediction.h>
#include <Physics/RagdollPending.h>
#include <mutex>

struct hkbContext;
struct hkbGeneratorOutput;

namespace
{
std::mutex s_bodyLock;
RagdollPending s_bodies;

void ApplyBodies(Actor& aCopy, hkbRagdollDriver& aDriver)
{
    std::vector<RagdollPending::Drive> bodies;
    {
        std::scoped_lock lock(s_bodyLock);
        bodies = s_bodies.Take(aCopy.formID);
    }
    const uint8_t cKnock = aCopy.actorState.GetKnockState();
    if (!aCopy.IsDead() && (cKnock < 1 || cKnock > 4))
        return;
    const auto* pRagdoll = aDriver.ragdoll;
    for (const auto& cDrive : bodies)
    {
        const auto& cFrame = cDrive.Frame;
        auto* pRigid = pRagdoll && pRagdoll->rigidBodies && cFrame.Key.Index < pRagdoll->rigidBodyCount
                           ? pRagdoll->rigidBodies[cFrame.Key.Index]
                           : nullptr;
        auto* pBody = pRigid ? pRigid->userData : nullptr;
        if (!pBody || !pRigid->IsDynamic())
            continue;
        BodyRead read;
        if (!cDrive.Place && ReadBody(*pBody, read) && glm::distance(read.Position, cFrame.Position) < 64.f)
        {
            const auto cSteer = BodyPrediction::Steer(read.Position, read.Rotation, cFrame);
            SteerBody(*pBody, cSteer.Linear, cSteer.Angular);
        }
        else
            PlaceBody(*pBody, cFrame.Position, cFrame.Rotation, cFrame.LinearVelocity, cFrame.AngularVelocity, true);
    }
}

// Both native paths use this ABI (Precision, 63562; checked in IDA).
using TDriveToPose = void(hkbRagdollDriver*, float, const hkbContext*, hkbGeneratorOutput*);
TDriveToPose* RealDriveToPose = nullptr;
TDriveToPose* RealDriveWithoutAnimation = nullptr;

void DriveToPose(TDriveToPose* apOriginal, hkbRagdollDriver* apDriver, float aDelta, const hkbContext* apContext,
                 hkbGeneratorOutput* apOutput)
{
    apOriginal(apDriver, aDelta, apContext, apOutput);
    auto* pGraph = apDriver->character
                       ? reinterpret_cast<BShkbAnimationGraph*>(reinterpret_cast<uint8_t*>(apDriver->character) -
                                                                offsetof(BShkbAnimationGraph, character))
                       : nullptr;
    Actor* pActor = pGraph ? pGraph->holder : nullptr;
    if (pActor && pActor->GetExtension()->IsCopy())
        ApplyBodies(*pActor, *apDriver);
}

void HookDriveToPose(hkbRagdollDriver* apDriver, float aDelta, const hkbContext* apContext,
                     hkbGeneratorOutput* apOutput)
{
    DriveToPose(RealDriveToPose, apDriver, aDelta, apContext, apOutput);
}

void HookDriveWithoutAnimation(hkbRagdollDriver* apDriver, float aDelta, const hkbContext* apContext,
                               hkbGeneratorOutput* apOutput)
{
    DriveToPose(RealDriveWithoutAnimation, apDriver, aDelta, apContext, apOutput);
}

Hooks::Initializer s_ragdollPhysicsHooks([] {
    const HookGate cGate(__FILE__);
    HOOK_ID(58291, &RealDriveToPose, HookDriveToPose);
    HOOK_ID(58292, &RealDriveWithoutAnimation, HookDriveWithoutAnimation);
});
} // namespace

void QueueCopyBodyFrame(uint32_t aFormId, const BodyFrame& acFrame, bool aPlace) noexcept
{
    std::scoped_lock lock(s_bodyLock);
    s_bodies.Put(aFormId, acFrame, aPlace);
}
