---
phase: 14-enet6-network-traffic-replay
plan: 01
subsystem: testing
tags: [serialization, base64, nlohmann-json, catch2, replay]

requires:
  - phase: 12-ndjson-replay-capture-and-integration-tests
    provides: ReplayTests target infrastructure, nlohmann_json package
provides:
  - MessageSerializer class for ClientMessage/ServerMessage to JSON conversion
  - Base64 header-only encode/decode utility
  - Type name registry for all 47 client + 53 server message opcodes
affects: [14-02-recording-layer, 14-03-replay-engine]

tech-stack:
  added: [nlohmann_json (to server target)]
  patterns: [DEF_MSG_NAME macro + Visit() static init for compile-time type name registry]

key-files:
  created:
    - Code/server/Recording/Base64.h
    - Code/server/Recording/MessageSerializer.h
    - Code/server/Recording/MessageSerializer.cpp
    - Code/tests/replay/test_message_serializer.cpp
  modified:
    - Code/server/xmake.lua
    - Code/tests/replay/xmake.lua

key-decisions:
  - "nlohmann_json added to server packages (not test-only) since Recording is a server component"
  - "DEF_MSG_NAME macro with Visit()-based static init for type names instead of PRETTY_FUNCTION parsing"
  - "Base64 in Recording namespace as header-only inline functions for zero-link inclusion"

patterns-established:
  - "Recording/ directory under server/ for replay infrastructure code"
  - "MessageName<T> trait pattern for compile-time message type to string mapping"

requirements-completed: [NET-REC-01]

duration: 8min
completed: 2026-03-29
---

# Phase 14 Plan 01: Message Serializer Summary

**MessageSerializer converts all 100 message types (47 client + 53 server) to NDJSON-ready JSON with type name, opcode, and base64 payload, with round-trip fidelity proven by 10 Catch2 tests**

## Performance

- **Duration:** 8 min
- **Started:** 2026-03-29T20:54:15Z
- **Completed:** 2026-03-29T21:02:00Z
- **Tasks:** 2
- **Files modified:** 6

## Accomplishments
- MessageSerializer provides ClientToJson/ServerToJson and ClientFromJson/ServerFromJson for all message types
- Base64 header-only utility with standard A-Za-z0-9+/= alphabet for binary payload encoding
- Type name registry covers all 47 client opcodes and 53 server opcodes via Visit()-based static init
- 10 Catch2 test cases (130 assertions) verify round-trip fidelity, name coverage, and factory Extract compatibility

## Task Commits

Each task was committed atomically:

1. **Task 1: Base64 utility and MessageSerializer with Visit()-based type name maps** - `445f5464` (feat)
2. **Task 2: Unit tests for MessageSerializer round-trip and Base64** - `d2943838` (test)

## Files Created/Modified
- `Code/server/Recording/Base64.h` - Header-only base64 encode/decode with constexpr lookup tables
- `Code/server/Recording/MessageSerializer.h` - Public API: ToJson/FromJson for client and server messages
- `Code/server/Recording/MessageSerializer.cpp` - DEF_MSG_NAME macro registers all 100 types, Visit() populates static arrays
- `Code/tests/replay/test_message_serializer.cpp` - 10 test cases covering round-trip, name lookup, and full opcode coverage
- `Code/server/xmake.lua` - Added nlohmann_json to server packages
- `Code/tests/replay/xmake.lua` - Added Recording source files to ReplayTests target

## Decisions Made
- Added nlohmann_json to the server target packages since Recording/ lives under server/ and the glob pattern `add_files("**.cpp")` picks up Recording/*.cpp automatically. This ensures the server library can use MessageSerializer directly in Plan 02.
- Used DEF_MSG_NAME macro approach instead of `__PRETTY_FUNCTION__` parsing for type name extraction. More explicit, portable, and avoids compiler-specific string format dependencies.
- Placed Base64 in `Recording` namespace as inline functions to allow both server and test targets to include without additional link dependencies.

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 3 - Blocking] Added nlohmann_json to server xmake packages**
- **Found during:** Task 1 (build verification)
- **Issue:** Server target uses `add_files("**.cpp")` glob which picks up Recording/*.cpp, but server had no nlohmann_json package
- **Fix:** Added `"nlohmann_json"` to server add_packages() in Code/server/xmake.lua
- **Files modified:** Code/server/xmake.lua
- **Verification:** `xmake build ReplayTests` succeeds (builds both server lib and test target)
- **Committed in:** 445f5464 (Task 1 commit)

**2. [Rule 3 - Blocking] Added glm and TiltedCore includes to MessageSerializer.cpp**
- **Found during:** Task 1 (build verification)
- **Issue:** MessageSerializer.cpp included message headers that transitively need glm/vec3 and TiltedPhoques types, but ReplayTests has no precompiled header
- **Fix:** Added explicit `#include <glm/glm.hpp>`, `<TiltedCore/Stl.hpp>`, `<TiltedCore/Serialization.hpp>` before message includes
- **Files modified:** Code/server/Recording/MessageSerializer.cpp
- **Verification:** Both server (with PCH) and ReplayTests (without PCH) compile cleanly
- **Committed in:** 445f5464 (Task 1 commit)

---

**Total deviations:** 2 auto-fixed (2 blocking)
**Impact on plan:** Both fixes necessary for compilation. No scope creep.

## Issues Encountered
- Post-test segfault (exit code 139) occurs after all 130 assertions pass during static destruction. This is a pre-existing issue in the ReplayTests binary (observed in prior phases), not caused by this plan's changes. Test results are valid.

## User Setup Required
None - no external service configuration required.

## Next Phase Readiness
- MessageSerializer ready for Plan 02 (recording layer) to call ClientToJson/ServerToJson during live session capture
- MessageSerializer ready for Plan 03 (replay engine) to call ClientFromJson/ServerFromJson during replay injection
- All 100 message types registered and round-trip tested

---
*Phase: 14-enet6-network-traffic-replay*
*Completed: 2026-03-29*
