---
phase: 15-remote-actor-spawning-game-integration
plan: 02
subsystem: networking
tags: [ecs, json-protocol, actor-spawning, entt]

# Dependency graph
requires:
  - phase: 07-native-linux-build-with-skse-tcp-relay
    provides: JSON TCP protocol, CharacterService ECS, TcpClient pattern
  - phase: 15-01
    provides: JSON protocol constants kCmdSpawnActor, kCmdRemoveActor, kSpawnResult
provides:
  - SendSpawnActor/SendRemoveActor JSON command senders
  - OnSpawnResult handler for DLL spawn feedback loop
  - m_serverIdToPtr mapping for serverId-to-actor-pointer resolution
affects: [15-remote-actor-spawning-game-integration, relay-dll-command-handlers]

# Tech tracking
tech-stack:
  added: []
  patterns:
    - "Command-response pattern: cmd_spawn_actor sent via JSON, spawn_result received back"
    - "serverId-to-actorPtr fallback map for pointer resolution before spawn completes"

key-files:
  created: []
  modified:
    - Code/native_client/Services/CharacterService.h
    - Code/native_client/Services/CharacterService.cpp

key-decisions:
  - "Used JSON wire protocol via TcpClient::SendJsonLine (consistent with all existing DLL commands)"
  - "spawn_result handled in OnHookEvent JSON dispatch (same pattern as other DLL events)"

patterns-established:
  - "Command-response spawn pattern: native sends cmd_spawn_actor JSON, DLL replies spawn_result JSON with actorPtr"
  - "m_serverIdToPtr fallback lookup in ApplyRemotePositions for pointer resolution race"

requirements-completed: [SPAWN-04, SPAWN-05, SPAWN-06]

# Metrics
duration: 10min
completed: 2026-03-30
---

# Phase 15 Plan 02: CharacterService Spawn/Remove Commands Summary

**JSON cmd_spawn_actor/cmd_remove_actor commands with spawn_result feedback loop for remote actor lifecycle management**

## Performance

- **Duration:** 10 min (includes manual merge conflict resolution)
- **Completed:** 2026-03-30
- **Tasks:** 2
- **Files modified:** 2

## Accomplishments
- CharacterService sends cmd_spawn_actor JSON to DLL when server assigns a new remote character
- CharacterService sends cmd_remove_actor JSON with tracked pointer when server removes a remote character
- spawn_result events from DLL populate m_serverIdToPtr map completing the spawn feedback loop
- ApplyRemotePositions uses m_serverIdToPtr fallback for pointer resolution before spawn completes
- OnHookEvent dispatches spawn_result to OnSpawnResult() (consistent with all other DLL events)

## Files Modified
- `Code/native_client/Services/CharacterService.h` - Added SendSpawnActor, SendRemoveActor, OnSpawnResult declarations and m_serverIdToPtr map
- `Code/native_client/Services/CharacterService.cpp` - Implemented spawn/remove senders, spawn result handler, wired into OnAssignCharacter/OnRemoveCharacter/ApplyRemotePositions

## Deviations from Plan

### Orchestrator-resolved: Merge conflict with Plan 01

The 15-02 executor agent ran in parallel with 15-01 and implemented a binary CommandPacket protocol (the json_protocol.h constants weren't available yet in its worktree). The orchestrator resolved this by:
1. Aborting the merge of the 15-02 worktree
2. Manually implementing all Plan 02 code using the correct JSON wire protocol (matching existing codebase patterns)
3. Verifying both targets compile cleanly

No scope change -- same functionality, correct protocol.

## Issues Encountered
None.

## Known Stubs
None -- all methods are fully implemented.

## Next Phase Readiness
- Plan 01 provides DLL-side CMD_SPAWN_ACTOR/CMD_REMOVE_ACTOR handlers
- Plan 03 adds INI configuration and human E2E verification
- All native-side wiring is complete for remote actor lifecycle

---
*Phase: 15-remote-actor-spawning-game-integration*
*Completed: 2026-03-30*
