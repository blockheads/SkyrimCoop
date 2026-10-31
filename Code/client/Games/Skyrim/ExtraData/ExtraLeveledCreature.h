#pragma once

#include "ExtraData.h"

struct TESActorBase;

// CommonLibSSE-NG ExtraLeveledCreature: the source base and the selected template for this reference.
struct ExtraLeveledCreature : BSExtraData
{
    inline static constexpr auto eExtraData = ExtraDataType::LeveledCreature;
    TESActorBase* originalBase;
    TESActorBase* templateBase;
};

static_assert(offsetof(ExtraLeveledCreature, originalBase) == 0x10);
static_assert(offsetof(ExtraLeveledCreature, templateBase) == 0x18);
static_assert(sizeof(ExtraLeveledCreature) == 0x20);
