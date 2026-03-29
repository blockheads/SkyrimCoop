---
phase: 13-network-replay-recording-and-regression-testing
plan: 01
subsystem: testing
tags: [ndjson, replay, regression-testing, catch2, service-getters]

# Dependency graph
requires:
  - phase: 12-ndjson-replay-capture-and-integration-tests
    provides: ReplayHarness, NDJSON replay infrastructure, 6 existing fixtures
provides:
  - ReplayHarness getters for all 8 native services
  - State accessors on 7 services for test assertions
  - InventoryService event counter for replay validation
  - 7 synthetic NDJSON fixture files covering all untested services
affects: [13-02, regression-tests]

# Tech tracking
tech-stack:
  added: []
  patterns: [service-state-accessors-for-testing, synthetic-ndjson-fixtures]

key-files:
  created:
    - tests/fixtures/replay/weather_change.ndjson
    - tests/fixtures/replay/calendar_simulate.ndjson
    - tests/fixtures/replay/combat_damage.ndjson
    - tests/fixtures/replay/inventory_changes.ndjson
    - tests/fixtures/replay/actorvalue_regen.ndjson
    - tests/fixtures/replay/magic_spellcast.ndjson
    - tests/fixtures/replay/quest_survival.ndjson
  modified:
    - Code/native_client/replay/replay_harness.h
    - Code/native_client/Services/WeatherService.h
    - Code/native_client/Services/CombatService.h
    - Code/native_client/Services/ActorValueService.h
    - Code/native_client/Services/MagicService.h
    - Code/native_client/Services/InventoryService.h
    - Code/native_client/Services/InventoryService.cpp
    - Code/native_client/Services/QuestService.h

key-decisions:
  - "CachedActorValues struct moved from private to public in ActorValueService for test access"

patterns-established:
  - "Service state accessors: const getter methods on services for test assertion without friend classes"
  - "Synthetic NDJSON fixtures: character_ctor bootstrap + service-specific ops with non-zero formIds"

requirements-completed: [REG-01, REG-02]

# Metrics
duration: 5min
completed: 2026-03-29
---

# Phase 13 Plan 01: Replay Harness Getters and NDJSON Fixtures Summary

**ReplayHarness exposes all 8 services via typed getters, 7 services gain state accessors, and 7 synthetic NDJSON fixtures provide test data for all untested services**

## Performance

- **Duration:** 5 min
- **Started:** 2026-03-29T19:39:52Z
- **Completed:** 2026-03-29T19:45:25Z
- **Tasks:** 2
- **Files modified:** 15

## Accomplishments
- ReplayHarness now exposes all 8 native services (Weather, Calendar, Quest, Combat, Inventory, ActorValue, Magic, Character) via typed getters
- 7 services gained public state accessor methods for test assertions (GetCachedWeatherId, GetDamageEventCount, GetProjectileEventCount, GetCachedValues, GetActiveCastCount, GetEventCount, GetLastQuestEventCount)
- InventoryService tracks event count across 8 hook event branches (add_inventory, pick_up, drop_object, equip, unequip, remove_inv_item, activate, lock_change)
- 7 synthetic NDJSON fixture files created with protocol-conformant field names matching json_protocol.h constants
- All 13 fixtures (6 existing + 7 new) parse as valid JSON
- Existing 5 test cases (26 assertions) pass without regressions

## Task Commits

Each task was committed atomically:

1. **Task 1: Add service getters to ReplayHarness and state accessors to services** - `f1abced4` (feat)
2. **Task 2: Create 7 synthetic NDJSON fixture files** - `b2a9af58` (feat)

## Files Created/Modified
- `Code/native_client/replay/replay_harness.h` - Added 7 service getters (Weather through Magic)
- `Code/native_client/Services/WeatherService.h` - Added GetCachedWeatherId() const getter
- `Code/native_client/Services/CombatService.h` - Added GetDamageEventCount(), GetProjectileEventCount() const getters
- `Code/native_client/Services/ActorValueService.h` - Moved CachedActorValues to public, added GetCachedValues() const getter
- `Code/native_client/Services/MagicService.h` - Added GetActiveCastCount() const getter
- `Code/native_client/Services/InventoryService.h` - Added m_eventCount member and GetEventCount() const getter
- `Code/native_client/Services/InventoryService.cpp` - Added m_eventCount++ in 8 hook event branches
- `Code/native_client/Services/QuestService.h` - Added GetLastQuestEventCount() const getter
- `tests/fixtures/replay/weather_change.ndjson` - WeatherService test fixture (4 events)
- `tests/fixtures/replay/calendar_simulate.ndjson` - CalendarService test fixture (2 events)
- `tests/fixtures/replay/combat_damage.ndjson` - CombatService test fixture (5 events)
- `tests/fixtures/replay/inventory_changes.ndjson` - InventoryService test fixture (5 events)
- `tests/fixtures/replay/actorvalue_regen.ndjson` - ActorValueService test fixture (3 events)
- `tests/fixtures/replay/magic_spellcast.ndjson` - MagicService test fixture (5 events)
- `tests/fixtures/replay/quest_survival.ndjson` - QuestService pipeline exercise (2 events)

## Decisions Made
- CachedActorValues struct moved from private to public section in ActorValueService.h to allow test code to inspect cached values without friend classes

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 3 - Blocking] Merged dev branch into worktree to access Phase 12 code**
- **Found during:** Task 1 (reading source files)
- **Issue:** Worktree was behind dev and missing all Phase 12 replay infrastructure (replay_harness.h, fixtures, json_protocol.h, JSON-based service APIs)
- **Fix:** Merged dev into worktree to get Phase 12 commits
- **Files modified:** N/A (git merge)
- **Verification:** All source files accessible, build succeeds

**2. [Rule 1 - Bug] Adapted plan to JSON-based InventoryService API**
- **Found during:** Task 1 (reading InventoryService.cpp)
- **Issue:** Plan described binary HookEventPacket-based API with HandleAddInventory/HandleEquip methods, but Phase 11 converted all services to JSON wire protocol with if/else op-string dispatch
- **Fix:** Added m_eventCount++ in the correct JSON-based if/else branches instead of binary handler methods
- **Files modified:** Code/native_client/Services/InventoryService.cpp
- **Verification:** xmake build ReplayTests compiles, all 26 assertions pass

---

**Total deviations:** 2 auto-fixed (1 blocking, 1 bug)
**Impact on plan:** Both deviations necessary for correct execution. The JSON API adaptation was mechanical -- same counter placement logic, different code structure. No scope creep.

## Issues Encountered
- ReplayTests binary segfaults during cleanup after all tests pass -- pre-existing issue, not caused by this plan's changes

## User Setup Required
None - no external service configuration required.

## Next Phase Readiness
- Plan 02 can now write per-service Catch2 regression tests using the new getters and fixture files
- All 8 services accessible via ReplayHarness typed getters
- 13 total fixture files available for replay testing

---
*Phase: 13-network-replay-recording-and-regression-testing*
*Completed: 2026-03-29*
