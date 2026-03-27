#pragma once

#include "ExtraData.h"

struct ExtraCharge : BSExtraData
{
    inline static constexpr auto eExtraData = ExtraDataType::Charge;

    float fCharge{};
};

SKYRIM_STRUCT_ASSERT(sizeof(ExtraCharge) == 0x18);
