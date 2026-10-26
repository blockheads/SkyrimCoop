#pragma once

#include <Games/Primitives.h>

#include <glm/gtc/quaternion.hpp>
#include <glm/vec3.hpp>

#include <vector>

// A 3D node's physics, as far as reading and moving its bodies (CommonLibSSE-NG bhkNiCollisionObject, bhkWorldObject,
// hkpWorldObject, hkpEntity, hkpMotion, hkMotionState, hkSweptTransform; checked in IDA: hkpEntity::Activate 60849).
struct hkpSimulationIsland
{
    uint8_t pad0[0x32];
    uint8_t marks; // 32: isInActiveIslandsArray (bits 0-1), activeMark (bits 2-3)

    [[nodiscard]] bool IsActive() const noexcept { return (marks >> 2) & 3; }
};

struct ahkpWorld;

struct hkpRigidBody
{
    // hkpMotion::MotionType
    enum MotionType : uint8_t
    {
        kInvalid,
        kDynamic,
        kSphereInertia,
        kBoxInertia,
        kKeyframed,
        kFixed,
        kThinBoxInertia,
        kCharacter,
    };

    uint8_t pad0[0x10];
    ahkpWorld* world; // 10, hkpWorldObject::world: null when out of the physics world
    struct bhkWorldObject* userData; // 18: its bhkRigidBody (HIGGS, PLANCK)
    uint8_t collidable[0x130 - 0x20]; // 20: hkpLinkedCollidable (TESHavokUtilities::FindCollidableRef 26003 takes it)
    hkpSimulationIsland* simulationIsland; // 130
    uint8_t pad138[0x150 - 0x138];
    // 150: its hkpMaxSizeMotion, embedded; called through its vtable (applyPointImpulse 0x14: hkpBoxMotion 61737)
    struct Motion
    {
        void** vtable;
    } motion;
    uint8_t pad158[0x160 - 0x158];
    MotionType motionType;  // 160: motion's type (10)
    uint8_t pad161[0x1A0 - 0x161];
    float translation[4];   // 1A0: motionState (20), its transform's translation
    uint8_t pad1B0[0x1D0 - 0x1B0];
    float rotations[2][4];  // 1D0: sweptTransform's rotation0 and rotation1 (x, y, z, w): rotation1 is now
    uint8_t pad1F0[0x230 - 0x1F0];
    float linearVelocity[4];  // 230: motion's (E0)
    float angularVelocity[4]; // 240: (F0)

    // Moved by the simulation, not by its node or keyframes.
    [[nodiscard]] bool IsDynamic() const noexcept
    {
        return motionType == kDynamic || motionType == kSphereInertia || motionType == kBoxInertia || motionType == kThinBoxInertia;
    }
    [[nodiscard]] bool IsActive() const noexcept { return simulationIsland && simulationIsland->IsActive(); }
};

struct bhkWorld;

struct ahkpWorld
{
    uint8_t pad0[0x430];
    bhkWorld* userData; // 430: its bhkWorld
};

struct bhkWorld
{
    uint8_t pad0[0xC598];
    BSReadWriteLock worldLock; // C598 (bhkWorld::HavokWorldEnterWrite 77928)
};

struct bhkWorldObject
{
    uint8_t pad0[0x10];
    hkpRigidBody* referencedObject; // 10, a rigid body's for a bhkRigidBody
};

struct NiAVObject;

struct bhkNiCollisionObject
{
    // bhkNiCollisionObject::Flag
    enum Flags : uint16_t
    {
        kActive = 1 << 0,
        kNotify = 1 << 2,
        kSetLocal = 1 << 3,
        kUseVelocity = 1 << 5,
        kReset = 1 << 6,
        kSyncOnUpdate = 1 << 7,
    };

    uint8_t pad0[0x10];
    NiAVObject* sceneObject; // 10, NiCollisionObject's: the node it moves
    uint16_t flags;          // 18
    uint8_t pad1A[0x20 - 0x1A];
    bhkWorldObject* body; // 20 (NiPointer)
};

// Every physics body under the 3D comes to a stop (bhkRigidBody::SetLinearVelocity 78089, SetAngularVelocity 78090)
// and stays there: keyframed (NiAVObject::SetMotionType 77866, as Papyrus SetMotionType(Motion_Keyframed)), moved only
// by its reference's position.
void HoldBodies(NiAVObject& aRoot) noexcept;

// Havok's units to the game's (bhkWorld's scale, 0.0142875).
constexpr float cHavokToGame = 1.f / 0.0142875f;

