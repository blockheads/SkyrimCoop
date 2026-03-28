# Phase 7: Native Linux Build with SKSE TCP Relay - Context

**Gathered:** 2026-03-28
**Status:** Ready for planning

<domain>
## Phase Boundary

Split the SkyrimCoop client so ~95% compiles natively on Linux as an ELF binary. A minimal MinGW-compiled SKSE plugin (~300-500 LOC) handles only function hook installation, hook event forwarding, and game function call execution. The native Linux process reads all game state via `/proc/pid/mem` (ptrace + pread pattern from LethalInjection) and runs the full ECS, all services, networking, and embedded server. The DLL has zero game struct knowledge — it forwards raw pointer values and executes function calls on the game thread when commanded.

### Architecture

```
┌──────────────────────────────────────────┐
│ Skyrim SE (Wine/Proton)                  │
│                                          │
│  SKSE → skyrim_coop_hooks.dll            │
│         (~300-500 LOC, MinGW)            │
│         ├── Hook trampolines             │
│         │   (forward raw args over TCP)  │
│         ├── Game-thread command queue     │
│         │   (execute game funcs on req)  │
│         ├── TCP server (localhost)        │
│         └── Auto-spawns native process   │
│              ↕ TCP localhost              │
└──────────────┼───────────────────────────┘
               │
┌──────────────┼───────────────────────────┐
│ Native Linux │                           │
│              ↕                            │
│  skyrim-coop (native ELF binary)         │
│  ├── /proc/pid/mem reader (ptrace+pread) │
│  │   (ALL struct reads happen here)      │
│  ├── Game struct definitions             │
│  │   (Code/client/Games/Skyrim/*.h)      │
│  ├── Hook-driven pointer table           │
│  │   (formId → in-game pointer map)      │
│  ├── Full ECS (EnTT)                     │
│  ├── All services (Character, Magic,     │
│  │   Inventory, Combat, Weather, etc.)   │
│  ├── Interpolation/Animation math        │
│  ├── Server networking (UDP/ENet)        │
│  └── Embedded server (host mode)         │
└──────────────────────────────────────────┘
```

### Key Design Principle

The DLL is a **dumb hook-and-forward relay** — it has zero game struct knowledge. When a hook fires, it sends the raw function argument values (pointers as uint64). The native process uses `/proc/pid/mem` to read whatever it needs from those pointers. All struct offset knowledge, form parsing, and data interpretation lives in native debuggable code.

</domain>

<decisions>
## Implementation Decisions

### Hook/Command Protocol
- **D-01:** Flat binary C structs over TCP localhost. Format: `{ uint16_t opcode, uint16_t length, uint8_t payload[] }`. Zero parsing overhead, no dependencies in the DLL. No JSON, no protobuf — this is localhost IPC between two C++ processes.
- **D-02:** Hook trampolines forward **raw function arguments only** (uint64 values). The DLL does not read any game struct fields. The native process interprets pointer values via `/proc/pid/mem` reads using its own struct definitions.
- **D-03:** Game function calls use a **game-thread command queue**. The DLL hooks the game update loop. The TCP receiver thread pushes commands to a lock-free queue. The game thread drains the queue each frame and executes the requested function calls. Thread-safe, no blocking.

### Memory Reading Strategy
- **D-04:** The native process reads all game state via **`/proc/pid/mem`** using the ptrace + pread pattern proven in LethalInjection. `ptrace(PTRACE_SEIZE)` establishes tracer relationship (bypasses Yama ptrace_scope=1), then `pread()` on `/proc/pid/mem` fd reads game memory. ~1-2 microseconds per read.
- **D-05:** Actor/form pointer discovery is **hook-driven**. DLL hooks ActorAdded/ActorRemoved and forwards the raw `Actor*` pointer value. Native process maintains a `formId → pointer` map. All `/proc/pid/mem` reads use these known-valid pointers.
- **D-06:** Pointer invalidation is **hook-driven**. DLL hooks ActorRemoved/Delete events and notifies the native process to remove the pointer from its table. The native process never reads a stale pointer because it only reads pointers confirmed alive by the DLL.

