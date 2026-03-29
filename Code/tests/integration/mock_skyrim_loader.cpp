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

static uint64_t FloatToU64(float aVal) {
    uint64_t v = 0;
    memcpy(&v, &aVal, sizeof(float));
    return v;
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

    // Send scripted HookEventPackets using new arg layout (Phase 9.1)
    // Convention: args[0] = pointer, args[1] = resolved formId, then payload
    constexpr uint64_t cPlayerFormId = 0x14;         // well-known Skyrim player formId
    constexpr uint64_t cIronSwordFormId = 0x00013B90; // well-known Iron Sword formId

    for (int i = 0; i < eventCount; i++) {
        uint64_t fakePtr = 0xDEAD0001u + static_cast<uint64_t>(i);

        // HOOK_ACTOR_ADDED: [actorPtr, formId]
        {
            uint64_t args[2] = {fakePtr, cPlayerFormId};
            if (!SendHookEvent(sock, HOOK_ACTOR_ADDED, 2, args)) {
                fprintf(stderr, "Failed to send HOOK_ACTOR_ADDED (event %d)\n", i);
                break;
            }
        }

        // HOOK_SET_POSITION: [refPtr, formId, posX_u64, posY_u64, posZ_u64]
        {
            uint64_t args[5] = {
                fakePtr,
                cPlayerFormId,
                FloatToU64(100.0f + static_cast<float>(i) * 10.0f),
                FloatToU64(200.0f + static_cast<float>(i) * 5.0f),
                FloatToU64(300.0f + static_cast<float>(i) * 2.5f),
            };
            if (!SendHookEvent(sock, HOOK_SET_POSITION, 5, args)) {
                fprintf(stderr, "Failed to send HOOK_SET_POSITION (event %d)\n", i);
                break;
            }
        }

        // HOOK_DAMAGE_ACTOR: [actorPtr, formId, damageU64]
        if (i == 0) {
            uint64_t args[3] = {fakePtr, cPlayerFormId, FloatToU64(25.0f)};
            if (!SendHookEvent(sock, HOOK_DAMAGE_ACTOR, 3, args)) {
                fprintf(stderr, "Failed to send HOOK_DAMAGE_ACTOR\n");
            }
        }

        // HOOK_ADD_INVENTORY: [actorPtr, actorFormId, itemPtr, itemFormId, countU64, refPtr]
        if (i == 0) {
            uint64_t args[6] = {fakePtr, cPlayerFormId, 0xBEEF0001u, cIronSwordFormId, 1, 0};
            if (!SendHookEvent(sock, HOOK_ADD_INVENTORY, 6, args)) {
                fprintf(stderr, "Failed to send HOOK_ADD_INVENTORY\n");
            }
        }

        // HOOK_CHARACTER_CTOR: [thisPtr, formId]
        if (i == 0) {
            uint64_t args[2] = {fakePtr, cPlayerFormId};
            if (!SendHookEvent(sock, HOOK_CHARACTER_CTOR, 2, args)) {
                fprintf(stderr, "Failed to send HOOK_CHARACTER_CTOR\n");
            }
        }

        // HOOK_SPAWN_ACTOR: [thisPtr, formId, posX_u64, posY_u64, posZ_u64]
        if (i == 0) {
            uint64_t args[5] = {
                fakePtr, cPlayerFormId,
                FloatToU64(100.0f), FloatToU64(200.0f), FloatToU64(300.0f),
            };
            if (!SendHookEvent(sock, HOOK_SPAWN_ACTOR, 5, args)) {
                fprintf(stderr, "Failed to send HOOK_SPAWN_ACTOR\n");
            }
        }

        if (delayMs > 0) Sleep(static_cast<DWORD>(delayMs));
    }

    printf("Sent %d event sets (ACTOR_ADDED + SET_POSITION + extras on first)\n", eventCount);

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
