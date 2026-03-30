# Phase 15: Remote Actor Spawning & Game Integration - Discussion Log

> **Audit trail only.** Do not use as input to planning, research, or execution agents.
> Decisions are captured in CONTEXT.md — this log preserves the alternatives considered.

**Date:** 2026-03-29
**Phase:** 15-remote-actor-spawning-game-integration
**Areas discussed:** Spawn mechanism, Actor lifecycle, Move command routing, INI auto-config

---

## Spawn Mechanism

### Initial Question: How should the relay DLL create NPCs?

| Option | Description | Selected |
|--------|-------------|----------|
| PlaceAtMe via console | Send console command equivalent, safest approach | |
| Direct TESObjectREFR::ctor | Instantiate Actor directly via RTTI constructors | |
| Clone existing NPC reference | Clone/move existing NPC template | |

**User's choice:** None — user pointed out that Skyrim Together already solved this and we should reference it.

### Follow-up: Architecture discussion

User questioned why we aren't using SKSE. Discussion revealed:
- SKSE is a loader + minimal interfaces, not a comprehensive game API
- CommonLibSSE (reversed game structures) requires MSVC linker — can't cross-compile from Linux with MinGW
- Our relay DLL already does what SKSE plugins do — hooks game functions via address library
- The right approach is to reference STR source + SKSE source and port the specific game function calls needed

### Follow-up: How much of STR's spawn pipeline to port?

User wanted to clarify before answering. After architecture discussion, settled on:
- Port the specific game function calls (Actor::Create, ModManager::Spawn, SpawnNewREFR at addr lib 13723) into the DLL's DrainCommandQueue
- Reference both STR and SKSE source for function signatures and patterns
- Skip appearance sync for this phase

---

## Actor Lifecycle

| Option | Description | Selected |
|--------|-------------|----------|
| Immediate delete on disconnect | Server sends NotifyRemoveCharacter → DLL deletes actor immediately | ✓ |
| Fade-out then delete | Brief delay before deletion for polish | |
| Disable instead of delete | Mark actor disabled, re-enable on reconnect | |

**User's choice:** Immediate delete on disconnect

### ServerId → Actor Pointer Tracking

| Option | Description | Selected |
|--------|-------------|----------|
| DLL-side map | DLL maintains serverId→pointer map | |
| Native-side with pointer passback | DLL sends pointer back, native tracks | ✓ (revised) |
| FormId-based lookup each time | DLL calls GetById every update | |

**User's choice:** Initially selected DLL-side map, but later revised to native-side tracking after raising the stateless DLL principle.

**Notes:** User expressed concern about overcomplicating the DLL. Core principle: "the whole point of the dll is to be the thinnest shim possible that just relays Skyrim's data directly over to our client." This led to revising the tracking decision — all state lives in the native process.

---

## Move Command Routing

| Option | Description | Selected |
|--------|-------------|----------|
| ServerId for remote, pointer for local | DLL looks up pointer from internal map | |
| All serverId-based | Unify all commands to use serverId | |
| Native sends raw pointer | Native tracks pointers, sends in all commands | ✓ |

**User's choice:** After stateless DLL discussion, settled on native sending raw actor pointers. Native process maintains serverId→pointer mapping, populated from DLL spawn result events.

---

## INI Auto-Configuration

### Where should INI logic live?

| Option | Description | Selected |
|--------|-------------|----------|
| Shared shell helper | Function in shared file sourced by all scripts | |
| Each script handles its own INI | Inline INI patching per launch script | ✓ |
| DLL does it at load time | DLL patches INI when loading | |

**User's choice:** Each script handles its own INI

### Which INI settings?

| Option | Description | Selected |
|--------|-------------|----------|
| bAlwaysActive + windowed only | Minimum needed for two instances | ✓ |
| Full co-op preset | Also disable autosave, pause, intro, etc. | |
| You decide | Claude picks minimal set | |

**User's choice:** bAlwaysActive + windowed only

---

## Claude's Discretion

- Exact address library IDs for game functions (from STR source)
- SKSE task interface port approach
- JSON schema for spawn_result event
- Error handling on spawn failure
- Which STR setup calls are strictly necessary

## Deferred Ideas

- Appearance sync (FaceGen, inventory, actor values) — future phase
- SKSE task interface port — possibly this phase or deferred
- Fade-out on removal — polish for later
- Disable-instead-of-delete optimization — later if needed
