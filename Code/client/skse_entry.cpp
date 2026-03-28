// skse_entry.cpp — Thin DLL entry point for SKSE plugin loading under MinGW
// Bridges SKSE's plugin interface to the client's RunTiltedInit/RunTiltedApp
#include <windows.h>
#include <cstdint>
#include <cstring>
#include <filesystem>

#include <TiltedCore/Stl.hpp>

using TiltedPhoques::String;

#define MAKE_EXE_VERSION_EX(major, minor, build, sub) \
    ((((major) & 0xFF) << 24) | (((minor) & 0xFF) << 16) | (((build) & 0xFFF) << 4) | ((sub) & 0xF))
#define MAKE_EXE_VERSION(major, minor, build) MAKE_EXE_VERSION_EX(major, minor, build, 0)

struct SKSEPluginVersionData
{
    enum { kVersion = 1 };
    enum
    {
        kVersionIndependent_AddressLibraryPostAE = 1 << 0,
        kVersionIndependent_StructsPost629 = 1 << 2,
    };
    enum { kVersionIndependentEx_NoStructUse = 1 << 0 };

    uint32_t dataVersion;
    uint32_t pluginVersion;
    char     name[256];
    char     author[256];
    char     supportEmail[252];
    uint32_t versionIndependenceEx;
    uint32_t versionIndependence;
    uint32_t compatibleVersions[16];
    uint32_t seVersionRequired;
};

struct SKSEInterface
{
    uint32_t skseVersion;
    uint32_t runtimeVersion;
    uint32_t editorVersion;
    uint32_t isEditor;
    void* (*QueryInterface)(uint32_t aId);
};

static HINSTANCE s_dllInstance = nullptr;

extern "C" __attribute__((dllexport))
const SKSEPluginVersionData SKSEPlugin_Version = {
    SKSEPluginVersionData::kVersion,
    1,
    "SkyrimTogether",
    "SkyrimCoop",
    "",
    SKSEPluginVersionData::kVersionIndependentEx_NoStructUse,
    SKSEPluginVersionData::kVersionIndependent_AddressLibraryPostAE
        | SKSEPluginVersionData::kVersionIndependent_StructsPost629,
    { MAKE_EXE_VERSION(1, 6, 1170), 0 },
    0,
};

extern void RunTiltedInit(const std::filesystem::path& acGamePath, const TiltedPhoques::String& aExeVersion);
extern void RunTiltedApp();

extern "C" __attribute__((dllexport))
bool SKSEPlugin_Load(const SKSEInterface* apSkse)
{
    if (!apSkse)
        return false;

    // Derive game root from DLL path: Data/SKSE/Plugins/SkyrimTogetherClient.dll -> Skyrim root
    wchar_t dllPath[MAX_PATH]{};
    GetModuleFileNameW(s_dllInstance, dllPath, MAX_PATH);
    std::filesystem::path gamePath = std::filesystem::path(dllPath).parent_path().parent_path().parent_path().parent_path();

    TiltedPhoques::String exeVersion("1.6.1170");

    RunTiltedInit(gamePath, exeVersion);
    RunTiltedApp();

    return true;
}

BOOL WINAPI DllMain(HINSTANCE aInstance, DWORD aReason, LPVOID apReserved)
{
    (void)apReserved;
    if (aReason == DLL_PROCESS_ATTACH)
        s_dllInstance = aInstance;
    return TRUE;
}
