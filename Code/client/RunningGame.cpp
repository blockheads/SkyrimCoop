#include <TiltedOnlinePCH.h>
#include <RunningGame.h>

#include <Bridges/IFormBridge.h>
#include <Structs/GameVersions.h>
#include <Structs/Mods.h>

namespace RunningGame
{
static GameVersions s_versions;

void SetVersions(uint32_t aRuntime, uint32_t aSkse)
{
    s_versions = {aRuntime, aSkse};
}

const GameVersions& GetVersions()
{
    return s_versions;
}

namespace
{
uintptr_t s_scriptExtenderBegin = 0;
uintptr_t s_scriptExtenderEnd = 0;
} // namespace

void SetScriptExtender(const void* apInModule) noexcept
{
    HMODULE module = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            static_cast<LPCWSTR>(apInModule), &module))
        return;
    // The module's extent from its PE header.
    const auto cBase = reinterpret_cast<uintptr_t>(module);
    const auto* pDos = reinterpret_cast<const IMAGE_DOS_HEADER*>(cBase);
    const auto* pNt = reinterpret_cast<const IMAGE_NT_HEADERS*>(cBase + pDos->e_lfanew);
    s_scriptExtenderBegin = cBase;
    s_scriptExtenderEnd = cBase + pNt->OptionalHeader.SizeOfImage;
}

bool IsScriptExtenderCode(const void* apAddress) noexcept
{
    const auto cAddress = reinterpret_cast<uintptr_t>(apAddress);
    return cAddress >= s_scriptExtenderBegin && cAddress < s_scriptExtenderEnd;
}

Mods Plugins(const IFormBridge& acForms)
{
    Mods plugins;
    for (const auto& cMod : acForms.GetLoadedMods())
    {
        auto& entry = plugins.ModList.emplace_back();
        entry.Id = cMod.Id;
        entry.IsLite = cMod.IsLite;
        entry.Filename = cMod.Filename.c_str();
    }
    return plugins;
}
} // namespace RunningGame
