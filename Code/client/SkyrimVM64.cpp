#include <HookSwitch.h>
#include "Plugin.h"
#include "CrashHandler.h"
#include "MainLoopTasks.h"
#include <Interface/UI.h>

#include <atomic>
#if COOP_TEST_HARNESS
#include "TestHarness/TestHarness.h"
#include <Hooks/FunctionHook.h>
#include <Hooks/Initializer.h>
#include <VersionDb.h>
#endif

extern std::unique_ptr<Plugin> g_plugin;


struct Main;
struct VMContext; // SkyrimVM; isFrozen is at 0x680 (CommonLibSSE), unused here

using TVMUpdate = int(VMContext*, float);
using TMainLoop = short(Main*);
using TVMDestructor = uintptr_t(void*);

static TVMUpdate* VMUpdate = nullptr;
static TMainLoop* MainLoop = nullptr;
static TVMDestructor* VMDestructor = nullptr;
// The VM's update ran since the last main loop tick (it does not at the main menu).
static std::atomic<bool> s_vmUpdated{false};

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

    s_vmUpdated = true;
    g_plugin->Update();

    return VMUpdate(apThis, a2);
}

short HookMainLoop(Main* apThis)
{
    EMPTY_HOOK_PLACEHOLDER

    CrashHandler::OnMainLoopTick();
#if COOP_TEST_HARNESS
    // Every frame from boot (the VM update hook only runs in-world), so the test
    // harness sees the plugin alive at the main menu too.
    TestHarness::Poll();
#endif
    MainLoopTasks::Run();
    // At the main menu the VM's update, where ours runs, does not tick: the session (a join from there) runs here.
    const UI* pUI = UI::Get();
    if (!s_vmUpdated.exchange(false) && g_plugin && pUI && pUI->GetMenuOpen("Main Menu"))
        g_plugin->Update();

    return MainLoop(apThis);
}

uintptr_t HookVMDestructor(void* apThis)
{
    EMPTY_HOOK_PLACEHOLDER

    return VMDestructor(apThis);
}

static Hooks::Initializer s_mainHooks(
    []()
    {
        const HookGate cGate(__FILE__);
        HOOK_ID(53926, &VMUpdate, HookVMUpdate);
        HOOK_ID(36564, &MainLoop, HookMainLoop);
        HOOK_ID(40412, &VMDestructor, HookVMDestructor);
    });

