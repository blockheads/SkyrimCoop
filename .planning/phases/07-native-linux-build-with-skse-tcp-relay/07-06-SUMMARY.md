---
phase: 07-native-linux-build-with-skse-tcp-relay
plan: 06
subsystem: client
tags: [native-linux, inventory, actor-values, magic, proc-mem, game-reader, tcp-relay]

requires:
  - phase: 07-native-linux-build-with-skse-tcp-relay
    plan: 03
    provides: "TCP client and protocol definitions"
  - phase: 07-native-linux-build-with-skse-tcp-relay
    plan: 04
    provides: "GameReader, ProcMemReader, PointerTable for /proc/pid/mem reads"
provides:
  - "InventoryService handling 6 hook types for inventory sync via GameReader"
  - "ActorValueService with 250ms periodic polling of health/magicka/stamina"
  - "MagicService handling 5 hook types with active cast tracking"
  - "Remote application methods for all three services via command queue"
affects: [07-07, 07-08]

tech-stack:
  added: []
  patterns:
    - "Service pattern: GameReader + TcpClient dependency injection, OnHookEvent dispatch, SendCommand for remote"
    - "Periodic polling with delta detection using cached values and float epsilon comparison"
    - "Active cast tracking with timeout-based cleanup for concentration spells"

key-files:
  created:
    - Code/native_client/Services/InventoryService.h
    - Code/native_client/Services/InventoryService.cpp
    - Code/native_client/Services/ActorValueService.h
    - Code/native_client/Services/ActorValueService.cpp
    - Code/native_client/Services/MagicService.h
    - Code/native_client/Services/MagicService.cpp
  modified: []

key-decisions:
  - "Actor value array read at offset 0x9F0 (base actor values in Skyrim SE Actor struct)"
  - "Float epsilon 0.01 for actor value change detection to avoid noise from floating point drift"
  - "Active cast timeout of 30s for concentration spells that never receive interrupt hook"

patterns-established:
  - "Medium-complexity service rewrite: same GameReader/TcpClient pattern as Plan 05 simple services"
  - "Remote application via SendCommand helper wrapping CommandPacket construction"

requirements-completed: [RELAY-08]

duration: 4min
completed: 2026-03-28
---

# Phase 07 Plan 06: Medium-Complexity Service Rewrite Summary

**InventoryService (6 hooks), ActorValueService (250ms polling), and MagicService (5 hooks + active cast tracking) rewritten for native Linux using GameReader /proc/pid/mem reads**

## Performance

- **Duration:** 4 min
- **Started:** 2026-03-28T16:32:23Z
- **Completed:** 2026-03-28T16:36:21Z
- **Tasks:** 2
- **Files modified:** 6

## Accomplishments
- InventoryService handles all 6 inventory hook types (ADD_INVENTORY, EQUIP, UNEQUIP, PICK_UP, DROP_OBJECT, REMOVE_INV_ITEM) with formId reads via GameReader
- ActorValueService polls health/magicka/stamina at 250ms intervals across all pointer table entries with delta detection
- MagicService handles 5 magic hook types with active concentration spell cast tracking and timeout cleanup
- All three services support remote application by sending commands (CMD_EQUIP_ITEM, CMD_SET_ACTOR_VALUE, CMD_CAST_SPELL, etc.) to DLL relay
- All compile cleanly as part of SkyrimCoopNative with no warnings

## Task Commits

Each task was committed atomically:

1. **Task 1: Rewrite InventoryService and ActorValueService** - `0958caf5` (feat)
2. **Task 2: Rewrite MagicService** - `03b018b8` (feat)

## Files Created/Modified
- `Code/native_client/Services/InventoryService.h` - Header for native inventory service with 6 hook handlers and 4 remote application methods
- `Code/native_client/Services/InventoryService.cpp` - Implementation dispatching hook events to formId-based handlers, sends commands to DLL
- `Code/native_client/Services/ActorValueService.h` - Header for native actor value service with periodic polling and regen hook
- `Code/native_client/Services/ActorValueService.cpp` - Implementation polling actor values at 250ms via PointerTable::ForEach, cached delta detection
- `Code/native_client/Services/MagicService.h` - Header for native magic service with active cast tracking
- `Code/native_client/Services/MagicService.cpp` - Implementation handling 5 magic hooks, reading spell/effect data from memory

## Decisions Made
- Actor value array offset set to 0x9F0 based on Skyrim SE Actor struct layout for base actor values
- Float epsilon of 0.01 used for actor value change detection to filter floating-point noise
- Active cast timeout of 30 seconds to clean up concentration spell casts that never receive an interrupt hook
- Spell casting type read at offset 0xA8 within SpellItem struct

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 1 - Bug] Removed unused variable in MagicService::ApplyRemoteCastSpell**
- **Found during:** Task 2 (MagicService)
- **Issue:** spellPtr variable was looked up but unused since CMD_CAST_SPELL passes formId directly
- **Fix:** Removed the unused LookupPointer call
- **Files modified:** Code/native_client/Services/MagicService.cpp
- **Verification:** Clean build with no warnings
- **Committed in:** 03b018b8

---

**Total deviations:** 1 auto-fixed (1 bug)
**Impact on plan:** Minor cleanup, no scope change.

## Issues Encountered
None

## User Setup Required
None - no external service configuration required.

## Next Phase Readiness
- Combined with Plan 05, 7 of 8 services now compile natively on Linux
- Ready for Plan 07 (CharacterService / AnimationService -- the most complex rewrites)
- All services follow the same GameReader + TcpClient pattern established in Plans 05-06

---
*Phase: 07-native-linux-build-with-skse-tcp-relay*
*Completed: 2026-03-28*
