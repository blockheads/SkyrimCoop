#pragma once

#include <Havok/bhkCollision.h>

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
    uint8_t pad0[0x88];
    hkaRagdollInstance* ragdoll; // 88
};

[[nodiscard]] std::vector<bhkWorldObject*> RagdollBodiesOf(const TESObjectREFR& acActor) noexcept;
// Takes every graph's ragdoll out of the physics world, as Papyrus ObjectReference.ForceRemoveRagdollFromWorld does
// (56128: BShkbAnimationGraph::RemoveRagdollFromWorld 63622 per graph, under the manager's lock).
bool RemoveRagdollFromWorld(TESObjectREFR& aActor) noexcept;

static_assert(offsetof(hkaRagdollInstance, rigidBodies) == 0x10);
static_assert(offsetof(hkaRagdollInstance, rigidBodyCount) == 0x18);
static_assert(offsetof(hkbRagdollDriver, ragdoll) == 0x88);
