# Phase 15: Remote Actor Spawning & Game Integration - Research

**Researched:** 2026-03-29
**Domain:** Game function invocation via relay DLL, actor lifecycle management, Skyrim INI configuration
**Confidence:** HIGH

## Summary

Phase 15 bridges the gap between network messages (CharacterSpawnRequest, NotifyRemoveCharacter, ServerReferencesMoveRequest) and actual in-game actor creation/deletion/movement. The infrastructure is already well-established: ServerMessageRouter already decodes all relevant messages with typed callbacks, the SPSC CommandQueue already handles game-thread execution, and the JSON wire protocol already supports 15 command ops. This phase adds 2 new command ops (cmd_spawn_actor, cmd_remove_actor) plus a new event type (spawn_result) for the DLL to report spawned actor pointers back to the native process.

The STR codebase provides a complete reference implementation for actor spawning (`Actor::Create`, `Actor::New`, `ModManager::Spawn`) with exact Address Library IDs (14617 for TESForm::GetById, 13723 for SpawnNewREFR, 40245 for CharacterConstructor, 11806 for SetLevelMod). The relay DLL already has the Address Library parser (`LoadAddressLibrary`/`ResolveAddress`) and the pattern for resolving IDs to function pointers. The new spawn command handler in DrainCommandQueue follows the exact same pattern as existing CMD_SET_POSITION but calls more game functions in sequence.

**Primary recommendation:** Port the exact function call sequence from STR's `Actor::Create()` into a new CMD_SPAWN_ACTOR case in DrainCommandQueue, resolve needed game function pointers via the existing Address Library infrastructure, and add a spawn_result JSON event to report the created actor pointer back to the native process for serverId-to-pointer tracking.

<user_constraints>
## User Constraints (from CONTEXT.md)

### Locked Decisions
- **D-01:** The relay DLL must remain a **stateless thin shim** -- it executes commands and reports results, but holds no maps, tracking state, or business logic. All actor identity tracking (serverId to pointer mapping) lives in the native process.
- **D-02:** Port actor spawning game function calls from STR's existing client code (`Code/client/Games/Skyrim/Actor.cpp`, `Code/client/Games/ModManager.cpp`) into the relay DLL's `DrainCommandQueue`. Reference both STR source and SKSE source for game function signatures, address library IDs, and calling conventions.
- **D-03:** Key functions to port: `TESForm::GetById()`, `Actor::New()`, `ModManager::Spawn()` (address library ID 13723 -- `SpawnNewREFR`), and related setup calls (`SetSkipSaveFlag`, `SetBaseForm`, `SetParentCell`, `ForcePosition`).
- **D-04:** New `cmd_spawn_actor` JSON command: native sends `{op: "cmd_spawn_actor", form_id: uint32, x, y, z, rx, ry}`. DLL executes the spawn on game thread, then sends back a hook event with the spawned actor's pointer so the native process can track it for future commands.
- **D-05:** Appearance sync (FaceGen, TESNPC::Create, inventory, actor values) is **out of scope** for Phase 15.
- **D-06:** Immediate deletion on disconnect. Server sends `NotifyRemoveCharacter` -> native sends `cmd_remove_actor {ptr}` -> DLL deletes the actor from Skyrim immediately. No fade-out, no disable-instead-of-delete.
- **D-07:** DLL reports spawn results back to native via a new hook event (e.g. `spawn_result` with actor pointer and formId). Native process maintains the serverId to actor pointer map and uses raw pointers in all subsequent commands.
- **D-08:** Native process tracks serverId to actor pointer (populated from DLL spawn result events). All move commands use raw actor pointers.
- **D-09:** The existing `CMD_SET_POSITION` implementation in DrainCommandQueue already handles pointer-based position updates.
- **D-10:** Each launch script handles its own INI patching inline. No shared helper.
- **D-11:** Minimal INI settings only: `bAlwaysActive=1` (game runs when unfocused) and `bFull Screen=0` with windowed resolution.

