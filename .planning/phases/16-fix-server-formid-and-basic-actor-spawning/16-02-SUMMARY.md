---
phase: 16-fix-server-formid-and-basic-actor-spawning
plan: 02
subsystem: native-client
tags: [spawn-routing, appearance-pipeline, worldstate-sync, disconnect-cleanup, base64, json-protocol]
dependency_graph:
  requires:
    - phase: 16-01
      provides: WorldStateSync message and kWorldStateSync opcode
  provides:
    - Dual-path spawn routing in native CharacterService (formId vs appearance)
    - WorldStateSync reconciliation handler
    - Disconnect cleanup for remote entities
    - JSON protocol field constants for appearance data
    - Extended SpawnCallback with appearance data in ServerMessageRouter
    - WorldStateSyncCallback in ServerMessageRouter
  affects: [16-03-relay-dll, native-client-services, spawn-pipeline]
tech_stack:
  added: []
  patterns: [dual-path-spawn-routing, base64-encoding-for-json-transport, reconciliation-pattern]
key_files:
  created: []
  modified:
    - Code/native_client/transport/ServerMessageRouter.h
    - Code/native_client/transport/ServerMessageRouter.cpp
    - Code/native_client/Services/CharacterService.h
    - Code/native_client/Services/CharacterService.cpp
    - Code/native_client/main.cpp
    - Code/relay_dll/json_protocol.h
    - Code/server/Recording/MessageSerializer.cpp
    - Code/server/Scripting/Player_Bindings.cpp
key_decisions:
  - "Base64 encoding applied to binary appearance data before JSON transport to DLL"
  - "FaceTints serialized via TiltedPhoques::Buffer::Writer in ServerMessageRouter before forwarding"
  - "WorldStateSync reconciliation logs missing entities but does not request re-spawn (awaits event-driven path)"
patterns-established:
  - "Dual-path spawn: FormId==0 + appearance -> TESNPC::Create path; non-zero FormId -> existing formId path"
  - "Base64Encode static utility in CharacterService.cpp for binary-to-JSON data transport"
  - "DisconnectCleanup pattern: iterate all RemoteComponent entities, send remove commands, destroy ECS entities"
requirements-completed: [SPAWN-08, SPAWN-09]
duration: 19min
completed: 2026-03-30
---

# Phase 16 Plan 02: Native Spawn Pipeline Extension Summary

**Dual-path spawn routing (formId vs appearance), WorldStateSync reconciliation, and disconnect cleanup in native CharacterService**

## Performance

- **Duration:** 19 min
- **Started:** 2026-03-30T04:46:06Z
- **Completed:** 2026-03-30T05:04:36Z
- **Tasks:** 2
- **Files modified:** 8

## Accomplishments
- Extended SpawnCallback to forward appearance data, change flags, face tints, and isPlayer flag from CharacterSpawnRequest
- Implemented dual-path spawn routing: FormId==0 with appearance data routes to SendSpawnActorWithAppearance; non-zero FormId routes to existing SendSpawnActor
- Added WorldStateSync handler in ServerMessageRouter that decodes entity entries and routes to CharacterService
- Implemented reconciliation logic: orphan removal, position correction, missing entity detection
- Implemented DisconnectCleanup that sends cmd_remove_actor for all remotes and clears ECS state
- Added JSON protocol constants for appearance, change_flags, face_tints, is_player, server_id

## Task Commits

Each task was committed atomically:

1. **Task 1: Extend JSON protocol fields + ServerMessageRouter + SpawnCallback** - `992d2ccf` (feat)
2. **Task 2: Dual-path CharacterService + WorldStateSync + disconnect cleanup + main.cpp wiring** - `1020e87d` (feat)

## Files Created/Modified
- `Code/relay_dll/json_protocol.h` - Added 5 new field constants for appearance pipeline
- `Code/native_client/transport/ServerMessageRouter.h` - Extended SpawnCallback (11 params), added WorldStateSyncEntry struct and callback
- `Code/native_client/transport/ServerMessageRouter.cpp` - Forward appearance data from CharacterSpawnRequest, handle WorldStateSync, serialize FaceTints
- `Code/native_client/Services/CharacterService.h` - Added OnSpawnCharacter, OnWorldStateSync, DisconnectCleanup, SendSpawnActorWithAppearance declarations
- `Code/native_client/Services/CharacterService.cpp` - Implemented dual-path spawn, base64 encoding, reconciliation, disconnect cleanup
- `Code/native_client/main.cpp` - Updated SpawnCallback wiring (both host/join), added WorldStateSyncCallback, wired DisconnectCleanup
- `Code/server/Recording/MessageSerializer.cpp` - Added DEF_MSG_NAME(WorldStateSync) and include (Rule 3 fix)
- `Code/server/Scripting/Player_Bindings.cpp` - Disambiguated Player::Send overload for sol2 (Rule 3 fix)

## Decisions Made
- Base64 encoding applied to binary appearance data before JSON transport to DLL (binary data cannot be embedded directly in JSON)
- FaceTints serialized via TiltedPhoques::Buffer::Writer in ServerMessageRouter before forwarding as std::string to CharacterService
- WorldStateSync reconciliation logs missing entities but does not request re-spawn (the event-driven CharacterSpawnRequest path handles initial spawns)
- JoinClient stores CharacterService pointer for disconnect cleanup callback

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 3 - Blocking] Added DEF_MSG_NAME(WorldStateSync) to MessageSerializer.cpp**
- **Found during:** Task 1 (build verification)
- **Issue:** Plan 01 added WorldStateSync to ServerMessageFactory but not to MessageSerializer's name registry, causing incomplete type error at compile time
- **Fix:** Added DEF_MSG_NAME(WorldStateSync) and #include <Messages/WorldStateSync.h>
- **Files modified:** Code/server/Recording/MessageSerializer.cpp
- **Verification:** SkyrimTogetherServer builds successfully
- **Committed in:** 992d2ccf (Task 1 commit)

**2. [Rule 3 - Blocking] Disambiguated Player::Send overload for sol2 binding**
- **Found during:** Task 1 (build verification)
- **Issue:** Plan 01 added a Send() overload with EPacketFlags to Player, causing sol2 binding ambiguity
- **Fix:** Applied static_cast to select the const ServerMessage& overload
- **Files modified:** Code/server/Scripting/Player_Bindings.cpp
- **Verification:** SkyrimTogetherServer builds successfully
- **Committed in:** 992d2ccf (Task 1 commit)

---

**Total deviations:** 2 auto-fixed (2 blocking issues from Plan 01)
**Impact on plan:** Both fixes necessary for compilation. No scope creep.

## Issues Encountered
- xmake in git worktrees compiles from main repo by default; required `xmake build -P $(pwd)` to target worktree source files

## User Setup Required
None - no external service configuration required.

## Known Stubs
None - all methods are fully implemented with real logic.

## Next Phase Readiness
- Native spawn pipeline is complete and ready for DLL-side consumption (Plan 03)
- DLL needs to handle cmd_spawn_actor with appearance/change_flags/face_tints JSON fields
- WorldStateSync reconciliation is wired and will operate once server broadcasts arrive

---
*Phase: 16-fix-server-formid-and-basic-actor-spawning*
*Completed: 2026-03-30*
