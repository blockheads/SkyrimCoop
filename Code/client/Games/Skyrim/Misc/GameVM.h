#pragma once

#include <Misc/BSScript.h>
#include <cstddef>

struct SkyrimVM
{
    virtual ~SkyrimVM();

    static SkyrimVM* Get();

    uint8_t pad8[0x200 - 0x8];
    BSScript::IVirtualMachine* virtualMachine;
    uint8_t pad208[0x680 - 0x208];
    bool isFrozen; // ProcessRegisteredUpdates (53926), CommonLibSSE SkyrimVM::isFrozen.
};

static_assert(offsetof(SkyrimVM, isFrozen) == 0x680);

using GameVM = SkyrimVM;
