#pragma once

#include <Forms/TESBoundAnimObject.h>
#include <Components/TESModelTextureSwap.h>
#include <Components/TESLeveledList.h>

struct TESLevCharacter : TESBoundAnimObject
{
    // Components
    TESLeveledList leveledList;
    TESModelTextureSwap modelTextureSwap;
};

SKYRIM_STRUCT_ASSERT(offsetof(TESLevCharacter, leveledList) == 0x30);
SKYRIM_STRUCT_ASSERT(offsetof(TESLevCharacter, modelTextureSwap) == 0x58);
SKYRIM_STRUCT_ASSERT(sizeof(TESLevCharacter) == 0x90);
