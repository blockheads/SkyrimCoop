#pragma once

#include <Misc/BSFixedString.h>

// A menu opening or closing (CommonLibSSE-NG MenuOpenCloseEvent), sent by UI to its sinks.
struct MenuOpenCloseEvent
{
    BSFixedString menuName; // 00
    bool opening;           // 08
};

static_assert(offsetof(MenuOpenCloseEvent, opening) == 0x8);
