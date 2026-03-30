# Phase 15: Remote Actor Spawning & Game Integration - Context

**Gathered:** 2026-03-29
**Status:** Ready for planning

<domain>
## Phase Boundary

Bridge server CharacterSpawnRequest/NotifyRemoveCharacter/ServerReferencesMoveRequest messages to in-game NPC creation and position sync via relay DLL commands. When a remote player connects, the native client sends spawn/move/remove commands to the relay DLL, which executes the corresponding Skyrim game functions. Also includes INI auto-configuration (bAlwaysActive, windowed mode) in all launch scripts.

</domain>

<decisions>
## Implementation Decisions

### Core Architecture Principle
- **D-01:** The relay DLL must remain a **stateless thin shim** — it executes commands and reports results, but holds no maps, tracking state, or business logic. All actor identity tracking (serverId → pointer mapping) lives in the native process. This keeps the hardest-to-debug component (DLL inside Skyrim/Wine) trivially simple and puts complexity where it's testable (native Linux process).

### Spawn Mechanism
- **D-02:** Port actor spawning game function calls from STR's existing client code (`Code/client/Games/Skyrim/Actor.cpp`, `Code/client/Games/ModManager.cpp`) into the relay DLL's `DrainCommandQueue`. Reference both STR source and SKSE source (`/home/bighass/Downloads/skse64_2_02_06 (1)/src/skse64/`) for game function signatures, address library IDs, and calling conventions.
- **D-03:** Key functions to port: `TESForm::GetById()`, `Actor::New()`, `ModManager::Spawn()` (address library ID 13723 — `SpawnNewREFR`), and related setup calls (`SetSkipSaveFlag`, `SetBaseForm`, `SetParentCell`, `ForcePosition`).
- **D-04:** New `cmd_spawn_actor` JSON command: native sends `{op: "cmd_spawn_actor", form_id: uint32, x, y, z, rx, ry}`. DLL executes the spawn on game thread, then sends back a hook event with the spawned actor's pointer so the native process can track it for future commands.
- **D-05:** Appearance sync (FaceGen, TESNPC::Create, inventory, actor values) is **out of scope** for Phase 15. Remote players spawn with default NPC appearance for the given formId. Appearance sync is future work.

### Actor Lifecycle
- **D-06:** Immediate deletion on disconnect. Server sends `NotifyRemoveCharacter` → native sends `cmd_remove_actor {ptr}` → DLL deletes the actor from Skyrim immediately. No fade-out, no disable-instead-of-delete. Reference STR's `Actor::Delete()` for the correct game function calls.
- **D-07:** DLL reports spawn results back to native via a new hook event (e.g. `spawn_result` with actor pointer and formId). Native process maintains the serverId → actor pointer map and uses raw pointers in all subsequent commands.

### Move Command Routing
- **D-08:** Native process tracks serverId → actor pointer (populated from DLL spawn result events). All move commands use raw actor pointers: `cmd_set_position {ptr, x, y, z, rx, ry}`. This is consistent with the existing `CMD_SET_POSITION` approach.
- **D-09:** The existing `CMD_SET_POSITION` implementation in DrainCommandQueue already handles pointer-based position updates. It may need minor changes to work with the new JSON protocol path but the core logic stays.

### INI Auto-Configuration
- **D-10:** Each launch script handles its own INI patching inline. No shared helper — keep scripts self-contained.
- **D-11:** Minimal INI settings only: `bAlwaysActive=1` (game runs when unfocused) and `bFull Screen=0` with windowed resolution. No other INI tweaks (autosave, pause, starting cell, intro) — those are user preference.

### Claude's Discretion
- Exact address library IDs for Actor::New, TESForm::GetById, Actor::Delete, and other game functions (determined by reading STR source)
- How to port SKSE's task interface / game-thread execution pattern (reference SKSE source for clean game-thread callback approach vs current HookActorProcess piggyback)
- Whether cmd_spawn_actor creates Actor via Actor::Create(TESNPC*) or the simpler Actor::Spawn(formId) path
- JSON schema for the new spawn_result hook event
- Error handling when spawn fails (formId not found, cell not loaded, etc.)
- Which STR setup calls are strictly necessary for a functional remote actor vs nice-to-have (SetLevelMod, magic caster init, flags, etc.)
- DrainCommandQueue changes needed for new commands via JSON protocol path

</decisions>

<canonical_refs>
## Canonical References

**Downstream agents MUST read these before planning or implementing.**

