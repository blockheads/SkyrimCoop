---
phase: 10-two-player-co-op-session
plan: 03
subsystem: testing
tags: [bash, proton, dual-instance, skse, wine, co-op]

requires:
  - phase: 10-two-player-co-op-session
    provides: SKYRIM_COOP_PORT env var in relay DLL (plan 02), native_client CLI with --connect flag (plan 01)
provides:
  - Dual-instance launch script with deterministic relay ports
  - Complete two-player co-op test orchestration
affects: [10-04, two-player-session, manual-testing]

tech-stack:
  added: []
  patterns: [SKYRIM_COOP_PORT env var for deterministic port binding, wait_for_port over relay_info.json polling]

key-files:
  created:
    - tests/launch_two_player.sh
  modified: []

key-decisions:
  - "Use wait_for_port (ss -tln) instead of relay_info.json polling to detect DLL readiness"
  - "Persistent client prefix at COMPAT_HOST_client instead of /tmp ephemeral clone"
  - "Deploy DLL from build dir if available, otherwise assume pre-deployed"

patterns-established:
  - "Deterministic port pattern: host=9667, client=9668 via SKYRIM_COOP_PORT env var"
  - "Dual Proton prefix pattern: persistent client prefix alongside host prefix"

requirements-completed: [D-10, D-11, D-12, D-13, D-15, D-21]

duration: 2min
completed: 2026-03-29
---

# Phase 10 Plan 03: Two-Player Co-op Launch Script Summary

**Dual-instance Skyrim launch script using SKYRIM_COOP_PORT for race-free port assignment with separate Proton prefixes**

## Performance

- **Duration:** 2 min
- **Started:** 2026-03-29T05:30:54Z
- **Completed:** 2026-03-29T05:33:03Z
- **Tasks:** 1
- **Files created:** 1

## Accomplishments
- Complete orchestration script for two-player co-op testing on one Linux machine
- Deterministic relay ports (host=9667, client=9668) via SKYRIM_COOP_PORT env var eliminate relay_info.json race
- Separate Proton prefixes for host and client instances (persistent, not ephemeral)
- Both instances configured for windowed 1280x720 + sStartingCell=Riverwood
- Client connects to host GameServer at 127.0.0.1:10578 via --connect flag
- Graceful cleanup trap kills all processes and restores INI files

## Task Commits

Each task was committed atomically:

1. **Task 1: Create dual-instance launch script with SKYRIM_COOP_PORT** - `1e83b0df` (feat)

## Files Created/Modified
- `tests/launch_two_player.sh` - Complete dual-instance co-op launch orchestration script

## Decisions Made
- Used persistent client prefix (`COMPAT_HOST_client`) instead of ephemeral `/tmp` clone -- avoids re-copying 200MB+ prefix each run
- Used `wait_for_port` via `ss -tln` instead of relay_info.json polling -- simpler, no file race, directly validates DLL is ready
- Paths match existing `launch_two_skyrim.sh` from reference_skyrim_testing.md rather than plan template paths

## Deviations from Plan

None - plan executed exactly as written.

## Issues Encountered
None

## User Setup Required
None - no external service configuration required.

## Next Phase Readiness
- Launch script ready for manual two-player co-op testing
- Plan 04 (verification) can use this script to validate the full pipeline

---
*Phase: 10-two-player-co-op-session*
*Completed: 2026-03-29*
