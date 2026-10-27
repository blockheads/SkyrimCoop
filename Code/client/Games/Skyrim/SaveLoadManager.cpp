#include <SaveLoadManager.h>

#include <HookSwitch.h>
#include <Hooks/FunctionHook.h>
#include <Hooks/Initializer.h>
#include <Interface/UI.h>
#include <Services/SaveService.h>
#include <VersionDb.h>
#include <World.h>

namespace
{
// CommonLibSSE-NG Load_Impl (35728): menu and console loads. Quickload queues its own request.
using TLoadImpl = bool(BGSSaveLoadManager*, const char*, int32_t, uint32_t, bool);
TLoadImpl* RealLoadImpl = nullptr;

bool HookLoadImpl(BGSSaveLoadManager* apManager, const char* apName, int32_t aDevice, uint32_t aOutputStats, bool aCheckForMods)
{
    auto* pSaves = entt::locator<World>::has_value() ? World::Get().ctx().find<SaveService>() : nullptr;
    if (pSaves && !pSaves->OnLoadRequested(apName ? apName : ""))
    {
        // The journal has already started its loading fade; refusing must release both menus.
        if (auto* pQueue = UIMessageQueue::Get())
        {
            pQueue->AddMessage("Fader Menu", UIMessage::kForceHide, nullptr);
            pQueue->AddMessage("Journal Menu", UIMessage::kForceHide, nullptr);
        }
        return false;
    }
    return RealLoadImpl(apManager, apName, aDevice, aOutputStats, aCheckForMods);
}

// QuickSaveLoadHandler queues 0xD0000400 through 35769; 35772 inlines the load instead of calling 35728.
using TQueueSaveLoad = void(BGSSaveLoadManager*, uint32_t);
TQueueSaveLoad* RealQueueSaveLoad = nullptr;

void HookQueueSaveLoad(BGSSaveLoadManager* apManager, uint32_t aEvent)
{
    if (aEvent == 0xD0000400)
    {
        auto* pSaves = entt::locator<World>::has_value() ? World::Get().ctx().find<SaveService>() : nullptr;
        if (pSaves && !pSaves->OnLoadRequested(apManager->QuickSaveName()))
            return;
    }
    RealQueueSaveLoad(apManager, aEvent);
}

Hooks::Initializer s_loadHook([]() {
    const HookGate cGate(__FILE__);
    HOOK_ID(35728, &RealLoadImpl, HookLoadImpl);
    HOOK_ID(35769, &RealQueueSaveLoad, HookQueueSaveLoad);
});
} // namespace

BGSSaveLoadManager* BGSSaveLoadManager::Get() noexcept
{
    POINTER_SKYRIMSE(BGSSaveLoadManager*, s_singleton, 403340);
    return *s_singleton.Get();
}

bool BGSSaveLoadManager::Save(const char* apName) noexcept
{
    using TSaveImpl = bool(BGSSaveLoadManager*, int32_t, uint32_t, const char*);
    POINTER_SKYRIMSE(TSaveImpl, s_save, 35727);
    return s_save(this, 2, 0, apName);
}

bool BGSSaveLoadManager::Load(const char* apName) noexcept
{
    // Only JoinBridge calls this: the party already authorized the transfer, so bypass the player-load hook.
    if (RealLoadImpl)
        return RealLoadImpl(this, apName, -1, 0, true);
    POINTER_SKYRIMSE(TLoadImpl, s_load, 35728);
    return s_load(this, apName, -1, 0, true);
}

std::filesystem::path BGSSaveLoadManager::SaveFilePath(const char* apName) noexcept
{
    // The utility singleton (GetWin32SaveDataSystemUtility 109278); its vfunc 2 joins the save folder and the name.
    using TGetUtility = void*();
    using TPreparePath = int(void*, const char*, char*, bool, bool);
    POINTER_SKYRIMSE(TGetUtility, s_getUtility, 109278);
    POINTER_SKYRIMSE(TPreparePath, s_preparePath, 109357);
    char path[0x104]{};
    void* pUtility = s_getUtility();
    if (!pUtility || s_preparePath(pUtility, apName, path, false, false) != 0)
        return {};
    return path;
}

std::optional<std::string> BGSSaveLoadManager::PreviousGameplaySave() noexcept
{
    // The catalogue of save files, read again (35760).
    using TPopulate = void(BGSSaveLoadManager*);
    POINTER_SKYRIMSE(TPopulate, s_populate, 35760);
    s_populate(this);
    const GameplaySaveEntry* pNewest = nullptr;
    for (const GameplaySaveEntry* cpEntry : saveGameList)
    {
        // This character's, not ours (a join's or a wipe's scratch save), and still on disk.
        if (!cpEntry || cpEntry->characterId != currentCharacterId || !cpEntry->fileName.AsAscii())
            continue;
        if (std::string_view(cpEntry->fileName.AsAscii()).starts_with("SkyrimCoop"))
            continue;
        std::error_code error;
        if (!std::filesystem::is_regular_file(SaveFilePath(cpEntry->fileName.AsAscii()), error))
            continue;
        if (!pNewest || cpEntry->fileTime > pNewest->fileTime)
            pNewest = cpEntry;
    }
    return pNewest ? std::optional<std::string>(pNewest->fileName.AsAscii()) : std::nullopt;
}

std::string BGSSaveLoadManager::QuickSaveName() noexcept
{
    using TGetSaveEntry = SaveEntry*(BGSSaveLoadManager*, const uint32_t*);
    POINTER_SKYRIMSE(TGetSaveEntry, s_entry, 35789);
    const SaveEntry* cpEntry = s_entry(this, &currentCharacterId);
    const char* cpName = cpEntry ? cpEntry->lastQuicksave.AsAscii() : nullptr;
    return cpName ? cpName : "";
}
