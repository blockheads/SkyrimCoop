// mock_skyrim_loader.cpp -- Loads relay DLL without Skyrim for headless testing
// Exploits the fact that SKSEPlugin_Load ignores its argument and hooks fail gracefully
// Usage: mock_skyrim_loader --dll <path.dll> [--events N] [--delay-ms N]
#include <winsock2.h>
#include <windows.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include "protocol.h"

typedef bool (*SKSEPlugin_Load_t)(void*);

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static uint64_t FloatBits(float aVal) {
    uint64_t bits = 0;
    memcpy(&bits, &aVal, sizeof(float));
    return bits;
}

static bool SendPacket(SOCKET aSock, const void* apData, int aSize) {
    return send(aSock, static_cast<const char*>(apData), aSize, 0) == aSize;
}

static bool SendHookEvent(SOCKET aSock, uint16_t aOpcode, uint8_t aArgCount, const uint64_t* apArgs) {
    HookEventPacket pkt{};
    pkt.header.opcode = aOpcode;
    pkt.header.length = sizeof(pkt) - sizeof(PacketHeader);
    pkt.argCount = aArgCount;
    for (int i = 0; i < aArgCount && i < 8; i++)
        pkt.args[i] = apArgs[i];
    return SendPacket(aSock, &pkt, sizeof(pkt));
}

static bool SendControl(SOCKET aSock, uint16_t aOpcode) {
    ControlPacket pkt{};
    pkt.header.opcode = aOpcode;
    pkt.header.length = 0;
    return SendPacket(aSock, &pkt, sizeof(pkt));
}

// ---------------------------------------------------------------------------
// relay_info.json parsing (minimal, no JSON library)
// ---------------------------------------------------------------------------

static bool ReadRelayPort(const char* apJsonPath, uint16_t& aOutPort) {
    // Wait up to 5 seconds for the file to appear
    for (int attempt = 0; attempt < 50; attempt++) {
        FILE* f = fopen(apJsonPath, "r");
        if (f) {
            char buf[512]{};
            fread(buf, 1, sizeof(buf) - 1, f);
            fclose(f);
            // Parse "port":NNNNN
            const char* pPort = strstr(buf, "\"port\":");
            if (pPort) {
                aOutPort = static_cast<uint16_t>(atoi(pPort + 7));
                return aOutPort > 0;
            }
        }
        Sleep(100);
    }
    return false;
}

// Derive relay_info.json path from DLL path
static void DeriveRelayInfoPath(const char* apDllPath, char* apOut, size_t aSize) {
    // Find last separator
    const char* pLastSlash = nullptr;
    for (const char* p = apDllPath; *p; p++) {
        if (*p == '\\' || *p == '/') pLastSlash = p;
    }
    if (pLastSlash) {
        size_t dirLen = static_cast<size_t>(pLastSlash - apDllPath + 1);
        if (dirLen >= aSize) dirLen = aSize - 1;
        memcpy(apOut, apDllPath, dirLen);
        apOut[dirLen] = '\0';
        strncat(apOut, "skyrim_coop_relay.json", aSize - dirLen - 1);
    } else {
        snprintf(apOut, aSize, "skyrim_coop_relay.json");
    }
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------

int main(int argc, char* argv[]) {
    const char* dllPath = nullptr;
    int eventCount = 10;
    int delayMs = 100;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--dll") == 0 && i + 1 < argc) dllPath = argv[++i];
        else if (strcmp(argv[i], "--events") == 0 && i + 1 < argc) eventCount = atoi(argv[++i]);
        else if (strcmp(argv[i], "--delay-ms") == 0 && i + 1 < argc) delayMs = atoi(argv[++i]);
    }

    if (!dllPath) {
        fprintf(stderr, "Usage: mock_skyrim_loader --dll <path.dll> [--events N] [--delay-ms N]\n");
        return 1;
    }

    // Initialize winsock
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);

    // Load the relay DLL
    HMODULE hDll = LoadLibraryA(dllPath);
    if (!hDll) { fprintf(stderr, "LoadLibrary failed: %lu\n", GetLastError()); return 1; }

    // Call SKSEPlugin_Load(nullptr) -- hooks fail but TCP server starts
    auto pfnLoad = reinterpret_cast<SKSEPlugin_Load_t>(GetProcAddress(hDll, "SKSEPlugin_Load"));
    if (!pfnLoad) { fprintf(stderr, "GetProcAddress SKSEPlugin_Load failed\n"); return 1; }
    if (!pfnLoad(nullptr)) { fprintf(stderr, "SKSEPlugin_Load returned false\n"); return 1; }
    printf("DLL loaded, TCP server should be running\n");

    // Read relay_info.json to get TCP port
    char relayInfoPath[MAX_PATH]{};
    DeriveRelayInfoPath(dllPath, relayInfoPath, sizeof(relayInfoPath));
    printf("Looking for relay info at: %s\n", relayInfoPath);

    uint16_t tcpPort = 0;
    if (!ReadRelayPort(relayInfoPath, tcpPort)) {
        fprintf(stderr, "Failed to read relay port from %s\n", relayInfoPath);
        FreeLibrary(hDll);
        WSACleanup();
        return 1;
    }
    printf("DLL TCP server on port %u\n", tcpPort);

    // Connect to DLL TCP server
    SOCKET sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock == INVALID_SOCKET) { fprintf(stderr, "socket() failed\n"); return 1; }

    struct sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(tcpPort);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    if (connect(sock, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) != 0) {
        fprintf(stderr, "connect() failed: %d\n", WSAGetLastError());
        closesocket(sock);
        FreeLibrary(hDll);
        WSACleanup();
        return 1;
    }
    printf("Connected to DLL TCP server\n");

    // Send CTRL_READY
    if (!SendControl(sock, CTRL_READY)) {
        fprintf(stderr, "Failed to send CTRL_READY\n");
        closesocket(sock);
        FreeLibrary(hDll);
        WSACleanup();
        return 1;
    }
    printf("Sent CTRL_READY\n");

    // Send scripted HookEventPackets
    for (int i = 0; i < eventCount; i++) {
        // HOOK_ACTOR_ADDED with fake actor pointer
        uint64_t actorArgs[1] = {0xDEAD0001u + static_cast<uint64_t>(i)};
        if (!SendHookEvent(sock, HOOK_ACTOR_ADDED, 1, actorArgs)) {
            fprintf(stderr, "Failed to send HOOK_ACTOR_ADDED (event %d)\n", i);
            break;
        }

        // HOOK_SET_POSITION with incrementing positions
        uint64_t posArgs[4] = {
            0xDEAD0001u + static_cast<uint64_t>(i),
            FloatBits(100.0f + static_cast<float>(i) * 10.0f),
            FloatBits(200.0f + static_cast<float>(i) * 5.0f),
            FloatBits(300.0f + static_cast<float>(i) * 2.5f),
        };
        if (!SendHookEvent(sock, HOOK_SET_POSITION, 4, posArgs)) {
            fprintf(stderr, "Failed to send HOOK_SET_POSITION (event %d)\n", i);
            break;
        }

        if (delayMs > 0) Sleep(static_cast<DWORD>(delayMs));
    }

    printf("Sent %d event pairs (ACTOR_ADDED + SET_POSITION)\n", eventCount);

    // Send CTRL_SHUTDOWN
    SendControl(sock, CTRL_SHUTDOWN);
    Sleep(200);

    // Cleanup
    closesocket(sock);
    FreeLibrary(hDll);
    WSACleanup();
    printf("mock_skyrim_loader: PASS\n");
    return 0;
}
