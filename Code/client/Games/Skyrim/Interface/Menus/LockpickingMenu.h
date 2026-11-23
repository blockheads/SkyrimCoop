#pragma once

#include <Interface/IMenu.h>

// LockpickingMenu (1.7.104, read in IDA): ProcessMessage 51950 builds the lock and pick 3D on its first main-thread kUpdate.
struct LockpickingMenu : IMenu
{
    uint8_t pad30[0x11C - sizeof(IMenu)];
    bool built3D; // set by 51950 after LockpickingMenu::Build3D (51960)
};
static_assert(offsetof(LockpickingMenu, built3D) == 0x11C);
