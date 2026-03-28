#pragma once
#include <windows.h>
#include <cstdint>

class ProcessLauncher {
public:
    // Launch the native skyrim-coop binary. nativePath is the Linux path to the binary.
    // tcpPort is the port the TCP server is listening on.
    bool Launch(const char* nativePath, uint16_t tcpPort);

    // Check if the native process is still running
    bool IsAlive() const;

    // Kill the native process
    void Kill();

    // Get the spawned process handle
    HANDLE GetProcessHandle() const { return m_processInfo.hProcess; }

private:
    PROCESS_INFORMATION m_processInfo{};
    bool m_launched = false;
};
