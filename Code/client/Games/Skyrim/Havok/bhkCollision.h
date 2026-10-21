#pragma once

#include <Games/Primitives.h>

// A 3D node's physics, as far as reading where its body is (CommonLibSSE-NG bhkNiCollisionObject, bhkWorldObject,
// hkpWorldObject, hkpEntity, hkpMotion, hkMotionState).
struct hkpRigidBody
{
    uint8_t pad0[0x10];
    void* world; // 10, hkpWorldObject::world: null when out of the physics world
    uint8_t pad18[0x1A0 - 0x18];
    float translation[4]; // 1A0: motion (150), its motionState (20), its transform's translation (30)
};

struct bhkWorldObject
{
    uint8_t pad0[0x10];
    hkpRigidBody* referencedObject; // 10, a rigid body's for a bhkRigidBody
};

struct bhkNiCollisionObject
{
    uint8_t pad0[0x20];
    bhkWorldObject* body; // 20 (NiPointer)
};

// Havok's units to the game's (bhkWorld's scale, 0.0142875).
constexpr float cHavokToGame = 1.f / 0.0142875f;

SKYRIM_STRUCT_ASSERT(offsetof(hkpRigidBody, world) == 0x10);
SKYRIM_STRUCT_ASSERT(offsetof(hkpRigidBody, translation) == 0x1A0);
SKYRIM_STRUCT_ASSERT(offsetof(bhkWorldObject, referencedObject) == 0x10);
SKYRIM_STRUCT_ASSERT(offsetof(bhkNiCollisionObject, body) == 0x20);
