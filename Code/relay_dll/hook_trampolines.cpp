// Code/relay_dll/hook_trampolines.cpp -- MinHook trampoline functions for relay DLL
// Per D-02: DLL is a "dumb relay" forwarding raw pointer arguments over TCP
// Per D-13: No TiltedCore, EnTT, spdlog -- only MinHook + winsock2
// Per D-14: No game struct headers -- uses hardcoded Address Library IDs
//
// Each trampoline:
// 1. Packs raw arguments (as uint64_t) into a HookEventPacket
// 2. Sends the packet over TCP via g_tcpServer.Send()
// 3. Calls the original function via the saved pointer
//
// Address Library IDs extracted from Code/client/Games/Skyrim/*.cpp POINTER_SKYRIMSE calls.

#include <winsock2.h>
#include <windows.h>
#include <MinHook.h>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "protocol.h"
#include "tcp_server.h"

// External TCP server instance (defined in relay_main.cpp)
extern TcpServer g_tcpServer;

// Logging helper (defined in relay_main.cpp)
extern void RelayLog(const char* fmt, ...);

// ---------------------------------------------------------------------------
// Utility: bit-cast float to uint64_t for packing into HookEventPacket args
// ---------------------------------------------------------------------------

static uint64_t FloatToU64(float aVal)
{
    uint64_t v = 0;
    memcpy(&v, &aVal, sizeof(float));
    return v;
}

// Safe formId read: TESForm::formID at offset +0x14
// Returns 0 if pointer is null (no crash)
static uint32_t ReadFormId(void* apForm)
{
    if (!apForm) return 0;
    return *reinterpret_cast<uint32_t*>(
        reinterpret_cast<uintptr_t>(apForm) + 0x14);
}

// Safe position read: TESObjectREFR::position (NiPoint3) at offset +0x54
static void ReadPosition(void* apRef, float* aOutX, float* aOutY, float* aOutZ)
{
    if (!apRef) { *aOutX = *aOutY = *aOutZ = 0.0f; return; }
    uintptr_t base = reinterpret_cast<uintptr_t>(apRef);
    *aOutX = *reinterpret_cast<float*>(base + 0x54);
    *aOutY = *reinterpret_cast<float*>(base + 0x58);
    *aOutZ = *reinterpret_cast<float*>(base + 0x5C);
}

// ---------------------------------------------------------------------------
// Hardcoded Address Library IDs
// Extracted from existing Code/client/Games/Skyrim/*.cpp POINTER_SKYRIMSE calls
// ---------------------------------------------------------------------------

namespace AddressIds {
    // Actor Lifecycle (from Actor.cpp lines 1270-1291)
    constexpr uint64_t kActorProcess        = 37356;
    constexpr uint64_t kSetPosition         = 19790;
    constexpr uint64_t kCharacterCtor       = 40245;
    constexpr uint64_t kCharacterCtor2      = 40246;
    constexpr uint64_t kSpawnActorInWorld   = 19742;
    constexpr uint64_t kAddDeathItems       = 37198;

    // Combat (from Actor.cpp)
    constexpr uint64_t kDamageActor         = 37335;
    constexpr uint64_t kApplyActorEffect    = 35086;
    constexpr uint64_t kRegenAttributes     = 37448;
    constexpr uint64_t kUpdateDetectionState = 42704;
    // CombatController.cpp
    constexpr uint64_t kUpdateTarget        = 33236;

    // Inventory (from Actor.cpp + TESObjectREFR.cpp)
    constexpr uint64_t kAddInventoryItemActor = 37525;
    constexpr uint64_t kPickUpObject        = 37521;
    constexpr uint64_t kDropObject          = 40454;
    constexpr uint64_t kUnequipObject       = 37975;
    constexpr uint64_t kAddInventoryItemREFR = 19708;
    constexpr uint64_t kRemoveInventoryItem = 19689;

    // Magic (from ActorMagicCaster.cpp + MagicTarget.cpp + Actor.cpp)
    constexpr uint64_t kSpellCast           = 34144;
    constexpr uint64_t kInterruptCast       = 34140;
    constexpr uint64_t kAddTarget           = 34526;
    constexpr uint64_t kFindTargets         = 34410;
    constexpr uint64_t kRemoveSpell         = 38717;

    // Object/World (from TESObjectREFR.cpp + Projectile.cpp)
    constexpr uint64_t kActivate            = 19796;
    constexpr uint64_t kLockChange          = 19512;
    constexpr uint64_t kPlayAnimation       = 56205;
    constexpr uint64_t kProjectileLaunch    = 44108;

    // Weather (from Sky.cpp)
    constexpr uint64_t kSetWeather          = 26241;
    constexpr uint64_t kForceWeather        = 26243;
    constexpr uint64_t kUpdateWeather       = 26231;

    // Other (from Actor.cpp)
    constexpr uint64_t kPerformAction       = 38952;  // AnimationExperiments.cpp PerformIdleAction
    constexpr uint64_t kSimulateTime        = 26231;  // Re-used; CalendarService hooks TimeManager
    constexpr uint64_t kInitiateMountPackage = 37905;
}

// ---------------------------------------------------------------------------
// Minimal Address Library .bin loader
// ---------------------------------------------------------------------------
//
// The Address Library binary format (v2, SSE 1.6+):
//   Header: uint32_t version (2), uint32_t padding[3], uint32_t pointerSize,
//           uint32_t addressCount
//   Entries: array of (uint64_t id, uint64_t offset) pairs
//           Note: In v2 format, offsets are stored as delta-encoded values.
//
// For simplicity, we do a linear scan since we only look up ~30 IDs at startup.
// Returns the runtime address (module base + offset) for a given Address Library ID.
// Returns 0 on failure.

// Address library lookup cache: ID -> resolved address
struct AddrEntry {
    uint64_t id;
    uint64_t offset;
};

