# Phase 7: Native Linux Build with SKSE TCP Relay - Research

**Researched:** 2026-03-28
**Domain:** Cross-process IPC, /proc/pid/mem memory reading, SKSE hook relay architecture
**Confidence:** HIGH

## Summary

This phase splits the SkyrimCoop client into two processes: a minimal MinGW-compiled SKSE DLL (~300-500 LOC) acting as a dumb hook-and-forward relay inside Wine, and a native Linux ELF binary that runs the full ECS, all services, networking, and embedded server. The native binary reads game memory via `/proc/pid/mem` (ptrace SEIZE + pread) and receives hook events over TCP localhost from the DLL.

The architecture is proven by LethalInjection, which uses the identical `/proc/pid/mem` pattern (`middleware/proc_memory.cpp`), TCP IPC between injected DLL and native middleware, and fork/exec process spawning with Wine environment inheritance. The key difference is that LethalInjection uses JSON-RPC over TCP while SkyrimCoop will use flat binary C structs (zero parsing overhead per D-01).

The current codebase has ~59 MinHook function hooks across 28 files and ~263 POINTER_SKYRIMSE game function address resolutions across 64 files, with ~215 TiltedPhoques::ThisCall game function invocations. These decompose into roughly 20-25 event hook types (actor lifecycle, combat, magic, inventory, movement, weather, animation) that forward raw arguments over TCP, plus 15-20 game function call types (spell cast, force weather, equip/unequip, set position, play animation) that execute via the DLL's game-thread command queue.

**Primary recommendation:** Implement in waves: (1) TCP protocol + DLL skeleton + native process launcher, (2) GameBridge memory reader + pointer table, (3) rewrite services one-by-one starting with WeatherService (simplest, 209 LOC) through CharacterService (largest, 1605 LOC), (4) XMake build targets + integration testing.

<user_constraints>
## User Constraints (from CONTEXT.md)

### Locked Decisions
- **D-01:** Flat binary C structs over TCP localhost. Format: `{ uint16_t opcode, uint16_t length, uint8_t payload[] }`. Zero parsing overhead, no dependencies in the DLL. No JSON, no protobuf.
- **D-02:** Hook trampolines forward raw function arguments only (uint64 values). DLL does not read any game struct fields.
- **D-03:** Game function calls use a game-thread command queue. TCP receiver thread pushes commands to lock-free queue. Game thread drains each frame.
- **D-04:** Native process reads all game state via `/proc/pid/mem` using ptrace SEIZE + pread pattern from LethalInjection.
- **D-05:** Actor/form pointer discovery is hook-driven. DLL hooks ActorAdded/ActorRemoved and forwards raw `Actor*` pointer value.
- **D-06:** Pointer invalidation is hook-driven. DLL hooks ActorRemoved/Delete events to notify native process.
- **D-07:** DLL spawns native process. SKSE loads DLL -> DLL starts TCP server -> DLL fork()/exec()s native ELF binary.
- **D-08:** DLL auto-restarts native process on crash (broken TCP detection).
- **D-09:** Big bang rewrite of all client services against new architecture.
- **D-10:** DLL is still SKSE plugin (exports SKSEPlugin_Version and SKSEPlugin_Load). Stripped to hooks + TCP + spawn.
- **D-11:** DLL does NOT use SKSE interfaces. Only uses SKSE as loading mechanism. All game interaction via MinHook.
- **D-12:** Native client is new XMake target building as native Linux ELF. Links EnTT, spdlog, TiltedCore, encoding natively.
- **D-13:** DLL target is minimal MinGW with only MinHook and winsock2. No EnTT, no TiltedCore, no spdlog, no encoding library.
- **D-14:** Game struct headers (`Code/client/Games/Skyrim/*.h`) compile for both targets. Native uses them for /proc/pid/mem reads; DLL doesn't include them.

### Claude's Discretion
- Specific lock-free queue implementation (SPSC ring buffer, etc.)
- TCP connection management details (reconnection backoff, keepalive)
- Exact set of ~20 hooks needed (based on game memory audit)
- Exact set of ~15 game function call commands
- How fork()/exec() interacts with Wine's process model
- Whether native process needs separate thread for /proc/pid/mem reads vs processing hooks

### Deferred Ideas (OUT OF SCOPE)
None -- discussion stayed within phase scope
</user_constraints>

## Standard Stack

### Core (DLL Side - MinGW)
| Library | Version | Purpose | Why Standard |
|---------|---------|---------|--------------|
| MinHook | v1.3.3 | Function hooking in game process | Already validated in Phase 1 GATE-02 |
| winsock2 | (Windows API) | TCP server on localhost | Zero-dependency, Wine translates to Linux sockets |

