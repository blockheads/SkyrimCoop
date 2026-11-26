#pragma once

#include <Interface/IMenu.h>

// LockpickingMenu, 1.7.104 only (read in IDA): ProcessMessage 51950 tests +0x11C, Build3D 51960 sets it.
struct LockpickingMenu : IMenu
{
    uint8_t pad30[0x11C - sizeof(IMenu)];
    bool built3D;
};
static_assert(offsetof(LockpickingMenu, built3D) == 0x11C);
