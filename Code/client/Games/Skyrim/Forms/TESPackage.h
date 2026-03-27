#pragma once

#include "TESForm.h"

struct TESPackage : TESForm
{
    uint8_t unk20[0xD8 - 0x20];
    int32_t ePROCEDURE_TYPE;
    uint32_t uiRefCount;
};

SKYRIM_STRUCT_ASSERT(offsetof(TESPackage, ePROCEDURE_TYPE) == 0xD8);
SKYRIM_STRUCT_ASSERT(sizeof(TESPackage) == 0xE0);