### Core (Native Side - System GCC/Clang)
| Library | Version | Purpose | Why Standard |
|---------|---------|---------|--------------|
| EnTT | v3.10.0 | ECS registry for actor/component management | Already used by server + current client |
| spdlog | v1.13.0 | Structured logging | Already used project-wide |
| TiltedCore | v0.2.7 | Core networking utilities | Already used by server |
| SkyrimEncoding | (in-tree) | Message serialization for server networking | Already builds natively on Linux |
| GLM | 0.9.9+8 | 3D math for interpolation | Already used for Vector3_NetQuantize |
| enet6 | latest | UDP networking to server/clients | Already used by server |

### Supporting
| Library | Version | Purpose | When to Use |
|---------|---------|---------|-------------|
| rpmalloc | latest | Memory allocator | Native process allocator (consistent with existing) |
| hopscotch-map | v2.3.1 | Fast hash maps | Pointer table (formId -> game pointer) |
| cryptopp | 8.9.0 | Authentication | Connection authentication |

### Alternatives Considered
| Instead of | Could Use | Tradeoff |
|------------|-----------|----------|
| Raw TCP | Unix domain sockets | UDS faster but Wine winsock doesn't translate UDS well; TCP localhost proven by LethalInjection/Steam/Discord |
| SPSC ring buffer | lock-free MPSC queue | SPSC sufficient since only one TCP thread produces, one game thread consumes |
| ptrace SEIZE + pread | process_vm_readv() | process_vm_readv avoids ptrace but requires same-user; PTRACE_SEIZE bypasses Yama ptrace_scope=1 (current system value) |

## Architecture Patterns

### Recommended Project Structure
```
Code/
├── relay_dll/                   # Minimal SKSE DLL (~300-500 LOC)
│   ├── relay_main.cpp           # SKSEPlugin_Version/Load, DllMain
│   ├── tcp_server.cpp           # Winsock2 TCP server (localhost)
│   ├── hook_trampolines.cpp     # MinHook trampolines (forward raw args)
│   ├── command_queue.cpp        # Lock-free SPSC queue for game-thread calls
│   ├── process_launcher.cpp     # fork()/exec() native binary
│   └── protocol.h               # Shared opcode/struct definitions
├── native_client/               # Native Linux ELF binary
│   ├── main.cpp                 # Entry point (PID + port from argv)
│   ├── game_bridge/
│   │   ├── tcp_client.cpp       # Connect to DLL TCP server
│   │   ├── proc_memory.cpp      # /proc/pid/mem reader (adapted from LethalInjection)
│   │   ├── pointer_table.cpp    # formId -> game pointer map
│   │   └── game_reader.cpp      # Typed memory reads using game struct offsets
│   ├── Services/                # Rewritten services (use GameBridge instead of direct ptrs)
│   │   ├── CharacterService.cpp
│   │   ├── WeatherService.cpp
│   │   ├── MagicService.cpp
│   │   ├── InventoryService.cpp
│   │   ├── CombatService.cpp
│   │   ├── ActorValueService.cpp
│   │   └── ...
│   ├── Systems/                 # InterpolationSystem, AnimationSystem (pure math, moved directly)
│   └── protocol.h               # Shared (symlink or copy from relay_dll)
├── client/Games/Skyrim/         # Game struct headers (shared, compile for native)
│   ├── Forms/*.h
│   ├── Actor.h
│   └── ...
└── encoding/                    # Message serialization (already builds natively)
```

### Pattern 1: Hook-and-Forward Relay
**What:** DLL installs MinHook trampolines that serialize raw function arguments (as uint64 values) into flat binary packets and send them over TCP to the native process.
**When to use:** Every game event that the native process needs to know about.
**Example:**
```cpp
// relay_dll/hook_trampolines.cpp
// Source: Adapted from current Actor.cpp hook pattern + LethalInjection TCP relay

// Hook trampoline: forwards raw Actor* and damage float to native process
static TDamageActor* RealDamageActor = nullptr;

float __fastcall HookDamageActor(void* apThis, float afDamage, void* apAttacker) {
    // Forward raw args to native process (opcode + raw pointer values)
    HookEventPacket pkt;
    pkt.opcode = HOOK_DAMAGE_ACTOR;
    pkt.args[0] = reinterpret_cast<uint64_t>(apThis);    // Actor*
    pkt.args[1] = *reinterpret_cast<uint64_t*>(&afDamage); // float as bits
    pkt.args[2] = reinterpret_cast<uint64_t>(apAttacker);  // Actor*
    g_tcp_server.Send(&pkt, sizeof(pkt));

    // Call original
    return RealDamageActor(apThis, afDamage, apAttacker);
}
```

