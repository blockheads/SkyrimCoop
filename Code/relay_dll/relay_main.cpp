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
#include <mutex>
#include <vector>

#include "protocol.h"
#include "tcp_server.h"
#include "process_launcher.h"
#include "command_queue.h"
#include "hook_trampolines.h"

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
TcpServer g_tcpServer;                     // non-static: accessed by hook_trampolines.cpp
static ProcessLauncher g_launcher;
static CommandQueue g_commandQueue;
static volatile bool g_shuttingDown = false;
static char g_nativeBinaryPath[MAX_PATH]{};

// ---------------------------------------------------------------------------
// Appearance spawn queue -- variable-length data, drained on game thread
// ---------------------------------------------------------------------------

struct AppearanceSpawnRequest {
    uint32_t changeFlags;
    float x, y, z;
    float rx, ry;
    std::vector<uint8_t> appearanceData;
    std::vector<uint8_t> faceTintData;
};

static std::mutex g_appearanceSpawnMutex;
static std::vector<AppearanceSpawnRequest> g_appearanceSpawnQueue;

// ---------------------------------------------------------------------------
// Logging helpers (file + OutputDebugString -- no spdlog per D-13)
// ---------------------------------------------------------------------------

static FILE* s_logFile = nullptr;

static void OpenLogFile()
{
    if (s_logFile)
        return;

    // Log next to the DLL: Data/SKSE/Plugins/skyrim_coop_hooks.log
    char dllPath[MAX_PATH]{};
    if (GetModuleFileNameA(s_dllInstance, dllPath, MAX_PATH) == 0)
        return;

    // Find last separator, replace filename
    char* pLastSlash = nullptr;
    for (char* p = dllPath; *p; p++)
        if (*p == '\\' || *p == '/') pLastSlash = p;
    if (pLastSlash)
    {
        *(pLastSlash + 1) = '\0';
        char logPath[MAX_PATH]{};
        snprintf(logPath, sizeof(logPath), "%sskyrim_coop_hooks.log", dllPath);
        s_logFile = fopen(logPath, "w");
    }
}

void RelayLog(const char* fmt, ...)  // non-static: accessed by hook_trampolines.cpp
{
    char buf[512];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    OutputDebugStringA(buf);
    if (s_logFile)
    {
        fputs(buf, s_logFile);
        fflush(s_logFile);
    }
}

// ---------------------------------------------------------------------------
// Stub: Execute a game command (placeholder for Plan 05+)
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// Helper: send a spawn result back to native process
// ---------------------------------------------------------------------------

static void SendSpawnResult(uint32_t aFormId, void* apActor, bool aSuccess)
{
    HookEventPacket pkt{};
    pkt.header.opcode = HOOK_SPAWN_RESULT;
    pkt.header.length = sizeof(HookEventPacket) - sizeof(PacketHeader);
    pkt.argCount = 3;
    pkt.args[0] = static_cast<uint64_t>(aFormId);
    pkt.args[1] = reinterpret_cast<uint64_t>(apActor);
    pkt.args[2] = aSuccess ? 1u : 0u;
    g_tcpServer.Send(&pkt, sizeof(pkt));
}

// ---------------------------------------------------------------------------
// Spawn actor by formId (existing world NPC path)
// ---------------------------------------------------------------------------