### Process Lifecycle
- **D-07:** The **DLL spawns the native process**. SKSE loads DLL → DLL starts TCP server on localhost → DLL `fork()/exec()`s the native ELF binary with Skyrim's PID + TCP port as arguments. Single entry point, guaranteed ordering. Matches LethalInjection's pattern.
- **D-08:** If the native process crashes, the **DLL restarts it automatically**. DLL detects broken TCP connection, respawns the native process. Game keeps running, player reconnects seamlessly.

### Migration Strategy
- **D-09:** **Big bang rewrite** of all client services against the new architecture. Services are rewritten to use `/proc/pid/mem` reads + hook event reception instead of direct pointer access. No incremental migration or abstraction layer — clean break from the current cross-compiled architecture.

### SKSE Integration
- **D-10:** The DLL is still an SKSE plugin (exports `SKSEPlugin_Version` and `SKSEPlugin_Load`). The existing `skse_entry.cpp` pattern is reused but stripped to just hook installation + TCP server startup. No `RunTiltedInit`/`RunTiltedApp` — those move to the native binary.
- **D-11:** The DLL does NOT use SKSE interfaces (Messaging, Papyrus, Serialization, Task). It only uses SKSE as a loading mechanism. All game interaction is via direct MinHook function hooks.

### Build System
- **D-12:** The native client is a new XMake target that builds as a native Linux ELF binary using system GCC/Clang. It links against EnTT, spdlog, TiltedCore, and the encoding library — all compiled natively.
- **D-13:** The DLL target is a minimal MinGW cross-compiled target with no heavy dependencies. Only MinHook (for hooking) and winsock2 (for TCP). No EnTT, no TiltedCore, no spdlog, no encoding library.
- **D-14:** Game struct header files (`Code/client/Games/Skyrim/*.h`) compile for both targets — they're plain struct definitions with offset constants. The native binary uses them for `/proc/pid/mem` reads; the DLL doesn't include them at all.

### Claude's Discretion
- Specific lock-free queue implementation for the command queue (SPSC ring buffer, etc.)
- TCP connection management details (reconnection backoff, keepalive)
- Exact set of ~20 hooks needed (based on the game memory audit)
- Exact set of ~15 game function call commands
- How `fork()/exec()` interacts with Wine's process model (may need Wine-specific handling)
- Whether the native process needs a separate thread for `/proc/pid/mem` reads vs processing hooks

</decisions>

<canonical_refs>
## Canonical References

**Downstream agents MUST read these before planning or implementing.**

### LethalInjection (reference architecture)
- `/media/bighass/4d2c4781-34de-41a5-8939-e35551bc9a5d5/Projects/LethalInjection/tools/injector/main.cpp` — Windows PE injection pattern (CreateRemoteThread + LoadLibraryA)
- `/media/bighass/4d2c4781-34de-41a5-8939-e35551bc9a5d5/Projects/LethalInjection/middleware/proc_memory.cpp` — `/proc/pid/mem` reader implementation (ptrace SEIZE + pread)
- `/media/bighass/4d2c4781-34de-41a5-8939-e35551bc9a5d5/Projects/LethalInjection/frontend/src/process/injector.cpp` — Process spawning with Wine environment inheritance
- `/media/bighass/4d2c4781-34de-41a5-8939-e35551bc9a5d5/Projects/LethalInjection/backend/dllmain.cpp` — Minimal DLL entry point pattern
- `/media/bighass/4d2c4781-34de-41a5-8939-e35551bc9a5d5/Projects/LethalInjection/backend/li_init.cpp` — DLL initialization (TCP server, hook system startup)