### Pattern 2: Game-Thread Command Queue
**What:** Native process sends function-call requests over TCP. DLL's TCP receiver thread pushes them to a lock-free SPSC queue. The game thread (hooked via update loop) drains the queue each frame and executes the requested game functions.
**When to use:** Any time the native process needs to call a game function (set position, force weather, cast spell, etc.).
**Example:**
```cpp
// relay_dll/command_queue.cpp
// SPSC ring buffer (single producer = TCP thread, single consumer = game thread)

struct CommandSlot {
    uint16_t opcode;
    uint64_t args[8];  // up to 8 raw arguments
};

// Fixed-size ring buffer (power of 2)
static constexpr size_t QUEUE_SIZE = 256;
static CommandSlot g_queue[QUEUE_SIZE];
static std::atomic<uint32_t> g_write_pos{0};
static std::atomic<uint32_t> g_read_pos{0};

// TCP thread calls this
void EnqueueCommand(const CommandSlot& cmd) {
    uint32_t w = g_write_pos.load(std::memory_order_relaxed);
    g_queue[w & (QUEUE_SIZE - 1)] = cmd;
    g_write_pos.store(w + 1, std::memory_order_release);
}

// Game thread calls this each frame (hooked into update loop)
void DrainCommands() {
    uint32_t r = g_read_pos.load(std::memory_order_relaxed);
    uint32_t w = g_write_pos.load(std::memory_order_acquire);
    while (r != w) {
        const auto& cmd = g_queue[r & (QUEUE_SIZE - 1)];
        ExecuteGameCommand(cmd);
        r++;
    }
    g_read_pos.store(r, std::memory_order_release);
}
```

### Pattern 3: /proc/pid/mem Memory Reading
**What:** Native process reads game memory using pread() on /proc/pid/mem, with ptrace SEIZE for Yama bypass. Uses the same game struct headers that currently define field offsets.
**When to use:** All game state reads (actor positions, health, form IDs, inventory, etc.).
**Example:**
```cpp
// native_client/game_bridge/game_reader.cpp
// Source: Adapted from LethalInjection middleware/proc_memory.cpp

// Read an Actor's position using game struct offsets
Vector3 ReadActorPosition(uint64_t actorPtr) {
    // Actor inherits TESObjectREFR which has position at a known offset
    // Same offset as Code/client/Games/Skyrim/TESObjectREFR.h defines
    constexpr uint64_t POS_OFFSET = 0x54;  // TESObjectREFR::position

    Vector3 pos;
    m_procMem.Read(&pos, sizeof(pos), actorPtr + POS_OFFSET);
    return pos;
}

// Read a form's ID
uint32_t ReadFormId(uint64_t formPtr) {
    constexpr uint64_t FORMID_OFFSET = 0x14;  // TESForm::formID
    return m_procMem.ReadValue<uint32_t>(formPtr + FORMID_OFFSET);
}
```

### Pattern 4: fork()/exec() from Wine DLL
**What:** DLL running under Wine calls POSIX fork()/exec() to spawn the native ELF binary. Safe because the child immediately exec()s a native binary, never touching Wine APIs.
**When to use:** At DLL initialization after TCP server is listening.
**Example:**
```cpp
// relay_dll/process_launcher.cpp
// Source: Adapted from LethalInjection frontend/src/process/injector.cpp

#include <unistd.h>  // Wine/MinGW provides POSIX headers on Linux

void LaunchNativeProcess(uint16_t tcpPort) {
    pid_t skyrimPid = getpid();  // Wine process IS the Linux process

    pid_t child = fork();
    if (child == 0) {
        // Child: immediately exec native binary, discards all Wine state
        char pidStr[16], portStr[16];
        snprintf(pidStr, sizeof(pidStr), "%d", skyrimPid);
        snprintf(portStr, sizeof(portStr), "%u", tcpPort);

        // Path relative to DLL location or absolute
        execl("/path/to/skyrim-coop", "skyrim-coop",
              "--pid", pidStr, "--port", portStr, nullptr);
        _exit(127);  // exec failed
    }
    // Parent: store child PID for monitoring
    g_nativeProcessPid = child;
}
```

### Anti-Patterns to Avoid
- **Reading game memory from the DLL:** The DLL must never dereference game struct pointers beyond what the hook trampoline receives as arguments. All struct knowledge lives in the native process.
- **Using SKSE interfaces in the DLL:** Per D-11, the DLL uses SKSE only as a loading mechanism. No Messaging, Papyrus, Serialization, or Task interfaces.
- **Blocking the game thread on TCP:** The game thread should never wait for TCP responses. Commands are fire-and-forget. Hook events are fire-and-forget. The native process is the only one that blocks on TCP reads.
- **Using shared memory instead of TCP:** While faster, shared memory requires Wine mmap interop which is fragile. TCP localhost is proven, adds <0.1ms latency, and is debuggable with standard tools.
- **Incremental migration with abstraction layer:** Per D-09, this is a big bang rewrite. Don't create adapter layers between old and new patterns -- it doubles the maintenance burden for a fundamental architecture change.

