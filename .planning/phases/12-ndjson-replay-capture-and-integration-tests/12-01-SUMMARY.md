---
phase: 12-ndjson-replay-capture-and-integration-tests
plan: 01
subsystem: testing
tags: [replay, ndjson, catch2, entt, nlohmann-json, integration-testing]

# Dependency graph
requires:
  - phase: 07-native-linux-build-with-skse-tcp-relay
    provides: native_client services, systems, game_bridge, protocol.h
provides:
  - DispatchHookEvent as shared linkable function (not static)
  - ReplayHarness class for replaying NDJSON hook events through full service stack
  - ReplayTests xmake target with Catch2 runner
  - TcpClient no-op behavior when disconnected (m_socket=-1)
affects: [12-02, integration-tests, replay-capture]

# Tech tracking
tech-stack:
  added: [nlohmann_json v3.11.3]
  patterns: [NDJSON-to-HookEventPacket conversion, stub dependency injection for offline testing]

key-files:
  created:
    - Code/native_client/dispatch_hook_event.h
    - Code/native_client/dispatch_hook_event.cpp
    - Code/native_client/replay/replay_harness.h
    - Code/native_client/replay/replay_harness.cpp
    - Code/tests/replay/xmake.lua
    - Code/tests/replay/main.cpp
  modified:
    - Code/native_client/main.cpp
    - Code/native_client/game_bridge/tcp_client.cpp
    - Code/native_client/xmake.lua
    - Code/tests/xmake.lua
    - xmake.lua

key-decisions:
  - "Adapted plan from JSON dispatch to binary HookEventPacket dispatch (actual codebase uses binary protocol, not JSON)"
  - "ReplayHarness converts NDJSON to HookEventPacket internally, keeping binary dispatch path unchanged"
  - "Removed SkyrimTogetherServer dependency from ReplayTests (caused segfault from global destructors)"
  - "Excluded replay/ from SkyrimCoopNative glob to avoid nlohmann_json dependency contamination"

patterns-established:
  - "NDJSON replay format: {\"op\": <uint16_opcode>, \"args\": [<uint64>, ...]} one per line"
  - "ReplayHarness pattern: stub ProcMemReader (fd=-1) + stub TcpClient (no-op sends) for offline testing"

requirements-completed: [REPLAY-01, REPLAY-02, REPLAY-03, REPLAY-05, REPLAY-06]

# Metrics
duration: 8min
completed: 2026-03-29
---

# Phase 12 Plan 01: Replay Harness Foundation Summary

**DispatchHookEvent extracted to shared module, ReplayHarness converts NDJSON to binary HookEventPacket for offline service-stack testing**

## Performance

- **Duration:** 8 min
- **Started:** 2026-03-29T16:17:38Z
- **Completed:** 2026-03-29T16:26:29Z
- **Tasks:** 2
- **Files modified:** 11

## Accomplishments
- Extracted DispatchHookEvent from main.cpp into shared dispatch_hook_event.h/.cpp for use by both native client and test harness
- Created ReplayHarness class that constructs all 8 services with stub deps and replays NDJSON files through binary dispatch
- Built ReplayTests xmake target that compiles, links, and runs cleanly (exits 0 with no test cases)
- TcpClient::Send returns true when disconnected, enabling silent no-op in replay/test mode

## Task Commits

Each task was committed atomically:

1. **Task 1: Extract DispatchHookEvent and make TcpClient no-op** - `43b352ca` (feat)
2. **Task 2: Create ReplayHarness class and ReplayTests xmake target** - `7b3db548` (feat)

## Files Created/Modified
- `Code/native_client/dispatch_hook_event.h` - Shared DispatchHookEvent declaration
- `Code/native_client/dispatch_hook_event.cpp` - DispatchHookEvent implementation (moved from main.cpp)
- `Code/native_client/replay/replay_harness.h` - ReplayHarness class with all 8 services and stub deps
- `Code/native_client/replay/replay_harness.cpp` - NDJSON parsing, JSON-to-HookEventPacket conversion, dispatch
- `Code/tests/replay/xmake.lua` - ReplayTests build target with all native_client sources
- `Code/tests/replay/main.cpp` - Catch2 runner with rpmalloc init
- `Code/native_client/main.cpp` - Removed static DispatchHookEvent, added include
- `Code/native_client/game_bridge/tcp_client.cpp` - Send returns true when m_socket=-1
- `Code/native_client/xmake.lua` - Exclude replay/ from native client glob
- `Code/tests/xmake.lua` - Added target_end() and includes("replay")
- `xmake.lua` - Added nlohmann_json v3.11.3 dependency

## Decisions Made
- Adapted plan from JSON-based dispatch to binary HookEventPacket dispatch (actual codebase uses binary protocol, not JSON as plan assumed)
- ReplayHarness parses NDJSON JSON and converts to HookEventPacket before calling DispatchHookEvent, keeping the binary dispatch path unchanged
- Removed SkyrimTogetherServer dependency from ReplayTests (caused segfault from global destructor order issues; native_client services don't actually need it)
- Excluded replay/ from SkyrimCoopNative's `**.cpp` glob to prevent nlohmann_json from becoming a dependency of the production binary

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 1 - Bug] Removed SkyrimTogetherServer dependency causing segfault**
- **Found during:** Task 2 (ReplayTests build/run)
- **Issue:** Linking ReplayTests against SkyrimTogetherServer caused segfault during exit handlers (global destructor order)
- **Fix:** Removed `add_deps("SkyrimTogetherServer")` from replay xmake.lua; native_client services link fine without it
- **Files modified:** Code/tests/replay/xmake.lua
- **Verification:** ReplayTests runs and exits 0 cleanly
- **Committed in:** 7b3db548 (Task 2 commit)

**2. [Rule 3 - Blocking] Adapted JSON dispatch to binary HookEventPacket dispatch**
- **Found during:** Task 1 (reading actual codebase)
- **Issue:** Plan assumed DispatchHookEvent takes nlohmann::json, but actual code uses binary HookEventPacket
- **Fix:** Kept binary dispatch, added JSON-to-HookEventPacket conversion in ReplayHarness
- **Files modified:** Code/native_client/replay/replay_harness.cpp
- **Verification:** ReplayTests builds and runs cleanly
- **Committed in:** 7b3db548 (Task 2 commit)

---

**Total deviations:** 2 auto-fixed (1 bug, 1 blocking)
**Impact on plan:** Both fixes necessary for correctness. Binary dispatch is more faithful to production code path.

## Issues Encountered
- xmake auto-detects project root directory instead of worktree; required `-P .` flag to build from worktree
- tests/xmake.lua needed `target_end()` before `includes("replay")` to avoid nesting ReplayTests inside TPTests target scope

## Known Stubs
None - all code is functional, not stubbed.

## User Setup Required
None - no external service configuration required.

## Next Phase Readiness
- ReplayHarness ready for test cases in Plan 02
- NDJSON fixture files needed (to be created in Plan 02)
- Test assertions against registry state, pointer table, and service behavior

---
*Phase: 12-ndjson-replay-capture-and-integration-tests*
*Completed: 2026-03-29*
