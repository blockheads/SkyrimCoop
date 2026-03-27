#pragma once

struct hkbSymbolIdMap
{
    virtual ~hkbSymbolIdMap();

    uint8_t pad8[0x20 - 0x8];
    void* pointer20;
};

SKYRIM_STRUCT_ASSERT(offsetof(hkbSymbolIdMap, pointer20) == 0x20);
