#include <HookSwitch.h>

#include <Events/SaveWrittenEvent.h>
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

// BSWin32SaveDataSystemUtility's write of a save's file (109353, vfunc 0x68): on the main thread for a menu save, on
// the save thread for a quicksave or autosave. When it returns the .ess is in place under the save's name.
using TWriteSaveFile = void*(void* apUtility, BGSSaveLoadFile** appFile, void* apRequest);
static TWriteSaveFile* RealWriteSaveFile = nullptr;

static void* HookWriteSaveFile(void* apUtility, BGSSaveLoadFile** appFile, void* apRequest)
{
    // Read first: the engine frees the file once it is written.
    std::string name = appFile && *appFile && (*appFile)->fileName ? (*appFile)->fileName : "";
    void* pResult = RealWriteSaveFile(apUtility, appFile, apRequest);
    if (!name.empty())
        MainLoopTasks::Queue([name = std::move(name)] {
            if (entt::locator<World>::has_value())
                World::Get().GetRunner().Trigger(SaveWrittenEvent{name});
        });
    return pResult;
}

static Hooks::Initializer s_saveGameHooks(
    []()
    {
        const HookGate cGate(__FILE__);
        HOOK_ID(35727, &RealSaveImpl, HookSaveImpl);
        HOOK_ID(109353, &RealWriteSaveFile, HookWriteSaveFile);
    });
