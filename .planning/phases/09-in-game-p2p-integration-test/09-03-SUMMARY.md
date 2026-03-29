---
phase: 09-in-game-p2p-integration-test
plan: 03
subsystem: testing
tags: [integration-test, proc-mem, pe-header, enet6, join-mode, log-collection]

# Dependency graph
requires:
  - phase: 05-testing-framework
    provides: "MockClient, fake_player binary, GameServer test infrastructure"
  - phase: 08-p2p-embedded-gameserver
    provides: "skyrim-coop --host-only embedded GameServer, LoopbackTransport"
  - phase: 09-01
    provides: "Multi-bot fake_player with BroadcastTrackingClient"
  - phase: 09-02
    provides: "mock_skyrim_loader and --record flag"
provides:
  - "test_ingame_sync.sh with default fake_player, port readiness, and log collection"
  - "fake_player /proc/pid/mem PE header verification via ProcMemReader"
  - "JoinClient enet6 client for --connect join mode in native_client"
affects: [09-04, 09-05, CI-pipeline]

# Tech tracking
tech-stack:
  added: []
  patterns: ["PE header verification via /proc/pid/mem for process memory access validation", "JoinClient pattern inheriting TiltedPhoques::Client for remote host connection"]

key-files:
  created: []
  modified:
    - tests/test_ingame_sync.sh
    - Code/tests/integration/fake_player.cpp
    - Code/native_client/main.cpp

key-decisions:
  - "Inverted flag from --with-fake-player to --no-fake-player so fake_player runs by default"
  - "Port readiness via ss -ulnp polling before launching fake_player (30s timeout)"
  - "Log collection to /tmp/skyrimcoop-test-XXXXXX/ with --keep-logs for failure preservation"
  - "PE verification reads /proc/pid/maps to find SkyrimSE.exe base, then validates MZ+PE signatures"
  - "JoinClient uses same PacketView/Send pattern as MockClient for auth request serialization"

patterns-established:
  - "Log collection pattern: mktemp directory, redirect all subprocess output, tail on failure"
  - "PE header verification: FindSkyrimBaseAddress from /proc/pid/maps + ProcMemReader MZ/PE check"
  - "JoinClient: TiltedPhoques::Client subclass with ServerMessageFactory for message parsing"

requirements-completed: [D-01, D-04, D-06, D-07, D-15]

# Metrics
duration: 5min
completed: 2026-03-29
---

# Phase 09 Plan 03: Test Script Fixes, PE Verification, and Join Mode Client Summary

**Fixed test_ingame_sync.sh with default fake_player and log collection, added /proc/pid/mem PE header verification, and implemented JoinClient enet6 connection for join mode**

## Performance

- **Duration:** 5 min
- **Started:** 2026-03-29T00:17:29Z
- **Completed:** 2026-03-29T00:23:27Z
- **Tasks:** 3
- **Files modified:** 3

## Accomplishments
- test_ingame_sync.sh now runs fake_player by default (--no-fake-player to skip), checks GameServer UDP port readiness via ss -ulnp, and collects all process logs to /tmp/skyrimcoop-test-XXXXXX/
- fake_player --verify-pid reads /proc/pid/maps to find SkyrimSE.exe base address, then validates MZ (0x5A4D) and PE signatures via ProcMemReader
- JoinClient class in native_client/main.cpp replaces the TODO stub: --connect flag creates enet6 client, connects to remote GameServer, authenticates with AuthenticationRequest
- All three modified files pass syntax check (bash -n) or build successfully (xmake build)

## Task Commits

Each task was committed atomically:

1. **Task 1: Fix test_ingame_sync.sh -- default fake_player, port readiness, log collection** - `e85f82fd` (feat)
2. **Task 2: Implement /proc/pid/mem PE header verification in fake_player** - `29c07271` (feat)
3. **Task 3: Implement JoinClient in native_client/main.cpp for join mode** - `b5dd63b2` (feat)

## Files Created/Modified
- `tests/test_ingame_sync.sh` - Inverted to --no-fake-player, added wait_for_gameserver_port(), setup_test_logs(), fail_with_logs(), --keep-logs flag, TEST_FAILED tracking
- `Code/tests/integration/fake_player.cpp` - Added FindSkyrimBaseAddress(), VerifyHostMemory() with MZ/PE signature validation via ProcMemReader
- `Code/native_client/main.cpp` - Added JoinClient class, replaced TODO stub with connection/auth logic, added to main loop and shutdown

## Decisions Made
- Inverted --with-fake-player to --no-fake-player so fake_player runs by default (embedded GameServer now exists)
- Port readiness uses ss -ulnp for UDP port checking (lightweight, no extra dependencies)
- Log collection to /tmp with --keep-logs flag; TEST_FAILED flag auto-preserves logs on failure
- PE verification finds base from /proc/pid/maps r-xp segment, validates DOS header (MZ) and PE signature
- JoinClient follows MockClient pattern: PacketView + Send for serialization, ServerMessageFactory for parsing

## Deviations from Plan

None - plan executed exactly as written.

## Issues Encountered
None.

## User Setup Required
None - no external service configuration required.

## Known Stubs
None - all functionality is fully wired.

## Next Phase Readiness
- test_ingame_sync.sh ready for full end-to-end testing with real Skyrim instance
- fake_player PE verification ready for integration test validation
- JoinClient ready for Plan 04 two-instance testing (host + join connection)
- All targets build successfully on Linux

---
*Phase: 09-in-game-p2p-integration-test*
*Completed: 2026-03-29*

## Self-Check: PASSED
