#pragma once

#include <Games/Primitives.h>

#include <cstddef>
#include <cstdint>

struct CombatGroup;

// CommonLibSSE-NG CombatManager: every combat group of this game.
struct CombatManager
{
    [[nodiscard]] static CombatManager* Get() noexcept; // 405246

    void* vtbl;           // NiTPrimitiveArray<CombatGroup*>
    CombatGroup** groups; // null entries possible
    uint16_t capacity;
    uint16_t usedCount; // entries including nulls (41240 walks this many)
    uint16_t size;
    uint16_t growth;
    BSReadWriteLock lock;
};
static_assert(offsetof(CombatManager, groups) == 0x8);
static_assert(offsetof(CombatManager, usedCount) == 0x12);
static_assert(offsetof(CombatManager, lock) == 0x18);
