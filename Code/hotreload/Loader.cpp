// SkyrimCoopLoader.dll: a resident SKSE plugin that loads the client DLL from a copy, so a rebuilt
// client can be swapped into a running game (scripts/coop_test.py reload).
//
//   Data/SKSE/Plugins/SkyrimCoopLoader.dll                    this plugin (SKSE loads it)
//   Data/SKSE/Plugins/SkyrimCoop/SkyrimTogetherClient.dll     the client, replaced by each build
//   Data/SKSE/Plugins/SkyrimCoop/loaded/<name>.<pid>.<n>.dll  the copy actually loaded
//
// The loader hooks the engine's main loop on top of the client's hook, so it runs before any client
// code each frame. A reload (the client's "reload" command calls SkyrimCoopLoader_RequestReload)
// happens there: no client frame is on the main thread's stack while the old copy unloads itself
// (SkyrimCoop_Unload) and the new copy loads.
//
// The old copy is never freed. Its static CRT and rpmalloc keep fiber-local storage callbacks that
// run when any thread exits, and engine threads may still be returning out of its hooks; leaving it
// mapped makes both harmless. Each reload costs the copy's image and heap (tens of MB).
#include <windows.h>

#include <MinHook.h>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <map>
#include <string>

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

struct SKSEInterface;

extern "C" __declspec(dllexport)
const SKSEPluginVersionData SKSEPlugin_Version = {
    SKSEPluginVersionData::kVersion,
    1,
    "SkyrimCoopLoader",
    "SkyrimCoop",
    "",
    SKSEPluginVersionData::kVersionIndependentEx_NoStructUse,
    SKSEPluginVersionData::kVersionIndependent_AddressLibraryPostAE
        | SKSEPluginVersionData::kVersionIndependent_StructsPost629,
    { MAKE_EXE_VERSION(1, 6, 1170), 0 },
    0,
};

