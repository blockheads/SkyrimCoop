---
phase: 12-ndjson-replay-capture-and-integration-tests
plan: 02
subsystem: testing
tags: [replay, ndjson, catch2, fixtures, cli, integration-testing]

# Dependency graph
requires:
  - phase: 12-ndjson-replay-capture-and-integration-tests
    plan: 01
    provides: ReplayHarness, DispatchHookEvent, ReplayTests xmake target
provides:
  - 4 golden NDJSON fixture files for replay regression testing
  - 4 Catch2 integration tests validating CharacterService via ReplayHarness
  - --replay CLI flag on skyrim-coop binary for offline NDJSON replay
affects: [regression-testing, ci-pipeline]

# Tech tracking
tech-stack:
  added: []
  patterns: [golden-fixture testing, FIXTURE_PATH macro for portable test paths]

key-files:
  created:
    - tests/fixtures/replay/actor_spawn.ndjson
    - tests/fixtures/replay/actor_remove.ndjson
    - tests/fixtures/replay/movement_sync.ndjson
    - tests/fixtures/replay/multi_actor.ndjson
    - Code/tests/replay/replay_tests.cpp
  modified:
    - Code/tests/replay/xmake.lua
    - Code/native_client/main.cpp
    - Code/native_client/xmake.lua

key-decisions:
  - "FIXTURE_PATH macro resolves from os.projectdir() for worktree-safe absolute paths"
  - "Re-included replay/ sources in SkyrimCoopNative since nlohmann_json already available via third_party"
  - "Real-time mode replays with 16.6ms inter-event delay for debugging at recorded speed"

patterns-established:
  - "Golden fixture files: hand-crafted NDJSON with known formIds and pointer values for hardcoded assertions"
  - "FIXTURE_PATH macro pattern: add_defines with path.absolute(path.join(os.projectdir(), ...))"

requirements-completed: [REPLAY-01, REPLAY-02, REPLAY-03, REPLAY-04, REPLAY-07, REPLAY-08]

# Metrics
duration: 4min
completed: 2026-03-29
---

# Phase 12 Plan 02: Golden Fixtures, Integration Tests, and --replay CLI Flag Summary

**4 Catch2 tests validate actor lifecycle and multi-actor tracking through ReplayHarness with golden NDJSON fixtures; --replay flag enables offline replay from CLI**

## Performance

- **Duration:** 4 min
- **Started:** 2026-03-29T16:38:05Z
- **Completed:** 2026-03-29T16:43:00Z
- **Tasks:** 2
- **Files modified:** 8

## Accomplishments
- Created 4 golden NDJSON fixture files covering actor spawn, remove, movement sync, and multi-actor scenarios
- Wrote 4 Catch2 test cases with 21 hardcoded assertions against registry state, PointerTable, and component data
- Added --replay and --real-time flags to skyrim-coop binary for offline NDJSON replay without Skyrim
- Fixed FIXTURE_PATH macro to resolve from project root for worktree-safe builds
- Verified determinism: same fixture file produces identical state across separate ReplayHarness instances

## Task Commits

Each task was committed atomically:

1. **Task 1: Golden NDJSON fixtures and Catch2 integration tests** - `452f990c` (test)
2. **Task 2: --replay and --real-time CLI flags** - `cfb5042c` (feat)

## Files Created/Modified
- `tests/fixtures/replay/actor_spawn.ndjson` - Player actor_added + set_position (2 events)
- `tests/fixtures/replay/actor_remove.ndjson` - NPC add, position, then remove (3 events)
- `tests/fixtures/replay/movement_sync.ndjson` - Player add + 3 position updates (4 events)
- `tests/fixtures/replay/multi_actor.ndjson` - 3 actors added + positioned (6 events)
- `Code/tests/replay/replay_tests.cpp` - 4 TEST_CASE blocks with 21 assertions
- `Code/tests/replay/xmake.lua` - Fixed FIXTURE_PATH to use os.projectdir()
- `Code/native_client/main.cpp` - Added --replay/--real-time parsing and ReplayHarness early exit
- `Code/native_client/xmake.lua` - Re-included replay/ sources for --replay CLI support

## Decisions Made
- FIXTURE_PATH uses `path.absolute(path.join(os.projectdir(), "tests/fixtures/replay"))` instead of relative path resolution from xmake.lua directory
- Replay sources included in production binary since nlohmann_json was already a transitive dependency via dispatch_hook_event
- Real-time mode implemented as simple 16.6ms inter-event sleep (no timestamp-based pacing)

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 3 - Blocking] Fixed FIXTURE_PATH macro resolution for worktree builds**
- **Found during:** Task 1 (test execution)
- **Issue:** `path.absolute("tests/fixtures/replay")` resolved relative to xmake.lua directory (Code/tests/replay/), not project root
- **Fix:** Changed to `path.absolute(path.join(os.projectdir(), "tests/fixtures/replay"))`
- **Files modified:** Code/tests/replay/xmake.lua
- **Commit:** 452f990c

---

**Total deviations:** 1 auto-fixed (blocking)
**Impact on plan:** Minor fix, no architectural change.

## Known Stubs
None - all code is functional, not stubbed.

## User Setup Required
None - no external service configuration required.

---
*Phase: 12-ndjson-replay-capture-and-integration-tests*
*Completed: 2026-03-29*
