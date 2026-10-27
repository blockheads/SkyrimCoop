#pragma once

#include <cstddef>
#include <cstdint>

// CommonLibSSE-NG hkpCollidable::broadPhaseHandle.collisionFilterInfo.
struct hkpCollidable
{
    uint8_t pad0[0x2C];
    uint32_t filterInfo;
};
static_assert(offsetof(hkpCollidable, filterInfo) == 0x2C);

// CommonLibSSE-NG bhkPickData; bhkWorld::PickObject (77860) fills its output under the world's read lock.
struct alignas(16) bhkPickData
{
    float From[4];        // 00 hkpWorldRayCastInput, Havok units
    float To[4];          // 10
    bool EnableShapeCollectionFilter; // 20
    uint8_t Pad21[3];
    uint32_t FilterInfo;  // 24: the ray's collision layer in the low 7 bits
    uint8_t Pad28[8];
    float Normal[4];      // 30 hkpWorldRayCastOutput
    float HitFraction;    // 40
    int32_t ExtraInfo;    // 44
    uint32_t ShapeKey;    // 48
    uint32_t Pad4C;
    uint32_t ShapeKeys[8]; // 50
    int32_t ShapeKeyIndex; // 70
    uint8_t Pad74[0x80 - 0x74];
    const hkpCollidable* RootCollidable; // 80: the hit's hkpCollidable, null for none
    uint64_t Pad88;
    float Ray[4];         // 90
    void* AabbCache;      // A0
    void* ClosestCollector; // A8
    void* AllCollector;   // B0
    void* AllTempCollector; // B8
    bool PickFailed;      // C0
    uint8_t PadC1[0xD0 - 0xC1];
};
static_assert(offsetof(bhkPickData, FilterInfo) == 0x24 && offsetof(bhkPickData, Normal) == 0x30 &&
              offsetof(bhkPickData, HitFraction) == 0x40 && offsetof(bhkPickData, RootCollidable) == 0x80 &&
              offsetof(bhkPickData, Ray) == 0x90 && offsetof(bhkPickData, PickFailed) == 0xC0 && sizeof(bhkPickData) == 0xD0);
