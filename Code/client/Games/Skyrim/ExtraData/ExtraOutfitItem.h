#pragma once

#include "ExtraData.h"

struct ExtraOutfitItem : BSExtraData
{
    inline static constexpr auto eExtraData = ExtraDataType::OutfitItem;

    TESForm* pOutfit;
};

SKYRIM_STRUCT_ASSERT(sizeof(ExtraOutfitItem) == 0x18);
