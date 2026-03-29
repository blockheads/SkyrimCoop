---
phase: 11-json-wire-protocol
plan: 01
subsystem: networking
tags: [nlohmann-json, ndjson, dirty-cache, wire-protocol, tcp-framing]

requires:
  - phase: 07-native-linux-build-with-skse-tcp-relay
    provides: relay_dll binary protocol (protocol.h) with hook/command/control opcodes
provides:
  - nlohmann/json v3.11.3 vendored for both DLL and native client
  - json_protocol.h with 48 op name constants and field name constants
  - dirty_cache.h with epsilon-based position/rotation dirty checking
  - line_buffer.h with NDJSON TCP stream reassembly
  - 19 unit tests covering all three components
affects: [11-02, 11-03, 11-04, 11-05]

tech-stack:
  added: [nlohmann/json v3.11.3]
  patterns: [constexpr string constants for wire protocol ops, epsilon-based dirty checking, newline-delimited JSON framing]

key-files:
  created:
    - Code/relay_dll/nlohmann/json.hpp
    - Code/native_client/third_party/nlohmann/json.hpp
    - Code/relay_dll/json_protocol.h
    - Code/relay_dll/dirty_cache.h
    - Code/relay_dll/line_buffer.h
    - Code/tests/json_protocol_tests.cpp
    - Code/tests/line_buffer_tests.cpp
    - Code/tests/dirty_cache_tests.cpp
  modified: []

key-decisions:
  - "json_protocol.h is pure constants (no nlohmann dependency) so both DLL and native can include without pulling in JSON parser"
  - "DirtyCache uses 1.0f position epsilon (Skyrim units) and 0.01f rotation epsilon (radians)"

patterns-established:
  - "Wire protocol op names: snake_case constexpr const char* in JsonOp namespace"
  - "Field names: constexpr const char* in JsonField namespace"
  - "DirtyCache: per-entity map with epsilon thresholds for bandwidth reduction"

requirements-completed: [JSON-01, JSON-02, JSON-03, JSON-04, JSON-05, JSON-06, JSON-07]

duration: 3min
completed: 2026-03-29
---

# Phase 11 Plan 01: JSON Wire Protocol Foundation Summary

**Vendored nlohmann/json v3.11.3 with complete wire protocol schema (48 ops), dirty-check cache, NDJSON line buffer, and 19 unit tests**

## Performance

- **Duration:** 3 min
- **Started:** 2026-03-29T14:53:06Z
- **Completed:** 2026-03-29T14:56:08Z
- **Tasks:** 2
- **Files modified:** 8

## Accomplishments
- Vendored nlohmann/json v3.11.3 single header in both relay_dll and native_client paths
- Created json_protocol.h with 30 hook + 15 command + 3 control op name constants plus all field name constants (97 constexpr lines)
- Implemented DirtyCache with epsilon-based position/rotation suppression and discrete actor process checking
- Implemented LineBuffer for NDJSON TCP stream reassembly with partial read handling
- 19 test cases (93 assertions) all passing: JSON round-trips, line buffer edge cases, dirty cache epsilon thresholds

## Task Commits

Each task was committed atomically:

1. **Task 1: Vendor nlohmann/json and create headers** - `45302261` (feat)
2. **Task 2: Unit tests for JSON protocol, dirty cache, and line buffer** - `5925f36f` (test)

## Files Created/Modified
- `Code/relay_dll/nlohmann/json.hpp` - nlohmann/json v3.11.3 single header (vendored)
- `Code/native_client/third_party/nlohmann/json.hpp` - Copy for native client include path
- `Code/relay_dll/json_protocol.h` - Op name and field name constants for JSON wire protocol
- `Code/relay_dll/dirty_cache.h` - Per-entity epsilon-based dirty checking for bandwidth reduction
- `Code/relay_dll/line_buffer.h` - NDJSON TCP stream line reassembly
- `Code/tests/json_protocol_tests.cpp` - 6 test cases for op names and JSON round-trips
- `Code/tests/line_buffer_tests.cpp` - 6 test cases for TCP stream edge cases
- `Code/tests/dirty_cache_tests.cpp` - 7 test cases for dirty cache behavior

## Decisions Made
- json_protocol.h kept as pure constants header (no nlohmann dependency) so both DLL and native can include it without pulling in the JSON parser
- DirtyCache uses 1.0f position epsilon (Skyrim units) and 0.01f rotation epsilon (radians) per plan spec

## Deviations from Plan

None - plan executed exactly as written.

## Issues Encountered

None.

## User Setup Required

None - no external service configuration required.

## Next Phase Readiness
- Plans 02 and 03 can now build against json_protocol.h, dirty_cache.h, and line_buffer.h
- nlohmann/json available in both DLL and native client include paths
- All shared constants and utilities tested and verified

---
*Phase: 11-json-wire-protocol*
*Completed: 2026-03-29*
