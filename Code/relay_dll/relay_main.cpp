// Code/relay_dll/relay_main.cpp -- SKSE plugin entry point for relay DLL
// Per D-10: Exports SKSEPlugin_Version and SKSEPlugin_Load
// Per D-11: Does NOT use SKSE interfaces (Messaging, Papyrus, Serialization, Task)
// Per D-13: No TiltedCore, EnTT, or spdlog dependencies
// Per D-07: Startup: TCP server -> spawn native -> monitor thread with auto-restart
#include <winsock2.h>  // must be before windows.h
#include <windows.h>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "protocol.h"
#include "tcp_server.h"
#include "process_launcher.h"
#include "command_queue.h"

// ---------------------------------------------------------------------------
// SKSE structures (inline, no SKSE SDK dependency)
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

struct SKSEInterface
{
    uint32_t skseVersion;
    uint32_t runtimeVersion;
    uint32_t editorVersion;
    uint32_t isEditor;
    void* (*QueryInterface)(uint32_t aId);
};

// ---------------------------------------------------------------------------
// Globals
// ---------------------------------------------------------------------------

static HINSTANCE s_dllInstance = nullptr;
static TcpServer g_tcpServer;
static ProcessLauncher g_launcher;
static CommandQueue g_commandQueue;
static volatile bool g_shuttingDown = false;
static char g_nativeBinaryPath[MAX_PATH]{};

// ---------------------------------------------------------------------------
// Logging helpers (OutputDebugString -- no spdlog per D-13)
// ---------------------------------------------------------------------------

static void RelayLog(const char* fmt, ...)
{
    char buf[512];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    OutputDebugStringA(buf);
}

// ---------------------------------------------------------------------------
// Stub: Execute a game command (placeholder for Plan 05+)
// ---------------------------------------------------------------------------

// Called from game thread update hook to drain and execute queued commands.
// Placeholder until Plan 05+ installs the actual game update hook.
[[maybe_unused]]
static void DrainCommandQueue()
{
    g_commandQueue.Drain([](const CommandSlot& cmd) {
        // TODO(Plan 05+): Execute actual game functions via hook trampolines
        RelayLog("[SkyrimCoopHooks] Executing command opcode=0x%04X argCount=%u\n",
                 cmd.opcode, cmd.argCount);
    });
}

// ---------------------------------------------------------------------------
// Native binary path derivation
// ---------------------------------------------------------------------------

static bool DeriveNativeBinaryPath()
{
    // Get DLL path: Data/SKSE/Plugins/skyrim_coop_hooks.dll
    char dllPath[MAX_PATH]{};
    if (GetModuleFileNameA(s_dllInstance, dllPath, MAX_PATH) == 0)
        return false;

    // Find last backslash to isolate directory
    char* pLastSlash = nullptr;
    for (char* p = dllPath; *p; p++)
    {
        if (*p == '\\' || *p == '/')
            pLastSlash = p;
    }

    if (!pLastSlash)
        return false;

    // Replace DLL filename with native binary name
    *(pLastSlash + 1) = '\0';

    // The native binary is "skyrim-coop" in the same directory
    // Under Wine, the DLL path is a Windows path (e.g. Z:\path\to\Data\SKSE\Plugins\)
    // We need to convert this to a Linux path for the native binary
    if (dllPath[0] == 'Z' && dllPath[1] == ':')
    {
        // Strip "Z:" prefix and convert backslashes to forward slashes
        char linuxDir[MAX_PATH]{};
        size_t j = 0;
        for (size_t i = 2; dllPath[i] != '\0' && j < sizeof(linuxDir) - 1; i++)
        {
            linuxDir[j++] = (dllPath[i] == '\\') ? '/' : dllPath[i];
        }
        linuxDir[j] = '\0';
        snprintf(g_nativeBinaryPath, sizeof(g_nativeBinaryPath), "%sskyrim-coop", linuxDir);
    }
    else
    {
        // Non-Wine or already a Windows path -- use as-is
        snprintf(g_nativeBinaryPath, sizeof(g_nativeBinaryPath), "%sskyrim-coop", dllPath);
    }

    return true;
}

// ---------------------------------------------------------------------------
// Monitor thread: accepts client, receives commands, auto-restarts on disconnect
// ---------------------------------------------------------------------------