### Current SkyrimCoop Client (to be rewritten)
- `Code/client/skse_entry.cpp` — Current SKSE plugin entry point (to be stripped down)
- `Code/client/Services/Generic/CharacterService.cpp` — Largest service, ~20 hooks + movement sync at 100ms
- `Code/client/Services/Generic/MagicService.cpp` — Spell casting hooks and command execution
- `Code/client/Services/Generic/InventoryService.cpp` — Inventory/equipment change hooks
- `Code/client/Services/Generic/CombatService.cpp` — Projectile launch hooks
- `Code/client/Services/Generic/WeatherService.cpp` — Weather sync (simplest service, good reference)
- `Code/client/Services/Generic/ActorValueService.cpp` — Health/attribute sync
- `Code/client/Systems/InterpolationSystem.h` — Movement interpolation math (moves to native)
- `Code/client/Systems/AnimationSystem.h` — Animation variable application (moves to native)
- `Code/client/Games/Skyrim/` — Game struct definitions (compile natively for /proc/pid/mem reads)

### Game Memory Audit
- Phase 7 discussion identified: ~20 event hook types, ~15 game function call types, ~20 state read categories
- Movement sync: 100ms tick rate (`CharacterService.cpp:1361`)
- Actor values: 250ms tick rate
- All other events: event-driven (no polling)

### Build System
- `xmake.lua` — Root build config (new native target + minimal DLL target)
- `Code/client/xmake.lua` — Current client target (to be split)

### Prior Phase Decisions
- `.planning/phases/03-client-dll-cross-compilation/03-CONTEXT.md` — HAS_CEF/HAS_DISCORD/HAS_DIRECTXTK guards, MinHook/Xbyak validation
- `.planning/phases/03.1-build-performance-resource-optimization/03.1-CONTEXT.md` — devbuild mode, lld, shared libs pattern

</canonical_refs>

<code_context>
## Existing Code Insights

### Reusable Assets
- `Code/client/Games/Skyrim/*.h` — Game struct definitions with field offsets, compile natively as-is
- `Code/client/Systems/InterpolationSystem.h` — Pure math, no game memory dependency, moves directly
- `Code/encoding/` — Message serialization library, already builds natively on Linux
- `Code/server/` — Full server code, already builds natively
- LethalInjection's `proc_memory.cpp` — Proven `/proc/pid/mem` reader pattern to adapt

### Established Patterns
- LethalInjection: TCP JSON-RPC between injected DLL and native middleware (we use flat binary instead)
- LethalInjection: ptrace SEIZE for Yama bypass + pread on /proc/pid/mem
- LethalInjection: DLL spawns middleware process with PID/port args
- SkyrimCoop: MinHook function hooking validated under MinGW (Phase 1 GATE-02)
- SkyrimCoop: SKSE plugin entry pattern in `skse_entry.cpp`

### Integration Points
- `skse_entry.cpp` — Stripped to hook installation + TCP server + native process spawn
- `xmake.lua` — New `skyrim-coop` native ELF target alongside minimal `skyrim_coop_hooks` DLL target
- `Code/client/Services/` — All services rewritten against new GameBridge API
- `Code/client/Games/Skyrim/*.h` — Shared between native and (potentially) DLL builds

</code_context>

<specifics>
## Specific Ideas

- The DLL should be so dumb that it's essentially a "game peripheral driver" — hook-and-forward, execute-on-command, nothing else.
- LethalInjection's proc_memory.cpp is the direct reference for the /proc/pid/mem pattern. Adapt it, don't reinvent.
- `pread()` on `/proc/pid/mem` is ~1-2 microseconds per call. Reading 50 actors at 100ms tick = ~0.1ms. Massive headroom.
- The game struct headers in `Code/client/Games/Skyrim/` are the key shared artifact — they define offsets used by the native process for memory reads.
- Wine's winsock translates TCP localhost to real Linux sockets — proven by Steam, Discord, and LethalInjection itself.
- Big bang rewrite chosen because the architecture change is fundamental — incremental migration would mean maintaining two incompatible patterns simultaneously.

</specifics>

<deferred>
## Deferred Ideas

None — discussion stayed within phase scope

</deferred>

---

*Phase: 07-native-linux-build-with-skse-tcp-relay*
*Context gathered: 2026-03-28*
