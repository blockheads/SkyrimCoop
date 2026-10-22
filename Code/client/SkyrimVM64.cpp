#include <TiltedOnlinePCH.h>
#include <HookSwitch.h>
#include "TiltedOnlineApp.h"
#include "CrashHandler.h"
#if COOP_TEST_HARNESS
#include "TestHarness/TestHarness.h"
#endif

extern std::unique_ptr<TiltedOnlineApp> g_appInstance;


struct Main;
struct VMContext; // SkyrimVM; isFrozen is at 0x680 (CommonLibSSE), unused here

using TVMUpdate = int(VMContext*, float);
using TMainLoop = short(Main*);
using TVMDestructor = uintptr_t(void*);

static TVMUpdate* VMUpdate = nullptr;
static TMainLoop* MainLoop = nullptr;
static TVMDestructor* VMDestructor = nullptr;

int HookVMUpdate(VMContext* apThis, float a2)
{
    // Tick even while the VM is frozen (menus, pause): the embedded server must keep
    // accepting connections. A 2026-10-10 startup crash blamed on this was the MinGW
    // vtable ABI bug (docs/re/crashes/2026-10-10-startup-crash.md).
    static bool s_loggedFirstUpdate = false;
    if (!s_loggedFirstUpdate)
    {
        s_loggedFirstUpdate = true;
        spdlog::info("Papyrus VM update: client update loop running");
    }

    g_appInstance->Update();

    return VMUpdate(apThis, a2);
}

short HookMainLoop(Main* apThis)
{
    TP_EMPTY_HOOK_PLACEHOLDER

    CrashHandler::OnMainLoopTick();
#if COOP_TEST_HARNESS
    // Every frame from boot (the VM update hook only runs in-world), so the test
    // harness sees the plugin alive at the main menu too.
    TestHarness::Poll();
#endif

    return MainLoop(apThis);
}

uintptr_t HookVMDestructor(void* apThis)
{
    TP_EMPTY_HOOK_PLACEHOLDER

    return VMDestructor(apThis);
}

static TiltedPhoques::Initializer s_mainHooks(
    []()
    {
        const HookGate cGate(__FILE__);
        POINTER_SKYRIMSE(TMainLoop, cMainLoop, 36564);
        POINTER_SKYRIMSE(TVMUpdate, cVMUpdate, 53926);
        POINTER_SKYRIMSE(TVMDestructor, cVMDestructor, 40412);

        VMUpdate = cVMUpdate.Get();
        MainLoop = cMainLoop.Get();
        VMDestructor = cVMDestructor.Get();

        TP_HOOK(&VMUpdate, HookVMUpdate);
        TP_HOOK(&MainLoop, HookMainLoop);
        TP_HOOK(&VMDestructor, HookVMDestructor);
    });

