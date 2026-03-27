#include "ThreadUtils.h"

#ifdef _WIN32
#include <windows.h>

// SetThreadDescription may not be available in MinGW headers.
// Dynamically load it from kernel32 at runtime.
namespace
{
using PFN_SetThreadDescription = HRESULT(WINAPI*)(HANDLE, PCWSTR);

PFN_SetThreadDescription GetSetThreadDescription()
{
    static PFN_SetThreadDescription s_pFunc = reinterpret_cast<PFN_SetThreadDescription>(
        GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "SetThreadDescription"));
    return s_pFunc;
}
} // namespace

#else
#include <pthread.h>
#endif

namespace Base
{
bool SetThreadName(void* apThreadHandle, const char* apThreadName)
{
#ifdef _WIN32
    // SetThreadDescription available Windows 10 1607+
    auto pSetThreadDescription = GetSetThreadDescription();
    if (!pSetThreadDescription)
        return false;
    wchar_t wideName[256];
    MultiByteToWideChar(CP_UTF8, 0, apThreadName, -1, wideName, 256);
    return SUCCEEDED(pSetThreadDescription(reinterpret_cast<HANDLE>(apThreadHandle), wideName));
#else
    (void)apThreadHandle;
    (void)apThreadName;
    return false;
#endif
}

bool SetCurrentThreadName(const char* apThreadName)
{
#ifdef _WIN32
    auto pSetThreadDescription = GetSetThreadDescription();
    if (!pSetThreadDescription)
        return false;
    wchar_t wideName[256];
    MultiByteToWideChar(CP_UTF8, 0, apThreadName, -1, wideName, 256);
    return SUCCEEDED(pSetThreadDescription(GetCurrentThread(), wideName));
#else
    return pthread_setname_np(pthread_self(), apThreadName) == 0;
#endif
}

} // namespace Base
