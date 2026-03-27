#pragma once

#include "ExtraData.h"

struct ExtraWornLeft : BSExtraData
{
    inline static constexpr auto eExtraData = ExtraDataType::WornLeft;
};

SKYRIM_STRUCT_ASSERT(sizeof(ExtraWornLeft) == 0x10);