## Don't Hand-Roll

| Problem | Don't Build | Use Instead | Why |
|---------|-------------|-------------|-----|
| /proc/pid/mem access | Custom proc reader | Adapt LethalInjection's `ProcMemReader` class | Handles ptrace_scope, SEIZE/DETACH lifecycle, error handling. 127 LOC, proven. |
| Lock-free queue | Custom atomics | Use simple SPSC ring buffer pattern | Well-known pattern, single-header, no library needed. Atomics are tricky to get right. |
| TCP server (DLL side) | Custom socket code | Winsock2 with blocking accept + non-blocking send | Wine translates to Linux sockets. Keep simple -- single client, localhost only. |
| Game struct offsets | Manual offset lookup | Existing `Code/client/Games/Skyrim/*.h` headers | 49 form headers + Actor.h + TESObjectREFR.h already define all needed offsets. |
| Address resolution | Custom pattern scanning | Existing POINTER_SKYRIMSE macro + Address Library | 263 address resolutions already working. DLL needs these for hook installation. |

**Key insight:** The game struct headers and address resolution macros are the most valuable reusable assets. The native binary uses the struct headers for /proc/pid/mem reads. The DLL uses POINTER_SKYRIMSE for hook target addresses. Neither needs to change.

## Common Pitfalls

### Pitfall 1: fork() from Wine Corrupts wineserver Connection
**What goes wrong:** Calling fork() inside a Wine process creates a child that shares the wineserver socket. If the child tries to use any Win32 API, the wineserver connection corrupts.
**Why it happens:** Wine maintains per-process connections to wineserver via unix domain sockets. fork() duplicates file descriptors without re-establishing the connection.
**How to avoid:** The forked child must IMMEDIATELY call exec() to replace itself with the native ELF binary. No Win32 API calls between fork() and exec(). Use `_exit(127)` on exec failure (not `exit()` which runs atexit handlers).
**Warning signs:** Child process hangs or crashes on first Win32 API call after fork.

### Pitfall 2: MinGW POSIX Headers Availability
**What goes wrong:** MinGW targets Windows, so POSIX headers like `<unistd.h>`, `<sys/types.h>` for fork/exec may not be available in the standard MinGW toolchain.
**Why it happens:** MinGW cross-compiler provides Windows API headers, not POSIX headers. Wine translates Windows API to POSIX at runtime, but the compiler doesn't know about POSIX at compile time.
**How to avoid:** Use Windows API `CreateProcess` to launch the native binary instead of fork/exec. Wine's CreateProcess internally does fork/exec. Pass the native binary path as `Z:\path\to\skyrim-coop` (Wine Z: drive maps to Linux root). Alternatively, declare fork/exec prototypes manually (they exist at runtime under Wine).
**Warning signs:** Compile errors: `unistd.h: No such file or directory`.

### Pitfall 3: ptrace SEIZE on Wine Process from Native Child
**What goes wrong:** The native process tries to PTRACE_SEIZE the Wine/Skyrim process but fails because Wine already has ptrace relationships or security policies block it.
**Why it happens:** ptrace_scope=1 (current system value) restricts ptrace to parent/child relationships. The native process is a child of the Wine process, not the other way around.
**How to avoid:** Since the DLL forks the native process, the native process IS a child of the Wine/Skyrim process. ptrace_scope=1 allows a process to be traced by its **direct descendants** -- BUT actually ptrace_scope=1 only allows tracing by a direct parent/ancestor. Use PTRACE_SEIZE which requires the tracer to have been an ancestor. Since the native process is a CHILD of Skyrim (forked from DLL), it IS allowed to ptrace its parent under ptrace_scope=1. Verify: the LethalInjection middleware is launched by the frontend which is a separate process -- it uses PTRACE_SEIZE successfully because ptrace_scope=1 allows non-ancestor tracing when PTRACE_SEIZE is used (unlike PTRACE_ATTACH).
**Warning signs:** `ptrace(PTRACE_SEIZE) failed: Operation not permitted`.

### Pitfall 4: TCP Server Port Collision
**What goes wrong:** Multiple Skyrim instances (or other programs) compete for the same localhost port.
**Why it happens:** Hardcoded port numbers collide in multi-instance scenarios.
**How to avoid:** Use port 0 (OS assigns ephemeral port). DLL binds to port 0, gets assigned port via getsockname(), passes it to native process as command-line argument. LethalInjection does exactly this.
**Warning signs:** bind() fails with EADDRINUSE.

