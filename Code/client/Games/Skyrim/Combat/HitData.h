#pragma once

#include <cstddef>
#include <cstdint>

// The hit a strike, a projectile or an explosion deals (CommonLibSSE-NG RE::HitData, 0x90), what ProcessHit (38586) takes.
struct HitData
{
    float hitPosition[3];
    float hitDirection[3];
    uint32_t aggressor; // ActorHandle: none for the world's (an object's projectile, an explosion with no cause)
    uint32_t target;
    uint32_t sourceRef; // the projectile or explosion
    uint32_t pad24;
    uint8_t unk28[0x50 - 0x28];
    float totalDamage;
    float physicalDamage;
    uint8_t unk58[0x90 - 0x58];
};
static_assert(offsetof(HitData, aggressor) == 0x18);
static_assert(offsetof(HitData, sourceRef) == 0x20);
static_assert(offsetof(HitData, totalDamage) == 0x50);
static_assert(sizeof(HitData) == 0x90);
