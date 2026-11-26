#pragma once

#include "ExtraData/ExtraData.h"

// CommonLibSSE-NG ExtraFlags: a script's BlockActivation sets kBlockActivate.
struct ExtraFlags : BSExtraData
{
    inline static constexpr auto eExtraData = ExtraDataType::Flags;
    static constexpr uint32_t kBlockActivate = 1 << 0;

    uint32_t flags; // 10
};

static_assert(offsetof(ExtraFlags, flags) == 0x10);
static_assert(sizeof(ExtraFlags) == 0x18);