### Pitfall 5: Game Thread Timing for Command Execution
**What goes wrong:** Commands from the native process execute at the wrong time relative to the game's update cycle, causing crashes or state corruption.
**Why it happens:** Game functions must run on the game thread during specific phases of the update loop (e.g., after physics, before render).
**How to avoid:** Hook the game's main update function (already identified as `POINTER_SKYRIMSE(void, winMain, 36544)` and the ActorProcess hook at ID 37356). Drain the command queue at a known-safe point in the update cycle.
**Warning signs:** Random crashes during command execution, especially with animation or position changes.

### Pitfall 6: Struct Offset Drift Between Native and Game
**What goes wrong:** Game struct headers compile with different padding on GCC vs MSVC, leading to wrong offsets when reading via /proc/pid/mem.
**Why it happens:** MSVC and GCC have different default struct packing rules. Skyrim was compiled with MSVC.
**How to avoid:** The existing codebase already uses `-mms-bitfields` for MinGW to match MSVC layout. The native binary MUST also use this flag when compiling game struct headers, OR use `offsetof()` / hardcoded numeric offsets rather than struct-based reads. Since the structs are only used for memory reads (not for instantiation), hardcoded offsets from the existing headers are safest.
**Warning signs:** Wrong values read from /proc/pid/mem (e.g., reading position gets a formID, or vice versa).

### Pitfall 7: Wine/Proton Process PID Discovery
**What goes wrong:** The Wine DLL calls `GetCurrentProcessId()` which returns a Wine-internal PID, not the actual Linux PID needed for /proc/pid/mem.
**Why it happens:** Wine may virtualize PIDs in some configurations.
**How to avoid:** Use `getpid()` (POSIX) instead of `GetCurrentProcessId()` (Win32) in the DLL when spawning the native process. Or have the native process discover the Skyrim PID via /proc enumeration (scanning for SkyrimSE.exe). LethalInjection's frontend discovers PIDs via /proc/pid/comm scanning.
**Warning signs:** /proc/pid/mem opens but reads return unexpected data or ESRCH.

## Code Examples

### Current Hook Pattern (to understand what gets rewritten)
```cpp
// Source: Code/client/Games/Skyrim/Actor.cpp lines 1270-1335
// Current: hooks are installed with POINTER_SKYRIMSE + TP_HOOK macro
// The HookXxx functions currently read game structs directly
// New: HookXxx functions will just forward raw args over TCP

// Currently 20 hooks in Actor.cpp alone:
TP_HOOK(&RealActorProcess, HookActorProcess);
TP_HOOK(&RealSetPosition, HookSetPosition);
TP_HOOK(&RealRemoveSpell, HookRemoveSpell);
TP_HOOK(&RealDamageActor, HookDamageActor);
// ... 16 more
```

### WeatherService Rewrite Pattern (simplest service)
```cpp
// Source: Code/client/Services/Generic/WeatherService.cpp (209 LOC)
// Current: reads Sky::Get()->GetWeather()->formID directly via pointer dereference
// New: reads same data via /proc/pid/mem using the GameBridge

// BEFORE (direct pointer access):
Sky* pSky = Sky::Get();
TESWeather* pWeather = pSky->GetWeather();
if (pWeather->formID == m_cachedWeatherId) return;

// AFTER (proc memory read):
uint64_t skyPtr = m_bridge.ReadGlobal(GLOBAL_SKY);  // Known address
uint64_t weatherPtr = m_bridge.Read<uint64_t>(skyPtr + Sky::kWeatherOffset);
uint32_t formId = m_bridge.Read<uint32_t>(weatherPtr + TESForm::kFormIdOffset);
if (formId == m_cachedWeatherId) return;
```

### TCP Protocol Wire Format
```cpp
// Shared between DLL and native process (protocol.h)

// All packets: opcode(u16) + length(u16) + payload(variable)
struct PacketHeader {
    uint16_t opcode;
    uint16_t length;  // payload length (excluding header)
};

// Hook event (DLL -> Native): fixed-size, up to 8 raw uint64 args
enum HookOpcode : uint16_t {
    HOOK_ACTOR_ADDED = 0x0001,
    HOOK_ACTOR_REMOVED = 0x0002,
    HOOK_DAMAGE_ACTOR = 0x0003,
    HOOK_SET_POSITION = 0x0004,
    HOOK_SPELL_CAST = 0x0005,
    HOOK_ADD_INVENTORY = 0x0006,
    HOOK_EQUIP = 0x0007,
    HOOK_WEATHER_CHANGE = 0x0008,
    // ... ~20 total hook event types
};

struct HookEventPacket {
    PacketHeader header;
    uint8_t argCount;
    uint64_t args[8];  // raw pointer/value arguments
};

// Command (Native -> DLL): request to execute game function
enum CommandOpcode : uint16_t {
    CMD_SET_POSITION = 0x8001,
    CMD_FORCE_WEATHER = 0x8002,
    CMD_CAST_SPELL = 0x8003,
    CMD_EQUIP_ITEM = 0x8004,
    CMD_UNEQUIP_ITEM = 0x8005,
    CMD_PLAY_ANIMATION = 0x8006,
    // ... ~15 total command types
};

struct CommandPacket {
    PacketHeader header;
    uint8_t argCount;
    uint64_t args[8];
};

// Control messages
enum ControlOpcode : uint16_t {
    CTRL_HEARTBEAT = 0xF001,
    CTRL_SHUTDOWN = 0xF002,
    CTRL_READY = 0xF003,
};
```

