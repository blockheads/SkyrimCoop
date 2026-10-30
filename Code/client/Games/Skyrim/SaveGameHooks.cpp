#include <HookSwitch.h>

#include <Services/SaveService.h>
#include <MainLoopTasks.h>
#include <SaveLoadManager.h>
#include <World.h>

#include <Hooks/FunctionHook.h>
#include <Hooks/Initializer.h>
#include <VersionDb.h>

// Every save goes through BGSSaveLoadManager::Save_Impl (35727): menu and console saves (device 2),
// quicksaves (4), the engine's autosaves (3, 5). Its callers ignore the result (docs/re/systems/save-game.md).
using TSaveImpl = bool(BGSSaveLoadManager* apManager, int32_t aDevice, uint32_t aOutputStats, const char* apName);
static TSaveImpl* RealSaveImpl = nullptr;

static bool HookSaveImpl(BGSSaveLoadManager* apManager, int32_t aDevice, uint32_t aOutputStats, const char* apName)
{
    auto* pSaves = entt::locator<World>::has_value() ? World::Get().ctx().find<SaveService>() : nullptr;
    if (pSaves && !pSaves->OnSaveRequested(aDevice == 3 || aDevice == 5))
        return false;
    if (pSaves && aDevice == 2)
    {
        const bool cNamed = apName != nullptr;
        const std::string cName = apName ? apName : "";
        // Console::RunQueuedCommands runs after the VM job starts. Device 2 must save between frames.
        return MainLoopTasks::RunOrQueue([aDevice, aOutputStats, cNamed, cName] {
            auto* pCurrentSaves = entt::locator<World>::has_value() ? World::Get().ctx().find<SaveService>() : nullptr;
            auto* pManager = BGSSaveLoadManager::Get();
            return pCurrentSaves && pCurrentSaves->MaySave() && pManager &&
                   RealSaveImpl(pManager, aDevice, aOutputStats, cNamed ? cName.c_str() : nullptr);
        });
    }
    return RealSaveImpl(apManager, aDevice, aOutputStats, apName);
}

static Hooks::Initializer s_saveGameHooks(
    []()
    {
        const HookGate cGate(__FILE__);
        HOOK_ID(35727, &RealSaveImpl, HookSaveImpl);
    });
