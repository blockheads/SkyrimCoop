---
phase: 09-in-game-p2p-integration-test
plan: 04
subsystem: testing
tags: [developer-tool, two-instance, proton-prefix, windowed-mode, co-op-testing]

# Dependency graph
requires:
  - phase: 09-01
    provides: "Multi-bot fake_player with BroadcastTrackingClient"
  - phase: 09-02
    provides: "mock_skyrim_loader and --record flag"
  - phase: 09-03
    provides: "JoinClient enet6 client for --connect join mode, test_ingame_sync.sh patterns"
provides:
  - "tests/launch_two_skyrim.sh: two-instance developer tool for manual co-op testing"
affects: [09-05, manual-testing]

# Tech tracking
tech-stack:
  added: []
  patterns: ["Proton prefix cloning for multi-instance Skyrim", "Sequential relay_info.json capture to prevent port collision", "INI injection for windowed 1280x720 with bAlwaysActive=1 mouse unlock"]

key-files:
  created:
    - tests/launch_two_skyrim.sh
  modified: []

key-decisions:
  - "Sequential relay_info.json capture: host relay info consumed, then deleted before join launch to prevent collision"
  - "Proton prefix cloned to /tmp with $$ PID suffix for isolation between host and join instances"
  - "Window positioning via wmctrl with xdotool fallback for side-by-side 1280x720 layout"

patterns-established:
  - "Two-instance launch pattern: sequential Proton startup with prefix isolation"
  - "INI injection for windowed mode: remove existing display keys, inject after section headers"

requirements-completed: [D-08, D-09, D-10, D-11, D-12, D-13]

# Metrics
duration: 2min
completed: 2026-03-29
---

# Phase 09 Plan 04: Two-Instance Developer Tool Summary

**Bash script launching two Skyrim instances side-by-side at 1280x720 with separate Proton prefixes, host GameServer on UDP 10578, and join via JoinClient --connect**

## Performance

- **Duration:** 2 min
- **Started:** 2026-03-29T00:25:56Z
- **Completed:** 2026-03-29T00:28:27Z
- **Tasks:** 1 of 2 (Task 2 is checkpoint:human-verify, awaiting manual testing)
- **Files created:** 1

## Accomplishments
- tests/launch_two_skyrim.sh creates a complete two-instance developer tool for manual co-op testing
- Script handles sequential relay_info.json capture to prevent port collision between host and join instances
- Proton prefix cloned via cp -a for join instance isolation, with automatic cleanup on exit
- INI injection sets 1280x720 windowed mode, bFull Screen=0, bBorderless=0, bAlwaysActive=1 for mouse unlock
- Window positioning via wmctrl/xdotool places both instances side-by-side

## Task Commits

Each task was committed atomically:

1. **Task 1: Create tests/launch_two_skyrim.sh developer tool** - `8421e410` (feat)
2. **Task 2: Verify two-instance tool launches correctly** - checkpoint:human-verify (awaiting manual testing)

## Files Created/Modified
- `tests/launch_two_skyrim.sh` - Two-instance developer tool: launches host + join Skyrim with separate Proton prefixes, 1280x720 windowed, side-by-side, with sequential relay_info.json handling, GameServer on port 10578, and full cleanup

## Decisions Made
- Sequential relay_info.json capture: host's relay info captured and file deleted before join launch, so join writes fresh info with different port/PID
- Proton prefix cloned to /tmp/skyrimcoop_join_prefix_$$ for process isolation
- Window positioning tries wmctrl first, falls back to xdotool, warns if neither available
- --host-only flag for launching just the host instance without join

## Deviations from Plan

None - plan executed exactly as written.

## Issues Encountered
None.

## User Setup Required
None - no external service configuration required.

## Known Stubs
None - all functionality is fully wired.

## Checkpoint Pending

**Task 2 (checkpoint:human-verify):** Two-instance tool needs manual verification by running `./tests/launch_two_skyrim.sh` and confirming both Skyrim windows appear side-by-side at 1280x720, host GameServer starts, join connects via JoinClient, and cleanup works on exit.

## Next Phase Readiness
- tests/launch_two_skyrim.sh ready for manual co-op testing
- Depends on Plan 03's JoinClient implementation for --connect join mode
- Plan 05 (full end-to-end validation) can proceed independently

---
*Phase: 09-in-game-p2p-integration-test*
*Completed: 2026-03-29*

## Self-Check: PASSED
