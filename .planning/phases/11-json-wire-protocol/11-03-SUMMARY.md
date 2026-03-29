---
phase: 11-json-wire-protocol
plan: 03
subsystem: networking
tags: [ndjson, json, tcp, wire-protocol, nlohmann, native-client]

requires:
  - phase: 11-01
    provides: json_protocol.h shared constants, line_buffer.h, DirtyCache

provides:
  - NDJSON-based TcpClient with ReceiveJsonLine/SendJsonLine API
  - JSON op-string dispatch loop in main.cpp
  - NDJSON record file format (human-readable)
  - All 8 service stubs accepting nlohmann::json

affects: [11-04, 11-05]

tech-stack:
  added: [nlohmann/json_fwd.hpp v3.11.3]
  patterns: [NDJSON line protocol, JSON op-string dispatch, LineBuffer-based receive]

key-files:
  created:
    - Code/native_client/third_party/nlohmann/json_fwd.hpp
  modified:
    - Code/native_client/xmake.lua
    - Code/native_client/game_bridge/tcp_client.h
    - Code/native_client/game_bridge/tcp_client.cpp
    - Code/native_client/main.cpp
    - Code/native_client/Services/CharacterService.h
    - Code/native_client/Services/CharacterService.cpp
    - Code/native_client/Services/WeatherService.h
    - Code/native_client/Services/WeatherService.cpp
    - Code/native_client/Services/CalendarService.h
    - Code/native_client/Services/CalendarService.cpp
    - Code/native_client/Services/QuestService.h
    - Code/native_client/Services/QuestService.cpp
    - Code/native_client/Services/CombatService.h
    - Code/native_client/Services/CombatService.cpp
    - Code/native_client/Services/InventoryService.h
    - Code/native_client/Services/InventoryService.cpp
    - Code/native_client/Services/ActorValueService.h
    - Code/native_client/Services/ActorValueService.cpp
    - Code/native_client/Services/MagicService.h
    - Code/native_client/Services/MagicService.cpp

key-decisions:
  - "Send() kept public on TcpClient for AnimationSystem binary command compatibility until Plan 04 converts all senders"
  - "ReadExact() retained for potential future use even though NDJSON receive no longer uses it"

patterns-established:
  - "NDJSON dispatch: msg[JsonField::kOp].get_ref<const string&>() for op routing"
  - "Service stubs: OnHookEvent(const nlohmann::json&) with debug log, TODO(Plan-04) marker"

requirements-completed: [JSON-08]

duration: 8min
completed: 2026-03-29
---

# Phase 11 Plan 03: Native Client JSON Wire Protocol Summary

**NDJSON receive/send on TcpClient, JSON op-string dispatch in main.cpp, all 8 service stubs accepting nlohmann::json**

## Performance

- **Duration:** 8 min
- **Started:** 2026-03-29T14:58:23Z
- **Completed:** 2026-03-29T15:06:23Z
- **Tasks:** 2
- **Files modified:** 20

## Accomplishments
- TcpClient converted from binary PacketHeader protocol to NDJSON using LineBuffer for framing
- main.cpp dispatch loop routes on JSON "op" strings via JsonOp constants instead of binary opcodes
- Record file writes human-readable NDJSON lines instead of binary HookEventPacket structs
- All 8 service headers and .cpp files updated to accept `const nlohmann::json&` with stub implementations
- Build compiles cleanly on Linux (`xmake build SkyrimCoopNative` exit 0)

## Task Commits

Each task was committed atomically:

1. **Task 1: Update build system and vendor json_fwd.hpp** - `10b43398` (chore)
2. **Task 2: Convert TcpClient to NDJSON, rewrite main.cpp dispatch, and add service stubs** - `828c03f3` (feat)

## Files Created/Modified
- `Code/native_client/third_party/nlohmann/json_fwd.hpp` - Forward declaration header for nlohmann::json (176 lines)
- `Code/native_client/xmake.lua` - Added third_party include dir for nlohmann headers
- `Code/native_client/game_bridge/tcp_client.h` - NDJSON API: ReceiveJsonLine, SendJsonLine, SendControlJson, LineBuffer member
- `Code/native_client/game_bridge/tcp_client.cpp` - NDJSON implementations using LineBuffer + json::parse
- `Code/native_client/main.cpp` - JSON dispatch loop, HandleControlMessage, NDJSON recording
- `Code/native_client/Services/*.h` (8 files) - OnHookEvent(const nlohmann::json&) signatures
- `Code/native_client/Services/*.cpp` (8 files) - Stub OnHookEvent with debug logging

## Decisions Made
- Send() kept public on TcpClient because AnimationSystem and CharacterService::SendSetPosition still send binary CommandPacket structs. Plan 04 will convert these to JSON and can then make Send() private.
- ReadExact() retained in tcp_client.cpp even though the NDJSON path doesn't use it -- removing it would be unnecessary churn.

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 3 - Blocking] Kept Send() public instead of private**
- **Found during:** Task 2 (TcpClient conversion)
- **Issue:** Plan specified making Send() private, but AnimationSystem::Update() and CharacterService::SendSetPosition() call Send() directly with binary CommandPacket structs
- **Fix:** Kept Send() public with a comment noting Plan 04 will convert all callers to JSON
- **Files modified:** Code/native_client/game_bridge/tcp_client.h
- **Verification:** Build compiles cleanly
- **Committed in:** 828c03f3

---

**Total deviations:** 1 auto-fixed (1 blocking)
**Impact on plan:** Necessary to avoid breaking AnimationSystem and CharacterService command senders. No scope creep.

## Known Stubs

All 8 service OnHookEvent methods are intentionally stubbed with `TODO(Plan-04)` markers. These stubs log the op string at debug level but do not process the JSON payload. Plan 04 replaces them with real implementations.

| File | Stub | Resolved by |
|------|------|-------------|
| CharacterService.cpp | OnHookEvent stub | Plan 04 |
| WeatherService.cpp | OnHookEvent stub | Plan 04 |
| CalendarService.cpp | OnHookEvent stub | Plan 04 |
| QuestService.cpp | OnHookEvent stub | Plan 04 |
| CombatService.cpp | OnHookEvent stub | Plan 04 |
| InventoryService.cpp | OnHookEvent stub | Plan 04 |
| ActorValueService.cpp | OnHookEvent stub | Plan 04 |
| MagicService.cpp | OnHookEvent stub | Plan 04 |

## Issues Encountered
- xmake platform was set to mingw; needed `xmake config -p linux` to build the native client target. Restored to mingw after verification.

## User Setup Required
None - no external service configuration required.

## Next Phase Readiness
- Plan 04 can now implement real JSON dispatch in all 8 service OnHookEvent methods
- Plan 05 can convert the DLL-side to emit NDJSON (completing the big-bang cutover)
- AnimationSystem and CharacterService binary command senders need conversion in Plan 04

---
*Phase: 11-json-wire-protocol*
*Completed: 2026-03-29*
