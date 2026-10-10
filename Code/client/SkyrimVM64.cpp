#include <TiltedOnlinePCH.h>
#include "TiltedOnlineApp.h"
#include "CrashHandler.h"
#include "HotReload/Runtime.h"

extern std::unique_ptr<TiltedOnlineApp> g_appInstance;

#include <GameVM.h>

struct Main;
struct VMContext; // SkyrimVM; isFrozen is at 0x680 (CommonLibSSE), unused here

TP_THIS_FUNCTION(TVMUpdate, int, VMContext, float);
TP_THIS_FUNCTION(TMainLoop, short, Main);
TP_THIS_FUNCTION(TVMDestructor, uintptr_t, void);

static TVMUpdate* VMUpdate = nullptr;
static TMainLoop* MainLoop = nullptr;
static TVMDestructor* VMDestructor = nullptr;

int TP_MAKE_THISCALL(HookVMUpdate, VMContext, float a2)
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

    return TiltedPhoques::ThisCall(VMUpdate, apThis, a2);
}

short TP_MAKE_THISCALL(HookMainLoop, Main)
{
    TP_EMPTY_HOOK_PLACEHOLDER

    CrashHandler::OnMainLoopTick();
    // Every frame from boot (the VM update hook only runs in-world), so the test
    // harness sees the plugin alive at the main menu too.
    CoopReload::Runtime::Get().Poll();

    return TiltedPhoques::ThisCall(MainLoop, apThis);
}

uintptr_t TP_MAKE_THISCALL(HookVMDestructor, void)
{
    TP_EMPTY_HOOK_PLACEHOLDER

    return TiltedPhoques::ThisCall(VMDestructor, apThis);
}

static TiltedPhoques::Initializer s_mainHooks(
    []()
    {
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

