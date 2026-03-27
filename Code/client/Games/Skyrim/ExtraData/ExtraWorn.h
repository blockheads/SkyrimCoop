#pragma once

#include "ExtraData.h"

struct ExtraWorn : BSExtraData
{
    inline static constexpr auto eExtraData = ExtraDataType::Worn;
};

SKYRIM_STRUCT_ASSERT(sizeof(ExtraWorn) == 0x10);
