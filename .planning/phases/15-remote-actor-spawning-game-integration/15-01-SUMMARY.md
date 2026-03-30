---
phase: 15-remote-actor-spawning-game-integration
plan: 01
subsystem: relay-dll
tags: [actor-spawning, address-library, game-functions, skse, mingw]

requires:
  - phase: 11-json-wire-protocol
    provides: JSON wire protocol, NDJSON TCP framing, ParseJsonCommand, DrainCommandQueue infrastructure
  - phase: 07-native-linux-build-with-skse-tcp-relay
    provides: Address Library parser, hook trampolines, SPSC command queue

provides:
  - CMD_SPAWN_ACTOR command handler in relay DLL DrainCommandQueue
  - CMD_REMOVE_ACTOR command handler for actor deletion via flag manipulation
  - ParseJsonCommand routing for cmd_spawn_actor and cmd_remove_actor JSON commands
  - spawn_result JSON event (DLL -> Native) reporting spawned actor pointer
  - 6 new Address Library-resolved game function pointers for actor creation pipeline
  - kCmdSpawnActor, kCmdRemoveActor, kSpawnResult JSON protocol constants

affects: [15-02, 15-03, native-client-character-service]

tech-stack:
  added: []
  patterns:
    - "Direct game function calls via Address Library resolution (not hooked)"
    - "STR Actor::Create spawn sequence ported to stateless relay DLL"
    - "Singleton dereference pattern for GameHeap (400188) and ModManager (400269)"

key-files:
  created: []
  modified:
    - Code/relay_dll/json_protocol.h
    - Code/relay_dll/protocol.h
    - Code/relay_dll/hook_trampolines.h
    - Code/relay_dll/hook_trampolines.cpp
    - Code/relay_dll/relay_main.cpp

key-decisions:
  - "Offset 0x60 for TESObjectREFR::parentCell, 0x128 for TESObjectCELL::worldspace (from struct asserts)"
  - "Offset 0x40 for TESObjectREFR::baseForm, 0x18 for TESForm::unk10, 0x10 for TESForm::flags (from struct layout)"
  - "Character alloc size 0x2B0 for SE 1.6.x (standard community value)"
  - "Separate s_pCharacterCtorDirect for direct calls vs RealCharacterCtor for hook trampoline"
  - "Actor removal uses flag-based disable+delete (bits 11+5 on TESForm::flags) rather than function calls"

patterns-established:
  - "Resolve-but-dont-hook pattern: game function pointers resolved via Address Library for direct calls from command handlers"
  - "Spawn result feedback: DLL sends spawn_result JSON with ptr+formId+success for native process actor tracking"

requirements-completed: [SPAWN-01, SPAWN-02, SPAWN-03]

duration: 8min
completed: 2026-03-30
---

# Phase 15 Plan 01: Relay DLL Actor Spawn/Remove Commands Summary

**Ported STR Actor::Create sequence into relay DLL with 6 Address Library-resolved game function pointers, CMD_SPAWN_ACTOR/CMD_REMOVE_ACTOR handlers, and spawn_result JSON feedback event**

## What Was Built

The relay DLL gained two new command handlers that enable the native process to create and delete actors in Skyrim's game world:

1. **CMD_SPAWN_ACTOR**: Executes the full actor creation pipeline on the game thread:
   - Looks up base NPC form via TESForm::GetById (addr 14617)
   - Allocates actor memory via GameHeap::Allocate (addr 68115) from GameHeap singleton (addr 400188)
   - Initializes via Character constructor (addr 40245)
   - Sets skip-save flag, level mod (addr 11806), and base form
   - Spawns into world via ModManager::SpawnNewREFR (addr 13723) using ModManager singleton (addr 400269)
   - Forces position via existing SetPosition hook
   - Reports spawned actor pointer back to native process via spawn_result JSON event

2. **CMD_REMOVE_ACTOR**: Sets Disabled (bit 11) and MarkedForDeletion (bit 5) flags on TESForm::flags for engine cleanup.

3. **ParseJsonCommand routing**: Routes cmd_spawn_actor (with form_id, x, y, z, rx, ry) and cmd_remove_actor (with actor_ptr) JSON commands to the SPSC command queue.

## Commits

| Task | Name | Commit | Key Files |
|------|------|--------|-----------|
| 1 | Add JSON protocol constants and CommandOpcodes | fa18fba3 | json_protocol.h, protocol.h |
| 2 | Resolve game function pointers, implement handlers | 8bbe96d6 | hook_trampolines.h/cpp, relay_main.cpp |

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 3 - Blocking] Worktree behind main repo -- missing JSON protocol layer**
- **Found during:** Task 2 setup
- **Issue:** Worktree branch was based on an old commit without the JSON wire protocol (Phase 11). Plan assumed json_protocol.h, SendJson, ParseJsonCommand existed.
- **Fix:** Merged dev branch into worktree to get Phase 11 JSON infrastructure
- **Files modified:** (merge brought all Phase 11-14 changes)

**2. [Rule 2 - Offset verification] Used STR struct asserts instead of plan's approximate offsets**
- **Found during:** Task 2 implementation
- **Issue:** Plan used approximate offsets (0x70 for parentCell, 0x78 for worldspace, 0x20 for baseForm, 0x0C for flags) that were incorrect
- **Fix:** Verified actual offsets from STR struct layout asserts: parentCell=0x60, worldspace=0x128 (on cell), baseForm=0x40, flags=0x10, unk10=0x18
- **Files modified:** Code/relay_dll/relay_main.cpp (CMD_SPAWN_ACTOR handler)

## Known Stubs

None -- all functionality is wired to game function pointers via Address Library resolution.

## Self-Check: PASSED