static DWORD WINAPI MonitorThread(LPVOID)
{
    while (!g_shuttingDown)
    {
        RelayLog("[SkyrimCoopHooks] Waiting for native process connection on port %u...\n",
                 g_tcpServer.GetPort());

        if (!g_tcpServer.AcceptClient())
        {
            if (g_shuttingDown)
                break;
            Sleep(1000);
            continue;
        }

        RelayLog("[SkyrimCoopHooks] Native process connected\n");

        // Receive loop: read command packets from native process
        PacketHeader header;
        uint8_t payload[512];

        while (g_tcpServer.Receive(&header, payload, sizeof(payload)))
        {
            if (g_shuttingDown)
                break;

            if (header.opcode >= 0x8000 && header.opcode < 0xF000)
            {
                // Command from native process -- enqueue for game thread execution
                // Reconstruct CommandPacket from header + payload
                CommandSlot slot{};
                slot.opcode = header.opcode;

                if (header.length >= sizeof(uint8_t) + 7 + sizeof(uint64_t) * 1)
                {
                    // Payload layout: argCount(1) + pad(7) + args(8*8)
                    slot.argCount = payload[0];
                    if (slot.argCount > 8)
                        slot.argCount = 8;
                    memcpy(slot.args, payload + 8, slot.argCount * sizeof(uint64_t));
                }

                g_commandQueue.Enqueue(slot);
            }
            else if (header.opcode == CTRL_HEARTBEAT)
            {
                // Heartbeat -- respond with heartbeat
                PacketHeader heartbeat;
                heartbeat.opcode = CTRL_HEARTBEAT;
                heartbeat.length = 0;
                g_tcpServer.Send(&heartbeat, sizeof(heartbeat));
            }
            else if (header.opcode == CTRL_SHUTDOWN)
            {
                RelayLog("[SkyrimCoopHooks] Native process requested shutdown\n");
                break;
            }
            else if (header.opcode == CTRL_READY)
            {
                RelayLog("[SkyrimCoopHooks] Native process signaled ready\n");
            }
        }

        // Client disconnected
        g_tcpServer.DisconnectClient();
        RelayLog("[SkyrimCoopHooks] Native process disconnected\n");

        // Auto-restart per D-08 unless shutting down
        if (!g_shuttingDown)
        {
            RelayLog("[SkyrimCoopHooks] Auto-restarting native process...\n");
            g_launcher.Kill();
            Sleep(2000);

            if (!g_launcher.Launch(g_nativeBinaryPath, g_tcpServer.GetPort()))
            {
                RelayLog("[SkyrimCoopHooks] Failed to restart native process\n");
            }
        }
    }

    return 0;
}

// ---------------------------------------------------------------------------
// SKSE plugin exports
// ---------------------------------------------------------------------------

extern "C" __attribute__((dllexport))
const SKSEPluginVersionData SKSEPlugin_Version = {
    SKSEPluginVersionData::kVersion,
    1,
    "SkyrimCoopHooks",
    "SkyrimCoop",
    "",
    SKSEPluginVersionData::kVersionIndependentEx_NoStructUse,
    SKSEPluginVersionData::kVersionIndependent_AddressLibraryPostAE
        | SKSEPluginVersionData::kVersionIndependent_StructsPost629,
    { MAKE_EXE_VERSION(1, 6, 1170), 0 },
    0,
};

extern "C" __attribute__((dllexport))
bool SKSEPlugin_Load(const SKSEInterface* apSkse)
{
    (void)apSkse; // Per D-11: we do NOT use SKSE interfaces

    RelayLog("[SkyrimCoopHooks] SKSEPlugin_Load starting...\n");

    // 1. Derive native binary path from DLL location
    if (!DeriveNativeBinaryPath())
    {
        RelayLog("[SkyrimCoopHooks] Failed to derive native binary path\n");
        return false;
    }
    RelayLog("[SkyrimCoopHooks] Native binary path: %s\n", g_nativeBinaryPath);

    // 2. Start TCP server on localhost (ephemeral port)
    if (!g_tcpServer.Start())
    {
        RelayLog("[SkyrimCoopHooks] Failed to start TCP server\n");
        return false;
    }
    RelayLog("[SkyrimCoopHooks] TCP server listening on port %u\n", g_tcpServer.GetPort());

    // 3. Launch native process
    if (!g_launcher.Launch(g_nativeBinaryPath, g_tcpServer.GetPort()))
    {
        RelayLog("[SkyrimCoopHooks] Failed to launch native process\n");
        g_tcpServer.Stop();
        return false;
    }
    RelayLog("[SkyrimCoopHooks] Native process launched\n");

    // 4. Start background monitor thread for command receive + auto-restart
    HANDLE hThread = CreateThread(nullptr, 0, MonitorThread, nullptr, 0, nullptr);
    if (hThread)
        CloseHandle(hThread); // Thread runs independently

    RelayLog("[SkyrimCoopHooks] Relay DLL initialized successfully\n");
    return true;
}

// ---------------------------------------------------------------------------
// DllMain
// ---------------------------------------------------------------------------

BOOL WINAPI DllMain(HINSTANCE aInstance, DWORD aReason, LPVOID apReserved)
{
    (void)apReserved;
    if (aReason == DLL_PROCESS_ATTACH)
    {
        s_dllInstance = aInstance;
        DisableThreadLibraryCalls(aInstance);
    }
    else if (aReason == DLL_PROCESS_DETACH)
    {
        g_shuttingDown = true;
        g_tcpServer.Stop();
        g_launcher.Kill();
    }
    return TRUE;
}