static AddrEntry* s_addrEntries = nullptr;
static uint32_t s_addrCount = 0;
static uintptr_t s_moduleBase = 0;

static bool LoadAddressLibrary()
{
    // Get Skyrim SE module base address
    s_moduleBase = reinterpret_cast<uintptr_t>(GetModuleHandleA("SkyrimSE.exe"));
    if (s_moduleBase == 0)
    {
        RelayLog("[SkyrimCoopHooks] Failed to get SkyrimSE.exe module base\n");
        return false;
    }

    // Build the address library path
    char skyrimPath[MAX_PATH]{};
    GetModuleFileNameA(GetModuleHandleA("SkyrimSE.exe"), skyrimPath, MAX_PATH);

    // Navigate to Data/SKSE/Plugins/
    char* pLastSlash = nullptr;
    for (char* p = skyrimPath; *p; p++)
        if (*p == '\\' || *p == '/') pLastSlash = p;

    if (!pLastSlash)
        return false;

    *pLastSlash = '\0';

    // Try common locations for the address library
    // versionlib-1-6-1170-0.bin (format: versionlib-MAJOR-MINOR-BUILD-SUB.bin)
    char libPath[MAX_PATH]{};

    // First try: same directory as SkyrimSE.exe -> Data/SKSE/Plugins/
    snprintf(libPath, sizeof(libPath), "%s\\Data\\SKSE\\Plugins\\versionlib-1-6-1170-0.bin", skyrimPath);

    HANDLE hFile = CreateFileA(libPath, GENERIC_READ, FILE_SHARE_READ, nullptr,
                               OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile == INVALID_HANDLE_VALUE)
    {
        // Try alternative name format
        snprintf(libPath, sizeof(libPath), "%s\\Data\\SKSE\\Plugins\\version-1-6-1170-0.bin", skyrimPath);
        hFile = CreateFileA(libPath, GENERIC_READ, FILE_SHARE_READ, nullptr,
                            OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    }

    if (hFile == INVALID_HANDLE_VALUE)
    {
        RelayLog("[SkyrimCoopHooks] Address Library not found at: %s\n", libPath);
        return false;
    }

    // Read the entire file
    DWORD fileSize = GetFileSize(hFile, nullptr);
    if (fileSize == INVALID_FILE_SIZE || fileSize < 16)
    {
        CloseHandle(hFile);
        return false;
    }

    auto* pBuf = static_cast<uint8_t*>(HeapAlloc(GetProcessHeap(), 0, fileSize));
    if (!pBuf)
    {
        CloseHandle(hFile);
        return false;
    }

    DWORD bytesRead = 0;
    if (!ReadFile(hFile, pBuf, fileSize, &bytesRead, nullptr) || bytesRead != fileSize)
    {
        HeapFree(GetProcessHeap(), 0, pBuf);
        CloseHandle(hFile);
        return false;
    }
    CloseHandle(hFile);

    // Parse header — matches VersionDb.h Load() exactly
    uint32_t pos = 0;
    auto RdI32 = [&]() -> int32_t { int32_t v; memcpy(&v, pBuf+pos, 4); pos += 4; return v; };
    auto RdU8  = [&]() -> uint8_t  { return pBuf[pos++]; };
    auto RdU16 = [&]() -> uint16_t { uint16_t v; memcpy(&v, pBuf+pos, 2); pos += 2; return v; };
    auto RdU32 = [&]() -> uint32_t { uint32_t v; memcpy(&v, pBuf+pos, 4); pos += 4; return v; };
    auto RdU64 = [&]() -> uint64_t { uint64_t v; memcpy(&v, pBuf+pos, 8); pos += 8; return v; };

    int32_t format = RdI32();
    if (format != 2)
    {
        RelayLog("[SkyrimCoopHooks] Address Library format %d not supported\n", format);
        HeapFree(GetProcessHeap(), 0, pBuf);
        return false;
    }

    // version[4]
    int32_t ver[4];
    for (int i = 0; i < 4; i++) ver[i] = RdI32();

    // Variable-length module name (e.g. "SkyrimSE.exe")
    int32_t nameLen = RdI32();
    if (nameLen < 0 || nameLen >= 0x10000) { HeapFree(GetProcessHeap(), 0, pBuf); return false; }
    pos += static_cast<uint32_t>(nameLen);

    int32_t ptrSize = RdI32();
    int32_t addrCount = RdI32();

    RelayLog("[SkyrimCoopHooks] Address Library v%d.%d.%d.%d: %d entries (ptrSize=%d)\n",
             ver[0], ver[1], ver[2], ver[3], addrCount, ptrSize);

    if (addrCount <= 0 || addrCount > 1000000)
    {
        RelayLog("[SkyrimCoopHooks] Suspicious entry count %d, aborting\n", addrCount);
        HeapFree(GetProcessHeap(), 0, pBuf);
        return false;
    }

    s_addrCount = static_cast<uint32_t>(addrCount);

        s_addrEntries = static_cast<AddrEntry*>(
            HeapAlloc(GetProcessHeap(), 0, s_addrCount * sizeof(AddrEntry)));
        if (!s_addrEntries)
        {
            HeapFree(GetProcessHeap(), 0, pBuf);
            return false;
        }

        // Delta-decode entries — matches VersionDb.h switch/case exactly
        uint64_t pvid = 0;
        uint64_t poffset = 0;

        for (uint32_t i = 0; i < s_addrCount && pos < fileSize; i++)
        {
            uint8_t type = RdU8();
            uint8_t low = type & 0xF;
            uint8_t high = type >> 4;

            // Decode ID (low nibble)
            uint64_t id = 0;
            switch (low)
            {
            case 0: id = RdU64(); break;
            case 1: id = pvid + 1; break;
            case 2: id = pvid + RdU8(); break;
            case 3: id = pvid - RdU8(); break;
            case 4: id = pvid + RdU16(); break;
            case 5: id = pvid - RdU16(); break;
            case 6: id = RdU16(); break;
            case 7: id = RdU32(); break;
            default:
                RelayLog("[SkyrimCoopHooks] Bad id encoding %u at entry %u\n", low, i);
                HeapFree(GetProcessHeap(), 0, s_addrEntries);
                s_addrEntries = nullptr; s_addrCount = 0;
                HeapFree(GetProcessHeap(), 0, pBuf);
                return false;
            }

            // Decode offset (high nibble, bit 3 = pointer-size division flag)
            uint64_t tpoffset = (high & 8) ? (poffset / static_cast<uint64_t>(ptrSize)) : poffset;
            uint64_t off = 0;
            switch (high & 7)
            {
            case 0: off = RdU64(); break;
            case 1: off = tpoffset + 1; break;
            case 2: off = tpoffset + RdU8(); break;
            case 3: off = tpoffset - RdU8(); break;
            case 4: off = tpoffset + RdU16(); break;
            case 5: off = tpoffset - RdU16(); break;
            case 6: off = RdU16(); break;
            case 7: off = RdU32(); break;
            }

            if (high & 8)
                off *= static_cast<uint64_t>(ptrSize);

            s_addrEntries[i].id = id;
            s_addrEntries[i].offset = off;

            pvid = id;
            poffset = off;
        }

    HeapFree(GetProcessHeap(), 0, pBuf);
    return true;
}

static void FreeAddressLibrary()
{
    if (s_addrEntries)
    {
        HeapFree(GetProcessHeap(), 0, s_addrEntries);
        s_addrEntries = nullptr;
    }
    s_addrCount = 0;
}

// Resolve an Address Library ID to a runtime function pointer.
// Returns nullptr if ID not found.
static void* ResolveAddress(uint64_t aId)
{
    for (uint32_t i = 0; i < s_addrCount; i++)
    {
        if (s_addrEntries[i].id == aId)
            return reinterpret_cast<void*>(s_moduleBase + s_addrEntries[i].offset);
    }
    return nullptr;
}

// ---------------------------------------------------------------------------
// Helper: send a HookEventPacket over TCP
// ---------------------------------------------------------------------------

static void SendHookEvent(HookOpcode aOpcode, uint8_t aArgCount, const uint64_t* apArgs)
{
    HookEventPacket pkt{};
    pkt.header.opcode = aOpcode;
    pkt.header.length = sizeof(HookEventPacket) - sizeof(PacketHeader);
    pkt.argCount = aArgCount;
    for (uint8_t i = 0; i < aArgCount && i < 8; i++)
        pkt.args[i] = apArgs[i];

    g_tcpServer.Send(&pkt, sizeof(pkt));
}

// ---------------------------------------------------------------------------
// Trampoline declarations -- generic function pointer types
// All trampolines use raw void* for "this" and integer/pointer args.
// Per D-14: no game struct field access, only raw pointer forwarding.
// ---------------------------------------------------------------------------

// Generic function pointer types for various signatures
using TFunc_VoidPtr = void* (__fastcall*)(void*);
using TFunc_VoidPtr_VoidPtr = void* (__fastcall*)(void*, void*);
using TFunc_VoidPtr_2 = void (__fastcall*)(void*, void*);
using TFunc_VoidPtr_3 = void (__fastcall*)(void*, void*, void*);
using TFunc_VoidPtr_4 = void (__fastcall*)(void*, void*, void*, void*);
using TFunc_VoidPtr_5 = void (__fastcall*)(void*, void*, void*, void*, void*);
using TFunc_VoidPtr_6 = void (__fastcall*)(void*, void*, void*, void*, void*, void*);
using TFunc_RetVoid = void (__fastcall*)(void*);
using TFunc_Float3 = void (__fastcall*)(void*, float, float, float);

// ---------------------------------------------------------------------------
// Actor Lifecycle hooks (6)
// ---------------------------------------------------------------------------

// ActorProcess (37356) - void __fastcall ActorProcess(Actor* apThis, float aDelta)
using TActorProcess = void (__fastcall*)(void*, float);
static TActorProcess RealActorProcess = nullptr;
static void __fastcall HookActorProcess(void* apThis, float aDelta)
{
    uint64_t args[3] = { reinterpret_cast<uint64_t>(apThis),
                          static_cast<uint64_t>(ReadFormId(apThis)),
                          FloatToU64(aDelta) };
    SendHookEvent(HOOK_ACTOR_PROCESS, 3, args);
    RealActorProcess(apThis, aDelta);
}

// SetPosition (19790) - void __fastcall SetPosition(REFR* apThis, NiPoint3* apPos)
using TSetPosition = void (__fastcall*)(void*, void*);
static TSetPosition RealSetPosition = nullptr;
static void __fastcall HookSetPosition(void* apThis, void* apPos)
{
    float posX = 0.0f, posY = 0.0f, posZ = 0.0f;
    if (apPos)
    {
        posX = *reinterpret_cast<float*>(reinterpret_cast<uintptr_t>(apPos) + 0);
        posY = *reinterpret_cast<float*>(reinterpret_cast<uintptr_t>(apPos) + 4);
        posZ = *reinterpret_cast<float*>(reinterpret_cast<uintptr_t>(apPos) + 8);
    }
    uint64_t args[5] = { reinterpret_cast<uint64_t>(apThis),
                          static_cast<uint64_t>(ReadFormId(apThis)),
                          FloatToU64(posX), FloatToU64(posY), FloatToU64(posZ) };
    SendHookEvent(HOOK_SET_POSITION, 5, args);
    RealSetPosition(apThis, apPos);
}

// CharacterConstructor (40245) - Character* __fastcall CharacterCtor(Character* apThis)
using TCharacterCtor = void* (__fastcall*)(void*);
static TCharacterCtor RealCharacterCtor = nullptr;
static void* __fastcall HookCharacterCtor(void* apThis)
{
    uint64_t args[2] = { reinterpret_cast<uint64_t>(apThis),
                          static_cast<uint64_t>(ReadFormId(apThis)) };
    SendHookEvent(HOOK_CHARACTER_CTOR, 2, args);
    return RealCharacterCtor(apThis);
}

// CharacterConstructor2 (40246) - Character* __fastcall CharacterCtor2(Character* apThis, uint8_t aFormType)
using TCharacterCtor2 = void* (__fastcall*)(void*, uint8_t);
static TCharacterCtor2 RealCharacterCtor2 = nullptr;
static void* __fastcall HookCharacterCtor2(void* apThis, uint8_t aFormType)
{
    uint64_t args[2] = { reinterpret_cast<uint64_t>(apThis),
                          static_cast<uint64_t>(ReadFormId(apThis)) };
    SendHookEvent(HOOK_CHARACTER_CTOR, 2, args);
    return RealCharacterCtor2(apThis, aFormType);
}

// SpawnActorInWorld (19742) - void __fastcall SpawnActorInWorld(void* apThis)
using TSpawnActor = void (__fastcall*)(void*);
static TSpawnActor RealSpawnActor = nullptr;
static void __fastcall HookSpawnActor(void* apThis)
{
    float posX = 0.0f, posY = 0.0f, posZ = 0.0f;
    ReadPosition(apThis, &posX, &posY, &posZ);
    uint64_t args[5] = { reinterpret_cast<uint64_t>(apThis),
                          static_cast<uint64_t>(ReadFormId(apThis)),
                          FloatToU64(posX), FloatToU64(posY), FloatToU64(posZ) };
    SendHookEvent(HOOK_SPAWN_ACTOR, 5, args);
    RealSpawnActor(apThis);
}

// AddDeathItems (37198) - void __fastcall AddDeathItems(Actor* apThis)
using TAddDeathItems = void (__fastcall*)(void*);
static TAddDeathItems RealAddDeathItems = nullptr;
static void __fastcall HookAddDeathItems(void* apThis)
{
    uint64_t args[2] = { reinterpret_cast<uint64_t>(apThis),
                          static_cast<uint64_t>(ReadFormId(apThis)) };
    SendHookEvent(HOOK_DEATH_ITEMS, 2, args);
    RealAddDeathItems(apThis);
}

// ---------------------------------------------------------------------------
// Combat hooks (5)
// ---------------------------------------------------------------------------

// DamageActor (37335) - float __fastcall DamageActor(Actor* apThis, float aDamage)
using TDamageActor = float (__fastcall*)(void*, float);
static TDamageActor RealDamageActor = nullptr;
static float __fastcall HookDamageActor(void* apThis, float aDamage)
{
    uint64_t args[3] = { reinterpret_cast<uint64_t>(apThis),
                          static_cast<uint64_t>(ReadFormId(apThis)),
                          FloatToU64(aDamage) };
    SendHookEvent(HOOK_DAMAGE_ACTOR, 3, args);
    return RealDamageActor(apThis, aDamage);
}

// ApplyActorEffect (35086) - void __fastcall ApplyActorEffect(void* apThis, void* apCaster, void* apEffect)
using TApplyActorEffect = void (__fastcall*)(void*, void*, void*);
static TApplyActorEffect RealApplyActorEffect = nullptr;
static void __fastcall HookApplyActorEffect(void* apThis, void* apCaster, void* apEffect)
{
    uint64_t args[5] = { reinterpret_cast<uint64_t>(apThis),
                          static_cast<uint64_t>(ReadFormId(apThis)),
                          reinterpret_cast<uint64_t>(apCaster),
                          static_cast<uint64_t>(ReadFormId(apCaster)),
                          reinterpret_cast<uint64_t>(apEffect) };
    SendHookEvent(HOOK_APPLY_EFFECT, 5, args);
    RealApplyActorEffect(apThis, apCaster, apEffect);
}

// RegenAttributes (37448) - void __fastcall RegenAttributes(Actor* apThis, int aType, float aValue)
using TRegenAttributes = void (__fastcall*)(void*, int, float);
static TRegenAttributes RealRegenAttributes = nullptr;
static void __fastcall HookRegenAttributes(void* apThis, int aType, float aValue)
{
    uint64_t args[4] = { reinterpret_cast<uint64_t>(apThis),
                          static_cast<uint64_t>(ReadFormId(apThis)),
                          static_cast<uint64_t>(aType),
                          FloatToU64(aValue) };
    SendHookEvent(HOOK_REGEN_ATTR, 4, args);
    RealRegenAttributes(apThis, aType, aValue);
}

// UpdateDetectionState (42704) - void __fastcall UpdateDetectionState(void* apThis, void* apTarget)
using TUpdateDetectionState = void (__fastcall*)(void*, void*);
static TUpdateDetectionState RealUpdateDetectionState = nullptr;
static void __fastcall HookUpdateDetectionState(void* apThis, void* apTarget)
{
    uint64_t args[4] = { reinterpret_cast<uint64_t>(apThis),
                          static_cast<uint64_t>(ReadFormId(apThis)),
                          reinterpret_cast<uint64_t>(apTarget),
                          static_cast<uint64_t>(ReadFormId(apTarget)) };
    SendHookEvent(HOOK_APPLY_EFFECT, 4, args);  // Re-use opcode for detection; native side filters
    RealUpdateDetectionState(apThis, apTarget);
}

// UpdateTarget (33236) - void __fastcall UpdateTarget(CombatController* apThis)
using TUpdateTarget = void (__fastcall*)(void*);
static TUpdateTarget RealUpdateTarget = nullptr;
static void __fastcall HookUpdateTarget(void* apThis)
{
    uint64_t args[2] = { reinterpret_cast<uint64_t>(apThis),
                          static_cast<uint64_t>(ReadFormId(apThis)) };
    SendHookEvent(HOOK_DAMAGE_ACTOR, 2, args);  // CombatController update
    RealUpdateTarget(apThis);
}

// ---------------------------------------------------------------------------
// Inventory hooks (6)
// ---------------------------------------------------------------------------

// AddInventoryItem (Actor) (37525)
using TAddInventoryItemActor = void (__fastcall*)(void*, void*, void*, int, void*);
static TAddInventoryItemActor RealAddInventoryItemActor = nullptr;
static void __fastcall HookAddInventoryItemActor(void* apThis, void* apItem, void* apExtraData, int aCount, void* apRef)
{
    uint64_t args[6] = { reinterpret_cast<uint64_t>(apThis),
                          static_cast<uint64_t>(ReadFormId(apThis)),
                          reinterpret_cast<uint64_t>(apItem),
                          static_cast<uint64_t>(ReadFormId(apItem)),
                          static_cast<uint64_t>(aCount),
                          reinterpret_cast<uint64_t>(apRef) };
    SendHookEvent(HOOK_ADD_INVENTORY, 6, args);
    RealAddInventoryItemActor(apThis, apItem, apExtraData, aCount, apRef);
}

// PickUpObject (37521)
using TPickUpObject = void (__fastcall*)(void*, void*, int, bool, bool);
static TPickUpObject RealPickUpObject = nullptr;
static void __fastcall HookPickUpObject(void* apThis, void* apObj, int aCount, bool a3, bool a4)
{
    uint64_t args[5] = { reinterpret_cast<uint64_t>(apThis),
                          static_cast<uint64_t>(ReadFormId(apThis)),
                          reinterpret_cast<uint64_t>(apObj),
                          static_cast<uint64_t>(ReadFormId(apObj)),
                          static_cast<uint64_t>(aCount) };
    SendHookEvent(HOOK_PICK_UP, 5, args);
    RealPickUpObject(apThis, apObj, aCount, a3, a4);
}

// DropObject (40454)
using TDropObject = void (__fastcall*)(void*, void*, void*, int, void*);
static TDropObject RealDropObject = nullptr;
static void __fastcall HookDropObject(void* apThis, void* apItem, void* apExtra, int aCount, void* apPos)
{
    uint64_t args[5] = { reinterpret_cast<uint64_t>(apThis),
                          static_cast<uint64_t>(ReadFormId(apThis)),
                          reinterpret_cast<uint64_t>(apItem),
                          static_cast<uint64_t>(ReadFormId(apItem)),
                          static_cast<uint64_t>(aCount) };
    SendHookEvent(HOOK_DROP_OBJECT, 5, args);
    RealDropObject(apThis, apItem, apExtra, aCount, apPos);
}

// UnequipObject (37975)
using TUnequipObject = void (__fastcall*)(void*, void*, bool, void*);
static TUnequipObject RealUnequipObject = nullptr;
static void __fastcall HookUnequipObject(void* apThis, void* apObj, bool aPreventEquip, void* apSlot)
{
    uint64_t args[4] = { reinterpret_cast<uint64_t>(apThis),
                          static_cast<uint64_t>(ReadFormId(apThis)),
                          reinterpret_cast<uint64_t>(apObj),
                          static_cast<uint64_t>(ReadFormId(apObj)) };
    SendHookEvent(HOOK_UNEQUIP, 4, args);
    RealUnequipObject(apThis, apObj, aPreventEquip, apSlot);
}

// AddInventoryItem (REFR) (19708)
using TAddInventoryItemREFR = void (__fastcall*)(void*, void*, void*, int);
static TAddInventoryItemREFR RealAddInventoryItemREFR = nullptr;
static void __fastcall HookAddInventoryItemREFR(void* apThis, void* apItem, void* apExtra, int aCount)
{
    uint64_t args[5] = { reinterpret_cast<uint64_t>(apThis),
                          static_cast<uint64_t>(ReadFormId(apThis)),
                          reinterpret_cast<uint64_t>(apItem),
                          static_cast<uint64_t>(ReadFormId(apItem)),
                          static_cast<uint64_t>(aCount) };
    SendHookEvent(HOOK_ADD_INVENTORY, 5, args);
    RealAddInventoryItemREFR(apThis, apItem, apExtra, aCount);
}

// RemoveInventoryItem (19689)
using TRemoveInventoryItem = void* (__fastcall*)(void*, void*, int, void*, void*);
static TRemoveInventoryItem RealRemoveInventoryItem = nullptr;
static void* __fastcall HookRemoveInventoryItem(void* apThis, void* apItem, int aCount, void* apExtra, void* apMoveTo)
{
    uint64_t args[5] = { reinterpret_cast<uint64_t>(apThis),
                          static_cast<uint64_t>(ReadFormId(apThis)),
                          reinterpret_cast<uint64_t>(apItem),
                          static_cast<uint64_t>(ReadFormId(apItem)),
                          static_cast<uint64_t>(aCount) };
    SendHookEvent(HOOK_REMOVE_INV_ITEM, 5, args);
    return RealRemoveInventoryItem(apThis, apItem, aCount, apExtra, apMoveTo);
}

// ---------------------------------------------------------------------------
// Magic hooks (5)
// ---------------------------------------------------------------------------

// SpellCast (34144)
using TSpellCast = void (__fastcall*)(void*, void*);
static TSpellCast RealSpellCast = nullptr;
static void __fastcall HookSpellCast(void* apThis, void* apSpell)
{
    uint64_t args[4] = { reinterpret_cast<uint64_t>(apThis),
                          static_cast<uint64_t>(ReadFormId(apThis)),
                          reinterpret_cast<uint64_t>(apSpell),
                          static_cast<uint64_t>(ReadFormId(apSpell)) };
    SendHookEvent(HOOK_SPELL_CAST, 4, args);
    RealSpellCast(apThis, apSpell);
}

// InterruptCast (34140)
using TInterruptCast = void (__fastcall*)(void*, bool);
static TInterruptCast RealInterruptCast = nullptr;
static void __fastcall HookInterruptCast(void* apThis, bool aRefund)
{
    uint64_t args[3] = { reinterpret_cast<uint64_t>(apThis),
                          static_cast<uint64_t>(ReadFormId(apThis)),
                          static_cast<uint64_t>(aRefund) };
    SendHookEvent(HOOK_INTERRUPT_CAST, 3, args);
    RealInterruptCast(apThis, aRefund);
}

// AddTarget (34526)
using TAddTarget = bool (__fastcall*)(void*, void*, void*);
static TAddTarget RealAddTarget = nullptr;
static bool __fastcall HookAddTarget(void* apThis, void* apTarget, void* apHitData)
{
    uint64_t args[3] = { reinterpret_cast<uint64_t>(apThis),
                          static_cast<uint64_t>(ReadFormId(apThis)),
                          reinterpret_cast<uint64_t>(apHitData) };
    SendHookEvent(HOOK_ADD_TARGET, 3, args);
    return RealAddTarget(apThis, apTarget, apHitData);
}

// FindTargets (34410)
using TFindTargets = void (__fastcall*)(void*, void*);
static TFindTargets RealFindTargets = nullptr;
static void __fastcall HookFindTargets(void* apThis, void* apCaster)
{
    uint64_t args[3] = { reinterpret_cast<uint64_t>(apThis),
                          reinterpret_cast<uint64_t>(apCaster),
                          static_cast<uint64_t>(ReadFormId(apCaster)) };
    SendHookEvent(HOOK_SPELL_CAST, 3, args);  // Reuse spell opcode for target finding
    RealFindTargets(apThis, apCaster);
}

// RemoveSpell (38717)
using TRemoveSpell = void (__fastcall*)(void*, void*);
static TRemoveSpell RealRemoveSpell = nullptr;
static void __fastcall HookRemoveSpell(void* apThis, void* apSpell)
{
    uint64_t args[4] = { reinterpret_cast<uint64_t>(apThis),
                          static_cast<uint64_t>(ReadFormId(apThis)),
                          reinterpret_cast<uint64_t>(apSpell),
                          static_cast<uint64_t>(ReadFormId(apSpell)) };
    SendHookEvent(HOOK_REMOVE_SPELL, 4, args);
    RealRemoveSpell(apThis, apSpell);
}

// ---------------------------------------------------------------------------
// Object/World hooks (4)
// ---------------------------------------------------------------------------

// Activate (19796)
using TActivate = bool (__fastcall*)(void*, void*, void*, void*, int, bool);
static TActivate RealActivate = nullptr;
static bool __fastcall HookActivate(void* apThis, void* apActivator, void* apObj, void* apUndefined, int aCount, bool aDefaultProcessing)
{
    uint64_t args[6] = { reinterpret_cast<uint64_t>(apThis),
                          static_cast<uint64_t>(ReadFormId(apThis)),
                          reinterpret_cast<uint64_t>(apActivator),
                          static_cast<uint64_t>(ReadFormId(apActivator)),
                          reinterpret_cast<uint64_t>(apObj),
                          static_cast<uint64_t>(aCount) };
    SendHookEvent(HOOK_ACTIVATE, 6, args);
    return RealActivate(apThis, apActivator, apObj, apUndefined, aCount, aDefaultProcessing);
}

// LockChange (19512)
using TLockChange = void (__fastcall*)(void*, bool);
static TLockChange RealLockChange = nullptr;
static void __fastcall HookLockChange(void* apThis, bool aLocked)
{
    uint64_t args[3] = { reinterpret_cast<uint64_t>(apThis),
                          static_cast<uint64_t>(ReadFormId(apThis)),
                          static_cast<uint64_t>(aLocked) };
    SendHookEvent(HOOK_LOCK_CHANGE, 3, args);
    RealLockChange(apThis, aLocked);
}

// PlayAnimation (56205)
using TPlayAnimation = void (__fastcall*)(void*, void*);
static TPlayAnimation RealPlayAnimation = nullptr;
static void __fastcall HookPlayAnimation(void* apThis, void* apAnim)
{
    uint64_t args[3] = { reinterpret_cast<uint64_t>(apThis),
                          static_cast<uint64_t>(ReadFormId(apThis)),
                          reinterpret_cast<uint64_t>(apAnim) };
    SendHookEvent(HOOK_PLAY_ANIMATION, 3, args);
    RealPlayAnimation(apThis, apAnim);
}

// ProjectileLaunch (44108) - Projectile::Launch(LaunchData* apData)
using TProjectileLaunch = void (__fastcall*)(void*);
static TProjectileLaunch RealProjectileLaunch = nullptr;
static void __fastcall HookProjectileLaunch(void* apData)
{
    uint64_t args[1] = { reinterpret_cast<uint64_t>(apData) };
    SendHookEvent(HOOK_PROJECTILE, 1, args);
    RealProjectileLaunch(apData);
}

// ---------------------------------------------------------------------------
// Weather hooks (3)
// ---------------------------------------------------------------------------

// SetWeather (26241) - void __fastcall SetWeather(Sky* apThis, void* apWeather, bool aOverride, bool aAccelerate)
using TSetWeather = void (__fastcall*)(void*, void*, bool, bool);
static TSetWeather RealSetWeather = nullptr;
static void __fastcall HookSetWeather(void* apThis, void* apWeather, bool aOverride, bool aAccelerate)
{
    uint64_t args[5] = { reinterpret_cast<uint64_t>(apThis),
                          reinterpret_cast<uint64_t>(apWeather),
                          static_cast<uint64_t>(ReadFormId(apWeather)),
                          static_cast<uint64_t>(aOverride),
                          static_cast<uint64_t>(aAccelerate) };
    SendHookEvent(HOOK_SET_WEATHER, 5, args);
    RealSetWeather(apThis, apWeather, aOverride, aAccelerate);
}

// ForceWeather (26243) - void __fastcall ForceWeather(Sky* apThis, void* apWeather, bool aOverride)
using TForceWeather = void (__fastcall*)(void*, void*, bool);
static TForceWeather RealForceWeather = nullptr;
static void __fastcall HookForceWeather(void* apThis, void* apWeather, bool aOverride)
{
    uint64_t args[4] = { reinterpret_cast<uint64_t>(apThis),
                          reinterpret_cast<uint64_t>(apWeather),
                          static_cast<uint64_t>(ReadFormId(apWeather)),
                          static_cast<uint64_t>(aOverride) };
    SendHookEvent(HOOK_FORCE_WEATHER, 4, args);
    RealForceWeather(apThis, apWeather, aOverride);
}

// UpdateWeather (26231) - void __fastcall UpdateWeather(Sky* apThis)
using TUpdateWeather = void (__fastcall*)(void*);
static TUpdateWeather RealUpdateWeather = nullptr;
static void __fastcall HookUpdateWeather(void* apThis)
{
    uint64_t args[1] = { reinterpret_cast<uint64_t>(apThis) };
    SendHookEvent(HOOK_WEATHER_CHANGE, 1, args);
    RealUpdateWeather(apThis);
}

// ---------------------------------------------------------------------------
// Other hooks (3)
// ---------------------------------------------------------------------------

// PerformAction (38952) - void __fastcall PerformAction(void* apProcess, void* apActor, void* apAction)
using TPerformAction = void (__fastcall*)(void*, void*, void*);
static TPerformAction RealPerformAction = nullptr;
static void __fastcall HookPerformAction(void* apProcess, void* apActor, void* apAction)
{
    uint64_t args[5] = { reinterpret_cast<uint64_t>(apProcess),
                          reinterpret_cast<uint64_t>(apActor),
                          static_cast<uint64_t>(ReadFormId(apActor)),
                          reinterpret_cast<uint64_t>(apAction),
                          static_cast<uint64_t>(ReadFormId(apAction)) };
    SendHookEvent(HOOK_PERFORM_ACTION, 5, args);
    RealPerformAction(apProcess, apActor, apAction);
}

// SimulateTime (26231) - reuses UpdateWeather ID; separated logically for CalendarService
// We hook a separate function for time simulation if available
// For now, calendar sync is handled via memory reads (not hooks)
// Placeholder: InitiateMountPackage (37905)

// InitiateMountPackage (37905) - void __fastcall InitiateMountPackage(Actor* apThis, Actor* apMount)
using TInitiateMountPackage = void (__fastcall*)(void*, void*);
static TInitiateMountPackage RealInitiateMountPackage = nullptr;
static void __fastcall HookInitiateMountPackage(void* apThis, void* apMount)
{
    uint64_t args[4] = { reinterpret_cast<uint64_t>(apThis),
                          static_cast<uint64_t>(ReadFormId(apThis)),
                          reinterpret_cast<uint64_t>(apMount),
                          static_cast<uint64_t>(ReadFormId(apMount)) };
    SendHookEvent(HOOK_MOUNT_PACKAGE, 4, args);
    RealInitiateMountPackage(apThis, apMount);
}

// ---------------------------------------------------------------------------
// Hook installation table
// ---------------------------------------------------------------------------

struct HookEntry {
    uint64_t addressId;
    void* pDetour;
    void** ppOriginal;
    const char* pName;
    bool essential;  // true = needed for basic actor sync (lifecycle + position)
};

// Helper macro to suppress function-to-void* warnings in the hook table
#define HOOK_ENTRY(id, hook, orig, name, ess) \
    { id, reinterpret_cast<void*>(hook), reinterpret_cast<void**>(&orig), name, ess }

static const HookEntry s_hookTable[] = {
    // Actor Lifecycle (6) -- ALL essential for basic actor sync
    HOOK_ENTRY(AddressIds::kActorProcess,         HookActorProcess,          RealActorProcess,          "ActorProcess",           true),
    HOOK_ENTRY(AddressIds::kSetPosition,          HookSetPosition,           RealSetPosition,           "SetPosition",            true),
    HOOK_ENTRY(AddressIds::kCharacterCtor,        HookCharacterCtor,         RealCharacterCtor,         "CharacterCtor",          true),
    HOOK_ENTRY(AddressIds::kCharacterCtor2,       HookCharacterCtor2,        RealCharacterCtor2,        "CharacterCtor2",         true),
    HOOK_ENTRY(AddressIds::kSpawnActorInWorld,    HookSpawnActor,            RealSpawnActor,            "SpawnActorInWorld",      true),
    HOOK_ENTRY(AddressIds::kAddDeathItems,        HookAddDeathItems,         RealAddDeathItems,         "AddDeathItems",          true),

    // Combat (5) -- non-essential
    HOOK_ENTRY(AddressIds::kDamageActor,          HookDamageActor,           RealDamageActor,           "DamageActor",            false),
    HOOK_ENTRY(AddressIds::kApplyActorEffect,     HookApplyActorEffect,      RealApplyActorEffect,      "ApplyActorEffect",       false),
    HOOK_ENTRY(AddressIds::kRegenAttributes,      HookRegenAttributes,       RealRegenAttributes,       "RegenAttributes",        false),
    HOOK_ENTRY(AddressIds::kUpdateDetectionState, HookUpdateDetectionState,  RealUpdateDetectionState,  "UpdateDetectionState",   false),
    HOOK_ENTRY(AddressIds::kUpdateTarget,         HookUpdateTarget,          RealUpdateTarget,          "UpdateTarget",           false),

    // Inventory (6) -- non-essential
    HOOK_ENTRY(AddressIds::kAddInventoryItemActor, HookAddInventoryItemActor, RealAddInventoryItemActor, "AddInventoryItem(Actor)", false),
    HOOK_ENTRY(AddressIds::kPickUpObject,         HookPickUpObject,          RealPickUpObject,          "PickUpObject",           false),
    HOOK_ENTRY(AddressIds::kDropObject,           HookDropObject,            RealDropObject,            "DropObject",             false),
    HOOK_ENTRY(AddressIds::kUnequipObject,        HookUnequipObject,         RealUnequipObject,         "UnequipObject",          false),
    HOOK_ENTRY(AddressIds::kAddInventoryItemREFR, HookAddInventoryItemREFR,  RealAddInventoryItemREFR,  "AddInventoryItem(REFR)", false),
    HOOK_ENTRY(AddressIds::kRemoveInventoryItem,  HookRemoveInventoryItem,   RealRemoveInventoryItem,   "RemoveInventoryItem",    false),

    // Magic (5) -- non-essential
    HOOK_ENTRY(AddressIds::kSpellCast,            HookSpellCast,             RealSpellCast,             "SpellCast",              false),
    HOOK_ENTRY(AddressIds::kInterruptCast,        HookInterruptCast,         RealInterruptCast,         "InterruptCast",          false),
    HOOK_ENTRY(AddressIds::kAddTarget,            HookAddTarget,             RealAddTarget,             "AddTarget",              false),
    HOOK_ENTRY(AddressIds::kFindTargets,          HookFindTargets,           RealFindTargets,           "FindTargets",            false),
    HOOK_ENTRY(AddressIds::kRemoveSpell,          HookRemoveSpell,           RealRemoveSpell,           "RemoveSpell",            false),

    // Object/World (4) -- non-essential
    HOOK_ENTRY(AddressIds::kActivate,             HookActivate,              RealActivate,              "Activate",               false),
    HOOK_ENTRY(AddressIds::kLockChange,           HookLockChange,            RealLockChange,            "LockChange",             false),
    HOOK_ENTRY(AddressIds::kPlayAnimation,        HookPlayAnimation,         RealPlayAnimation,         "PlayAnimation",          false),
    HOOK_ENTRY(AddressIds::kProjectileLaunch,     HookProjectileLaunch,      RealProjectileLaunch,      "ProjectileLaunch",       false),

    // Weather (3) -- non-essential
    HOOK_ENTRY(AddressIds::kSetWeather,           HookSetWeather,            RealSetWeather,            "SetWeather",             false),
    HOOK_ENTRY(AddressIds::kForceWeather,         HookForceWeather,          RealForceWeather,          "ForceWeather",           false),
    HOOK_ENTRY(AddressIds::kUpdateWeather,        HookUpdateWeather,         RealUpdateWeather,         "UpdateWeather",          false),

    // Other (2) -- non-essential
    HOOK_ENTRY(AddressIds::kPerformAction,        HookPerformAction,         RealPerformAction,         "PerformAction",          false),
    HOOK_ENTRY(AddressIds::kInitiateMountPackage, HookInitiateMountPackage,  RealInitiateMountPackage,  "InitiateMountPackage",   false),
};

static constexpr uint32_t kHookCount = sizeof(s_hookTable) / sizeof(s_hookTable[0]);

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

bool InstallAllHooks(bool aEssentialOnly)
{
    RelayLog("[SkyrimCoopHooks] Installing hooks (mode: %s, total entries: %u)...\n",
             aEssentialOnly ? "essential-only" : "full", kHookCount);

    // Step 1: Load Address Library
    if (!LoadAddressLibrary())
    {
        RelayLog("[SkyrimCoopHooks] Failed to load Address Library -- hooks disabled\n");
        return false;
    }

    // Step 2: Initialize MinHook
    MH_STATUS status = MH_Initialize();
    if (status != MH_OK && status != MH_ERROR_ALREADY_INITIALIZED)
    {
        RelayLog("[SkyrimCoopHooks] MH_Initialize failed: %d\n", static_cast<int>(status));
        FreeAddressLibrary();
        return false;
    }

    // Step 3: Create hooks for each entry
    uint32_t installedCount = 0;
    for (uint32_t i = 0; i < kHookCount; i++)
    {
        const auto& entry = s_hookTable[i];

        // Skip non-essential hooks if in essential-only mode
        if (aEssentialOnly && !entry.essential)
        {
            continue;
        }

        void* pTarget = ResolveAddress(entry.addressId);

        if (!pTarget)
        {
            RelayLog("[SkyrimCoopHooks] Address Library ID %llu not found for %s -- skipping\n",
                     entry.addressId, entry.pName);
            continue;
        }

        status = MH_CreateHook(pTarget, entry.pDetour, entry.ppOriginal);
        if (status != MH_OK)
        {
            RelayLog("[SkyrimCoopHooks] MH_CreateHook failed for %s (ID %llu): %d\n",
                     entry.pName, entry.addressId, static_cast<int>(status));
            continue;
        }

        installedCount++;
    }

    RelayLog("[SkyrimCoopHooks] Created %u hooks (%s mode)\n",
             installedCount, aEssentialOnly ? "essential-only" : "full");

    // Step 4: Enable all hooks at once
    status = MH_EnableHook(MH_ALL_HOOKS);
    if (status != MH_OK)
    {
        RelayLog("[SkyrimCoopHooks] MH_EnableHook(MH_ALL_HOOKS) failed: %d\n", static_cast<int>(status));
        MH_Uninitialize();
        FreeAddressLibrary();
        return false;
    }

    RelayLog("[SkyrimCoopHooks] All hooks enabled successfully\n");
    return true;
}

void RemoveAllHooks()
{
    RelayLog("[SkyrimCoopHooks] Removing all hooks...\n");
    MH_DisableHook(MH_ALL_HOOKS);
    MH_Uninitialize();
    FreeAddressLibrary();
    RelayLog("[SkyrimCoopHooks] All hooks removed\n");
}
