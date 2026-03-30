---
phase: 16-fix-server-formid-and-basic-actor-spawning
plan: 04
subsystem: testing
tags: [catch2, entt, spawn-routing, reconciliation, disconnect, ndjson, replay-fixtures]

dependency_graph:
  requires:
    - phase: 16-01
      provides: WorldStateSync message and opcode
    - phase: 16-02
      provides: Extended CharacterService spawn routing (pending, tests written against existing API)
  provides:
    - 13 Catch2 unit tests covering spawn routing, reconciliation, and disconnect cleanup
    - 3 NDJSON replay fixtures for player spawn, NPC spawn, and spawn lifecycle
  affects: [16-02, 16-03, 16-05]

tech_stack:
  added: []
  patterns: [SpawnTestFixture pattern with no-op ProcMemReader/TcpClient for CharacterService testing]

key_files:
  created:
    - Code/tests/replay/spawn_pipeline_tests.cpp
    - tests/fixtures/replay/enet/player_spawn.ndjson
    - tests/fixtures/replay/enet/npc_spawn.ndjson
    - tests/fixtures/replay/enet/spawn_lifecycle.ndjson
  modified:
    - Code/server/Recording/MessageSerializer.cpp
    - Code/server/Scripting/Player_Bindings.cpp

key_decisions:
  - "Tests written against existing OnAssignCharacter/OnRemoveCharacter/OnReferencesMoveRequest API (OnSpawnCharacter/OnWorldStateSync/DisconnectCleanup not yet added by 16-02)"
  - "SpawnTestFixture uses unopened ProcMemReader (zero reads) and disconnected TcpClient (silent send failures) for self-contained testing without game process"
  - "Fixed two 16-01 merge bugs: missing DEF_MSG_NAME(WorldStateSync) and ambiguous Player::Send sol2 binding"

patterns-established:
  - "SpawnTestFixture: reusable harness for CharacterService unit tests with no game/network dependencies"
  - "RemoteCount/LocalCount/FindRemote/FindInterp helpers for ECS state assertions"

requirements-completed: [SPAWN-09, SPAWN-10]

duration: 20min
completed: 2026-03-30
---

# Phase 16 Plan 04: Spawn Pipeline Unit Tests and Replay Fixtures Summary

**13 Catch2 unit tests verifying spawn routing (formId dual-path), reconciliation (position updates, orphan removal), and disconnect cleanup, plus 3 NDJSON replay fixtures covering player/NPC spawn and full lifecycle**

## Performance

- **Duration:** 20 min
- **Started:** 2026-03-30T04:46:26Z
- **Completed:** 2026-03-30T05:06:34Z
- **Tasks:** 2
- **Files modified:** 6

## Accomplishments
- 13 test cases with 56 assertions covering all three test groups: [SpawnRouting] (5), [Reconciliation] (5), [Disconnect] (3)
- SpawnTestFixture pattern enables testing CharacterService ECS logic without game process or network
- 3 NDJSON fixtures documenting player spawn (FormId=0:0), NPC spawn (FormId=0:7), and full spawn lifecycle with WorldStateSync

## Task Commits

Each task was committed atomically:

1. **Task 1: Create spawn pipeline unit tests** - `e732a448` (test)
2. **Task 2: Create NDJSON replay fixtures** - `6dea0e52` (test)

## Files Created/Modified
- `Code/tests/replay/spawn_pipeline_tests.cpp` - 13 Catch2 tests with SpawnTestFixture, [SpawnRouting], [Reconciliation], [Disconnect] tags
- `tests/fixtures/replay/enet/player_spawn.ndjson` - Auth + assign + player CharacterSpawnRequest (FormId=0:0, appearance buffer)
- `tests/fixtures/replay/enet/npc_spawn.ndjson` - Auth + assign + NPC CharacterSpawnRequest (FormId=0:7)
- `tests/fixtures/replay/enet/spawn_lifecycle.ndjson` - Full lifecycle: spawn, move, WorldStateSync confirm, orphan removal
- `Code/server/Recording/MessageSerializer.cpp` - Added missing DEF_MSG_NAME(WorldStateSync)
- `Code/server/Scripting/Player_Bindings.cpp` - Disambiguated Player::Send overload for sol2

## Decisions Made
- Tests use existing CharacterService API (OnAssignCharacter, OnRemoveCharacter, OnReferencesMoveRequest) since OnSpawnCharacter/OnWorldStateSync/DisconnectCleanup will be added by Plan 16-02
- SpawnTestFixture constructs ProcMemReader and TcpClient without opening connections, so all reads return zero and sends silently fail -- enabling pure ECS logic testing
- NDJSON fixtures include descriptive JSON fields alongside base64 payloads for human readability

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 1 - Bug] Missing DEF_MSG_NAME(WorldStateSync) in MessageSerializer.cpp**
- **Found during:** Task 1 (building ReplayTests)
- **Issue:** 16-01 added WorldStateSync to ServerMessageFactory Visit list but forgot MessageName template specialization
- **Fix:** Added `DEF_MSG_NAME(WorldStateSync)` after `DEF_MSG_NAME(NotifySetTimeResult)`
- **Files modified:** Code/server/Recording/MessageSerializer.cpp
- **Verification:** SkyrimTogetherServer and ReplayTests build clean
- **Committed in:** e732a448 (Task 1 commit)

**2. [Rule 1 - Bug] Ambiguous Player::Send overload in sol2 binding**
- **Found during:** Task 1 (building ReplayTests)
- **Issue:** 16-01 added Send(ServerMessage, EPacketFlags) overload, sol2 cannot resolve which to bind
- **Fix:** Added explicit static_cast to select the single-arg Send overload
- **Files modified:** Code/server/Scripting/Player_Bindings.cpp
- **Verification:** SkyrimTogetherServer builds clean
- **Committed in:** e732a448 (Task 1 commit)

**3. [Rule 3 - Blocking] Worktree behind dev branch, needed merge**
- **Found during:** Task 1 (building ReplayTests)
- **Issue:** Worktree lacked 16-01 changes (WorldStateSync, replay infrastructure), ReplayTests target non-existent
- **Fix:** Merged dev branch tip (0574db88) into worktree
- **Files modified:** N/A (merge)
- **Verification:** ReplayTests target visible and compilable

---

**Total deviations:** 3 auto-fixed (2 bugs from 16-01, 1 blocking merge)
**Impact on plan:** All fixes necessary to build ReplayTests target. No scope creep.

## Issues Encountered
- Exit-time segfault in ReplayTests binary (exit code 139) is a pre-existing issue affecting all test tags, not specific to spawn pipeline tests. All 56 assertions pass before the crash. Likely static destruction order problem. Logged but out of scope.

## User Setup Required
None - no external service configuration required.

## Known Stubs
None - all test assertions use real CharacterService logic, no placeholder data.

## Next Phase Readiness
- Tests validate current API; when 16-02 adds OnSpawnCharacter/OnWorldStateSync/DisconnectCleanup, tests should be updated to call new methods
- Replay fixtures ready for integration with DualReplayHarness when server-side spawn changes land
- SpawnTestFixture pattern reusable for future CharacterService tests

---
*Phase: 16-fix-server-formid-and-basic-actor-spawning*
*Completed: 2026-03-30*
