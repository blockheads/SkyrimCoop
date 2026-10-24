#include <HookSwitch.h>

#include <Services/SaveService.h>
#include <World.h>

#include <Hooks/FunctionHook.h>
#include <Hooks/Initializer.h>
#include <VersionDb.h>

// Every save goes through BGSSaveLoadManager::Save_Impl (35727), on the main thread: menu and console saves (device 2),
// quicksaves (4), the engine's autosaves (3, 5). Its callers ignore the result (docs/re/systems/save-game.md).
using TSaveImpl = bool(void* apManager, int32_t aDevice, uint32_t aOutputStats, const char* apName);
static TSaveImpl* RealSaveImpl = nullptr;

static bool HookSaveImpl(void* apManager, int32_t aDevice, uint32_t aOutputStats, const char* apName)
{
    auto* pSaves = entt::locator<World>::has_value() ? World::Get().ctx().find<SaveService>() : nullptr;
    if (pSaves && !pSaves->OnSaveRequested(aDevice == 3 || aDevice == 5))
        return false;
    return RealSaveImpl(apManager, aDevice, aOutputStats, apName);
}

static Hooks::Initializer s_saveGameHooks(
    []()
    {
        const HookGate cGate(__FILE__);
        HOOK_ID(35727, &RealSaveImpl, HookSaveImpl);
    });