### STR Actor Spawning (reference implementation)
- `Code/client/Games/Skyrim/Actor.cpp` — `Actor::Create(TESNPC*)` (line 175) and `Actor::Spawn(uint32_t)` (line 210). Shows full actor creation pipeline including SetSkipSaveFlag, SetRemote, SetLevelMod, ForcePosition, magic caster init.
- `Code/client/Games/ModManager.cpp` — `ModManager::Spawn()` (line 41) calls `SpawnNewREFR` (address lib ID 13723). `SpawnNewREFR` signature at line 31.
- `Code/client/Services/Generic/CharacterService.cpp` — `OnCharacterSpawn()` (line 350). Full handler for CharacterSpawnRequest with custom form support, appearance deserialization, WaitingFor3D pattern.

### SKSE Source (reference for game-thread execution)
- `/home/bighass/Downloads/skse64_2_02_06 (1)/src/skse64/` — Task interface implementation for clean game-thread callback execution. Reference for replacing HookActorProcess-based DrainCommandQueue.

### Current Relay DLL (modification target)
- `Code/relay_dll/relay_main.cpp` — `DrainCommandQueue()` (line 122). Currently only handles CMD_SET_POSITION. New CMD_SPAWN_ACTOR and CMD_REMOVE_ACTOR handlers go here.
- `Code/relay_dll/json_protocol.h` — JSON op constants. Needs new `cmd_spawn_actor`, `cmd_remove_actor` ops.
- `Code/relay_dll/protocol.h` — CommandOpcode enum and CommandSlot struct used by DrainCommandQueue.
- `Code/relay_dll/hook_trampolines.h` — Hook infrastructure and GetRealSetPosition(). New game function pointers for spawn/delete go here.

### Native Client (routing logic)
- `Code/native_client/transport/ServerMessageRouter.cpp` — Already decodes CharacterSpawnRequest (line 96), NotifyRemoveCharacter (line 108), and ServerReferencesMoveRequest (line 68) with callbacks.
- `Code/native_client/Services/CharacterService.cpp` — `OnAssignCharacter()` creates ECS entities. Needs to also send cmd_spawn_actor to DLL and track pointer from spawn_result event.
- `Code/native_client/main.cpp` — Wires ServerMessageRouter callbacks to CharacterService (lines 560-568 for host, 636-644 for join).

### Message Encoding
- `Code/encoding/Messages/CharacterSpawnRequest.h` — Spawn message fields (ServerId, FormId, Position, Rotation, AppearanceBuffer, etc.)
- `Code/encoding/Messages/NotifyRemoveCharacter.h` — Remove message (ServerId)
- `Code/encoding/Messages/ServerReferencesMoveRequest.h` — Move message (Updates map keyed by ServerId)

</canonical_refs>

<code_context>
## Existing Code Insights

### Reusable Assets
- `ServerMessageRouter` already decodes all relevant server messages (spawn, remove, move) with typed callbacks — no new message parsing needed
- `DrainCommandQueue` pattern with SPSC queue provides safe game-thread execution — extend with new command cases
- STR's `Actor::Create()` and `ModManager::Spawn()` are battle-tested spawning code with correct address library IDs
- JSON wire protocol (`json_protocol.h`) already defines command ops — just add new entries

### Established Patterns
- Commands flow: native process → JSON over TCP → DLL command queue → game thread execution
- Events flow: game hooks → DLL → JSON over TCP → native process services
- Address library ID resolution via `POINTER_SKYRIMSE` macro in STR source (e.g., `POINTER_SKYRIMSE(TSpawnNewREFR, s_realSpawnNewREFR, 13723)`)

### Integration Points
- `CharacterService::OnAssignCharacter()` — needs to send cmd_spawn_actor command to DLL after creating ECS entity
- `CharacterService::OnRemoveCharacter()` — needs to send cmd_remove_actor command to DLL
- `CharacterService::OnReferencesMoveRequest()` — needs to send cmd_set_position with tracked actor pointer
- DLL's TCP JSON handler — needs to parse new command ops and enqueue to CommandQueue
- New spawn_result event type needs handler in native process to capture actor pointers

</code_context>

<specifics>
## Specific Ideas

- User emphasized: the relay DLL is "the thinnest shim possible that just relays Skyrim's data directly over to our client" — any state or logic that can live in the native process MUST live there
- Reference both STR source (`Code/client/`) and SKSE source for any game function call patterns — don't reimplement from scratch when existing code shows exactly what to call
- The project is essentially porting SKSE-like functionality natively on Linux via the relay DLL + native process architecture, since SKSE plugins can't be cross-compiled with MinGW

</specifics>

<deferred>
## Deferred Ideas

- Appearance sync (FaceGen, TESNPC::Create from appearance buffer, actor values, inventory) — future phase
- SKSE task interface port (replace HookActorProcess piggyback with clean game-thread callback) — could be part of this phase at Claude's discretion or deferred
- Fade-out animation on actor removal — polish for later
- Disable-instead-of-delete for rapid reconnect optimization — later if needed

</deferred>

---

*Phase: 15-remote-actor-spawning-game-integration*
*Context gathered: 2026-03-29*
