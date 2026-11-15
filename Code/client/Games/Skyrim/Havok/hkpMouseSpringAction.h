#pragma once

#include <cstddef>
#include <cstdint>

struct hkpRigidBody;

// Havok 2010's mouse spring: pulls a grabbed body's point toward a target each step (PlayerCharacter::UpdateGrab 40556
// moves the target; setMousePosition 62469 writes +0x50). docs/re/systems/physics.md, "Grabbing".
struct hkpMouseSpringAction
{
    uint8_t pad0[0x30];
    hkpRigidBody* entity;          // 30: hkpUnaryAction::m_entity, the held body
    uint8_t pad38[0x40 - 0x38];
    float positionInRbLocal[4];    // 40: the held point, in the body's space (Havok units)
    float mousePositionInWorld[4]; // 50: the target, eye + look * hold distance (Havok units)
};

static_assert(offsetof(hkpMouseSpringAction, entity) == 0x30);
static_assert(offsetof(hkpMouseSpringAction, positionInRbLocal) == 0x40);
static_assert(offsetof(hkpMouseSpringAction, mousePositionInWorld) == 0x50);

// The engine's wrapper (bhkRefObject): its Havok action at +0x10, as UpdateGrab reads it.
struct bhkMouseSpringAction
{
    uint8_t pad0[0x10];
    hkpMouseSpringAction* referencedObject; // 10
};

static_assert(offsetof(bhkMouseSpringAction, referencedObject) == 0x10);
