#pragma once

struct hkbSymbolIdMap;

// Real name unknown
struct CharacterContext
{
    uint8_t pad0[0x18];
    hkbSymbolIdMap* symbolIdMap; // 18
    uint8_t byte20;              // 20
};

SKYRIM_STRUCT_ASSERT(offsetof(CharacterContext, symbolIdMap) == 0x18);
SKYRIM_STRUCT_ASSERT(offsetof(CharacterContext, byte20) == 0x20);
