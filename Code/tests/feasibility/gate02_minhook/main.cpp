// GATE-02: MinHook function hooking test compiled with MinGW
// Hooks GetTickCount, triggers it, logs pass/fail.
// Hook setup in SKSEPlugin_Load (not DllMain) for stability.

#include <windows.h>
#include <MinHook.h>
#include <cstdint>
#include <cstdio>
#include <cstring>

// ---------------------------------------------------------------------------
// SKSE version data (required by SKSE 2.2+)
// ---------------------------------------------------------------------------

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

extern "C" __attribute__((dllexport))
const SKSEPluginVersionData SKSEPlugin_Version = {
    SKSEPluginVersionData::kVersion,
    1,
    "GATE-02 MinHook Feasibility Test",
    "SkyrimCoop",
    "",
    SKSEPluginVersionData::kVersionIndependentEx_NoStructUse,
    SKSEPluginVersionData::kVersionIndependent_AddressLibraryPostAE
        | SKSEPluginVersionData::kVersionIndependent_StructsPost629,
    { MAKE_EXE_VERSION(1, 6, 1170), 0 },
    0,
};

// ---------------------------------------------------------------------------
// Hook state
// ---------------------------------------------------------------------------

typedef DWORD (WINAPI *GetTickCount_t)(void);
static GetTickCount_t fpGetTickCount = NULL;
static int s_hookCallCount = 0;

static DWORD WINAPI DetourGetTickCount(void)
{
    s_hookCallCount++;
    return fpGetTickCount();
}

// ---------------------------------------------------------------------------
// Helpers — absolute path to avoid MO2 USVFS CWD issues
// ---------------------------------------------------------------------------

static void LogResult(const char* aMessage)
{
    HANDLE hFile = CreateFileA(
        "Z:\\media\\bighass\\1f640250-798d-4f2f-87f2-c442109fc5d0\\SteamLibrary\\steamapps\\common\\Skyrim Special Edition\\Data\\SKSE\\Plugins\\gate02_minhook.log",
        GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile != INVALID_HANDLE_VALUE)
    {
        DWORD bytesWritten = 0;
        WriteFile(hFile, aMessage, (DWORD)strlen(aMessage), &bytesWritten, NULL);
        CloseHandle(hFile);
    }
}

// ---------------------------------------------------------------------------
// SKSE plugin load — perform MinHook test here
// ---------------------------------------------------------------------------

extern "C" __attribute__((dllexport))
bool SKSEPlugin_Load(const void* apSkse)
{
    (void)apSkse;

    MH_STATUS status = MH_Initialize();
    if (status != MH_OK)
    {
        char buf[256];
        snprintf(buf, sizeof(buf), "GATE-02 FAIL: MH_Initialize status %d\n", (int)status);
        LogResult(buf);
        return true;
    }

    status = MH_CreateHookApi(
        L"kernel32", "GetTickCount",
        (LPVOID)&DetourGetTickCount,
        (LPVOID*)&fpGetTickCount);
    if (status != MH_OK)
    {
        char buf[256];
        snprintf(buf, sizeof(buf), "GATE-02 FAIL: MH_CreateHookApi status %d\n", (int)status);
        LogResult(buf);
        MH_Uninitialize();
        return true;
    }

    status = MH_EnableHook(MH_ALL_HOOKS);
    if (status != MH_OK)
    {
        char buf[256];
        snprintf(buf, sizeof(buf), "GATE-02 FAIL: MH_EnableHook status %d\n", (int)status);
        LogResult(buf);
        MH_Uninitialize();
        return true;
    }

    // Trigger the hook
    DWORD tick = GetTickCount();

    // Disable hook immediately to avoid interfering with game
    MH_DisableHook(MH_ALL_HOOKS);

    char buf[256];
    if (s_hookCallCount > 0)
    {
        snprintf(buf, sizeof(buf),
            "GATE-02 PASS: MinHook working. Hook called %d times. Tick=%u\n",
            s_hookCallCount, (unsigned)tick);
    }
    else
    {
        snprintf(buf, sizeof(buf),
            "GATE-02 FAIL: Hook enabled but detour never fired. Tick=%u\n",
            (unsigned)tick);
    }
    LogResult(buf);

    return true;
}

// ---------------------------------------------------------------------------
// DLL entry point — minimal
// ---------------------------------------------------------------------------

BOOL WINAPI DllMain(HINSTANCE aInstance, DWORD aReason, LPVOID apReserved)
{
    (void)aInstance;
    (void)apReserved;
    (void)aReason;
    return TRUE;
}
