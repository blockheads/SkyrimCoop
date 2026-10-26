#pragma once

#include <Interface/FxDelegateArgs.h>
#include <Interface/IMenu.h>

// CommonLibSSE-NG SleepWaitMenu (AE 1.6.1170); ProcessMessage 52486 checks these fields.
struct SleepWaitMenu : IMenu
{
    float timer;
    uint32_t pad34;
    GFxValue root;
    bool sleeping;
    bool active;
    uint16_t pad52;
    uint32_t pad54;
};

static_assert(offsetof(SleepWaitMenu, timer) == 0x30);
static_assert(offsetof(SleepWaitMenu, sleeping) == 0x50);
static_assert(offsetof(SleepWaitMenu, active) == 0x51);
static_assert(sizeof(SleepWaitMenu) == 0x58);
