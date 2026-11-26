#pragma once

#include <cstddef>
#include <cstdint>

struct TESObjectREFR;

// An actor's graph update input (20123 builds it, Actor::ModifyAnimationUpdateData 38054 fills it; CommonLibSSE).
struct BSAnimationUpdateData
{
    float deltaTime;        // 00: the frame's seconds x VATS multiplier (41453)
    uint32_t pad04;
    void (*functor)(void*); // 08: after each behaviour update (Character 38019)
    TESObjectREFR* refr;    // 10
    void* eyePosition;      // 18: the camera's, for 63587's distance
    void* postUpdate;       // 20
    uint16_t flags;         // 28
    bool force;             // 2A: every frame, no interpolation (kForceAnimGraphUpdate)
    bool interpAllowed;     // 2B: 37988
    bool visible;           // 2C
    bool unk2D;
    bool unk2E;
    bool unk2F;
};

static_assert(offsetof(BSAnimationUpdateData, refr) == 0x10);
static_assert(offsetof(BSAnimationUpdateData, force) == 0x2A);
static_assert(offsetof(BSAnimationUpdateData, visible) == 0x2C);
static_assert(sizeof(BSAnimationUpdateData) == 0x30);