### Claude's Discretion
- Exact address library IDs for Actor::New, TESForm::GetById, Actor::Delete, and other game functions (determined by reading STR source)
- How to port SKSE's task interface / game-thread execution pattern
- Whether cmd_spawn_actor creates Actor via Actor::Create(TESNPC*) or the simpler Actor::Spawn(formId) path
- JSON schema for the new spawn_result hook event
- Error handling when spawn fails (formId not found, cell not loaded, etc.)
- Which STR setup calls are strictly necessary for a functional remote actor vs nice-to-have
- DrainCommandQueue changes needed for new commands via JSON protocol path

### Deferred Ideas (OUT OF SCOPE)
- Appearance sync (FaceGen, TESNPC::Create from appearance buffer, actor values, inventory) -- future phase
- SKSE task interface port (replace HookActorProcess piggyback with clean game-thread callback) -- could be part of this phase at Claude's discretion or deferred
- Fade-out animation on actor removal -- polish for later
- Disable-instead-of-delete for rapid reconnect optimization -- later if needed
</user_constraints>

## Standard Stack

### Core
| Library | Version | Purpose | Why Standard |
|---------|---------|---------|--------------|
| MinHook | v1.3.3 | Function hooking in relay DLL | Already installed and proven for 31 hook trampolines |
| nlohmann/json | v3.11.3 | JSON wire protocol for DLL<->native IPC | Already used by both relay DLL and native client |
| EnTT | v3.10.0 | ECS for actor tracking in native process | Project-wide ECS framework |
| spdlog | v1.13.0 | Logging in native process | Project standard |

### Supporting
| Library | Version | Purpose | When to Use |
|---------|---------|---------|-------------|
| Catch2 | 2.13.9 | Unit tests for new command parsing | Existing test framework |
| GLM | 0.9.9+8 | 3D math (position/rotation) | Already used by CharacterService |

No new dependencies needed. All required libraries are already in the project.

## Architecture Patterns

### Recommended Project Structure
```
Code/relay_dll/
  relay_main.cpp          # DrainCommandQueue: add CMD_SPAWN_ACTOR, CMD_REMOVE_ACTOR cases
  json_protocol.h         # Add kCmdSpawnActor, kCmdRemoveActor, kSpawnResult constants
  protocol.h              # Add CMD_SPAWN_ACTOR, CMD_REMOVE_ACTOR to CommandOpcode enum
  hook_trampolines.cpp    # Resolve new game function addresses (GetById, CharCtor, SpawnNewREFR, etc.)
  hook_trampolines.h      # Export new function pointer getters

Code/native_client/
  Services/CharacterService.cpp  # Add SendSpawnActor(), SendRemoveActor(), OnSpawnResult()
  Services/CharacterService.h    # Add serverId->actorPtr map, new methods
  main.cpp                       # Wire spawn_result events to CharacterService

tests/
  launch_host.sh          # Add INI patching (bAlwaysActive, windowed)
  launch_client.sh        # Add INI patching
  launch_two_player.sh    # Add INI patching
```

### Pattern 1: Game Function Invocation via CommandQueue (Existing Pattern)
**What:** Native process sends JSON command -> DLL parses into CommandSlot -> game thread drains queue and calls game functions
**When to use:** Any game function that must run on the game thread (which is all of them for actor manipulation)
**Example (existing CMD_SET_POSITION):**
```cpp
// In DrainCommandQueue:
case CMD_SET_POSITION: {
    void* pActor = reinterpret_cast<void*>(cmd.args[0]);
    if (!pActor || !pRealSetPosition) break;
    // Unpack position from args...
    pRealSetPosition(pActor, pos);
    break;
}
```

### Pattern 2: Spawn Result Feedback (New Pattern)
**What:** After executing a spawn command, DLL sends a JSON event back to native with the created actor's pointer
**When to use:** When the DLL creates a game object and the native process needs the resulting pointer
**Example:**
```cpp
// In DrainCommandQueue after spawn succeeds:
nlohmann::json result = {
    {"op", "spawn_result"},
    {"form_id", formId},
    {"ptr", reinterpret_cast<uint64_t>(pSpawnedActor)}
};
g_tcpServer.SendJson(result);
```

