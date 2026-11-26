#pragma once

#include "ExtraData/ExtraData.h"

struct ExtraWornLeft : BSExtraData
{
    inline static constexpr auto eExtraData = ExtraDataType::WornLeft;
};

static_assert(sizeof(ExtraWornLeft) == 0x10);