namespace
{
using TLoad = bool (*)(const SKSEInterface*);
using TSetPluginDirectory = void (*)(const wchar_t*);
using TExtensionLayout = uint32_t (*)();
using TUnload = bool (*)();
using TMainLoopAddress = void* (*)();
using TMainLoop = short (*)(void*);

struct Client
{
    HMODULE Module = nullptr;
    TLoad Load = nullptr;
    TSetPluginDirectory SetPluginDirectory = nullptr;
    TExtensionLayout ExtensionLayout = nullptr;
    TUnload Unload = nullptr;
    TMainLoopAddress MainLoopAddress = nullptr;
};

HMODULE s_self = nullptr;
const SKSEInterface* s_pSkse = nullptr;
std::filesystem::path s_pluginDirectory;
Client s_client;
void* s_pMainLoop = nullptr;
TMainLoop s_realMainLoop = nullptr;
std::atomic<bool> s_reloadRequested{false};
uint32_t s_generation = 0;
std::string s_lastError;
// State a client hands to the next one (SkyrimCoopLoader_Store/Fetch).
std::map<std::string, std::string> s_handoff;
FILE* s_pLog = nullptr;

void Log(const char* apFormat, const std::string& acArgument = {})
{
    if (!s_pLog)
        return;
    SYSTEMTIME time;
    GetLocalTime(&time);
    fprintf(s_pLog, "[%02u:%02u:%02u.%03u] ", time.wHour, time.wMinute, time.wSecond, time.wMilliseconds);
    fprintf(s_pLog, apFormat, acArgument.c_str());
    fputc('\n', s_pLog);
    fflush(s_pLog);
}

std::string Narrow(const std::filesystem::path& acPath)
{
    return acPath.string();
}

std::string InstanceSuffix()
{
    char instance[64]{};
    const DWORD cLength = GetEnvironmentVariableA("SKYRIMCOOP_INSTANCE", instance, sizeof(instance));
    return cLength > 0 && cLength < sizeof(instance) ? std::string("-") + instance : std::string();
}

std::filesystem::path ClientSource()
{
    return s_pluginDirectory / "SkyrimCoop" / "SkyrimTogetherClient.dll";
}

// This instance's copies from earlier runs.
void RemoveOldCopies(const std::filesystem::path& acDirectory, const std::string& acPrefix)
{
    std::error_code error;
    for (const auto& cEntry : std::filesystem::directory_iterator(acDirectory, error))
        if (cEntry.path().filename().string().starts_with(acPrefix))
            std::filesystem::remove(cEntry.path(), error);
}

// Loads a fresh copy of the client without starting it.
bool LoadCopy(Client& aClient)
{
    const auto cDirectory = s_pluginDirectory / "SkyrimCoop" / "loaded";
    std::error_code error;
    std::filesystem::create_directories(cDirectory, error);
    // Per instance: two test games share the install, and their Wine prefixes can hand out the
    // same process ids.
    const std::string cPrefix = "SkyrimTogetherClient" + InstanceSuffix() + ".";
    if (s_generation == 0)
        RemoveOldCopies(cDirectory, cPrefix);

    const auto cCopy = cDirectory / (cPrefix + std::to_string(GetCurrentProcessId()) + "." +
                                     std::to_string(s_generation) + ".dll");
    if (!std::filesystem::copy_file(ClientSource(), cCopy, std::filesystem::copy_options::overwrite_existing, error))
    {
        s_lastError = "could not copy " + Narrow(ClientSource()) + ": " + error.message();
        return false;
    }

    aClient = {};
    aClient.Module = LoadLibraryW(cCopy.c_str());
    if (!aClient.Module)
    {
        s_lastError = "LoadLibrary failed for " + Narrow(cCopy) + " (error " + std::to_string(GetLastError()) + ")";
        return false;
    }
    aClient.Load = reinterpret_cast<TLoad>(GetProcAddress(aClient.Module, "SKSEPlugin_Load"));
    aClient.SetPluginDirectory = reinterpret_cast<TSetPluginDirectory>(GetProcAddress(aClient.Module, "SkyrimCoop_SetPluginDirectory"));
    aClient.ExtensionLayout = reinterpret_cast<TExtensionLayout>(GetProcAddress(aClient.Module, "SkyrimCoop_ExtensionLayout"));
    aClient.Unload = reinterpret_cast<TUnload>(GetProcAddress(aClient.Module, "SkyrimCoop_Unload"));
    aClient.MainLoopAddress = reinterpret_cast<TMainLoopAddress>(GetProcAddress(aClient.Module, "SkyrimCoop_MainLoopAddress"));
    if (!aClient.Load || !aClient.SetPluginDirectory || !aClient.ExtensionLayout || !aClient.Unload || !aClient.MainLoopAddress)
    {
        s_lastError = "the client does not export the hot reload interface";
        FreeLibrary(aClient.Module); // Never started: nothing of it runs.
        aClient = {};
        return false;
    }
    Log("Loaded %s", Narrow(cCopy));
    return true;
}

bool StartClient(const Client& acClient)
{
    acClient.SetPluginDirectory(s_pluginDirectory.c_str());
    return acClient.Load(s_pSkse);
}

short HookMainLoop(void* apThis);

// Logs a crash during a reload (the client's own crash handler is gone or not up yet).
LONG CALLBACK LogReloadCrash(EXCEPTION_POINTERS* apInfo)
{
    // Everything but C++ exceptions, debug output and thread naming: an abort() raises a
    // breakpoint or a fast-fail first, and the client's crash handler is not up to report it.
    static int s_logged = 0;
    const DWORD cCode = apInfo->ExceptionRecord->ExceptionCode;
    if (cCode == 0xE06D7363 || cCode == 0x40010006 || cCode == 0x4001000A || cCode == 0x406D1388 || s_logged >= 20)
        return EXCEPTION_CONTINUE_SEARCH;
    ++s_logged;
    const auto cAddress = reinterpret_cast<uintptr_t>(apInfo->ExceptionRecord->ExceptionAddress);
    HMODULE module = nullptr;
    char name[MAX_PATH] = "?";
    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCSTR>(cAddress), &module))
        GetModuleFileNameA(module, name, sizeof(name));
    char line[512];
    snprintf(line, sizeof(line), "Exception %08lX at %s+0x%llX (access %llX)", cCode, name,
             static_cast<unsigned long long>(cAddress - reinterpret_cast<uintptr_t>(module)),
             apInfo->ExceptionRecord->NumberParameters > 1
                 ? static_cast<unsigned long long>(apInfo->ExceptionRecord->ExceptionInformation[1]) : 0ull);
    Log("%s", line);
    return EXCEPTION_CONTINUE_SEARCH;
}

// Hooks the main loop on top of the client's hook: ours runs first.
bool HookOutermost()
{
    if (MH_CreateHook(s_pMainLoop, reinterpret_cast<void*>(&HookMainLoop), reinterpret_cast<void**>(&s_realMainLoop)) != MH_OK ||
        MH_EnableHook(s_pMainLoop) != MH_OK)
    {
        s_lastError = "could not hook the main loop";
        return false;
    }
    return true;
}