static void SpawnActorByFormId(uint32_t aFormId, float aX, float aY, float aZ, float aRx, float aRy)
{
    RelayLog("[SkyrimCoopHooks] SPAWN_FORMID: formId=0x%08X pos=(%.1f, %.1f, %.1f)\n",
             aFormId, aX, aY, aZ);

    // Resolve game function pointers
    auto pGetById = reinterpret_cast<void*(*)(uint32_t)>(GetFormById());
    auto pHeapAlloc = reinterpret_cast<void*(__fastcall*)(void*, size_t, size_t, bool)>(GetGameHeapAllocate());
    auto pCharCtor = reinterpret_cast<void*(__fastcall*)(void*)>(GetCharacterCtor());
    auto pSetLvlMod = reinterpret_cast<void(__fastcall*)(void*, uint32_t)>(GetSetLevelMod());
    void* pHeapInst = GetGameHeapInstance() ? *reinterpret_cast<void**>(GetGameHeapInstance()) : nullptr;
    void* pModMgr = GetModManagerInstance() ? *reinterpret_cast<void**>(GetModManagerInstance()) : nullptr;

    if (!pGetById || !pHeapAlloc || !pCharCtor || !pHeapInst || !pModMgr)
    {
        RelayLog("[SkyrimCoopHooks] SPAWN_FORMID: missing game function pointers\n");
        SendSpawnResult(aFormId, nullptr, false);
        return;
    }

    // Look up the base form
    void* pBaseForm = pGetById(aFormId);
    if (!pBaseForm)
    {
        RelayLog("[SkyrimCoopHooks] SPAWN_FORMID: form 0x%08X not found\n", aFormId);
        SendSpawnResult(aFormId, nullptr, false);
        return;
    }

    // Allocate and construct Character (Actor subclass)
    // Character is approximately 0x2B0 bytes on SSE x64
    void* pActor = pHeapAlloc(pHeapInst, 0x2B0, 0, false);
    if (!pActor)
    {
        RelayLog("[SkyrimCoopHooks] SPAWN_FORMID: heap alloc failed\n");
        SendSpawnResult(aFormId, nullptr, false);
        return;
    }
    memset(pActor, 0, 0x2B0);
    pCharCtor(pActor);

    // SetSkipSaveFlag: unk10 = 0xFFFF (offset 0x18 from TESForm base)
    *reinterpret_cast<uint16_t*>(reinterpret_cast<uintptr_t>(pActor) + 0x18) = 0xFFFF;

    // AI suppression for remote actors (per D-06)
    // Actor flags1 bit 0x02000000 disables AI processing
    uint32_t& flags1 = *reinterpret_cast<uint32_t*>(reinterpret_cast<uintptr_t>(pActor) + 0x0C);
    flags1 |= 0x02000000;

    // SetLevelMod(4) via ExtraDataList (offset 0x70 from Actor base)
    if (pSetLvlMod)
    {
        void* pExtraData = reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(pActor) + 0x70);
        pSetLvlMod(pExtraData, 4);
    }

    // Set baseForm (offset 0x40)
    *reinterpret_cast<void**>(reinterpret_cast<uintptr_t>(pActor) + 0x40) = pBaseForm;

    // Get player ref for cell/worldspace context
    void* pPlayer = pGetById(0x14);
    void* pCell = nullptr;
    void* pWorldSpace = nullptr;
    if (pPlayer)
    {
        pCell = *reinterpret_cast<void**>(reinterpret_cast<uintptr_t>(pPlayer) + 0x60);
        if (pCell)
            pWorldSpace = *reinterpret_cast<void**>(reinterpret_cast<uintptr_t>(pCell) + 0x128);
    }

    // Interior/exterior cell check
    void* pCellForSpawn = pCell;
    if (pCell)
    {
        uint8_t cellFlags = *reinterpret_cast<uint8_t*>(reinterpret_cast<uintptr_t>(pCell) + 0x40);
        if (!(cellFlags & 1))
            pCellForSpawn = nullptr; // exterior: pass null cell
    }

    // SpawnNewREFR (addr 13723)
    auto pSpawnREFR = reinterpret_cast<void(__fastcall*)(void*, uint32_t&, void*, float*, float*, void*, void*, void*, int, int, int, int)>(GetSpawnNewREFR());
    if (pSpawnREFR)
    {
        float pos[3] = {aX, aY, aZ};
        float rot[3] = {aRx, aRy, 0.0f};
        uint32_t refrHandle = 0;
        pSpawnREFR(pModMgr, refrHandle, pBaseForm, pos, rot, pCellForSpawn, pWorldSpace, pActor, 0, 0, 0, 1);
    }

    // Clear flags bit 21 (offset 0x10 from TESForm)
    uint32_t& actorFlags = *reinterpret_cast<uint32_t*>(reinterpret_cast<uintptr_t>(pActor) + 0x10);
    actorFlags &= ~(1u << 21);

    RelayLog("[SkyrimCoopHooks] SPAWN_FORMID: spawned actor ptr=%p formId=0x%08X\n", pActor, aFormId);
    SendSpawnResult(aFormId, pActor, true);
}

