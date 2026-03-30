// Code/relay_dll/relay_main.cpp -- SKSE plugin entry point for relay DLL
// Per D-10: Exports SKSEPlugin_Version and SKSEPlugin_Load
// Per D-11: Does NOT use SKSE interfaces (Messaging, Papyrus, Serialization, Task)
// Per D-13: No TiltedCore, EnTT, or spdlog dependencies
// Per D-07: Startup: TCP server -> spawn native -> monitor thread with auto-restart
#include <winsock2.h>  // must be before windows.h
#include <windows.h>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "nlohmann/json.hpp"
#include "json_protocol.h"
#include "dirty_cache.h"
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
extern DirtyCache g_dirtyCache;            // defined in hook_trampolines.cpp
static ProcessLauncher g_launcher;
static CommandQueue g_commandQueue;
static volatile bool g_shuttingDown = false;
static char g_nativeBinaryPath[MAX_PATH]{};

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
// DrainCommandQueue: Execute queued commands on the game thread
// Called from HookActorProcess when the player character (formId 0x14) fires
// ---------------------------------------------------------------------------

void DrainCommandQueue()
{
    using TSetPosition = void (__fastcall*)(void*, void*);
    auto pRealSetPosition = reinterpret_cast<TSetPosition>(GetRealSetPosition());

    g_commandQueue.Drain([pRealSetPosition](const CommandSlot& cmd) {
        switch (cmd.opcode) {
        case CMD_SET_POSITION: {
            void* pActor = reinterpret_cast<void*>(cmd.args[0]);
            if (!pActor || !pRealSetPosition) break;

            uint32_t posXBits = static_cast<uint32_t>(cmd.args[1]);
            uint32_t posYBits = static_cast<uint32_t>(cmd.args[1] >> 32);
            uint32_t posZBits = static_cast<uint32_t>(cmd.args[2]);

            float pos[3];
            memcpy(&pos[0], &posXBits, sizeof(float));
            memcpy(&pos[1], &posYBits, sizeof(float));
            memcpy(&pos[2], &posZBits, sizeof(float));

            pRealSetPosition(pActor, pos);
            break;
        }

        case CMD_SPAWN_ACTOR: {
            // Phase 15: Spawn an actor in Skyrim following STR's Actor::Create sequence.
            // Per D-01: DLL is stateless -- spawn, report pointer, forget.
            uint32_t formId = static_cast<uint32_t>(cmd.args[0]);

            // Unpack position from args[1..2]
            uint32_t xBits = static_cast<uint32_t>(cmd.args[1]);
            uint32_t yBits = static_cast<uint32_t>(cmd.args[1] >> 32);
            uint32_t zBits = static_cast<uint32_t>(cmd.args[2]);
            float pos[3];
            memcpy(&pos[0], &xBits, sizeof(float));
            memcpy(&pos[1], &yBits, sizeof(float));
            memcpy(&pos[2], &zBits, sizeof(float));

            // Unpack rotation from args[3]
            uint32_t rxBits = static_cast<uint32_t>(cmd.args[3]);
            uint32_t ryBits = static_cast<uint32_t>(cmd.args[3] >> 32);
            float rot[3] = {0};
            memcpy(&rot[0], &rxBits, sizeof(float));
            memcpy(&rot[1], &ryBits, sizeof(float));

            // Type aliases for game function signatures (from STR source analysis)
            using TGetFormById   = void*(__cdecl*)(uint32_t);
            using THeapAllocate  = void*(__fastcall*)(void*, size_t, size_t, bool);
            using TCharCtor      = void*(__fastcall*)(void*);
            using TSetLevelMod   = void(__fastcall*)(void*, uint32_t);
            // SpawnNewREFR: __fastcall thiscall with 12 params (per ModManager.cpp)
            using TSpawnNewREFR  = uint32_t&(__fastcall*)(void*, uint32_t&, void*, float*, float*,
                                                           void*, void*, void*, uintptr_t, uintptr_t, char, char);

            auto pGetById    = reinterpret_cast<TGetFormById>(GetFormById());
            auto pHeapAlloc  = reinterpret_cast<THeapAllocate>(GetGameHeapAllocate());
            auto pCharCtor   = reinterpret_cast<TCharCtor>(GetCharacterCtor());
            auto pSpawnREFR  = reinterpret_cast<TSpawnNewREFR>(GetSpawnNewREFR());
            auto pSetLvlMod  = reinterpret_cast<TSetLevelMod>(GetSetLevelMod());
            void* pHeapInst  = GetGameHeapInstance() ? *reinterpret_cast<void**>(GetGameHeapInstance()) : nullptr;
            void* pModMgr    = GetModManagerInstance() ? *reinterpret_cast<void**>(GetModManagerInstance()) : nullptr;

            // Validate all required function pointers
            if (!pGetById || !pHeapAlloc || !pCharCtor || !pSpawnREFR || !pHeapInst || !pModMgr)
            {
                RelayLog("[SkyrimCoopHooks] CMD_SPAWN_ACTOR: missing game function pointers\n");
                nlohmann::json result = {
                    {JsonField::kOp, JsonOp::kSpawnResult},
                    {JsonField::kFormId, formId},
                    {JsonField::kPtr, uint64_t(0)},
                    {JsonField::kSuccess, false}
                };
                g_tcpServer.SendJson(result);
                break;
            }

            // 1. Look up base NPC form
            void* pNpc = pGetById(formId);
            if (!pNpc)
            {
                RelayLog("[SkyrimCoopHooks] CMD_SPAWN_ACTOR: GetById(%u) returned null\n", formId);
                nlohmann::json result = {
                    {JsonField::kOp, JsonOp::kSpawnResult},
                    {JsonField::kFormId, formId},
                    {JsonField::kPtr, uint64_t(0)},
                    {JsonField::kSuccess, false}
                };
                g_tcpServer.SendJson(result);
                break;
            }

            // 2. Get player for cell/worldspace context
            void* pPlayer = pGetById(0x14);
            if (!pPlayer)
            {
                RelayLog("[SkyrimCoopHooks] CMD_SPAWN_ACTOR: player (0x14) not found\n");
                nlohmann::json result = {
                    {JsonField::kOp, JsonOp::kSpawnResult},
                    {JsonField::kFormId, formId},
                    {JsonField::kPtr, uint64_t(0)},
                    {JsonField::kSuccess, false}
                };
                g_tcpServer.SendJson(result);
                break;
            }

            // Read parentCell from player at offset 0x60 (TESObjectREFR::parentCell)
            void* pCell = *reinterpret_cast<void**>(reinterpret_cast<uintptr_t>(pPlayer) + 0x60);
            // Read worldspace from cell at offset 0x128 (TESObjectCELL::worldspace)
            void* pWorldSpace = nullptr;
            if (pCell)
                pWorldSpace = *reinterpret_cast<void**>(reinterpret_cast<uintptr_t>(pCell) + 0x128);

            // 3. Allocate Actor via game heap + character constructor
            // Character size is approximately 0x2B0 in SE 1.6.x
            void* pActor = pHeapAlloc(pHeapInst, 0x2B0, 0, false);
            if (!pActor)
            {
                RelayLog("[SkyrimCoopHooks] CMD_SPAWN_ACTOR: heap alloc failed\n");
                nlohmann::json result = {
                    {JsonField::kOp, JsonOp::kSpawnResult},
                    {JsonField::kFormId, formId},
                    {JsonField::kPtr, uint64_t(0)},
                    {JsonField::kSuccess, false}
                };
                g_tcpServer.SendJson(result);
                break;
            }
            memset(pActor, 0, 0x2B0);
            pCharCtor(pActor);

            // 4. SetSkipSaveFlag: unk10 = 0xFFFF (offset 0x18 in TESForm)
            *reinterpret_cast<uint16_t*>(reinterpret_cast<uintptr_t>(pActor) + 0x18) = 0xFFFF;

            // 5. SetLevelMod(pActor->extraData, 4)
            // TESObjectREFR::extraData is at offset 0x70.
            // ExtraDataList is the struct, SetLevelMod takes &extraData as "this"
            if (pSetLvlMod)
            {
                void* pExtraData = reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(pActor) + 0x70);
                pSetLvlMod(pExtraData, 4);
            }

            // 6. SetBaseForm: baseForm pointer at offset 0x40 (TESObjectREFR::baseForm)
            *reinterpret_cast<void**>(reinterpret_cast<uintptr_t>(pActor) + 0x40) = pNpc;

            // 7. Determine cell for SpawnNewREFR: interior cells pass through, exterior pass nullptr
            void* pCellForSpawn = pCell;
            if (pCell)
            {
                // cellFlags at offset 0x40 in TESObjectCELL (bit 0 = interior)
                uint8_t cellFlags = *reinterpret_cast<uint8_t*>(reinterpret_cast<uintptr_t>(pCell) + 0x40);
                if (!(cellFlags & 1))
                    pCellForSpawn = nullptr; // exterior cell -- pass nullptr per STR pattern
            }

            // 8. SpawnNewREFR(modManager, refrHandle, baseForm, &pos, &rot, cell, worldspace, actor, 0, 0, 0, 1)
            uint32_t refrHandle = 0;
            pSpawnREFR(pModMgr, refrHandle, pNpc, pos, rot, pCellForSpawn, pWorldSpace, pActor, 0, 0, 0, 1);

            // 9. ForcePosition using existing SetPosition
            if (pRealSetPosition && pActor)
                pRealSetPosition(pActor, pos);

            // 10. Clear save flag on formFlags (bit 21 = 0x200000)
            uint32_t& actorFlags = *reinterpret_cast<uint32_t*>(reinterpret_cast<uintptr_t>(pActor) + 0x10);
            actorFlags &= 0xFFDFFFFF;

            RelayLog("[SkyrimCoopHooks] CMD_SPAWN_ACTOR: spawned formId=%u ptr=%p\n", formId, pActor);

            nlohmann::json result = {
                {JsonField::kOp, JsonOp::kSpawnResult},
                {JsonField::kFormId, formId},
                {JsonField::kPtr, reinterpret_cast<uint64_t>(pActor)},
                {JsonField::kSuccess, true}
            };
            g_tcpServer.SendJson(result);
            break;
        }

        case CMD_REMOVE_ACTOR: {
            // Phase 15: Remove a spawned actor from Skyrim.
            // Per D-06: Immediate deletion via disable + mark-for-deletion flags.
            void* pActor = reinterpret_cast<void*>(cmd.args[0]);
            if (!pActor)
            {
                RelayLog("[SkyrimCoopHooks] CMD_REMOVE_ACTOR: null actor pointer\n");
                break;
            }

            // Set DISABLED flag (bit 11 = 0x800) and DELETED flag (bit 5 = 0x20) on
            // TESForm::flags at offset 0x10
            uint32_t& flags = *reinterpret_cast<uint32_t*>(reinterpret_cast<uintptr_t>(pActor) + 0x10);
            flags |= (1 << 11); // kDisabled
            flags |= (1 << 5);  // kMarkedForDeletion

            RelayLog("[SkyrimCoopHooks] CMD_REMOVE_ACTOR: marked for deletion ptr=%p\n", pActor);
            break;
        }

        default:
            RelayLog("[SkyrimCoopHooks] Unknown command opcode=0x%04X\n", cmd.opcode);
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
// JSON command parsing: translate JSON commands to CommandSlot for SPSC queue
// ---------------------------------------------------------------------------

static void ParseJsonCommand(const nlohmann::json& aMsg)
{
    auto opIt = aMsg.find(JsonField::kOp);
    if (opIt == aMsg.end() || !opIt->is_string())
        return;

    const auto& op = opIt->get_ref<const std::string&>();

    if (op == JsonOp::kCmdSetPosition)
    {
        CommandSlot slot{};
        slot.opcode = CMD_SET_POSITION;
        slot.argCount = 3;
        slot.args[0] = aMsg.value("actor_ptr", uint64_t(0));

        float x = aMsg.value(JsonField::kX, 0.0f);
        float y = aMsg.value(JsonField::kY, 0.0f);
        float z = aMsg.value(JsonField::kZ, 0.0f);
        uint32_t xBits, yBits, zBits;
        memcpy(&xBits, &x, sizeof(float));
        memcpy(&yBits, &y, sizeof(float));
        memcpy(&zBits, &z, sizeof(float));
        slot.args[1] = (static_cast<uint64_t>(yBits) << 32) | xBits;
        slot.args[2] = zBits;
        g_commandQueue.Enqueue(slot);
    }
    else if (op == JsonOp::kCmdForceWeather)
    {
        CommandSlot slot{};
        slot.opcode = CMD_FORCE_WEATHER;
        slot.argCount = 2;
        slot.args[0] = aMsg.value(JsonField::kSkyPtr, uint64_t(0));
        slot.args[1] = aMsg.value(JsonField::kWeatherPtr, uint64_t(0));
        g_commandQueue.Enqueue(slot);
    }
    else if (op == JsonOp::kCmdCastSpell)
    {
        CommandSlot slot{};
        slot.opcode = CMD_CAST_SPELL;
        slot.argCount = 2;
        slot.args[0] = aMsg.value(JsonField::kCasterPtr, uint64_t(0));
        slot.args[1] = aMsg.value(JsonField::kSpellPtr, uint64_t(0));
        g_commandQueue.Enqueue(slot);
    }
    else if (op == JsonOp::kCmdEquipItem)
    {
        CommandSlot slot{};
        slot.opcode = CMD_EQUIP_ITEM;
        slot.argCount = 2;
        slot.args[0] = aMsg.value(JsonField::kActorPtr, uint64_t(0));
        slot.args[1] = aMsg.value(JsonField::kItemPtr, uint64_t(0));
        g_commandQueue.Enqueue(slot);
    }
    else if (op == JsonOp::kCmdUnequipItem)
    {
        CommandSlot slot{};
        slot.opcode = CMD_UNEQUIP_ITEM;
        slot.argCount = 2;
        slot.args[0] = aMsg.value(JsonField::kActorPtr, uint64_t(0));
        slot.args[1] = aMsg.value(JsonField::kItemPtr, uint64_t(0));
        g_commandQueue.Enqueue(slot);
    }
    else if (op == JsonOp::kCmdPlayAnimation)
    {
        CommandSlot slot{};
        slot.opcode = CMD_PLAY_ANIMATION;
        slot.argCount = 2;
        slot.args[0] = aMsg.value(JsonField::kThisPtr, uint64_t(0));
        slot.args[1] = aMsg.value("anim_ptr", uint64_t(0));
        g_commandQueue.Enqueue(slot);
    }
    else if (op == JsonOp::kCmdSetActorValue)
    {
        CommandSlot slot{};
        slot.opcode = CMD_SET_ACTOR_VALUE;
        slot.argCount = 3;
        slot.args[0] = aMsg.value(JsonField::kActorPtr, uint64_t(0));
        slot.args[1] = static_cast<uint64_t>(aMsg.value(JsonField::kType, 0));
        float val = aMsg.value(JsonField::kValue, 0.0f);
        uint32_t valBits;
        memcpy(&valBits, &val, sizeof(float));
        slot.args[2] = valBits;
        g_commandQueue.Enqueue(slot);
    }
    else if (op == JsonOp::kCmdDamageActor)
    {
        CommandSlot slot{};
        slot.opcode = CMD_DAMAGE_ACTOR;
        slot.argCount = 2;
        slot.args[0] = aMsg.value(JsonField::kActorPtr, uint64_t(0));
        float dmg = aMsg.value(JsonField::kDamage, 0.0f);
        uint32_t dmgBits;
        memcpy(&dmgBits, &dmg, sizeof(float));
        slot.args[1] = dmgBits;
        g_commandQueue.Enqueue(slot);
    }
    else if (op == JsonOp::kCmdAddSpell)
    {
        CommandSlot slot{};
        slot.opcode = CMD_ADD_SPELL;
        slot.argCount = 2;
        slot.args[0] = aMsg.value(JsonField::kActorPtr, uint64_t(0));
        slot.args[1] = aMsg.value(JsonField::kSpellPtr, uint64_t(0));
        g_commandQueue.Enqueue(slot);
    }
    else if (op == JsonOp::kCmdRemoveSpell)
    {
        CommandSlot slot{};
        slot.opcode = CMD_REMOVE_SPELL;
        slot.argCount = 2;
        slot.args[0] = aMsg.value(JsonField::kActorPtr, uint64_t(0));
        slot.args[1] = aMsg.value(JsonField::kSpellPtr, uint64_t(0));
        g_commandQueue.Enqueue(slot);
    }
    else if (op == JsonOp::kCmdAddItem)
    {
        CommandSlot slot{};
        slot.opcode = CMD_ADD_ITEM;
        slot.argCount = 3;
        slot.args[0] = aMsg.value(JsonField::kActorPtr, uint64_t(0));
        slot.args[1] = aMsg.value(JsonField::kItemPtr, uint64_t(0));
        slot.args[2] = static_cast<uint64_t>(aMsg.value(JsonField::kCount, 0));
        g_commandQueue.Enqueue(slot);
    }
    else if (op == JsonOp::kCmdRemoveItem)
    {
        CommandSlot slot{};
        slot.opcode = CMD_REMOVE_ITEM;
        slot.argCount = 3;
        slot.args[0] = aMsg.value(JsonField::kActorPtr, uint64_t(0));
        slot.args[1] = aMsg.value(JsonField::kItemPtr, uint64_t(0));
        slot.args[2] = static_cast<uint64_t>(aMsg.value(JsonField::kCount, 0));
        g_commandQueue.Enqueue(slot);
    }
    else if (op == JsonOp::kCmdActivate)
    {
        CommandSlot slot{};
        slot.opcode = CMD_ACTIVATE;
        slot.argCount = 2;
        slot.args[0] = aMsg.value(JsonField::kThisPtr, uint64_t(0));
        slot.args[1] = aMsg.value(JsonField::kActivatorPtr, uint64_t(0));
        g_commandQueue.Enqueue(slot);
    }
    else if (op == JsonOp::kCmdLockChange)
    {
        CommandSlot slot{};
        slot.opcode = CMD_LOCK_CHANGE;
        slot.argCount = 2;
        slot.args[0] = aMsg.value(JsonField::kThisPtr, uint64_t(0));
        slot.args[1] = static_cast<uint64_t>(aMsg.value(JsonField::kLocked, false));
        g_commandQueue.Enqueue(slot);
    }
    else if (op == JsonOp::kCmdLaunchProjectile)
    {
        CommandSlot slot{};
        slot.opcode = CMD_LAUNCH_PROJECTILE;
        slot.argCount = 1;
        slot.args[0] = aMsg.value(JsonField::kDataPtr, uint64_t(0));
        g_commandQueue.Enqueue(slot);
    }
    else if (op == JsonOp::kCmdSpawnActor)
    {
        // Per D-04: cmd_spawn_actor carries form_id, x, y, z, rx, ry
        CommandSlot slot{};
        slot.opcode = CMD_SPAWN_ACTOR;
        slot.argCount = 4;
        slot.args[0] = static_cast<uint64_t>(aMsg.value(JsonField::kFormId, uint32_t(0)));

        float x = aMsg.value(JsonField::kX, 0.0f);
        float y = aMsg.value(JsonField::kY, 0.0f);
        float z = aMsg.value(JsonField::kZ, 0.0f);
        float rx = aMsg.value(JsonField::kRx, 0.0f);
        float ry = aMsg.value(JsonField::kRy, 0.0f);

        uint32_t xBits, yBits, zBits, rxBits, ryBits;
        memcpy(&xBits, &x, sizeof(float));
        memcpy(&yBits, &y, sizeof(float));
        memcpy(&zBits, &z, sizeof(float));
        memcpy(&rxBits, &rx, sizeof(float));
        memcpy(&ryBits, &ry, sizeof(float));

        slot.args[1] = (static_cast<uint64_t>(yBits) << 32) | xBits;
        slot.args[2] = zBits;
        slot.args[3] = (static_cast<uint64_t>(ryBits) << 32) | rxBits;
        g_commandQueue.Enqueue(slot);
    }
    else if (op == JsonOp::kCmdRemoveActor)
    {
        // Per D-06: cmd_remove_actor carries actor_ptr (raw pointer from spawn_result)
        CommandSlot slot{};
        slot.opcode = CMD_REMOVE_ACTOR;
        slot.argCount = 1;
        slot.args[0] = aMsg.value(JsonField::kActorPtr, uint64_t(0));
        g_commandQueue.Enqueue(slot);
    }
    else
    {
        RelayLog("[SkyrimCoopHooks] Unknown command op: %s\n", op.c_str());
    }
}

// ---------------------------------------------------------------------------
// Monitor thread: accepts client, receives JSON commands, auto-restarts on disconnect
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

        // Send ready signal as JSON
        nlohmann::json readyMsg = {{JsonField::kOp, JsonOp::kCtrlReady}};
        g_tcpServer.SendJson(readyMsg);

        // Receive loop: read JSON lines from native process
        nlohmann::json msg;
        while (g_tcpServer.ReceiveJsonLine(msg))
        {
            if (g_shuttingDown)
                break;

            auto opIt = msg.find(JsonField::kOp);
            if (opIt == msg.end() || !opIt->is_string())
            {
                RelayLog("[SkyrimCoopHooks] WARNING: Received JSON without 'op' field\n");
                continue;
            }

            const auto& op = opIt->get_ref<const std::string&>();

            // Control messages
            if (op == JsonOp::kCtrlHeartbeat)
            {
                // Respond with heartbeat
                nlohmann::json heartbeat = {{JsonField::kOp, JsonOp::kCtrlHeartbeat}};
                g_tcpServer.SendJson(heartbeat);
            }
            else if (op == JsonOp::kCtrlShutdown)
            {
                RelayLog("[SkyrimCoopHooks] Native process requested shutdown\n");
                break;
            }
            else if (op == JsonOp::kCtrlReady)
            {
                RelayLog("[SkyrimCoopHooks] Native process signaled ready\n");
            }
            // Command messages: parse JSON into CommandSlot, enqueue for game thread
            else if (op.size() > 4 && op.compare(0, 4, "cmd_") == 0)
            {
                ParseJsonCommand(msg);
            }
            else
            {
                RelayLog("[SkyrimCoopHooks] Unknown op from native: %s\n", op.c_str());
            }
        }

        // Client disconnected -- clear dirty cache
        g_dirtyCache.Clear();
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

    // 2. Start TCP server on localhost
    //    Per D-11: SKYRIM_COOP_PORT env var for explicit port (host=9667, client=9668)
    uint16_t relayPort = 0; // default: ephemeral
    {
        const char* pPortEnv = getenv("SKYRIM_COOP_PORT");
        if (pPortEnv)
        {
            int parsed = atoi(pPortEnv);
            if (parsed > 0 && parsed < 65536)
            {
                relayPort = static_cast<uint16_t>(parsed);
                RelayLog("[SkyrimCoopHooks] SKYRIM_COOP_PORT=%u: binding to explicit port\n", relayPort);
            }
            else
            {
                RelayLog("[SkyrimCoopHooks] SKYRIM_COOP_PORT=%s: invalid, using ephemeral\n", pPortEnv);
            }
        }
    }
    if (!g_tcpServer.Start(relayPort))
    {
        RelayLog("[SkyrimCoopHooks] Failed to start TCP server\n");
        return false;
    }
    RelayLog("[SkyrimCoopHooks] TCP server listening on port %u\n", g_tcpServer.GetPort());

    // 3. Install hook trampolines (after TCP server so hooks can send events)
    // Default is essential-only (6 hooks) for stability during cell loading.
    // Set SKYRIM_COOP_ALL_HOOKS=1 to install all 31 hooks.
    bool essentialOnly = true;
    {
        const char* pEnv = getenv("SKYRIM_COOP_ALL_HOOKS");
        if (pEnv && pEnv[0] == '1')
        {
            essentialOnly = false;
            RelayLog("[SkyrimCoopHooks] SKYRIM_COOP_ALL_HOOKS=1: installing full hook set\n");
        }
        else
        {
            RelayLog("[SkyrimCoopHooks] Essential-only hooks (set SKYRIM_COOP_ALL_HOOKS=1 for full set)\n");
        }
    }
    if (!InstallAllHooks(essentialOnly))
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
