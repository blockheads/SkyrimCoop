---
phase: 11-json-wire-protocol
plan: 04
subsystem: networking
tags: [json, ndjson, nlohmann-json, native-client, service-dispatch, command-sender]

# Dependency graph
requires:
  - phase: 11-json-wire-protocol (plan 01)
    provides: json_protocol.h shared constants (JsonOp, JsonField namespaces)
  - phase: 11-json-wire-protocol (plan 03)
    provides: JSON-based OnHookEvent stubs in all 8 services, SendJsonLine API on TcpClient
provides:
  - Full JSON dispatch logic in all 8 native client services
  - All command senders converted from binary CommandPacket to JSON SendJsonLine
  - Zero binary protocol references remaining in service or system code
affects: [native-client, relay-dll, in-game-testing]

# Tech tracking
tech-stack:
  added: []
  patterns:
    - "JSON field access via aMsg.value(JsonField::kFieldName, default) for safe extraction"
    - "JSON command building via nlohmann::json initializer list + m_client.SendJsonLine(cmd)"
    - "Service OnHookEvent dispatches on op string using if/else chain with JsonOp:: constants"

key-files:
  created: []
  modified:
    - Code/native_client/Services/CharacterService.cpp
    - Code/native_client/Services/CharacterService.h
    - Code/native_client/Services/WeatherService.cpp
    - Code/native_client/Services/WeatherService.h
    - Code/native_client/Services/CalendarService.cpp
    - Code/native_client/Services/QuestService.cpp
    - Code/native_client/Services/CombatService.cpp
    - Code/native_client/Services/CombatService.h
    - Code/native_client/Services/InventoryService.cpp
    - Code/native_client/Services/InventoryService.h
    - Code/native_client/Services/ActorValueService.cpp
    - Code/native_client/Services/ActorValueService.h
    - Code/native_client/Services/MagicService.cpp
    - Code/native_client/Services/MagicService.h
    - Code/native_client/Systems/AnimationSystem.cpp
    - Code/native_client/Systems/AnimationSystem.h
    - Code/native_client/Systems/InterpolationSystem.h

key-decisions:
  - "QuestService OnHookEvent is a log-only placeholder since no quest hooks exist in the relay DLL yet"
  - "AnimationSystem converted from binary to JSON as Rule 3 deviation (blocking reference to protocol.h)"

patterns-established:
  - "Service OnHookEvent pattern: extract op string, dispatch via if/else chain, use .value() for safe field access"
  - "Command sender pattern: build nlohmann::json object with JsonOp/JsonField constants, send via SendJsonLine"

requirements-completed: [JSON-08]

# Metrics
duration: 8min
completed: 2026-03-29
---

# Phase 11 Plan 04: Native Service JSON Dispatch Summary

**All 8 native client services converted from binary stub/CommandPacket to full JSON dispatch and JSON command senders, completing the native half of the big-bang cutover**

## Performance

- **Duration:** 8 min
- **Started:** 2026-03-29T15:11:15Z
- **Completed:** 2026-03-29T15:19:43Z
- **Tasks:** 2
- **Files modified:** 17

## Accomplishments
- CharacterService: full JSON dispatch with position cache, actor lifecycle (6 ops), and perform action handler; SendSetPosition/SendPlayAnimation converted to JSON
- All 7 remaining services: WeatherService (3 ops + SendForceWeather), CalendarService (simulate_time), QuestService (placeholder), CombatService (3 ops + 2 senders), InventoryService (8 ops + 4 senders), ActorValueService (regen_attr + SetActorValue), MagicService (4 ops + 3 senders)
- Zero binary protocol references (HookEventPacket, FloatFromU64, U64ToFloat, CommandPacket, protocol.h) remain in any native client service or system file

## Task Commits

Each task was committed atomically:

1. **Task 1: CharacterService JSON dispatch and command senders** - `732a438f` (feat)
2. **Task 2: Remaining 7 services JSON dispatch and command senders** - `22d75cf2` (feat)

## Files Created/Modified
- `Code/native_client/Services/CharacterService.cpp` - Full JSON dispatch (11 ops), JSON command senders
- `Code/native_client/Services/CharacterService.h` - Updated comments to reference JSON ops
- `Code/native_client/Services/WeatherService.cpp` - JSON dispatch (3 ops), SendForceWeather JSON sender
- `Code/native_client/Services/WeatherService.h` - Added SendForceWeather declaration
- `Code/native_client/Services/CalendarService.cpp` - JSON dispatch for simulate_time
- `Code/native_client/Services/QuestService.cpp` - Placeholder JSON dispatch (no quest hooks yet)
- `Code/native_client/Services/CombatService.cpp` - JSON dispatch (3 ops), 2 JSON command senders
- `Code/native_client/Services/CombatService.h` - Added SendDamageActor/SendLaunchProjectile declarations
- `Code/native_client/Services/InventoryService.cpp` - JSON dispatch (8 ops), 4 JSON command senders
- `Code/native_client/Services/InventoryService.h` - Removed old binary handler declarations
- `Code/native_client/Services/ActorValueService.cpp` - JSON dispatch for regen_attr, JSON SetActorValue sender
- `Code/native_client/Services/ActorValueService.h` - Removed old HandleRegenAttr binary declaration
- `Code/native_client/Services/MagicService.cpp` - JSON dispatch (4 ops), 3 JSON command senders
- `Code/native_client/Services/MagicService.h` - Removed old binary handler declarations
- `Code/native_client/Systems/AnimationSystem.cpp` - Converted CMD_PLAY_ANIMATION to JSON
- `Code/native_client/Systems/AnimationSystem.h` - Updated comment
- `Code/native_client/Systems/InterpolationSystem.h` - Updated comment

## Decisions Made
- QuestService OnHookEvent is a log-only placeholder since no quest hooks exist in the relay DLL yet; it will be wired when quest hooks are added
- AnimationSystem was converted from binary to JSON as a Rule 3 deviation since it was the last remaining binary command sender in the native client

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 3 - Blocking] Converted AnimationSystem from binary to JSON**
- **Found during:** Task 1 (CharacterService)
- **Issue:** AnimationSystem.cpp still included protocol.h and used CommandPacket for CMD_PLAY_ANIMATION, violating the plan's goal of zero binary protocol references
- **Fix:** Converted to JSON using SendJsonLine with JsonOp::kCmdPlayAnimation
- **Files modified:** Code/native_client/Systems/AnimationSystem.cpp, Code/native_client/Systems/AnimationSystem.h
- **Verification:** Build compiles, grep confirms no binary references
- **Committed in:** 732a438f (Task 1 commit)

---

**Total deviations:** 1 auto-fixed (1 blocking)
**Impact on plan:** Auto-fix was necessary to achieve the plan's stated goal of zero binary protocol references in native client service/system code. No scope creep.

## Issues Encountered
None

## User Setup Required
None - no external service configuration required.

## Next Phase Readiness
- All native client service code speaks JSON with real logic, not stubs
- Ready for Plan 05 (if any) or phase transition/verification
- The relay DLL command receiver (relay_main.cpp) already handles JSON commands from Plan 02

---
*Phase: 11-json-wire-protocol*
*Completed: 2026-03-29*
