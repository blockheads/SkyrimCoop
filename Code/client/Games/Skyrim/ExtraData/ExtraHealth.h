#pragma once

#include "ExtraData.h"

struct ExtraHealth : BSExtraData
{
    inline static constexpr auto eExtraData = ExtraDataType::Health;

    float fHealth{};
};

SKYRIM_STRUCT_ASSERT(sizeof(ExtraHealth) == 0x18);
