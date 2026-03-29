---
phase: 09-in-game-p2p-integration-test
plan: 02
subsystem: testing
tags: [mingw, wine, tcp, relay-dll, headless-test, binary-recording]

requires:
  - phase: 07-native-linux-build-with-skse-tcp-relay
    provides: relay DLL with TCP server, protocol.h, SKSEPlugin_Load entry point
  - phase: 08-p2p-embedded-gameserver
    provides: native client main.cpp with event loop and service dispatch

provides:
  - mock_skyrim_loader.exe MinGW binary for headless relay DLL testing under Wine
  - --record flag in native process for binary hook event recording
  - XMake target for mock_skyrim_loader on MinGW platform

affects: [09-03, 09-04, 09-05, CI-pipeline]

tech-stack:
  added: []
  patterns: [headless-dll-testing-via-loadlibrary, binary-event-recording]

key-files:
  created:
    - Code/tests/integration/mock_skyrim_loader.cpp
  modified:
    - Code/tests/integration/xmake.lua
    - Code/native_client/main.cpp
    - Code/xmake.lua

key-decisions:
  - "mock_skyrim_loader derives relay_info.json path from DLL path (same directory convention)"
  - "Binary record format: uint64 timestamp_us + HookEventPacket per entry (simple, no framing needed)"
  - "Integration tests included separately for mingw in Code/xmake.lua since tests/ excluded on mingw"

patterns-established:
  - "Headless DLL testing: LoadLibrary + SKSEPlugin_Load(nullptr) to start TCP server without Skyrim"
  - "Binary event recording: timestamp + raw packet for CI replay"

requirements-completed: [D-03, D-06, D-14]

duration: 3min
completed: 2026-03-29
---

# Phase 09 Plan 02: Mock Skyrim Loader and Event Recording Summary

**Headless relay DLL test binary (mock_skyrim_loader.exe) and --record flag for binary hook event capture in native process**

## Performance

- **Duration:** 3 min
- **Started:** 2026-03-29T00:11:10Z
- **Completed:** 2026-03-29T00:15:06Z
- **Tasks:** 1
- **Files modified:** 4

## Accomplishments
- Created mock_skyrim_loader.cpp (~150 LOC) that loads relay DLL via LoadLibrary, calls SKSEPlugin_Load(nullptr), reads relay_info.json for TCP port, connects, sends CTRL_READY, then scripted HOOK_ACTOR_ADDED + HOOK_SET_POSITION events
- Added --record <file> flag to native process that writes timestamped binary log of all received HookEventPackets
- Both targets build successfully: SkyrimCoopNative on Linux, mock_skyrim_loader on MinGW

## Task Commits

Each task was committed atomically:

1. **Task 1: Create mock_skyrim_loader.cpp and add --record to native process** - `32eb032a` (feat)

## Files Created/Modified
- `Code/tests/integration/mock_skyrim_loader.cpp` - Headless relay DLL test binary (~150 LOC MinGW C++)
- `Code/tests/integration/xmake.lua` - Added mock_skyrim_loader target (MinGW, static-linked)
- `Code/native_client/main.cpp` - Added --record flag for binary hook event recording
- `Code/xmake.lua` - Include integration tests on mingw platform for mock_skyrim_loader

## Decisions Made
- mock_skyrim_loader derives relay_info.json path from DLL path by replacing filename with skyrim_coop_relay.json (matching DLL convention)
- Binary record format is simple: uint64 microsecond timestamp + raw HookEventPacket struct, no framing overhead
- Integration tests directory included separately for mingw in Code/xmake.lua since Code/tests is excluded on mingw platform

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 3 - Blocking] Added mingw include for integration tests in Code/xmake.lua**
- **Found during:** Task 1 (XMake target for mock_skyrim_loader)
- **Issue:** Code/tests is excluded on mingw platform (`if not is_plat("mingw") then includes("tests") end`), so mock_skyrim_loader target was invisible
- **Fix:** Added `if is_plat("mingw") then includes("tests/integration") end` at end of Code/xmake.lua
- **Files modified:** Code/xmake.lua
- **Verification:** `xmake f -p mingw && xmake build mock_skyrim_loader` succeeds
- **Committed in:** 32eb032a (Task 1 commit)

---

**Total deviations:** 1 auto-fixed (1 blocking)
**Impact on plan:** Essential fix for mock_skyrim_loader to be discoverable on mingw platform. No scope creep.

## Issues Encountered
None beyond the auto-fixed xmake include issue.

## User Setup Required
None - no external service configuration required.

## Known Stubs
None - all functionality is fully wired.

## Next Phase Readiness
- mock_skyrim_loader.exe ready for integration with deploy_and_test.sh (Plan 03)
- --record flag ready for CI replay testing (Plan 04/05)
- Both targets verified to compile on their respective platforms

---
*Phase: 09-in-game-p2p-integration-test*
*Completed: 2026-03-29*
