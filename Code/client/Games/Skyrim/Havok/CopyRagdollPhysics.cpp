#include <Actor.h>
#include <Games/ActorExtension.h>
#include <Games/KnockReplay.h>
#include <Havok/BShkbAnimationGraph.h>
#include <Havok/Ragdoll.h>
#include <HookSwitch.h>
#include <Hooks/FunctionHook.h>
#include <Hooks/Initializer.h>
#include <Physics/BodyPrediction.h>
#include <Physics/RagdollPending.h>
#include <RTTI.h>
#include <map>
#include <mutex>

struct hkbContext;
struct hkbGeneratorOutput;

namespace
{
std::mutex s_bodyLock;
RagdollPending s_bodies;
// A copy's body on its way to its owner's rest: which rest, and the physics steps it has been at it.
struct Resting
{
    glm::vec3 Position{};
    uint16_t Steps{};
};
std::map<std::pair<uint32_t, uint8_t>, Resting> s_rested;

void ApplyBodies(Actor& aCopy, hkbRagdollDriver& aDriver)
{
    std::vector<RagdollPending::Drive> bodies;
    {
        std::scoped_lock lock(s_bodyLock);
        bodies = s_bodies.Take(aCopy.formID);
    }
    if (!aCopy.IsDead() && !KnockReplay::BodiesSimulated(aCopy.actorState.GetKnockState()))
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
        // At its owner's rest: steered there a while, every body put exactly there in one step, then left to fall
        // asleep (steering wakes it each step).
        const auto cBody = std::make_pair(aCopy.formID, cFrame.Key.Index);
        if (cDrive.Rest && ReadBody(*pBody, read))
        {
            std::scoped_lock lock(s_bodyLock);
            Resting& resting = s_rested[cBody];
            if (resting.Position != cFrame.Position)
                resting = {.Position = cFrame.Position};
            const auto cStep = BodyPrediction::StepToRest(resting.Steps);
            if (resting.Steps <= BodyPrediction::kRestSettleSteps)
                ++resting.Steps;
            if (cStep == BodyPrediction::RestStep::kLeave)
                continue;
            if (cStep == BodyPrediction::RestStep::kPlace)
            {
                PlaceBody(*pBody, cFrame.Position, cFrame.Rotation, {}, {}, true);
                continue;
            }
        }
        else
        {
            std::scoped_lock lock(s_bodyLock);
            s_rested.erase(cBody);
        }
        if (ReadBody(*pBody, read) && glm::distance(read.Position, cFrame.Position) < BodyPrediction::PlaceDistance(cDrive.Place))
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
    // A graph's holder need not be an actor: a loaded Long Bow's ragdoll driver comes here too (a crash, 2026-10-28).
    Actor* pActor = pGraph ? Cast<Actor>(static_cast<TESForm*>(pGraph->holder)) : nullptr;
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

void HoldCopyRagdollStill(Actor& aCopy) noexcept
{
    for (bhkWorldObject* pBody : RagdollBodiesOf(aCopy))
        if (pBody && pBody->referencedObject && pBody->referencedObject->IsDynamic())
            SteerBody(*pBody, {}, {});
}

void QueueCopyBodyFrame(uint32_t aFormId, const BodyFrame& acFrame, bool aPlace, bool aRest) noexcept
{
    std::scoped_lock lock(s_bodyLock);
    s_bodies.Put(aFormId, acFrame, aPlace, aRest);
}
