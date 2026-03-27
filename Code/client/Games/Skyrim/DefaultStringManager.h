#pragma once

#include <Misc/BSFixedString.h>

struct DefaultStringManager
{
    static DefaultStringManager& Get();

    uint8_t pad0[0x3B8];
    BSFixedString fIdleTimer; // 3B8
};

SKYRIM_STRUCT_ASSERT(offsetof(DefaultStringManager, fIdleTimer) == 0x3B8);
