#pragma once

#include <Forms/TESBoundAnimObject.h>
#include <Components/BGSKeywordForm.h>

// CommonLibSSE-NG TESObjectACTI (0xC8); TESFurniture derives from it. Only what we read.
struct TESObjectACTI : TESBoundAnimObject
{
    uint8_t pad30[0x90 - 0x30];
    BGSKeywordForm keywordForm; // 90: TESFurniture::Activate 17420 tests it at +0x90
};

static_assert(offsetof(TESObjectACTI, keywordForm) == 0x90);
