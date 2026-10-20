#pragma once

#include <Misc/BSFixedString.h>

struct TESQuest;

// A quest's alias (reference or location), as TESQuest::aliases holds it. CommonLibSSE-NG BGSBaseAlias.
struct BGSBaseAlias
{
    void* vtable;         // 00 (not called through)
    BSFixedString name;   // 08 ALID
    TESQuest* owningQuest; // 10
    uint32_t aliasId;     // 18 ALST/ALLS
    uint32_t flags;       // 1C FNAM
    uint16_t fillType;    // 20
    uint16_t pad22;
    uint32_t pad24;
};

SKYRIM_STRUCT_ASSERT(offsetof(BGSBaseAlias, name) == 0x8);
SKYRIM_STRUCT_ASSERT(offsetof(BGSBaseAlias, aliasId) == 0x18);
SKYRIM_STRUCT_ASSERT(sizeof(BGSBaseAlias) == 0x28);