void Reload()
{
    Log("Reload %s", std::to_string(s_generation + 1));
    if (!std::filesystem::exists(ClientSource()))
    {
        s_lastError = "no client at " + Narrow(ClientSource());
        Log("Refused: %s", s_lastError);
        return;
    }

    ++s_generation; // Names the copy.
    Client next;
    if (!LoadCopy(next))
    {
        --s_generation;
        Log("Refused: %s", s_lastError);
        return;
    }
    if (next.ExtensionLayout() != s_client.ExtensionLayout())
    {
        // Existing actors carry the old layout in their memory.
        FreeLibrary(next.Module);
        --s_generation;
        s_lastError = "the actor extension layout changed: restart the game";
        Log("Refused: %s", s_lastError);
        return;
    }

    void* const cCrashLogger = AddVectoredExceptionHandler(1, &LogReloadCrash);

    // Our hook sits on top of the old client's: take ours off so the old client restores the
    // engine's own bytes, then put it back on top of the new client's.
    MH_DisableHook(s_pMainLoop);
    MH_RemoveHook(s_pMainLoop);
    s_realMainLoop = nullptr;

    Log("Unloading the old client%s");
    s_client.Unload();
    s_client = next;
    s_lastError.clear();
    Log("Starting the new client%s");
    if (!StartClient(s_client))
        s_lastError = "the new client failed to load";
    Log("Started%s");
    if (!HookOutermost())
        Log("Fatal: %s", s_lastError);
    RemoveVectoredExceptionHandler(cCrashLogger);
    Log("Reloaded: %s", s_lastError.empty() ? std::string("ok") : s_lastError);
}

short HookMainLoop(void* apThis)
{
    if (s_reloadRequested.exchange(false))
        Reload();
    return s_realMainLoop(apThis);
}
} // namespace

// Called by the client (its "reload" command) on the game thread; the reload runs next frame.
extern "C" __declspec(dllexport) void SkyrimCoopLoader_RequestReload()
{
    s_lastError.clear(); // Pending: no error and the same generation.
    s_reloadRequested = true;
}

// Reloads done so far; the harness waits for it to go up.
extern "C" __declspec(dllexport) uint32_t SkyrimCoopLoader_Generation()
{
    return s_generation;
}

// Hands state to the next client: what the engine told the old one once at boot (e.g. Papyrus
// native addresses) and will not tell again. Game thread only.
extern "C" __declspec(dllexport) void SkyrimCoopLoader_Store(const char* apKey, const char* apData, size_t aSize)
{
    s_handoff[apKey].assign(apData, aSize);
}

// Copies up to aCapacity bytes of a stored value into apOut; returns its size, or SIZE_MAX if none.
extern "C" __declspec(dllexport) size_t SkyrimCoopLoader_Fetch(const char* apKey, char* apOut, size_t aCapacity)
{
    const auto cEntry = s_handoff.find(apKey);
    if (cEntry == s_handoff.end())
        return SIZE_MAX;
    memcpy(apOut, cEntry->second.data(), std::min(aCapacity, cEntry->second.size()));
    return cEntry->second.size();
}

// Why the last reload was refused or failed, or empty.
extern "C" __declspec(dllexport) const char* SkyrimCoopLoader_LastError()
{
    return s_lastError.c_str();
}

extern "C" __declspec(dllexport) bool SKSEPlugin_Load(const SKSEInterface* apSkse)
{
    if (!apSkse)
        return false;
    s_pSkse = apSkse;

    wchar_t path[MAX_PATH]{};
    GetModuleFileNameW(s_self, path, MAX_PATH);
    s_pluginDirectory = std::filesystem::path(path).parent_path();

    std::error_code error;
    std::filesystem::create_directories(s_pluginDirectory / "logs", error);
    const auto cLogPath = s_pluginDirectory / "logs" / ("SkyrimCoopLoader" + InstanceSuffix() + ".log");
    _wfopen_s(&s_pLog, cLogPath.c_str(), L"w");

    if (MH_Initialize() != MH_OK)
    {
        Log("MinHook did not initialize%s");
        return false;
    }
    if (!LoadCopy(s_client))
    {
        Log("Not loaded: %s", s_lastError);
        return false;
    }
    if (!StartClient(s_client))
    {
        Log("The client failed to load%s");
        return false;
    }
    s_pMainLoop = s_client.MainLoopAddress();
    if (!HookOutermost())
    {
        Log("%s", s_lastError);
        return false;
    }
    Log("Client started%s");
    return true;
}

BOOL WINAPI DllMain(HINSTANCE aInstance, DWORD aReason, LPVOID)
{
    if (aReason == DLL_PROCESS_ATTACH)
        s_self = aInstance;
    return TRUE;
}
