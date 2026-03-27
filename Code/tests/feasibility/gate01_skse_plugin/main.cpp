// GATE-01: MinGW SKSE plugin — incremental test: logging only
#include <windows.h>
#include <cstdint>
#include <cstdio>
#include <cstring>

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
    "GATE-01 Feasibility Test",
    "SkyrimCoop",
    "",
    SKSEPluginVersionData::kVersionIndependentEx_NoStructUse,
    SKSEPluginVersionData::kVersionIndependent_AddressLibraryPostAE
        | SKSEPluginVersionData::kVersionIndependent_StructsPost629,
    { MAKE_EXE_VERSION(1, 6, 1170), 0 },
    0,
};

// Use absolute path for logging — avoids CWD ambiguity under MO2
static void LogResult(const char* aMessage)
{
    HANDLE hFile = CreateFileA(
        "Z:\\media\\bighass\\1f640250-798d-4f2f-87f2-c442109fc5d0\\SteamLibrary\\steamapps\\common\\Skyrim Special Edition\\Data\\SKSE\\Plugins\\gate01_test.log",
        GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile != INVALID_HANDLE_VALUE)
    {
        DWORD bytesWritten = 0;
        WriteFile(hFile, aMessage, (DWORD)strlen(aMessage), &bytesWritten, NULL);
        CloseHandle(hFile);
    }
}

struct SKSEInterface
{
    uint32_t skseVersion;
    uint32_t runtimeVersion;
    uint32_t editorVersion;
    uint32_t isEditor;
    void* (*QueryInterface)(uint32_t aId);
};

extern "C" __attribute__((dllexport))
bool SKSEPlugin_Load(const SKSEInterface* apSkse)
{
    if (!apSkse)
    {
        LogResult("GATE-01 FAIL: null interface\n");
        return false;
    }

    char buf[256];
    snprintf(buf, sizeof(buf),
        "GATE-01 PASS: SKSEPlugin_Load called. SKSE=%u Runtime=%u\n",
        apSkse->skseVersion, apSkse->runtimeVersion);
    LogResult(buf);

    return true;
}

BOOL WINAPI DllMain(HINSTANCE aInstance, DWORD aReason, LPVOID apReserved)
{
    (void)aInstance;
    (void)apReserved;
    (void)aReason;
    return TRUE;
}
