#pragma once

#include "TESLeveledList.h"

struct TESLevItem : TESBoundObject, TESLeveledList
{
};

SKYRIM_STRUCT_ASSERT(sizeof(TESLevItem) == 0x58);
