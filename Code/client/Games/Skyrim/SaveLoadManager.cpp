#include <SaveLoadManager.h>

#include <VersionDb.h>

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
    using TLoadImpl = bool(BGSSaveLoadManager*, const char*, int32_t, uint32_t, bool);
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
