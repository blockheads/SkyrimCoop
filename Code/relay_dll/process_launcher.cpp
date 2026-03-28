#include "process_launcher.h"
#include <cstdio>
#include <cstring>

// Under Wine, getpid() resolves at runtime to the real Linux PID.
// This is needed because GetCurrentProcessId() returns a Wine-internal PID,
// but the native process needs the Linux PID for /proc/pid/mem access.
// Per Pitfall 7 (Wine PID): prefer getpid() for real Linux PID.
extern "C" int getpid(void);

bool ProcessLauncher::Launch(const char* nativePath, uint16_t tcpPort)
{
    if (m_launched)
        Kill();

    // Get the real Linux PID via getpid() (resolves under Wine to actual PID)
    int linuxPid = getpid();

    // Convert Linux path to Wine Z: drive format
    // Linux path: /path/to/binary -> Wine path: Z:\path\to\binary
    char winePath[MAX_PATH]{};
    if (nativePath[0] == '/')
    {
        // Prefix with Z: and convert forward slashes to backslashes
        snprintf(winePath, sizeof(winePath), "Z:");
        size_t offset = 2;
        for (size_t i = 0; nativePath[i] != '\0' && offset < sizeof(winePath) - 1; i++)
        {
            winePath[offset++] = (nativePath[i] == '/') ? '\\' : nativePath[i];
        }
        winePath[offset] = '\0';
    }
    else
    {
        // Already a Windows-style path or relative
        strncpy(winePath, nativePath, sizeof(winePath) - 1);
    }

    // Build command line: <binary> --pid <pid> --port <port>
    char cmdLine[1024]{};
    snprintf(cmdLine, sizeof(cmdLine), "\"%s\" --pid %d --port %u",
             winePath, linuxPid, static_cast<unsigned>(tcpPort));

    // Launch via CreateProcess with CREATE_NEW_CONSOLE for separate logging output.
    // Wine's CreateProcess internally does fork/exec (per Pitfall 2).
    STARTUPINFOA si{};
    si.cb = sizeof(si);

    BOOL success = CreateProcessA(
        nullptr,              // lpApplicationName (derived from cmdLine)
        cmdLine,              // lpCommandLine
        nullptr,              // lpProcessAttributes
        nullptr,              // lpThreadAttributes
        FALSE,                // bInheritHandles
        CREATE_NEW_CONSOLE,   // dwCreationFlags -- native process gets own console
        nullptr,              // lpEnvironment
        nullptr,              // lpCurrentDirectory
        &si,
        &m_processInfo
    );

    if (!success)
        return false;

    // Close the thread handle -- we only need the process handle for monitoring
    CloseHandle(m_processInfo.hThread);
    m_processInfo.hThread = nullptr;

    m_launched = true;
    return true;
}

bool ProcessLauncher::IsAlive() const
{
    if (!m_launched || m_processInfo.hProcess == nullptr)
        return false;

    return WaitForSingleObject(m_processInfo.hProcess, 0) == WAIT_TIMEOUT;
}

void ProcessLauncher::Kill()
{
    if (!m_launched)
        return;

    if (m_processInfo.hProcess != nullptr)
    {
        TerminateProcess(m_processInfo.hProcess, 1);
        WaitForSingleObject(m_processInfo.hProcess, 3000);
        CloseHandle(m_processInfo.hProcess);
    }

    m_processInfo = {};
    m_launched = false;
}
