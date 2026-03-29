---
phase: 09-in-game-p2p-integration-test
plan: 07
subsystem: relay-dll
tags: [minhook, hooks, stability, essential-only, environment-variable]

requires:
  - phase: 07-native-linux-build-with-skse-tcp-relay
    provides: "relay DLL with 31 MinHook trampolines and TCP forwarding"
provides:
  - "Configurable hook installation with essential-only mode (6 of 31 hooks)"
  - "SKYRIM_COOP_ALL_HOOKS environment variable for full hook set"
affects: [09-08, relay-dll-stability, in-game-testing]

tech-stack:
  added: []
  patterns: ["essential-only hook filtering via bool field in HookEntry table"]

key-files:
  created: []
  modified:
    - Code/relay_dll/hook_trampolines.h
    - Code/relay_dll/hook_trampolines.cpp
    - Code/relay_dll/relay_main.cpp

key-decisions:
  - "6 essential hooks: ActorProcess, SetPosition, CharacterCtor, CharacterCtor2, SpawnActorInWorld, AddDeathItems"
  - "Default is essential-only for stability; full set via SKYRIM_COOP_ALL_HOOKS=1"

patterns-established:
  - "HookEntry.essential field for categorizing hook importance"
  - "Environment variable toggle for hook mode selection"

requirements-completed: [D-08, RELAY-10]

duration: 3min
completed: 2026-03-29
---

# Phase 09 Plan 07: Essential-Only Hook Mode Summary

**Configurable relay DLL hook installation defaulting to 6 essential hooks for Skyrim stability during cell loading**

## Performance

- **Duration:** 3 min
- **Started:** 2026-03-29T01:19:29Z
- **Completed:** 2026-03-29T01:22:39Z
- **Tasks:** 2
- **Files modified:** 3

## Accomplishments
- Added essential/non-essential classification to all 31 hook table entries
- Essential-only mode installs 6 hooks for actor lifecycle and position sync
- Environment variable SKYRIM_COOP_ALL_HOOKS=1 enables full 31-hook set
- DLL compiles and links successfully under MinGW

## Task Commits

Each task was committed atomically:

1. **Task 1: Add essential-only hook mode to hook_trampolines** - `45a1c5e7` (feat)
2. **Task 2: Wire essential-only mode via environment variable in relay_main** - `36c7639e` (feat)

## Files Created/Modified
- `Code/relay_dll/hook_trampolines.h` - Updated InstallAllHooks signature with aEssentialOnly parameter
- `Code/relay_dll/hook_trampolines.cpp` - Added essential field to HookEntry, marked 6 hooks essential, filter in install loop
- `Code/relay_dll/relay_main.cpp` - Added SKYRIM_COOP_ALL_HOOKS env var check before InstallAllHooks call

## Decisions Made
- 6 essential hooks chosen: ActorProcess (per-frame updates), SetPosition (position sync), CharacterCtor/Ctor2 (spawn detection), SpawnActorInWorld (spawn), AddDeathItems (death detection)
- Default is essential-only (true) for stability; rationale is that combat/inventory/magic/weather hooks likely cause the Riverwood cell crash

## Deviations from Plan

None - plan executed exactly as written.

## Known Stubs

None.

## Issues Encountered

None.

## User Setup Required

None - no external service configuration required.

## Next Phase Readiness
- Relay DLL now stable enough for Riverwood cell load testing
- Full hook set available via environment variable once stability confirmed
- Ready for Plan 08 (end-to-end in-game test with essential hooks)

---
*Phase: 09-in-game-p2p-integration-test*
*Completed: 2026-03-29*
