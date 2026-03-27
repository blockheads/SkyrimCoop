#include "ThreadUtils.h"

#ifdef _WIN32
#include <windows.h>
#else
#include <pthread.h>
#endif

namespace Base
{
bool SetThreadName(void* apThreadHandle, const char* apThreadName)
{
#ifdef _WIN32
    // SetThreadDescription available Windows 10 1607+
    wchar_t wideName[256];
    MultiByteToWideChar(CP_UTF8, 0, apThreadName, -1, wideName, 256);
    return SUCCEEDED(SetThreadDescription(reinterpret_cast<HANDLE>(apThreadHandle), wideName));
#else
    // pthread_setname_np only works on current thread on Linux
    // For non-current thread, this is a best-effort no-op
    (void)apThreadHandle;
    (void)apThreadName;
    return false;
#endif
}

bool SetCurrentThreadName(const char* apThreadName)
{
#ifdef _WIN32
    wchar_t wideName[256];
    MultiByteToWideChar(CP_UTF8, 0, apThreadName, -1, wideName, 256);
    return SUCCEEDED(SetThreadDescription(GetCurrentThread(), wideName));
#else
    return pthread_setname_np(pthread_self(), apThreadName) == 0;
#endif
}

} // namespace Base
