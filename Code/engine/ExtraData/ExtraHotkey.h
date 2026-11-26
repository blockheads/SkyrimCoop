#pragma once

#include "ExtraData/ExtraData.h"

// A favorited stack: its number key, -1 for none, 0-7 (CommonLibSSE-NG ExtraHotkey; read in IDA, 11664).
struct ExtraHotkey : BSExtraData
{
    inline static constexpr auto eExtraData = ExtraDataType::Hotkey;

    int8_t hotkey{-1};
};

static_assert(offsetof(ExtraHotkey, hotkey) == 0x10);
static_assert(sizeof(ExtraHotkey) == 0x18);
