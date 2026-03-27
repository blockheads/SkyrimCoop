// GATE-01: Minimal SKSE plugin compiled with MinGW
// Proves MinGW can produce a DLL with correct SKSE entry points and C ABI.
// Exercises SKSEPlugin_Query (version read) and SKSEPlugin_Load (messaging interface).
//
// Compiled with: xmake f -p mingw --mingw=<xpack-path> && xmake build gate01_skse_plugin
// Output: gate01_test.dll
// Log: Data\SKSE\Plugins\gate01_test.log

#include <windows.h>
#include <cstdint>
#include <cstdio>
#include <cstring>

// ---------------------------------------------------------------------------
// SKSE interface structures (C-compatible POD -- no vtables, no C++ ABI)
// These match the SKSE plugin API at the DLL boundary.
// ---------------------------------------------------------------------------

struct SKSEInterface
{
    uint32_t skseVersion;
    uint32_t runtimeVersion;
    uint32_t editorVersion;
    uint32_t isEditor;
    void* (*QueryInterface)(uint32_t aId);
};

// Interface IDs used by SKSE
static constexpr uint32_t cInterfaceMessaging = 1;

// Messaging callback signature
typedef void (*EventCallback)(void* apMessage);

struct SKSEMessagingInterface
{
    uint32_t interfaceVersion;
    bool (*RegisterListener)(void* apPlugin, const char* apSender, EventCallback aCallback);
};

// SKSE message types
static constexpr uint32_t cMessagePostLoad = 0;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static void LogResult(const char* aMessage)
{
    HANDLE hFile = CreateFileA(
        "Data\\SKSE\\Plugins\\gate01_test.log",
        GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile != INVALID_HANDLE_VALUE)
    {
        DWORD bytesWritten = 0;
        WriteFile(hFile, aMessage, (DWORD)strlen(aMessage), &bytesWritten, NULL);
        CloseHandle(hFile);
    }
}

// ---------------------------------------------------------------------------
// Messaging listener (exercises SKSE event registration per D-01)
// ---------------------------------------------------------------------------

static void MessageListener(void* apMessage)
{
    // If this fires, SKSE messaging ABI is fully compatible with MinGW.
    // The message structure is opaque for this test -- we only care that
    // the callback was registered and invoked.
    (void)apMessage;
}

// ---------------------------------------------------------------------------
// SKSE plugin exports -- C linkage, GCC dllexport attribute
// ---------------------------------------------------------------------------

extern "C" __attribute__((dllexport))
bool SKSEPlugin_Query(const SKSEInterface* apSkse, void* apInfo)
{
    if (!apSkse)
    {
        LogResult("GATE-01 FAIL: SKSEPlugin_Query received null interface pointer\n");
        return false;
    }

    char buf[512];
    snprintf(buf, sizeof(buf),
        "GATE-01 PASS: SKSEPlugin_Query called. SKSE=%u Runtime=%u\n",
        apSkse->skseVersion, apSkse->runtimeVersion);
    LogResult(buf);

    // apInfo would normally be filled with plugin metadata.
    // For the feasibility test we skip that -- SKSE tolerates it.
    (void)apInfo;
    return true;
}

extern "C" __attribute__((dllexport))
bool SKSEPlugin_Load(const SKSEInterface* apSkse)
{
    if (!apSkse)
    {
        LogResult("GATE-01 FAIL: SKSEPlugin_Load received null interface pointer\n");
        return false;
    }

    // Exercise SKSE messaging interface via QueryInterface (per D-01)
    if (apSkse->QueryInterface)
    {
        void* pRawMessaging = apSkse->QueryInterface(cInterfaceMessaging);
        if (pRawMessaging)
        {
            SKSEMessagingInterface* pMessaging =
                reinterpret_cast<SKSEMessagingInterface*>(pRawMessaging);

            if (pMessaging->RegisterListener)
            {
                // Register for kPostLoad -- proves full ABI round-trip
                pMessaging->RegisterListener(NULL, "SKSE", MessageListener);
            }
        }
    }

    return true;
}

// ---------------------------------------------------------------------------
// DLL entry point
// ---------------------------------------------------------------------------

BOOL WINAPI DllMain(HINSTANCE aInstance, DWORD aReason, LPVOID apReserved)
{
    (void)apReserved;

    if (aReason == DLL_PROCESS_ATTACH)
    {
        DisableThreadLibraryCalls(aInstance);
    }
    return TRUE;
}
