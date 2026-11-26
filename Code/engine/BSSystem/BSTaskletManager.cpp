
#include <base/threading/ThreadUtils.h>
#include <HookSwitch.h>
#include <Hooks/Initializer.h>
#include <VersionDb.h>
#include <Hooks/Memory.h>

namespace
{
struct BSTaskletManager
{
    char pad0[0x30];
    void* threadHandles[6];
};

static void (*Construct_TaskletManager)(BSTaskletManager*);

static void Hook_Construct_TaskletManager(BSTaskletManager* apSelf)
{
    Construct_TaskletManager(apSelf);

    for (int i = 0; i < 6; i++)
    {
        if (!apSelf->threadHandles[i])
            continue;

        auto name = std::format("TaskletThread{}", i);
        Base::SetThreadName(apSelf->threadHandles[i], name.c_str());
    }
}
} // namespace

static Hooks::Initializer s_BSThreadInit(
    []()
    {
        const HookGate cGate(__FILE__);
        const VersionDbPtr<uint8_t> getTaskletManagerInstance(69554);

        // tasklet naming
        Hooks::SwapCall(getTaskletManagerInstance.Get() + 0x63, Construct_TaskletManager, &Hook_Construct_TaskletManager);
    });
