---
phase: 11-json-wire-protocol
plan: 05
subsystem: testing
tags: [json, ndjson, protocol, deprecation, integration-test, skyrim]

requires:
  - phase: 11-json-wire-protocol (plans 02-04)
    provides: "JSON wire protocol implementation (DLL trampolines, native dispatch, services)"
provides:
  - "Full test suite validated for JSON protocol (64 tests, 1098 assertions)"
  - "protocol.h deprecated with pointer to json_protocol.h"
  - "Human-verified NDJSON communication in live Skyrim session"
  - "Auto-deploy xmake.lua steps for both DLL and native binary"
affects: [phase-10, phase-09]

tech-stack:
  added: []
  patterns:
    - "Legacy binary tests tagged [Legacy] for preservation alongside new [JsonProtocol] tests"
    - "Post-build auto-deploy to SKSE plugins directory via xmake after_build hooks"

key-files:
  created: []
  modified:
    - Code/relay_dll/protocol.h
    - Code/tests/relay_protocol_tests.cpp
    - Code/tests/relay_integration_tests.cpp
    - Code/tests/xmake.lua
    - Code/relay_dll/hook_trampolines.cpp
    - Code/relay_dll/tcp_server.cpp
    - Code/native_client/xmake.lua
    - Code/relay_dll/xmake.lua

key-decisions:
  - "Legacy binary tests preserved with [Legacy] tag rather than deleted, for regression reference"
  - "Post-build auto-deploy added to both xmake.lua files during verification (speeds iteration)"

patterns-established:
  - "Auto-deploy pattern: after_build() in xmake.lua copies artifacts to SKSE plugins dir"

requirements-completed: [JSON-08]

duration: 15min
completed: 2026-03-29
---

# Phase 11 Plan 05: Legacy Test Updates, Protocol Deprecation, and End-to-End Verification Summary

**Full JSON wire protocol validated: 64 tests pass (1098 assertions), protocol.h deprecated, NDJSON communication human-verified in live Skyrim session**

## Performance

- **Duration:** 15 min (Task 1) + human verification session (Task 2)
- **Started:** 2026-03-29T10:15:00Z
- **Completed:** 2026-03-29T15:43:00Z
- **Tasks:** 2
- **Files modified:** 8

## Accomplishments

- Deprecated protocol.h with prominent notice pointing to json_protocol.h as the active wire protocol schema
- Updated all legacy binary tests with [Legacy] tag, converted relay integration tests from binary to NDJSON API
- Human-verified: NDJSON protocol works end-to-end in live Skyrim -- DLL sends JSON events, native client receives and dispatches correctly, no crashes or disconnects
- Added post-build auto-deploy to both xmake.lua files for faster iteration during verification

## Task Commits

Each task was committed atomically:

1. **Task 1: Update legacy tests and deprecate protocol.h** - `279bc5f4` (feat)
2. **Task 2: Human verification of NDJSON protocol in Skyrim** - checkpoint approved, no code commit (verification only)

**Auto-deploy changes:** Uncommitted modifications to `Code/native_client/xmake.lua` and `Code/relay_dll/xmake.lua` (after_build deploy hooks added during verification)

## Files Created/Modified

- `Code/relay_dll/protocol.h` - Added deprecation comment pointing to json_protocol.h
- `Code/tests/relay_protocol_tests.cpp` - Tagged legacy binary tests with [Legacy]
- `Code/tests/relay_integration_tests.cpp` - Converted from binary HookEventPacket to NDJSON API
- `Code/tests/xmake.lua` - Added nlohmann third_party include path for TPTests
- `Code/relay_dll/hook_trampolines.cpp` - Fixed InstallAllHooks() signature, added GetRealSetPosition()
- `Code/relay_dll/tcp_server.cpp` - Fixed TcpServer::Start() signature to match header
- `Code/native_client/xmake.lua` - Added after_build auto-deploy to SKSE plugins dir
- `Code/relay_dll/xmake.lua` - Added after_build auto-deploy to SKSE plugins dir

## Decisions Made

- Legacy binary tests preserved with [Legacy] tag rather than deleted -- maintains regression reference for the binary protocol era
- Post-build auto-deploy hooks added to xmake.lua during human verification -- speeds build-deploy-test iteration cycle

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 1 - Bug] Fixed TcpServer::Start() signature mismatch**
- **Found during:** Task 1
- **Issue:** tcp_server.cpp Start() signature didn't match header declaration
- **Fix:** Updated implementation to match header
- **Files modified:** Code/relay_dll/tcp_server.cpp
- **Committed in:** 279bc5f4

**2. [Rule 1 - Bug] Fixed InstallAllHooks() signature and added GetRealSetPosition()**
- **Found during:** Task 1
- **Issue:** hook_trampolines.cpp had mismatched signature, missing GetRealSetPosition() needed by tests
- **Fix:** Fixed signature, added missing function
- **Files modified:** Code/relay_dll/hook_trampolines.cpp
- **Committed in:** 279bc5f4

**3. [Rule 2 - Missing] Post-build auto-deploy xmake.lua hooks**
- **Found during:** Task 2 (human verification)
- **Issue:** Manual copy of DLL/binary to SKSE plugins dir was tedious during verification
- **Fix:** Added after_build() hooks to both xmake.lua files that auto-deploy artifacts
- **Files modified:** Code/native_client/xmake.lua, Code/relay_dll/xmake.lua
- **Not yet committed** (working tree changes from verification session)

---

**Total deviations:** 3 auto-fixed (2 bugs, 1 missing convenience)
**Impact on plan:** Bug fixes were necessary for test compilation. Auto-deploy is a quality-of-life improvement for ongoing development.

### Pre-existing Issues (Not Phase 11)

- **formId=0 warnings:** The native client logs formId=0 for some hook events. This is a pre-existing issue -- the relay DLL sends raw pointers as formId values (from Phase 09.1 DLL-side resolution). The values are guest-process addresses, not actual form IDs. This is tracked separately and not a Phase 11 regression.

## Issues Encountered

None beyond the auto-fixed items above. All 64 tests passed with 1098 assertions on first run after fixes.

## User Setup Required

None - no external service configuration required.

## Next Phase Readiness

- Phase 11 (JSON Wire Protocol) is now fully complete -- all 5 plans executed and verified
- The JSON protocol is the active wire format between relay DLL and native client
- Ready to resume Phase 10 (two-player co-op) or Phase 9 remaining gap closure plans
- Auto-deploy hooks in xmake.lua will speed future verification cycles

## Known Stubs

None -- this plan was test/verification focused with no new feature stubs.

## Self-Check: PASSED

- SUMMARY.md: FOUND
- Commit 279bc5f4 (Task 1): FOUND
- Commit 2ee5f514 (docs): FOUND

---
*Phase: 11-json-wire-protocol*
*Completed: 2026-03-29*
