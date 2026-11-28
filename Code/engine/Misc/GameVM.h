#pragma once

#include <Misc/BSScript.h>
#include <RuntimeVersion.h>
#include <cstddef>

// A script's OnGainLOS / OnLostLOS registration (CommonLibSSE SkyrimVM::LOSDataEvent; 54037 reads it, read in IDA).
struct LosRegistration
{
    uint32_t refCount;
    uint32_t pad04;
    uint64_t handle;    // the script's
    uint32_t viewer;    // form ids
    uint32_t target;
    uint32_t kind;      // 0 gain, 1 lost, 2 both
    uint32_t lastState; // 1 seen, 2 not, 0 never polled
};
static_assert(offsetof(LosRegistration, viewer) == 0x10 && offsetof(LosRegistration, lastState) == 0x1C && sizeof(LosRegistration) == 0x20);

struct SkyrimVM
{
    virtual ~SkyrimVM();

    static SkyrimVM* Get();

    uint8_t pad8[0x200 + SkyrimRuntime::VmShift - 0x8];
    BSScript::IVirtualMachine* virtualMachine;
    uint8_t pad208[0x680 - 0x208];
    bool isFrozen; // ProcessRegisteredUpdates (53926), CommonLibSSE SkyrimVM::isFrozen.
    uint8_t pad681[0x6F8 - 0x681];
    // CommonLibSSE queuedLOSEventChecks: a BSTArray of LosRegistration pointers (54037).
    LosRegistration** losRegistrations;
    uint32_t losCapacity;
    uint32_t pad;
    uint32_t losCount;
};

static_assert(offsetof(SkyrimVM, virtualMachine) == 0x200 + SkyrimRuntime::VmShift);
static_assert(offsetof(SkyrimVM, isFrozen) == 0x680 + SkyrimRuntime::VmShift);
static_assert(offsetof(SkyrimVM, losRegistrations) == 0x6F8 + SkyrimRuntime::VmShift && offsetof(SkyrimVM, losCount) == 0x708 + SkyrimRuntime::VmShift);

using GameVM = SkyrimVM;