### Pattern 3: serverId-to-Pointer Mapping (Native Process Side)
**What:** Native process maintains a map from server-assigned entity IDs to game actor pointers, populated from spawn_result events
**When to use:** All remote actor commands (position, animation, removal) need the game pointer
**Example:**
```cpp
// In CharacterService:
std::unordered_map<uint32_t, uint64_t> m_serverIdToPtr;  // serverId -> actorPtr

void OnSpawnResult(uint32_t aFormId, uint64_t aActorPtr) {
    // Find RemoteComponent with matching formId and store pointer
    auto view = m_registry.view<RemoteComponent>();
    for (auto entity : view) {
        auto& comp = view.get<RemoteComponent>(entity);
        if (comp.formId == aFormId && comp.gamePtr == 0) {
            comp.gamePtr = aActorPtr;
            m_serverIdToPtr[comp.serverId] = aActorPtr;
            break;
        }
    }
}
```

### Anti-Patterns to Avoid
- **Storing serverId maps in the DLL:** Violates D-01 (stateless shim). All identity tracking lives in the native process.
- **Blocking on spawn result:** The spawn command is enqueued; the result comes asynchronously via TCP event. Native process must handle the async nature.
- **Calling game functions off the game thread:** All Actor manipulation must go through DrainCommandQueue which runs on the HookActorProcess game-thread callback.

## Don't Hand-Roll

| Problem | Don't Build | Use Instead | Why |
|---------|-------------|-------------|-----|
| Address Library resolution | Custom parser | Existing `LoadAddressLibrary()`/`ResolveAddress()` in hook_trampolines.cpp | Already works, V2 format parser included |
| Game-thread execution | New thread sync | Existing SPSC `CommandQueue` + `DrainCommandQueue()` pattern | Lock-free, proven safe |
| Actor spawn sequence | Custom spawn logic | Port STR's `Actor::Create()` function call sequence exactly | Battle-tested, correct game function order |
| JSON IPC protocol | New protocol | Existing `json_protocol.h` constants + `TcpServer::SendJson()`/`ReceiveJsonLine()` | Already handles all command/event patterns |
| Form lookup | Custom memory scan | TESForm::GetById (Address Library ID 14617) | Engine's own lookup function |

**Key insight:** Almost all infrastructure is already built. This phase is primarily about adding 2 new command opcodes and 1 new event type using existing patterns.

## Common Pitfalls

### Pitfall 1: Calling Game Functions Off the Game Thread
**What goes wrong:** Skyrim crashes or corrupts state when Actor manipulation functions are called from a non-game thread
**Why it happens:** The MonitorThread receives JSON commands on the TCP thread; game functions must run during the ActorProcess hook callback
**How to avoid:** All new commands MUST go through the CommandQueue (Enqueue on TCP thread, Drain on game thread). Never call game functions directly from ParseJsonCommand.
**Warning signs:** Crash in ntdll, corrupt Actor state, Skyrim freezes

### Pitfall 2: Actor Pointer Lifetime
**What goes wrong:** Native process sends cmd_set_position with a stale actor pointer after the actor was deleted by the game
**Why it happens:** Skyrim may delete actors during cell unloads or other cleanup. The pointer stored in RemoteComponent becomes dangling.
**How to avoid:** When actor_removed events arrive for a spawned remote actor, immediately clear the gamePtr in RemoteComponent and remove from m_serverIdToPtr. Stop sending commands for actors with gamePtr == 0.
**Warning signs:** Random crashes during cell transitions, position updates to freed memory

### Pitfall 3: Spawn Timing vs Cell Loading
**What goes wrong:** cmd_spawn_actor fails because the player's cell is not loaded yet or the formId does not exist in the current load order
**Why it happens:** CharacterSpawnRequest can arrive before the player has fully loaded into a cell
**How to avoid:** Check that GetById(0x14) returns a valid player with a valid parentCell before attempting spawn. If not ready, either retry on next DrainCommandQueue tick or report failure back to native.
**Warning signs:** pPlayer or pCell is null, SpawnNewREFR returns 0

