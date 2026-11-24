#include <Actor.h>
#include <Games/ActorExtension.h>
#include <Games/KnockReplay.h>
#include <Havok/BShkbAnimationGraph.h>
#include <Havok/Ragdoll.h>
#include <HookSwitch.h>
#include <Hooks/FunctionHook.h>
#include <Hooks/Initializer.h>
#include <Physics/BodyGroups.h>
#include <Physics/BodyPrediction.h>
#include <Physics/RagdollPending.h>
#include <Physics/RestPlacer.h>
#include <RTTI.h>
#include <chrono>
#include <map>
#include <set>
#include <mutex>

struct hkbContext;
struct hkbGeneratorOutput;

namespace
{
std::mutex s_bodyLock;
RagdollPending s_bodies;
// Every body of a copy this near its owner's rest is there: copies reached it within 0.2 to 0.8 units (duo, 2026-11-12).
constexpr float kNear = 1.f;
constexpr float kNearAngle = 0.05f;

// Each copy at its owner's rest: the rest's poses by body, and where it is in RestPlacer (a ragdoll's joints settle
// it where it lands: no hold).
struct CopyRest
{
    std::map<uint8_t, std::pair<glm::vec3, glm::quat>> Poses;
    RestPlacer Rest{0, false};
    std::optional<RestPlacer::Step> Logged; // the last step logged: each change once
};
std::map<uint32_t, CopyRest> s_rests;

uint64_t NowMs() noexcept
{
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
}

struct Resting
{
    bhkWorldObject* pBody;
    BodyFrame Frame;
    BodyRead Read;
};

// Its owner's rest, all its bodies together (RestPlacer): glided there, put there once, put to sleep, then left alone.
void RestBodies(Actor& aCopy, std::vector<Resting>& aBodies)
{
    bool fresh = false, allNear = true, anyFar = false, allStill = true, allAsleep = true, anyWoken = false;
    std::unique_lock lock(s_bodyLock);
    CopyRest& rest = s_rests[aCopy.formID];
    for (const auto& cBody : aBodies)
    {
        auto& pose = rest.Poses[cBody.Frame.Key.Index];
        const auto cPose = std::make_pair(cBody.Frame.Position, cBody.Frame.Rotation);
        fresh |= pose != cPose;
        pose = cPose;
        const auto cError = BodyPrediction::Between({.Position = cBody.Read.Position, .Rotation = cBody.Read.Rotation}, cBody.Frame);
        const bool cNear = cError.Distance <= kNear && cError.Angle <= kNearAngle;
        allNear &= cNear;
        anyFar |= cError.Distance > BodyPrediction::PlaceDistance(false);
        allAsleep &= !cBody.Read.Active;
        allStill &= BodyPrediction::StillToSleep(cBody.Read.Dynamic, glm::length(cBody.Read.LinearVelocity), glm::length(cBody.Read.AngularVelocity));
        anyWoken |= cBody.Read.Active && !cNear;
    }
    const uint64_t cNow = NowMs();
    if (fresh)
        rest.Rest = RestPlacer(cNow, false);
    const auto cStep = rest.Rest.Next(allNear, anyFar, allStill, allAsleep, anyWoken, cNow);
    if (std::exchange(rest.Logged, cStep) != cStep)
        spdlog::info("CopyRagdollPhysics: {:X} at its owner's rest: step {} ({} bodies, near {}, still {}, woken {})", aCopy.formID,
                     static_cast<int>(cStep), aBodies.size(), allNear, allStill, anyWoken);
    lock.unlock(); // the engine calls below take the world's lock
    switch (cStep)
    {
    case RestPlacer::Step::kGlide:
    case RestPlacer::Step::kKeep:
        for (const auto& cBody : aBodies)
        {
            const auto cSteer = BodyPrediction::Steer(cBody.Read.Position, cBody.Read.Rotation, cBody.Frame, BodyPrediction::kRestGlideStep);
            SteerBody(*cBody.pBody, cSteer.Linear, cSteer.Angular);
        }
        break;
    case RestPlacer::Step::kPlace:
        // All in one step: one at a time, its joints held about three of them off (physics.md, A copy at its owner's rest).
        for (const auto& cBody : aBodies)
            PlaceBody(*cBody.pBody, cBody.Frame.Position, cBody.Frame.Rotation, {}, {}, true);
        break;
    case RestPlacer::Step::kSleep:
    {
        // What this game moves keeps the island awake (sleeping it froze a carried wheel: physics.md, A pile sleeps together).
        const auto cMoved = [](const bhkWorldObject* apOther) {
            return apOther && apOther->referencedObject && (apOther->referencedObject->CollisionFilterInfo() >> 16) == BodyGroups::kMoving;
        };
        // Its own bodies settle against their joints after the place: their motion does not keep it awake.
        std::set<const bhkWorldObject*> own;
        for (const auto& cBody : aBodies)
            own.insert(cBody.pBody);
        const bool cAsleep = SleepBody(*aBodies.front().pBody, cMoved, [&own](const bhkWorldObject* apOther) { return own.contains(apOther); });
        std::scoped_lock again(s_bodyLock);
        auto& slept = s_rests[aCopy.formID].Rest;
        slept.Slept(cAsleep, cNow);
        if (slept.Done)
            spdlog::info("CopyRagdollPhysics: {:X} rests {}", aCopy.formID, cAsleep ? "asleep" : "awake: its island would not sleep in 2 s");
        break;
    }
    case RestPlacer::Step::kHold:
    case RestPlacer::Step::kLeave: break;
    }
}

bool Played(uint32_t aFormId) noexcept
{
    std::scoped_lock lock(s_bodyLock);
    return s_bodies.Has(aFormId) || s_rests.contains(aFormId);
}

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
    std::vector<Resting> resting;
    for (const auto& cDrive : bodies)
    {
        const auto& cFrame = cDrive.Frame;
        auto* pRigid = pRagdoll && pRagdoll->rigidBodies && cFrame.Key.Index < pRagdoll->rigidBodyCount
                           ? pRagdoll->rigidBodies[cFrame.Key.Index]
                           : nullptr;
        auto* pBody = pRigid ? pRigid->userData : nullptr;
        BodyRead read;
        if (!pBody || !pRigid->IsDynamic() || !ReadBody(*pBody, read))
            continue;
        if (cDrive.Rest)
        {
            resting.push_back({pBody, cFrame, read});
            continue;
        }
        if (glm::distance(read.Position, cFrame.Position) < BodyPrediction::PlaceDistance(cDrive.Place))
        {
            const auto cSteer = BodyPrediction::Steer(read.Position, read.Rotation, cFrame);
            SteerBody(*pBody, cSteer.Linear, cSteer.Angular);
        }
        else
            PlaceBody(*pBody, cFrame.Position, cFrame.Rotation, cFrame.LinearVelocity, cFrame.AngularVelocity, true);
    }
    if (!resting.empty())
        RestBodies(aCopy, resting);
    else if (!bodies.empty())
    {
        std::scoped_lock lock(s_bodyLock);
        s_rests.erase(aCopy.formID);
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
    // A copy's, or this game's own corpse another game's player moves (its frames queued here).
    if (pActor && (pActor->GetExtension()->IsCopy() || Played(pActor->formID)))
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
