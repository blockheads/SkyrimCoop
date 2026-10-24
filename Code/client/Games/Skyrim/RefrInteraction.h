#pragma once

#include <cstddef>
#include <cstdint>

// What links an actor to another in both their ExtraInteraction (1.6.1170, CommonLibSSE-NG RefrInteraction); a
// MountInteraction is 0x38.
struct RefrInteraction
{
    virtual ~RefrInteraction(); // 00: the engine's deleting destructor

    volatile long refCount; // 08
    uint32_t pad0C;
    uint32_t targetHandle; // 10: the mount
    uint32_t actorHandle;  // 14: the rider
};

static_assert(offsetof(RefrInteraction, refCount) == 0x8);
static_assert(offsetof(RefrInteraction, targetHandle) == 0x10);
static_assert(offsetof(RefrInteraction, actorHandle) == 0x14);