### Pitfall 4: CommandSlot Arg Packing for Spawn
**What goes wrong:** float position values get truncated or mangled when packed into uint64_t args
**Why it happens:** CMD_SET_POSITION already does float-to-uint32 memcpy packing. Spawn needs formId + 5 floats (x,y,z,rx,ry) = 6 values in 8 arg slots -- feasible but must be packed consistently.
**How to avoid:** Follow the exact same memcpy bit-packing pattern used by CMD_SET_POSITION in ParseJsonCommand. Document the arg layout in protocol.h comments.
**Warning signs:** Actors spawn at origin (0,0,0) or at nonsensical positions

### Pitfall 5: INI File Path Under Proton
**What goes wrong:** INI patching fails because the Skyrim.ini path varies between Proton prefixes
**Why it happens:** Different launch scripts may use different COMPAT_DATA paths. The INI is at `$COMPAT_DATA/pfx/drive_c/users/steamuser/Documents/My Games/Skyrim Special Edition/Skyrim.ini`
**How to avoid:** Each script derives the INI path from its own COMPAT_DATA variable. Check file exists before patching. Back up before modifying.
**Warning signs:** sed commands fail silently, game launches fullscreen despite patching

## Code Examples

### Game Function Call Sequence for Actor Spawn (from STR Actor::Create)

The DLL must execute these game functions in order on the game thread:

```cpp
// Source: Code/client/Games/Skyrim/Actor.cpp lines 175-208
// Source: Code/client/Games/ModManager.cpp lines 41-48
// Source: Code/client/Games/Forms.cpp lines 12-18

// Address Library IDs (verified from STR source POINTER_SKYRIMSE calls):
// TESForm::GetById        = 14617   (Forms.cpp:15)
// CharacterConstructor    = 40245   (Actor.cpp, already in AddressIds namespace)
// SpawnNewREFR            = 13723   (ModManager.cpp:76)
// SetLevelMod (ExtraData) = 11806   (Actor.cpp:219)
// SetPosition             = 19790   (already hooked)
// ModManager singleton    = 400269  (ModManager.cpp:9)
// ActorProcess            = 37356   (already hooked)

// Simplified spawn sequence for Phase 15 (no appearance sync):
// 1. pNpc = GetById(formId)           -- look up the TESNPC base form
// 2. pActor = Memory::Allocate + CharacterConstructor  -- create empty Actor
// 3. SetSkipSaveFlag(pActor, true)    -- prevent save corruption
// 4. SetLevelMod(pActor, 4)           -- level scaling
// 5. SetParentCell(pActor, playerCell)
// 6. SetBaseForm(pActor, pNpc)
// 7. SpawnNewREFR(modManager, ..., pActor, ...)  -- insert into game world
// 8. ForcePosition(pActor, position)  -- move to target location
```

### New Command Opcodes (protocol.h additions)

```cpp
// Add to CommandOpcode enum:
CMD_SPAWN_ACTOR   = 0x8010,
CMD_REMOVE_ACTOR  = 0x8011,
```

### New JSON Protocol Constants (json_protocol.h additions)

```cpp
// Add to JsonOp namespace:
constexpr const char* kCmdSpawnActor  = "cmd_spawn_actor";
constexpr const char* kCmdRemoveActor = "cmd_remove_actor";
constexpr const char* kSpawnResult    = "spawn_result";
```

### cmd_spawn_actor JSON Schema

```json
{
  "op": "cmd_spawn_actor",
  "form_id": 123456,
  "x": 1234.5,
  "y": 5678.9,
  "z": 100.0,
  "rx": 0.0,
  "ry": 1.57
}
```

### spawn_result Event Schema

```json
{
  "op": "spawn_result",
  "form_id": 123456,
  "ptr": 140234567890,
  "success": true
}
```