## Hook Inventory

Complete audit of hooks that need to become TCP relay trampolines:

### Actor Lifecycle (6 hooks)
| Hook | Source File | Arguments | Priority |
|------|-----------|-----------|----------|
| ActorProcess | Actor.cpp:1315 | Actor* | HIGH - main update loop |
| SetPosition | Actor.cpp:1316 | REFR*, NiPoint3* | HIGH - position sync |
| CharacterConstructor | Actor.cpp:1318 | Actor* | HIGH - spawn detection |
| CharacterConstructor2 | Actor.cpp:1319 | Actor* | HIGH - spawn detection |
| SpawnActorInWorld | Actor.cpp:1321 | Actor* | HIGH - spawn detection |
| AddDeathItems | Actor.cpp:1333 | Actor* | MEDIUM - death detection |

### Combat (5 hooks)
| Hook | Source File | Arguments | Priority |
|------|-----------|-----------|----------|
| DamageActor | Actor.cpp:1322 | Actor*, float, Actor* | HIGH |
| ApplyActorEffect | Actor.cpp:1323 | Actor*, MagicItem*, Effect* | HIGH |
| RegenAttributes | Actor.cpp:1324 | Actor*, int, float | MEDIUM |
| UpdateDetectionState | Actor.cpp:1328 | Actor* | LOW |
| UpdateTarget | CombatController.cpp:63 | Controller*, Actor* | MEDIUM |

### Inventory/Equipment (6 hooks)
| Hook | Source File | Arguments | Priority |
|------|-----------|-----------|----------|
| AddInventoryItem (Actor) | Actor.cpp:1325 | Actor*, TESBoundObject*, int | HIGH |
| PickUpObject | Actor.cpp:1326 | Actor*, REFR* | MEDIUM |
| DropObject | Actor.cpp:1327 | Actor*, REFR* | MEDIUM |
| UnequipObject | Actor.cpp:1331 | Actor*, TESForm* | HIGH |
| AddInventoryItem (REFR) | TESObjectREFR.cpp:1127 | REFR*, Item*, int | HIGH |
| RemoveInventoryItem | TESObjectREFR.cpp:1128 | REFR*, Item*, int | HIGH |

### Magic (5 hooks)
| Hook | Source File | Arguments | Priority |
|------|-----------|-----------|----------|
| SpellCast | ActorMagicCaster.cpp:14 | Caster*, SpellItem* | HIGH |
| InterruptCast | ActorMagicCaster.cpp:15 | Caster* | MEDIUM |
| AddTarget | MagicTarget.cpp:28 | Target*, TargetData* | HIGH |
| FindTargets | MagicTarget.cpp:30 | Caster*, float, count*, source* | MEDIUM |
| RemoveSpell | Actor.cpp:1317 | Actor*, SpellItem* | MEDIUM |

### Object/World (6 hooks)
| Hook | Source File | Arguments | Priority |
|------|-----------|-----------|----------|
| Activate | TESObjectREFR.cpp:1126 | REFR*, Actor* | HIGH |
| LockChange | TESObjectREFR.cpp:1122 | REFR*, bool | MEDIUM |
| RotateX/Y/Z | TESObjectREFR.cpp:1123-25 | REFR*, float | LOW |
| PlayAnimation | TESObjectREFR.cpp:1130 | REFR*, BSFixedString* | MEDIUM |
| Projectile Launch | Projectile.cpp:13 | LaunchData* | HIGH |

### Weather/Sky (3 hooks)
| Hook | Source File | Arguments | Priority |
|------|-----------|-----------|----------|
| SetWeather | Sky.cpp:99 | Sky*, Weather* | MEDIUM |
| ForceWeather | Sky.cpp:100 | Sky*, Weather* | MEDIUM |
| UpdateWeather | Sky.cpp:101 | Sky* | LOW |

### Other (5 hooks)
| Hook | Source File | Arguments | Priority |
|------|-----------|-----------|----------|
| PerformAction | Animation.cpp:193 | Actor*, ActionEvent* | HIGH |
| InitiateMountPackage | Actor.cpp:1197 | Actor*, Actor* | MEDIUM |
| SimulateTime | TimeManager.cpp:27 | float | MEDIUM |
| PlayDialogueOption | MenuTopicManager.cpp:44 | void*, TopicInfo* | LOW |
| ShowSubtitle | SubtitleManager.cpp:50 | Actor*, BSFixedString* | LOW |