// The rigid bodies under a 3D in their fixed order (docs/design/physics-sync.md, "Body keys"): breadth first, each
// node's collision object's, at most 255.
[[nodiscard]] std::vector<bhkWorldObject*> RigidBodiesOf(NiAVObject& aRoot) noexcept;
// Their collision objects, in the same order.
[[nodiscard]] std::vector<bhkNiCollisionObject*> CollisionObjectsOf(NiAVObject& aRoot) noexcept;

// Its node, children included, to where its body is now: the engine's own write for a dynamic body (the collision
// object's vfunc 0x2B, bhkCollisionObject 0x140E972C0, through 78382), which a keyframed body never gets; kSetLocal on
// for it (a keyframed body's collision object has it off: 78182).
void FollowBody(bhkNiCollisionObject& aCollision) noexcept;

// A rigid body as the guests get it, in game units: where (its Havok transform), how it moves, and whether it is
// simulated (dynamic) and awake (its island active). Under its world's read lock.
struct BodyRead
{
    glm::vec3 Position{};
    glm::quat Rotation{1.f, 0.f, 0.f, 0.f};
    glm::vec3 LinearVelocity{};
    glm::vec3 AngularVelocity{};
    bool Dynamic{};
    bool Active{};
};
[[nodiscard]] bool ReadBody(const bhkWorldObject& acBody, BodyRead& aRead) noexcept;

// Put there exactly, keyframed (bhkRigidBody::SetMotionType 78077), moving so (still by default): its own
// SetPositionAndRotation (78082, which takes the lock), then 78089, 78090. Each frame for a body played back: a
// keyframe toward a pose over the frame's time (applyHardKeyFrame) overshoots when Havok's step is longer.
bool PlaceBody(bhkWorldObject& aBody, const glm::vec3& acPosition, const glm::quat& acRotation, const glm::vec3& acLinearVelocity = {},
               const glm::vec3& acAngularVelocity = {}) noexcept;
// Simulated again as aMotion (the type it had before it was keyframed), moving so: through its collision object
// (bhkCollisionObject::SetMotionType 78182, vfunc 0x2E), which sets kSetLocal back for a dynamic body, then 78089,
// 78090; aAsleep: still, never woken (hkpRigidBody::setMotionType 60908 not activating; hkpEntity::deactivate 60850)
// until something touches it.
bool FreeBody(bhkNiCollisionObject& aCollision, hkpRigidBody::MotionType aMotion, const glm::vec3& acLinearVelocity = {},
              const glm::vec3& acAngularVelocity = {}, bool aAsleep = false) noexcept;
// A dynamic body woken and pushed at that point (hkpEntity::activate 60849, its motion's applyPointImpulse), in the
// game's units (mass x units/s); under its world's write lock.
bool PushBody(bhkWorldObject& aBody, const glm::vec3& acImpulse, const glm::vec3& acPoint) noexcept;

struct TESObjectREFR;
// The reference a body belongs to (TESHavokUtilities::FindCollidableRef 26003), none for the world's own.
[[nodiscard]] TESObjectREFR* ReferenceOf(const hkpRigidBody& acBody) noexcept;

static_assert(offsetof(hkpSimulationIsland, marks) == 0x32);
static_assert(offsetof(hkpRigidBody, world) == 0x10);
static_assert(offsetof(hkpRigidBody, userData) == 0x18);
static_assert(offsetof(hkpRigidBody, collidable) == 0x20);
static_assert(offsetof(hkpRigidBody, simulationIsland) == 0x130);
static_assert(offsetof(hkpRigidBody, motion) == 0x150);
static_assert(offsetof(hkpRigidBody, motionType) == 0x160);
static_assert(offsetof(hkpRigidBody, translation) == 0x1A0);
static_assert(offsetof(hkpRigidBody, rotations) == 0x1D0);
static_assert(offsetof(hkpRigidBody, linearVelocity) == 0x230);
static_assert(offsetof(hkpRigidBody, angularVelocity) == 0x240);
static_assert(offsetof(ahkpWorld, userData) == 0x430);
static_assert(offsetof(bhkWorld, worldLock) == 0xC598);
static_assert(offsetof(bhkWorldObject, referencedObject) == 0x10);
static_assert(offsetof(bhkNiCollisionObject, sceneObject) == 0x10);
static_assert(offsetof(bhkNiCollisionObject, flags) == 0x18);
static_assert(offsetof(bhkNiCollisionObject, body) == 0x20);