On failure:
```json
{
  "op": "spawn_result",
  "form_id": 123456,
  "ptr": 0,
  "success": false
}
```

### cmd_remove_actor JSON Schema

```json
{
  "op": "cmd_remove_actor",
  "actor_ptr": 140234567890
}
```

### CommandSlot Arg Layout for CMD_SPAWN_ACTOR

```cpp
// args[0] = formId (uint32 zero-extended to uint64)
// args[1] = (yBits << 32) | xBits   -- packed x,y position floats
// args[2] = zBits                    -- packed z position float
// args[3] = (ryBits << 32) | rxBits -- packed rx,ry rotation floats
```

### DrainCommandQueue Spawn Handler (DLL Side)

```cpp
case CMD_SPAWN_ACTOR: {
    uint32_t formId = static_cast<uint32_t>(cmd.args[0]);

    // Unpack position
    uint32_t xBits = static_cast<uint32_t>(cmd.args[1]);
    uint32_t yBits = static_cast<uint32_t>(cmd.args[1] >> 32);
    uint32_t zBits = static_cast<uint32_t>(cmd.args[2]);
    float pos[3];
    memcpy(&pos[0], &xBits, sizeof(float));
    memcpy(&pos[1], &yBits, sizeof(float));
    memcpy(&pos[2], &zBits, sizeof(float));

    // Unpack rotation
    uint32_t rxBits = static_cast<uint32_t>(cmd.args[3]);
    uint32_t ryBits = static_cast<uint32_t>(cmd.args[3] >> 32);
    float rot[2];
    memcpy(&rot[0], &rxBits, sizeof(float));
    memcpy(&rot[1], &ryBits, sizeof(float));

    // 1. Look up base form
    void* pNpc = pGetById(formId);
    if (!pNpc) {
        // Report failure
        nlohmann::json result = {{"op", JsonOp::kSpawnResult}, {"form_id", formId}, {"ptr", 0}, {"success", false}};
        g_tcpServer.SendJson(result);
        break;
    }

    // 2. Create Actor via CharacterConstructor sequence
    // ... (call sequence from Actor::Create)

    // 3. Report success with actor pointer
    nlohmann::json result = {
        {"op", JsonOp::kSpawnResult},
        {"form_id", formId},
        {"ptr", reinterpret_cast<uint64_t>(pSpawnedActor)},
        {"success", true}
    };
    g_tcpServer.SendJson(result);
    break;
}
```

### INI Patching in Launch Scripts

```bash
# Pattern from existing launch_two_skyrim.sh (adapted per D-10, D-11):
SKYRIM_INI="$COMPAT_DATA/pfx/drive_c/users/steamuser/Documents/My Games/Skyrim Special Edition/Skyrim.ini"

if [[ -f "$SKYRIM_INI" ]]; then
    cp "$SKYRIM_INI" "${SKYRIM_INI}.bak"

    # Remove existing entries to avoid duplicates
    sed -i '/^bFull Screen=/d; /^iSize H=/d; /^iSize W=/d; /^bAlwaysActive=/d; /^bBorderless=/d' "$SKYRIM_INI"

    # Add windowed mode under [Display]
    sed -i '/^\[Display\]/a bFull Screen=0\niSize H=720\niSize W=1280\nbBorderless=0' "$SKYRIM_INI"

    # Add bAlwaysActive under [Interface] (or [General] if no [Interface])
    if grep -q '^\[Interface\]' "$SKYRIM_INI"; then
        sed -i '/^\[Interface\]/a bAlwaysActive=1' "$SKYRIM_INI"
    else
        echo -e "\n[Interface]\nbAlwaysActive=1" >> "$SKYRIM_INI"
    fi
fi

# Restore on exit
trap 'cp "${SKYRIM_INI}.bak" "$SKYRIM_INI" 2>/dev/null' EXIT
```

## State of the Art

