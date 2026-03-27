#pragma once

#include "ExtraData.h"

struct TESNPC;

struct ExtraLeveledCreature : BSExtraData
{
    inline static constexpr auto eExtraData = ExtraDataType::LeveledCreature;

    virtual ~ExtraLeveledCreature();

    TESNPC* npc1;
    TESNPC* npc2;
};

SKYRIM_STRUCT_ASSERT(sizeof(ExtraLeveledCreature) == 0x20);
SKYRIM_STRUCT_ASSERT(offsetof(ExtraLeveledCreature, npc1) == 0x10);
