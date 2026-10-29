#include <HookSwitch.h>
#include <Hooks/FunctionHook.h>
#include <Hooks/Initializer.h>
#include <Services/SessionService.h>
#include <VersionDb.h>
#include <World.h>

namespace
{
// FadeThenMainMenuCallback::Run (53290 / 55646): sets Main's return-to-menu flag before world teardown.
using TQuit = void(void*);
TQuit* RealMenuQuit = nullptr;
TQuit* RealScriptQuit = nullptr;

void LeaveSession() noexcept
{
    if (entt::locator<World>::has_value())
        World::Get().GetSessionService().QuitToMainMenu();
}

void HookMenuQuit(void* apCallback)
{
    LeaveSession();
    RealMenuQuit(apCallback);
}

void HookScriptQuit(void* apCallback)
{
    LeaveSession();
    RealScriptQuit(apCallback);
}

Hooks::Initializer s_hooks([] {
    const HookGate cGate(__FILE__);
    HOOK_ID(53290, &RealMenuQuit, HookMenuQuit);
    HOOK_ID(55646, &RealScriptQuit, HookScriptQuit);
});
} // namespace
