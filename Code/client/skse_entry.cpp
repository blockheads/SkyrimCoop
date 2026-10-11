// skse_entry.cpp — Thin DLL entry point for SKSE plugin loading under MinGW
// Bridges SKSE's plugin interface to the client's RunTiltedInit/RunTiltedApp
#include <windows.h>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <exception>

#include <TiltedCore/Stl.hpp>
#include "HotReload/Runtime.h"

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

extern "C" __declspec(dllexport)
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

// The resident hot-reload loader (Code/loader) loads a copy of this DLL from elsewhere and tells it
// where the SKSE plugin directory is.
static std::filesystem::path s_pluginDirectory;

extern "C" __declspec(dllexport) void SkyrimCoop_SetPluginDirectory(const wchar_t* apDirectory)
{
    s_pluginDirectory = apDirectory;
}

// abort() fast-fails, which ends the process before any exception handler (ours or the hot reload
// loader's) runs; std::terminate ends in abort(). Both leave a stack in logs/SkyrimCoopAbort*.log.
static std::filesystem::path s_abortLogPath;

static void LogAbort(const char* apReason)
{
    void* frames[48];
    const USHORT cCount = RtlCaptureStackBackTrace(1, 48, frames, nullptr);
    FILE* pFile = _wfopen(s_abortLogPath.c_str(), L"a");
    if (!pFile)
        return;
    fprintf(pFile, "%s (thread %lu)\n", apReason, GetCurrentThreadId());
    for (USHORT i = 0; i < cCount; ++i)
    {
        HMODULE module = nullptr;
        char name[MAX_PATH] = "?";
        if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                               static_cast<LPCSTR>(frames[i]), &module))
            GetModuleFileNameA(module, name, sizeof(name));
        const char* pBase = strrchr(name, '\\');
        fprintf(pFile, "  backtrace #%02u %s+0x%llx\n", i, pBase ? pBase + 1 : name,
                static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(frames[i]) - reinterpret_cast<uintptr_t>(module)));
    }
    fclose(pFile);
}

static void InstallAbortLogging(const std::filesystem::path& acPluginDirectory)
{
    wchar_t instance[64]{};
    const DWORD cLength = GetEnvironmentVariableW(L"SKYRIMCOOP_INSTANCE", instance, 64);
    s_abortLogPath = acPluginDirectory / "logs" /
                     (std::wstring(L"SkyrimCoopAbort") + (cLength > 0 && cLength < 64 ? L"-" + std::wstring(instance) : L"") + L".log");
    _set_abort_behavior(0, _CALL_REPORTFAULT); // Raise SIGABRT instead of fast-failing.
    signal(SIGABRT, [](int) { LogAbort("abort()"); });
    std::set_terminate([] {
        LogAbort("std::terminate");
        std::_Exit(3);
    });
}

extern "C" __declspec(dllexport)
bool SKSEPlugin_Load(const SKSEInterface* apSkse)
{
    if (!apSkse)
        return false;

    // Data/SKSE/Plugins/SkyrimTogetherClient.dll -> Skyrim root
    std::filesystem::path pluginDirectory = s_pluginDirectory;
    if (pluginDirectory.empty())
    {
        wchar_t dllPath[MAX_PATH]{};
        GetModuleFileNameW(s_dllInstance, dllPath, MAX_PATH);
        pluginDirectory = std::filesystem::path(dllPath).parent_path();
    }
    InstallAbortLogging(pluginDirectory);
    std::filesystem::path gamePath = pluginDirectory.parent_path().parent_path().parent_path();
    CoopReload::Runtime::Get().Configure(pluginDirectory);

    TiltedPhoques::String exeVersion("1.6.1170.0");

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
