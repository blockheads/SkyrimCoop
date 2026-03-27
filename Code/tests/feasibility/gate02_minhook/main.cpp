// GATE-02: MinHook function hooking test compiled with MinGW
// Proves MinHook can create and enable x64 function hooks when cross-compiled.
// Hooks GetTickCount from kernel32.dll, calls it to trigger the detour,
// and logs pass/fail to a file.
//
// Compiled with: xmake f -p mingw --mingw=<xpack-path> && xmake build gate02_minhook
// Output: gate02_test.dll
// Log: Data\SKSE\Plugins\gate02_minhook.log

#include <windows.h>
#include <MinHook.h>
#include <cstdint>
#include <cstdio>
#include <cstring>

// ---------------------------------------------------------------------------
// Hook state
// ---------------------------------------------------------------------------

typedef DWORD (WINAPI *GetTickCount_t)(void);
static GetTickCount_t fpGetTickCount = NULL;
static int s_hookCallCount = 0;

// ---------------------------------------------------------------------------
// Detour function
// ---------------------------------------------------------------------------

static DWORD WINAPI DetourGetTickCount(void)
{
    s_hookCallCount++;
    return fpGetTickCount();
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static void LogResult(const char* aMessage)
{
    HANDLE hFile = CreateFileA(
        "Data\\SKSE\\Plugins\\gate02_minhook.log",
        GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile != INVALID_HANDLE_VALUE)
    {
        DWORD bytesWritten = 0;
        WriteFile(hFile, aMessage, (DWORD)strlen(aMessage), &bytesWritten, NULL);
        CloseHandle(hFile);
    }
}

// ---------------------------------------------------------------------------
// DLL entry point -- performs hook setup and validation
// ---------------------------------------------------------------------------

BOOL WINAPI DllMain(HINSTANCE aInstance, DWORD aReason, LPVOID apReserved)
{
    (void)apReserved;

    if (aReason == DLL_PROCESS_ATTACH)
    {
        DisableThreadLibraryCalls(aInstance);

        // Step 1: Initialize MinHook
        MH_STATUS status = MH_Initialize();
        if (status != MH_OK)
        {
            char buf[256];
            snprintf(buf, sizeof(buf),
                "GATE-02 FAIL: MH_Initialize failed with status %d\n",
                (int)status);
            LogResult(buf);
            return TRUE;
        }

        // Step 2: Hook GetTickCount from kernel32.dll
        status = MH_CreateHookApi(
            L"kernel32", "GetTickCount",
            (LPVOID)&DetourGetTickCount,
            (LPVOID*)&fpGetTickCount);
        if (status != MH_OK)
        {
            char buf[256];
            snprintf(buf, sizeof(buf),
                "GATE-02 FAIL: MH_CreateHookApi failed with status %d\n",
                (int)status);
            LogResult(buf);
            MH_Uninitialize();
            return TRUE;
        }

        // Step 3: Enable all hooks
        status = MH_EnableHook(MH_ALL_HOOKS);
        if (status != MH_OK)
        {
            char buf[256];
            snprintf(buf, sizeof(buf),
                "GATE-02 FAIL: MH_EnableHook failed with status %d\n",
                (int)status);
            LogResult(buf);
            MH_Uninitialize();
            return TRUE;
        }

        // Step 4: Trigger the hook by calling GetTickCount
        DWORD tick = GetTickCount();

        // Step 5: Log results
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
                "GATE-02 FAIL: Hook was enabled but detour never fired. Tick=%u\n",
                (unsigned)tick);
        }
        LogResult(buf);
    }
    else if (aReason == DLL_PROCESS_DETACH)
    {
        MH_DisableHook(MH_ALL_HOOKS);
        MH_Uninitialize();
    }

    return TRUE;
}