// ---------------------------------------------------------------------------
// Spawn actor from AppearanceBuffer (TESNPC::Create pipeline)
// Port of STR's Forms.cpp:116-127 + Actor.cpp:175-208
// ---------------------------------------------------------------------------

static void SpawnActorFromAppearance(const AppearanceSpawnRequest& aReq)
{
    RelayLog("[SkyrimCoopHooks] APPEARANCE_SPAWN: appearance %zu bytes, changeFlags=%u, pos=(%.1f, %.1f, %.1f)\n",
             aReq.appearanceData.size(), aReq.changeFlags, aReq.x, aReq.y, aReq.z);

    // Resolve game function pointers
    auto pGetById = reinterpret_cast<void*(*)(uint32_t)>(GetFormById());
    auto pHeapAlloc = reinterpret_cast<void*(__fastcall*)(void*, size_t, size_t, bool)>(GetGameHeapAllocate());
    auto pCharCtor = reinterpret_cast<void*(__fastcall*)(void*)>(GetCharacterCtor());
    auto pSetLvlMod = reinterpret_cast<void(__fastcall*)(void*, uint32_t)>(GetSetLevelMod());
    void* pHeapInst = GetGameHeapInstance() ? *reinterpret_cast<void**>(GetGameHeapInstance()) : nullptr;
    void* pModMgr = GetModManagerInstance() ? *reinterpret_cast<void**>(GetModManagerInstance()) : nullptr;
    void* pFactoryTable = GetIFormFactoryTable();

    if (!pGetById || !pHeapAlloc || !pCharCtor || !pHeapInst || !pModMgr || !pFactoryTable)
    {
        RelayLog("[SkyrimCoopHooks] APPEARANCE_SPAWN: missing game function pointers\n");
        SendSpawnResult(0, nullptr, false);
        return;
    }

    // === TESNPC::Create pipeline (port of STR Forms.cpp:116-127) ===

    // 1. IFormFactory::Create<TESNPC>() - type 43 (FormType::NPC)
    //    addr 400508 is a POINTER_SKYRIMSE: dereference to get IFormFactory** array
    void** pFactories = *reinterpret_cast<void***>(pFactoryTable);
    if (!pFactories)
    {
        RelayLog("[SkyrimCoopHooks] APPEARANCE_SPAWN: factory table dereference is null\n");
        SendSpawnResult(0, nullptr, false);
        return;
    }

    void* pNpcFactory = pFactories[43]; // FormType::NPC = 43
    if (!pNpcFactory)
    {
        RelayLog("[SkyrimCoopHooks] APPEARANCE_SPAWN: NPC factory (index 43) is null\n");
        SendSpawnResult(0, nullptr, false);
        return;
    }

    // Call IFormFactory::Create() at vtable index 1
    // Verified from Code/client/Games/IFormFactory.h:
    //   vtable[0] = ~IFormFactory() (destructor)
    //   vtable[1] = Create()
    auto pFactoryVtable = *reinterpret_cast<uintptr_t*>(pNpcFactory);
    auto pCreateFunc = reinterpret_cast<void*(__fastcall*)(void*)>(
        *reinterpret_cast<uintptr_t*>(pFactoryVtable + 1 * 8));
    void* pNpc = pCreateFunc(pNpcFactory);
    if (!pNpc)
    {
        RelayLog("[SkyrimCoopHooks] APPEARANCE_SPAWN: IFormFactory::Create returned null\n");
        SendSpawnResult(0, nullptr, false);
        return;
    }

    // 2. Initialize() -- copy key defaults from player base form
    //    Get player ref (formId 0x14) -> baseForm (offset 0x40) -> TESNPC*
    void* pPlayer = pGetById(0x14);
    if (pPlayer)
    {
        void* pPlayerBase = *reinterpret_cast<void**>(reinterpret_cast<uintptr_t>(pPlayer) + 0x40);
        if (pPlayerBase)
        {
            // Copy npcClass (0x1C0) -- the most critical field for initialization
            // Verified: SKYRIM_STRUCT_ASSERT(offsetof(TESNPC, npcClass) == 0x1C0)
            *reinterpret_cast<void**>(reinterpret_cast<uintptr_t>(pNpc) + 0x1C0) =
                *reinterpret_cast<void**>(reinterpret_cast<uintptr_t>(pPlayerBase) + 0x1C0);

            // Note: raceForm, combatStyle, outfits will be set by Deserialize below
            // if the change flags include those fields. The npcClass copy provides
            // a safe default in case they aren't in the appearance buffer.
        }
    }

    // 3. Deserialize appearance buffer via BGSLoadFormBuffer
    //    For the MVP, log the appearance data availability.
    //    Full Deserialize requires BGSLoadFormBuffer struct layout from SaveLoad.h.
    //    The actor will spawn with player defaults; appearance reconstruction is iterative.
    if (!aReq.appearanceData.empty())
    {
        RelayLog("[SkyrimCoopHooks] APPEARANCE_SPAWN: appearance data available (%zu bytes), "
                 "Deserialize deferred to future iteration\n", aReq.appearanceData.size());
        // TODO(Phase 16+): Construct BGSLoadFormBuffer and call pNpc->Load(&loadBuffer)
        // This requires reading Code/client/Games/SaveLoad.h for exact struct layout.
        // The actor will spawn with default appearance for now.
    }

    // 4. Clear overlayRace to force facegen (per STR Forms.cpp:124)
    //    overlayRace is approximately at offset 0x1E8 from TESNPC base
    //    (after npcClass + headData + unk114 + combatStyle + unk11C)
    *reinterpret_cast<void**>(reinterpret_cast<uintptr_t>(pNpc) + 0x1E8) = nullptr;

    // === Actor::Create pipeline (port of STR Actor.cpp:175-208) ===

    // 5. Heap alloc + Character constructor
    void* pActor = pHeapAlloc(pHeapInst, 0x2B0, 0, false);
    if (!pActor)
    {
        RelayLog("[SkyrimCoopHooks] APPEARANCE_SPAWN: heap alloc failed\n");
        SendSpawnResult(0, nullptr, false);
        return;
    }
    memset(pActor, 0, 0x2B0);
    pCharCtor(pActor);

    // 6. SetSkipSaveFlag: unk10 = 0xFFFF (offset 0x18 from TESForm base)
    *reinterpret_cast<uint16_t*>(reinterpret_cast<uintptr_t>(pActor) + 0x18) = 0xFFFF;

    // 7. AI suppression: set flag that prevents AI evaluation (per D-06)
    uint32_t& flags1 = *reinterpret_cast<uint32_t*>(reinterpret_cast<uintptr_t>(pActor) + 0x0C);
    flags1 |= 0x02000000;

    // 8. SetLevelMod(4) via ExtraDataList (offset 0x70 from Actor base)
    if (pSetLvlMod)
    {
        void* pExtraData = reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(pActor) + 0x70);
        pSetLvlMod(pExtraData, 4);
    }

    // 9. Set baseForm to our created TESNPC (offset 0x40)
    *reinterpret_cast<void**>(reinterpret_cast<uintptr_t>(pActor) + 0x40) = pNpc;

    // 10. Get cell/worldspace from player for spawn context
    void* pCell = nullptr;
    void* pWorldSpace = nullptr;
    if (pPlayer)
    {
        pCell = *reinterpret_cast<void**>(reinterpret_cast<uintptr_t>(pPlayer) + 0x60);
        if (pCell)
            pWorldSpace = *reinterpret_cast<void**>(reinterpret_cast<uintptr_t>(pCell) + 0x128);
    }

    // 11. Interior/exterior cell check
    void* pCellForSpawn = pCell;
    if (pCell)
    {
        uint8_t cellFlags = *reinterpret_cast<uint8_t*>(reinterpret_cast<uintptr_t>(pCell) + 0x40);
        if (!(cellFlags & 1))
            pCellForSpawn = nullptr; // exterior: pass null cell
    }

    // 12. SpawnNewREFR (addr 13723)
    auto pSpawnREFR = reinterpret_cast<void(__fastcall*)(void*, uint32_t&, void*, float*, float*, void*, void*, void*, int, int, int, int)>(GetSpawnNewREFR());
    if (pSpawnREFR)
    {
        float pos[3] = {aReq.x, aReq.y, aReq.z};
        float rot[3] = {aReq.rx, aReq.ry, 0.0f};
        uint32_t refrHandle = 0;
        pSpawnREFR(pModMgr, refrHandle, pNpc, pos, rot, pCellForSpawn, pWorldSpace, pActor, 0, 0, 0, 1);
    }

    // 13. Clear flags bit 21 (offset 0x10 from TESForm)
    uint32_t& actorFlags = *reinterpret_cast<uint32_t*>(reinterpret_cast<uintptr_t>(pActor) + 0x10);
    actorFlags &= ~(1u << 21);

    RelayLog("[SkyrimCoopHooks] APPEARANCE_SPAWN: spawned custom NPC ptr=%p\n", pActor);
    SendSpawnResult(0, pActor, true);
}

