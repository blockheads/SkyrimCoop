---
phase: 09-in-game-p2p-integration-test
plan: 09
subsystem: game-bridge
tags: [pread, proc-mem, diagnostics, formid, hex-dump, deferred-read]

requires:
  - phase: 07-native-linux-build-with-skse-tcp-relay
    provides: ProcMemReader, GameReader, native_client binary
provides:
  - FindModuleBase for module base address lookup in /proc/pid/maps
  - HexDump diagnostic for raw memory inspection
  - LoadMemoryMaps and ValidateAddress for address validation
  - ReadFormIdDeferred with retry+delay for hook timing issues
  - Startup pread smoke test reading SkyrimSE.exe PE header
affects: [09-in-game-p2p-integration-test]

tech-stack:
  added: []
  patterns: [deferred-read-with-retry, pread-smoke-test, hex-dump-diagnostics]

key-files:
  created: []
  modified:
    - Code/native_client/game_bridge/proc_memory.h
    - Code/native_client/game_bridge/proc_memory.cpp
    - Code/native_client/game_bridge/game_reader.h
    - Code/native_client/game_bridge/game_reader.cpp
    - Code/native_client/main.cpp
    - Code/native_client/Services/CharacterService.cpp

key-decisions:
  - "LoadMemoryMaps/ValidateAddress/DumpMemoryMapSummary added to ProcMemReader as infrastructure for FindModuleBase and diagnostic validation"
  - "ReadFormIdDeferred uses usleep-based retry (3 retries, 1ms each) to handle constructor timing race"

patterns-established:
  - "Deferred read pattern: retry with microsecond delay for hook events firing during object construction"
  - "Smoke test pattern: validate pread via known PE header before entering main loop"

requirements-completed: [D-06, D-07, RELAY-10, P2P-01, P2P-02, P2P-03, P2P-04]

duration: 4min
completed: 2026-03-29
---

# Phase 09 Plan 09: pread Diagnostics and FormId Deferred Reads Summary

**ProcMemReader gains FindModuleBase/HexDump/LoadMemoryMaps for diagnostics, GameReader gains ReadFormIdDeferred with retry for hook timing, startup MZ smoke test validates pread works**

## Performance

- **Duration:** 4 min
- **Started:** 2026-03-29T01:49:36Z
- **Completed:** 2026-03-29T01:53:40Z
- **Tasks:** 2
- **Files modified:** 6

## Accomplishments
- ProcMemReader now loads /proc/pid/maps into MemoryRegion vector for address validation and module lookup
- FindModuleBase enables case-insensitive module search by name suffix in mapped regions
- HexDump provides raw pread diagnostic logging (up to 64 bytes) for inspecting memory at failed read addresses
- ReadFormIdDeferred retries formId reads with configurable delay to handle constructor timing races
- Startup smoke test reads SkyrimSE.exe MZ PE header to prove pread works before entering main loop
- All 6 hook event processing paths updated to use ReadFormIdDeferred instead of ReadFormId

## Task Commits

Each task was committed atomically:

1. **Task 1: Add pread smoke test and hex dump diagnostic to ProcMemReader** - `17e5e976` (feat)
2. **Task 2: Add hex dump and deferred read to GameReader for hook pointer diagnostics** - `c2dfdaaf` (feat)

## Files Created/Modified
- `Code/native_client/game_bridge/proc_memory.h` - Added MemoryRegion struct, LoadMemoryMaps, ValidateAddress, FindModuleBase, HexDump, DumpMemoryMapSummary
- `Code/native_client/game_bridge/proc_memory.cpp` - Implementations for all new ProcMemReader methods
- `Code/native_client/game_bridge/game_reader.h` - Added ReadFormIdDeferred declaration
- `Code/native_client/game_bridge/game_reader.cpp` - Enhanced ReadFormId with hex dump on failure, added ReadFormIdDeferred with retry loop
- `Code/native_client/main.cpp` - Startup smoke test reading MZ header, updated DispatchHookEvent to use ReadFormIdDeferred
- `Code/native_client/Services/CharacterService.cpp` - All 4 handler methods updated to use ReadFormIdDeferred

## Decisions Made
- Added LoadMemoryMaps/ValidateAddress/DumpMemoryMapSummary infrastructure to ProcMemReader since the plan referenced m_regions but the code didn't have it -- needed for FindModuleBase
- ReadFormIdDeferred uses usleep-based retry (3 retries, 1ms delay each) as a lightweight workaround for constructor timing without requiring a queue

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 3 - Blocking] Added LoadMemoryMaps, ValidateAddress, DumpMemoryMapSummary infrastructure**
- **Found during:** Task 1
- **Issue:** Plan referenced m_regions, LoadMemoryMaps(), ValidateAddress() in interfaces section but ProcMemReader had none of these
- **Fix:** Added MemoryRegion struct, m_regions vector, LoadMemoryMaps() parsing /proc/pid/maps, ValidateAddress() for region lookup, DumpMemoryMapSummary() for logging
- **Files modified:** proc_memory.h, proc_memory.cpp
- **Verification:** Build passes, FindModuleBase uses m_regions correctly
- **Committed in:** 17e5e976 (Task 1 commit)

---

**Total deviations:** 1 auto-fixed (1 blocking)
**Impact on plan:** Infrastructure was required for FindModuleBase to work. No scope creep.

## Issues Encountered
None

## User Setup Required
None - no external service configuration required.

## Next Phase Readiness
- pread diagnostic infrastructure complete for runtime debugging
- Deferred reads should resolve constructor timing issues with hook events
- Smoke test will immediately surface any pread/permissions failures at startup

## Self-Check: PASSED

All 6 modified files verified present on disk. Both task commits (17e5e976, c2dfdaaf) verified in git log.

---
*Phase: 09-in-game-p2p-integration-test*
*Completed: 2026-03-29*
