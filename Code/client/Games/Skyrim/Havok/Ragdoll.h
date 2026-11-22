#pragma once

#include <Havok/bhkCollision.h>
#include <Structs/BodyFrame.h>

#include <optional>
#include <vector>

struct TESObjectREFR;

// An actor's ragdoll (CommonLib-NG hkbRagdollDriver, hkaRagdollInstance): its bodies in the ragdoll's order
// (BodyKey::Index), from its active graph's character (hkbCharacter::ragdollDriver, hkbRagdollDriver::getRagdoll 58286:
// +0x88); empty without one.
struct hkaRagdollInstance
{
    uint8_t pad0[0x10];
    hkpRigidBody** rigidBodies; // 10, hkArray
    int32_t rigidBodyCount;     // 18
};

struct hkbRagdollDriver
{
    uint8_t pad0[0x80];
    struct hkbCharacter* character; // 80 (CommonLib-NG): its graph owns the actor
    hkaRagdollInstance* ragdoll; // 88
    uint8_t pad90[0xC6 - 0x90];
    // Read in driveToPose 58291 / postPhysics 58293: powered = output track 6 on, rigid body = track 4 on; either puts
    // the skeleton on the ragdoll's pose.
    bool isEnabled;                    // C6
    bool isPoweredControllerEnabled;   // C7
    bool isRigidBodyControllerEnabled; // C8
    bool wasRigidBodyEnabledLastFrame; // C9
    bool ragdollPoseWasUsed;           // CA
    bool allBonesKeyframed;            // CB
};

[[nodiscard]] std::vector<bhkWorldObject*> RagdollBodiesOf(const TESObjectREFR& acActor) noexcept;
// Its ragdoll out of the physics world and its 3D keyframed, queued on the engine's task pool as the graph's
// RemoveRagdoll (42877) and a get-up's end (39897) do: taken out between our update and the step, the step's ragdoll
// pass (63562) would put the bodies back (physics.md, "A revived player's ragdoll stays in the world").
bool QueueRagdollOutOfWorld(TESObjectREFR& aActor) noexcept;
// A graph of its steps with the physics world (BShkbAnimationGraph +0x238): its ragdoll driver then drives the ragdoll's
// bodies (63562). The engine detaches every graph in furniture (40063) and attaches them again as the actor gets up (39912).
// None without a graph.
[[nodiscard]] std::optional<bool> GraphsStepPhysics(const TESObjectREFR& acActor) noexcept;
// Every graph attached to its cell's physics world, each ragdoll reset first (39912), or detached (40063), through
// IAnimationGraphManagerHolder::SetWorld (38087). False: as wanted already.
bool SetGraphsStepPhysics(TESObjectREFR& aActor, bool aStep) noexcept;

static_assert(offsetof(hkaRagdollInstance, rigidBodies) == 0x10);
static_assert(offsetof(hkaRagdollInstance, rigidBodyCount) == 0x18);
static_assert(offsetof(hkbRagdollDriver, ragdoll) == 0x88);

static_assert(offsetof(hkbRagdollDriver, character) == 0x80);
static_assert(offsetof(hkbRagdollDriver, isEnabled) == 0xC6);
static_assert(offsetof(hkbRagdollDriver, allBonesKeyframed) == 0xCB);

// A copy holding its pose takes no graph update, so nothing drives its ragdoll: its limbs would fall.
void HoldCopyRagdollStill(Actor& aCopy) noexcept;
void QueueCopyBodyFrame(uint32_t aFormId, const BodyFrame& acFrame, bool aPlace, bool aRest = false) noexcept;
