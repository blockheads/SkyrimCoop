#include <HookSwitch.h>

#include <Services/SaveService.h>
#include <Interface/UI.h>
#include <MainLoopTasks.h>
#include <SaveLoadManager.h>
#include <World.h>

#include <Hooks/FunctionHook.h>
#include <Hooks/Initializer.h>
#include <VersionDb.h>

#include <atomic>

// Every save goes through BGSSaveLoadManager::Save_Impl (35727): menu and console saves (device 2),
// quicksaves (4), the engine's autosaves (3, 5). Its callers ignore the result (docs/re/systems/save-game.md).
using TSaveImpl = bool(void* apManager, int32_t aDevice, uint32_t aOutputStats, const char* apName);
static TSaveImpl* RealSaveImpl = nullptr;
static std::atomic_bool s_pendingJournalSave{};

static bool HookSaveImpl(void* apManager, int32_t aDevice, uint32_t aOutputStats, const char* apName)
{
    auto* pSaves = entt::locator<World>::has_value() ? World::Get().ctx().find<SaveService>() : nullptr;
    if (pSaves && !pSaves->OnSaveRequested(aDevice == 3 || aDevice == 5))
        return false;
    const UI* cpUI = UI::Get();
    if (pSaves && aDevice == 2 && World::Get().GetTransport().IsConnected() && cpUI &&
        cpUI->GetMenuOpen("Journal Menu"))
    {
        if (s_pendingJournalSave.exchange(true))
            return false;
        const bool cNamed = apName != nullptr;
        const std::string cName = apName ? apName : "";
        // The journal's UI job cannot serialize a live VM. Save between frames, like JoinBridge::SaveNow.
        MainLoopTasks::Queue([aDevice, aOutputStats, cNamed, cName] {
            s_pendingJournalSave = false;
            auto* pCurrentSaves = entt::locator<World>::has_value() ? World::Get().ctx().find<SaveService>() : nullptr;
            if (pCurrentSaves && pCurrentSaves->MaySave())
                RealSaveImpl(BGSSaveLoadManager::Get(), aDevice, aOutputStats, cNamed ? cName.c_str() : nullptr);
        });
        return true;
    }
    return RealSaveImpl(apManager, aDevice, aOutputStats, apName);
}

static Hooks::Initializer s_saveGameHooks(
    []()
    {
        const HookGate cGate(__FILE__);
        HOOK_ID(35727, &RealSaveImpl, HookSaveImpl);
    });
