#pragma once

#include "TESForm.h"

// CommonLib PackageTarget: type 0 is a specific reference, its handle at +0x8 (PackageTarget__SetReference 0x1404A5C40).
struct PackageTarget
{
    uint8_t targetType; // 00
    uint8_t pad1[7];
    uint32_t handle; // 08 for type 0
};
static_assert(offsetof(PackageTarget, handle) == 0x8);

struct TESPackage : TESForm
{
    // PACKAGE_TYPE (CommonLib).
    static constexpr uint8_t kDoNothing = 27;
    static constexpr uint8_t kStealWarning = 34; // StartStealWarning 37556

    uint32_t packFlags; // 20, PKDT (CommonLib packData)
    uint8_t packType;   // 24
    uint8_t unk25[0x40 - 0x25];
    PackageTarget* packTarget; // 40 (CommonLib packTarg)
    uint8_t unk48[0xD8 - 0x48];
    int32_t ePROCEDURE_TYPE;
    uint32_t uiRefCount;
};

static_assert(offsetof(TESPackage, packType) == 0x24);
static_assert(offsetof(TESPackage, packTarget) == 0x40);
static_assert(offsetof(TESPackage, ePROCEDURE_TYPE) == 0xD8);
static_assert(sizeof(TESPackage) == 0xE0);