| Old Approach | Current Approach | When Changed | Impact |
|--------------|------------------|--------------|--------|
| Binary CommandPacket protocol | JSON NDJSON wire protocol | Phase 11 | ParseJsonCommand handles all command routing |
| Direct game API calls (STR) | Address Library + raw function pointers (relay DLL) | Phase 7 | Same game functions, just resolved at runtime via address IDs |
| SKSE Task interface for game-thread | HookActorProcess + SPSC queue | Phase 7 | Simpler, no SKSE dependency, fires every actor process tick |

## Open Questions

1. **Memory::Allocate for Actor**
   - What we know: STR uses `Memory::Allocate<Actor>()` which calls Skyrim's internal allocator. The relay DLL has no access to STR's Memory class.
   - What's unclear: The exact size and allocator function. STR's `Memory::Allocate` uses Skyrim's heap. Need to find the address library ID for the allocator or use a VirtualAlloc alternative.
   - Recommendation: Research STR's Memory.h to find the underlying game allocator address. Alternatively, the CharacterConstructor (addr 40245) may handle allocation internally -- needs verification. If `Actor::New()` calls `Memory::Allocate` then calls `CharacterConstructor`, the DLL needs both. If `CharacterConstructor` allocates internally, only one call needed.

2. **Actor Deletion Function**
   - What we know: D-06 says reference STR's `Actor::Delete()` for the correct game function calls. Need to locate this function and its address library ID.
   - What's unclear: The exact deletion sequence. STR may use `TESObjectREFR::Delete` or a specific Actor destructor.
   - Recommendation: Search STR source for Actor::Delete or destruction patterns. The CharacterDestructor (addr in AddressIds, via RealCharacterDestructor) may be the right call, but deletion also requires removing from the cell and cleaning up references.

3. **SetSkipSaveFlag, SetBaseForm, SetParentCell Virtual Calls**
   - What we know: These are virtual functions on TESObjectREFR/Actor. STR calls them via `this->` but the DLL has no class definitions.
   - What's unclear: Whether these are simple vtable calls (need vtable offset) or direct function calls (need address library ID).
   - Recommendation: For Phase 15, the simplest path may be the `Actor::Spawn(formId)` approach which wraps all setup into one call path via `Actor::Create(TESNPC*)`. If SpawnNewREFR handles cell insertion and base form setup, many individual setup calls may be unnecessary.

4. **SpawnNewREFR Parameter Complexity**
   - What we know: The function signature is `(ModManager*, uint32_t&, TESForm*, NiPoint3*, NiPoint3*, TESObjectCELL*, TESWorldSpace*, Actor*, uintptr_t, uintptr_t, char, char)` -- 12 parameters including this-pointer.
   - What's unclear: Whether all parameters are strictly needed. The last 4 params (a9, a10, aForcePersist, a12) are passed as (0, 0, 0, 1) in STR.
   - Recommendation: Follow STR exactly for parameter values. The player's parentCell and WorldSpace can be obtained from the player actor (formId 0x14) looked up via GetById.

## Validation Architecture

### Test Framework
| Property | Value |
|----------|-------|
| Framework | Catch2 2.13.9 |
| Config file | Code/tests/main.cpp (Catch2 header-only) |
| Quick run command | `./build/linux/x86_64/releasedbg/ReplayTests "[Spawn]"` |
| Full suite command | `./build/linux/x86_64/releasedbg/ReplayTests` |

### Phase Requirements to Test Map
| Req ID | Behavior | Test Type | Automated Command | File Exists? |
|--------|----------|-----------|-------------------|-------------|
| P15-01 | cmd_spawn_actor JSON parsing into CommandSlot | unit | `./build/.../ReplayTests "[SpawnCmd]"` | No -- Wave 0 |
| P15-02 | cmd_remove_actor JSON parsing into CommandSlot | unit | `./build/.../ReplayTests "[RemoveCmd]"` | No -- Wave 0 |
| P15-03 | spawn_result event parsing in CharacterService | unit | `./build/.../ReplayTests "[SpawnResult]"` | No -- Wave 0 |
| P15-04 | serverId-to-ptr mapping lifecycle | unit | `./build/.../ReplayTests "[ServerIdMap]"` | No -- Wave 0 |
| P15-05 | OnAssignCharacter + spawn command flow | integration | `./build/.../ReplayTests "[SpawnFlow]"` | No -- Wave 0 |
| P15-06 | INI patching in launch scripts | manual-only | Manual: run script, check INI | N/A |
| P15-07 | In-game actor spawn via DLL | manual-only | Manual: run two-player test | N/A |