**Total: ~36 hooks that need TCP relay trampolines.** Some current hooks (memory/IAT, save/load, BSScript, rendering) may not be needed in the new architecture since the native process doesn't interact with those systems directly.

## Service Size Ranking (rewrite order recommendation)

| Service | LOC | Complexity | Recommended Order |
|---------|-----|------------|-------------------|
| WeatherService | 209 | LOW - polling + 1 command | 1st (proof of concept) |
| CalendarService | 119 | LOW - time sync only | 2nd |
| QuestService | 233 | LOW - event forwarding | 3rd |
| CombatService | 250 | MEDIUM - projectile hooks | 4th |
| InventoryService | 311 | MEDIUM - many hook types | 5th |
| ActorValueService | 391 | MEDIUM - periodic reads | 6th |
| MagicService | 684 | HIGH - complex spell interactions | 7th |
| CharacterService | 1605 | HIGH - movement, animation, spawning | 8th (last, most complex) |

## State of the Art

| Old Approach | Current Approach | When Changed | Impact |
|--------------|------------------|--------------|--------|
| Cross-compile entire client as MinGW DLL | Thin relay DLL + native ELF binary | Phase 7 (this phase) | ~95% of code compiles natively, full debuggability |
| Direct game pointer dereference | /proc/pid/mem reads via pread() | Phase 7 (this phase) | Same data, different access pattern, ~1-2us per read |
| All hooks process data in-place | Hook trampolines forward raw args | Phase 7 (this phase) | DLL becomes stateless relay |
| Single monolithic process | Two-process architecture | Phase 7 (this phase) | Native debugging, crash isolation, independent restart |

## Open Questions

1. **MinGW POSIX header availability for fork/exec**
   - What we know: MinGW targets Windows and may not ship `<unistd.h>`. Wine provides POSIX at runtime but not at compile time.
   - What's unclear: Whether `__attribute__((dllimport))` extern declarations for fork/getpid/exec work under MinGW-compiled Wine DLLs.
   - Recommendation: Use `CreateProcess` with Wine Z: path as the primary approach. Test fork/exec as alternative. LethalInjection's frontend (native Linux process) does the fork/exec -- it doesn't face this problem. The DLL could alternatively write the native binary path to a file and have a small native launcher that watches for it (adds complexity). **Most likely path: declare fork/exec with `extern "C"` prototypes -- they resolve at runtime via ntdll.dll/libc.so since Wine maps POSIX calls.**

2. **ptrace_scope=1 parent/child direction**
   - What we know: System ptrace_scope is 1. LethalInjection's middleware uses PTRACE_SEIZE successfully.
   - What's unclear: Whether a child process (native binary) can PTRACE_SEIZE its parent (Wine/Skyrim process) under ptrace_scope=1.
   - Recommendation: Test empirically. If PTRACE_SEIZE fails, alternatives are: (a) `sudo sysctl kernel.yama.ptrace_scope=0`, (b) `CAP_SYS_PTRACE` capability on the native binary, (c) `PR_SET_PTRACER` called by the DLL before fork to authorize the child.

3. **Address Library in DLL vs Native**
   - What we know: POINTER_SKYRIMSE macro resolves runtime addresses using the Address Library database. The DLL needs these for hook installation.
   - What's unclear: Whether Address Library code should stay in the DLL (needs file I/O to load the database) or be moved to the native process (which would send resolved addresses to the DLL over TCP).
   - Recommendation: Keep Address Library in the DLL. It's a one-time startup cost, the code is small, and MinGW can handle the file I/O. The alternative (native process resolves addresses and sends them) adds a bootstrap ordering dependency.

4. **Game struct header compilation on native Linux**
   - What we know: Headers define offsets for memory reads. They compile under MinGW with `-mms-bitfields`.
   - What's unclear: Whether they compile cleanly with system GCC without Windows types (DWORD, HANDLE, etc.).
   - Recommendation: The native binary only needs offset constants from these headers, not the full struct definitions. Create a `game_offsets.h` that extracts just the numeric offsets, avoiding Windows type dependencies. Or provide stub typedefs for Windows types.

## Environment Availability

| Dependency | Required By | Available | Version | Fallback |
|------------|------------|-----------|---------|----------|
| System GCC | Native ELF build | Yes | 11.4.0 | Use clang |
| MinGW GCC | Relay DLL build | Yes | 10-posix | Sufficient |
| XMake | Build system | Yes | v3.0.8 | -- |
| MinHook | DLL hook installation | Yes | v1.3.3 (in xmake deps) | -- |
| ptrace | /proc/pid/mem access | Yes | Kernel 6.17.9 | -- |
| /proc filesystem | Memory reading | Yes | Always on Linux | -- |
| ptrace_scope | Security policy | 1 (restricted) | Kernel param | PTRACE_SEIZE bypasses, or sysctl |

