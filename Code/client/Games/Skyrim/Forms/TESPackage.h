#pragma once

#include "TESForm.h"

struct TESPackage : TESForm
{
    // PACKAGE_TYPE (CommonLib).
    static constexpr uint8_t kDoNothing = 27;

    uint32_t packFlags; // 20, PKDT (CommonLib packData)
    uint8_t packType;   // 24
    uint8_t unk25[0xD8 - 0x25];
    int32_t ePROCEDURE_TYPE;
    uint32_t uiRefCount;
};

static_assert(offsetof(TESPackage, packType) == 0x24);
static_assert(offsetof(TESPackage, ePROCEDURE_TYPE) == 0xD8);
static_assert(sizeof(TESPackage) == 0xE0);
