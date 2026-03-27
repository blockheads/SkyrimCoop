#pragma once

#include <ExtraData.h>

struct TESFaction;

struct ExtraFactionChanges : BSExtraData
{
    inline static constexpr auto eExtraData = ExtraDataType::Faction;

    virtual ~ExtraFactionChanges();

    struct Entry
    {
        TESFaction* faction;
        int8_t rank;
    };

    GameArray<Entry> entries;
};

SKYRIM_STRUCT_ASSERT(sizeof(ExtraFactionChanges::Entry) == 0x10);
SKYRIM_STRUCT_ASSERT(offsetof(ExtraFactionChanges, entries) == 0x10);