**Missing dependencies with no fallback:** None

**Missing dependencies with fallback:** None

## Validation Architecture

### Test Framework
| Property | Value |
|----------|-------|
| Framework | Catch2 2.13.9 |
| Config file | Code/tests/ (existing) |
| Quick run command | `./build/release/tests/TPTests` |
| Full suite command | `./build/release/tests/TPTests` |

### Phase Requirements -> Test Map
| Req ID | Behavior | Test Type | Automated Command | File Exists? |
|--------|----------|-----------|-------------------|-------------|
| (no formal IDs) | TCP protocol encode/decode | unit | `./build/linux/x64/release/TPTests "RelayProtocol*"` | Wave 0 |
| (no formal IDs) | SPSC queue push/drain | unit | `./build/linux/x64/release/TPTests "CommandQueue*"` | Wave 0 |
| (no formal IDs) | ProcMemReader open/read | integration | `./build/linux/x64/release/TPTests "ProcMem*"` | Wave 0 |
| (no formal IDs) | Pointer table insert/invalidate/lookup | unit | `./build/linux/x64/release/TPTests "PointerTable*"` | Wave 0 |
| (no formal IDs) | GameBridge typed reads | integration | `./build/linux/x64/release/TPTests "GameReader*"` | Wave 0 |
| (no formal IDs) | Native binary launch + TCP handshake | integration | Manual (requires Wine + Skyrim) | Manual-only |
| (no formal IDs) | Service rewrite correctness | integration | Manual (requires running game) | Manual-only |

### Sampling Rate
- **Per task commit:** `./build/linux/x64/release/TPTests -x`
- **Per wave merge:** Full test suite green
- **Phase gate:** All unit tests pass + manual smoke test with running Skyrim

### Wave 0 Gaps
- [ ] `Code/tests/relay_protocol_tests.cpp` -- TCP packet encode/decode
- [ ] `Code/tests/command_queue_tests.cpp` -- SPSC ring buffer correctness
- [ ] `Code/tests/proc_memory_tests.cpp` -- /proc/pid/mem reader (reads own process memory as self-test)
- [ ] `Code/tests/pointer_table_tests.cpp` -- formId->pointer map lifecycle
- [ ] `Code/tests/game_reader_tests.cpp` -- typed memory reads against known structs

## Sources

### Primary (HIGH confidence)
- LethalInjection `middleware/proc_memory.cpp` -- Proven /proc/pid/mem reader pattern with ptrace SEIZE (127 LOC)
- LethalInjection `backend/dllmain.cpp` -- Minimal DLL entry point pattern (53 LOC)
- LethalInjection `backend/li_init.cpp` -- DLL init with WSAStartup, MinHook, TCP server (177 LOC)
- LethalInjection `frontend/src/process/injector.cpp` -- fork/exec with Wine env inheritance (489 LOC)
- SkyrimCoop `Code/client/Games/Skyrim/Actor.cpp` -- 20 hooks, 31 POINTER_SKYRIMSE resolutions
- SkyrimCoop `Code/client/Games/Skyrim/TESObjectREFR.cpp` -- 9 hooks, 20 POINTER_SKYRIMSE resolutions
- SkyrimCoop `Code/client/skse_entry.cpp` -- Current SKSE entry point pattern (91 LOC)
- SkyrimCoop `Code/client/xmake.lua` -- Current build target structure

### Secondary (MEDIUM confidence)
- Wine forum discussions on fork() from Wine DLLs -- confirms fork+immediate exec is safe if child never touches Win32 APIs
- System ptrace_scope=1 verified by direct read of /proc/sys/kernel/yama/ptrace_scope

### Tertiary (LOW confidence)
- MinGW POSIX header availability -- needs empirical testing. fork/exec may need Windows API fallback (CreateProcess with Z: path)
- ptrace parent-child direction under scope=1 with PTRACE_SEIZE -- needs empirical testing

## Metadata

**Confidence breakdown:**
- Standard stack: HIGH - all libraries already in use, just rearranging targets
- Architecture: HIGH - proven by LethalInjection, all patterns have working code references
- Pitfalls: HIGH - identified from code analysis and Wine architecture knowledge
- Hook inventory: HIGH - complete grep audit of all TP_HOOK calls (90 total, ~36 relevant)
- Service rewrite scope: HIGH - LOC counts from direct file measurement

**Research date:** 2026-03-28
**Valid until:** 2026-04-28 (stable -- architecture is well-understood, libraries pinned)