// ---------------------------------------------------------------------------
// DrainCommandQueue: Execute queued commands on game thread
// Called from game thread update hook.
// ---------------------------------------------------------------------------

[[maybe_unused]]
static void DrainCommandQueue()
{
    // 1. Process appearance spawns (variable-length, game thread only)
    {
        std::vector<AppearanceSpawnRequest> pendingAppearance;
        {
            std::lock_guard<std::mutex> lock(g_appearanceSpawnMutex);
            pendingAppearance.swap(g_appearanceSpawnQueue);
        }

        for (const auto& req : pendingAppearance)
        {
            SpawnActorFromAppearance(req);
        }
    }

    // 2. Process fixed-size commands from SPSC queue
    g_commandQueue.Drain([](const CommandSlot& cmd) {
        switch (cmd.opcode)
        {
        case CMD_SPAWN_ACTOR:
        {
            // FormId-based spawn: args[0]=formId, args[1]=x|y packed, args[2]=z, args[3]=rx|ry packed
            uint32_t formId = static_cast<uint32_t>(cmd.args[0]);
            uint32_t xb = static_cast<uint32_t>(cmd.args[1] & 0xFFFFFFFF);
            uint32_t yb = static_cast<uint32_t>(cmd.args[1] >> 32);
            uint32_t zb = static_cast<uint32_t>(cmd.args[2] & 0xFFFFFFFF);
            uint32_t rxb = static_cast<uint32_t>(cmd.args[3] & 0xFFFFFFFF);
            uint32_t ryb = static_cast<uint32_t>(cmd.args[3] >> 32);
            float x, y, z, rx, ry;
            memcpy(&x, &xb, 4); memcpy(&y, &yb, 4); memcpy(&z, &zb, 4);
            memcpy(&rx, &rxb, 4); memcpy(&ry, &ryb, 4);
            SpawnActorByFormId(formId, x, y, z, rx, ry);
            break;
        }
        default:
            RelayLog("[SkyrimCoopHooks] Executing command opcode=0x%04X argCount=%u\n",
                     cmd.opcode, cmd.argCount);
            break;
        }
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
        uint8_t payload[16384]; // 16KB: accommodates AppearanceBuffer payloads (~2-4KB typical)

        while (g_tcpServer.Receive(&header, payload, sizeof(payload)))
        {
            if (g_shuttingDown)
                break;

            if (header.opcode == CMD_SPAWN_ACTOR_APPEARANCE)
            {
                // Variable-length appearance spawn -- parse SpawnAppearanceHeader from payload
                if (header.length < sizeof(SpawnAppearanceHeader))
                {
                    RelayLog("[SkyrimCoopHooks] CMD_SPAWN_ACTOR_APPEARANCE: payload too small (%u < %zu)\n",
                             header.length, sizeof(SpawnAppearanceHeader));
                    continue;
                }

                SpawnAppearanceHeader spawnHdr;
                memcpy(&spawnHdr, payload, sizeof(SpawnAppearanceHeader));

                uint32_t expectedSize = static_cast<uint32_t>(sizeof(SpawnAppearanceHeader))
                                        + spawnHdr.appearanceSize + spawnHdr.faceTintSize;
                if (header.length < expectedSize)
                {
                    // Need to read remainder -- for now, handle up to payload buffer size
                    RelayLog("[SkyrimCoopHooks] CMD_SPAWN_ACTOR_APPEARANCE: payload length mismatch "
                             "(%u < %u)\n", header.length, expectedSize);
                    continue;
                }

                AppearanceSpawnRequest req;
                req.changeFlags = spawnHdr.changeFlags;
                req.x = spawnHdr.x;
                req.y = spawnHdr.y;
                req.z = spawnHdr.z;
                req.rx = spawnHdr.rx;
                req.ry = spawnHdr.ry;

                const uint8_t* pData = payload + sizeof(SpawnAppearanceHeader);
                if (spawnHdr.appearanceSize > 0)
                {
                    req.appearanceData.assign(pData, pData + spawnHdr.appearanceSize);
                    pData += spawnHdr.appearanceSize;
                }
                if (spawnHdr.faceTintSize > 0)
                {
                    req.faceTintData.assign(pData, pData + spawnHdr.faceTintSize);
                }

                // Enqueue for game thread processing
                {
                    std::lock_guard<std::mutex> lock(g_appearanceSpawnMutex);
                    g_appearanceSpawnQueue.push_back(std::move(req));
                }

                RelayLog("[SkyrimCoopHooks] CMD_SPAWN_ACTOR_APPEARANCE: queued (appearance=%u, faceTint=%u)\n",
                         spawnHdr.appearanceSize, spawnHdr.faceTintSize);
            }
            else if (header.opcode >= 0x8000 && header.opcode < 0xF000)
            {
                // Fixed-size command from native process -- enqueue for game thread execution
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

    OpenLogFile();
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

    // 3. Install hook trampolines (after TCP server so hooks can send events)
    if (!InstallAllHooks())
    {
        RelayLog("[SkyrimCoopHooks] WARNING: Hook installation failed -- continuing without hooks\n");
        // Non-fatal: the DLL can still relay commands even without hook events
    }

    // 4. Write relay info file for external launcher to pick up.
    //    Wine's CreateProcess can't launch native ELF binaries, so the native
    //    process is launched by deploy_and_test.sh watching for this file.
    {
        // Get the real Linux PID (resolves to actual PID under Wine)
        int linuxPid = static_cast<int>(GetCurrentProcessId());

        char relayInfoPath[MAX_PATH]{};
        // Derive from g_nativeBinaryPath directory
        // g_nativeBinaryPath is a Linux path like /path/to/plugins/skyrim-coop
        const char* lastSlash = nullptr;
        for (const char* p = g_nativeBinaryPath; *p; p++)
            if (*p == '/') lastSlash = p;
        if (lastSlash)
        {
            size_t dirLen = static_cast<size_t>(lastSlash - g_nativeBinaryPath + 1);
            memcpy(relayInfoPath, g_nativeBinaryPath, dirLen);
            relayInfoPath[dirLen] = '\0';
            strncat(relayInfoPath, "skyrim_coop_relay.json", sizeof(relayInfoPath) - dirLen - 1);
        }

        FILE* f = fopen(relayInfoPath, "w");
        if (f)
        {
            fprintf(f, "{\"port\":%u,\"pid\":%d,\"binary\":\"%s\"}\n",
                    static_cast<unsigned>(g_tcpServer.GetPort()), linuxPid, g_nativeBinaryPath);
            fclose(f);
            RelayLog("[SkyrimCoopHooks] Relay info written to %s (port=%u, pid=%d)\n",
                     relayInfoPath, static_cast<unsigned>(g_tcpServer.GetPort()), linuxPid);
        }
        else
        {
            RelayLog("[SkyrimCoopHooks] WARNING: Could not write relay info file: %s\n", relayInfoPath);
        }
    }

    // 6. Start background monitor thread for command receive + auto-restart
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
        RemoveAllHooks();
        g_tcpServer.Stop();
        g_launcher.Kill();
        if (s_logFile) { fclose(s_logFile); s_logFile = nullptr; }
    }
    return TRUE;
}