### Sampling Rate
- **Per task commit:** `./build/linux/x86_64/releasedbg/ReplayTests "[Spawn]" "[Remove]" "[SpawnResult]"`
- **Per wave merge:** Full ReplayTests suite
- **Phase gate:** Full suite green before verification

### Wave 0 Gaps
- [ ] `Code/tests/spawn_command_tests.cpp` -- covers P15-01, P15-02 (JSON parse to CommandSlot)
- [ ] `Code/tests/spawn_result_tests.cpp` -- covers P15-03, P15-04 (spawn_result event handling, serverId map)
- [ ] `Code/tests/replay/spawn_flow_tests.cpp` -- covers P15-05 (end-to-end spawn message flow)

## Project Constraints (from CLAUDE.md)

- **Build system:** XMake with MinGW cross-compilation for DLL, native Linux for native_client
- **No exceptions:** Return success/failure via boolean or result codes
- **Naming conventions:** `a` prefix for function args, `m_` for members, PascalCase for classes, `s_` for statics
- **Error handling:** spdlog for native process, OutputDebugStringA/RelayLog for DLL
- **No SKSE SDK:** DLL uses inline SKSE structs and Address Library, not SKSE interfaces
- **C++20:** Modern features allowed
- **Format:** clang-format for C++
- **GSD workflow:** All changes through GSD commands

## Sources

### Primary (HIGH confidence)
- `Code/client/Games/Skyrim/Actor.cpp` -- Actor::Create, Actor::New, Actor::Spawn, ForcePosition, address library IDs (40245, 11806, 37334)
- `Code/client/Games/ModManager.cpp` -- SpawnNewREFR signature and address library ID 13723
- `Code/client/Games/Forms.cpp` -- TESForm::GetById address library ID 14617
- `Code/relay_dll/relay_main.cpp` -- DrainCommandQueue pattern, ParseJsonCommand pattern, MonitorThread
- `Code/relay_dll/hook_trampolines.cpp` -- AddressIds namespace, LoadAddressLibrary, ResolveAddress
- `Code/relay_dll/json_protocol.h` -- All existing JsonOp and JsonField constants
- `Code/relay_dll/protocol.h` -- CommandOpcode enum, CommandSlot struct
- `Code/native_client/Services/CharacterService.cpp` -- OnAssignCharacter, OnRemoveCharacter, SendSetPosition
- `Code/native_client/main.cpp` -- ServerMessageRouter callback wiring (lines 550-644)
- `Code/native_client/transport/ServerMessageRouter.cpp` -- CharacterSpawnRequest decoding
- `Code/encoding/Messages/CharacterSpawnRequest.h` -- Spawn message fields
- `tests/launch_two_skyrim.sh` -- Reference INI patching implementation (lines 51-65)

### Secondary (MEDIUM confidence)
- STR CharacterService::OnCharacterSpawn (Code/client/Services/Generic/CharacterService.cpp:350) -- full spawn handler with WaitingFor3D pattern (we skip WaitingFor3D for Phase 15)

## Metadata

**Confidence breakdown:**
- Standard stack: HIGH -- all libraries already in project, no new dependencies
- Architecture: HIGH -- extending existing proven patterns (CommandQueue, JSON protocol, hook trampolines)
- Pitfalls: HIGH -- based on direct code analysis of existing implementation and game function requirements
- Spawn function calls: MEDIUM -- exact Memory::Allocate equivalent for DLL needs runtime verification
- Actor deletion: MEDIUM -- exact delete sequence needs STR source verification

**Research date:** 2026-03-29
**Valid until:** 2026-04-28 (stable -- game engine functions don't change)
