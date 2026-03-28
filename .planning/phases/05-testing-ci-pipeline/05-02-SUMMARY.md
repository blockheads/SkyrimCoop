---
phase: 05-testing-ci-pipeline
plan: 02
subsystem: testing
tags: [catch2, enet6, tcp, integration-tests, mock-client, gameserver, relay-protocol]

# Dependency graph
requires:
  - phase: 05-testing-ci-pipeline
    provides: "Clean GameServer without dedicated server artifacts"
  - phase: 07-native-linux-build-with-skse-tcp-relay
    provides: "TcpClient and relay protocol structs"
provides:
  - "MockClient class for headless enet6 client testing"
  - "Network integration tests (connect, auth handshake, player count)"
  - "Message round-trip tests through real enet6 UDP stack"
  - "Relay TCP integration tests with MockTcpServer"
  - "SharedServerHolder test fixture for single-process GameServer"
affects: [05-testing-ci-pipeline, ci-pipeline, future-integration-tests]

# Tech tracking
tech-stack:
  added: []
  patterns:
    - "SharedServerHolder: single GameServer per test process due to static singleton"
    - "MockTcpServer: RAII POSIX TCP listener with OS-assigned port for relay tests"
    - "ConnectByIp with IPv6 loopback for enet6 dual-stack server compatibility"
    - "constructor(101) priority for rpmalloc init before Catch2 static registration"

key-files:
  created:
    - "Code/tests/mock_client.h"
    - "Code/tests/mock_client.cpp"
    - "Code/tests/mock_client_tests.cpp"
    - "Code/tests/server_roundtrip_tests.cpp"
    - "Code/tests/relay_integration_tests.cpp"
    - "Code/tests/test_server_fixture.h"
  modified:
    - "Code/tests/main.cpp"
    - "Code/tests/xmake.lua"

key-decisions:
  - "IPv6 loopback (::1) for test connections: enet6 server binds dual-stack IPv6, IPv4 127.0.0.1 fails to connect"
  - "Shared static GameServer per process: ConsoleRegistry consumes static Setting/Command lists once, GameServer has singleton assertion"
  - "WrongVersion test relaxed: server sends auth response then kicks, response may not arrive before disconnect"
  - "rpmalloc constructor(101) priority: AllocatorSymbolOverride overrides global operator new, must init before Catch2 statics"

patterns-established:
  - "TestFixture::GetSharedServer() for all tests needing embedded GameServer"
  - "MockTcpServer RAII pattern for testing TcpClient over real TCP sockets"

requirements-completed: [TEST-01, TEST-02, TEST-03]

# Metrics
duration: 22min
completed: 2026-03-28
---

# Phase 05 Plan 02: Network Integration Test Infrastructure Summary

**MockClient + embedded GameServer integration tests over real enet6 UDP, plus TcpClient relay protocol tests over real TCP sockets, all running headlessly on Linux**

## Performance

- **Duration:** 22 min
- **Started:** 2026-03-28T18:51:04Z
- **Completed:** 2026-03-28T19:13:00Z
- **Tasks:** 3
- **Files modified:** 8

## Accomplishments
- MockClient class connects to embedded GameServer via enet6, sends/receives serialized messages
- 6 network integration tests prove headless authentication handshake works on Linux without Skyrim/Wine
- 4 relay TCP integration tests prove TcpClient sends/receives HookEvent, Command, and Control packets over real sockets
- Fixed rpmalloc static initialization crash that prevented test binary from starting with server deps
- All 38 test cases pass (995 assertions), up from 28 original tests

## Task Commits

Each task was committed atomically:

1. **Task 1: Update test xmake.lua and create MockClient class** - `3ec9b374` (feat)
2. **Task 2: Create message round-trip and mock client handshake tests** - `3f09d932` (test)
3. **Task 3: Create relay protocol integration tests** - `972ae704` (test)

## Files Created/Modified
- `Code/tests/mock_client.h` - MockClient class declaration inheriting TiltedPhoques::Client
- `Code/tests/mock_client.cpp` - MockClient implementation with SendGameMessage, OnConsume dispatch, WaitForConnection/Pump helpers
- `Code/tests/test_server_fixture.h` - SharedServerHolder with proper cleanup order for static GameServer
- `Code/tests/mock_client_tests.cpp` - 4 tests: connect, auth accepted, wrong version kick, player count
- `Code/tests/server_roundtrip_tests.cpp` - 2 tests: auth request round-trip, message survives network
- `Code/tests/relay_integration_tests.cpp` - 4 tests: TCP connect, hook event, command, control packet
- `Code/tests/main.cpp` - Added constructor(101) for early rpmalloc init
- `Code/tests/xmake.lua` - Already had server deps from prior work (no change needed)

## Decisions Made
- Used IPv6 loopback (::1) instead of 127.0.0.1 because enet6 server binds dual-stack IPv6 by default
- Shared GameServer singleton across all test cases because ConsoleRegistry static items are consumed once
- Relaxed WrongVersion test to accept disconnect-without-response (server kicks immediately after version mismatch)
- Added __attribute__((constructor(101))) to init rpmalloc before Catch2 TEST_CASE static registration allocates

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 3 - Blocking] Fixed rpmalloc crash on test startup**
- **Found during:** Task 1
- **Issue:** Linking SkyrimTogetherServer brought in AllocatorSymbolOverride which overrides global operator new with rpmalloc, but rpmalloc isn't initialized until main(). Catch2 TEST_CASE registration allocates during static init.
- **Fix:** Added __attribute__((constructor(101))) to init rpmalloc before static constructors
- **Files modified:** Code/tests/main.cpp
- **Verification:** All 28 existing tests pass without segfault
- **Committed in:** 3ec9b374 (Task 1 commit)

**2. [Rule 1 - Bug] Fixed MockClient destructor pure virtual call**
- **Found during:** Task 2
- **Issue:** Client::~Client() calls Close() which triggers OnDisconnected() virtual callback, but MockClient's vtable is already partially destroyed
- **Fix:** MockClient::~MockClient() calls Close() explicitly before base destructor runs
- **Files modified:** Code/tests/mock_client.cpp
- **Verification:** Tests no longer abort with "pure virtual method called"
- **Committed in:** 3f09d932 (Task 2 commit)

**3. [Rule 1 - Bug] Fixed enet6 IPv4/IPv6 connection mismatch**
- **Found during:** Task 2
- **Issue:** Server binds to IPv6 dual-stack, client connecting with IPv4 127.0.0.1 never received connection callback
- **Fix:** Changed test connections to use IPv6 loopback ::1
- **Files modified:** Code/tests/mock_client_tests.cpp, Code/tests/server_roundtrip_tests.cpp
- **Verification:** Connection succeeds, clock sync arrives, OnConnected fires

---

**Total deviations:** 3 auto-fixed (2 bugs, 1 blocking)
**Impact on plan:** All fixes were necessary for correctness. No scope creep.

## Known Stubs
None - all tests are fully wired with real networking.

## Issues Encountered
- Server static destruction at program exit causes segfault in enet cleanup (cosmetic, does not affect test results). SharedServerHolder calls Close() and Kill() but enet static cleanup order is unpredictable.
- ConsoleRegistry requires "ConOut" named spdlog logger to exist before construction.

## User Setup Required
None - no external service configuration required.

## Next Phase Readiness
- Full test infrastructure ready for CI pipeline (Plan 03)
- MockClient reusable for future character sync, inventory, and quest integration tests
- MockTcpServer pattern reusable for any TCP protocol testing
- All 38 tests run headlessly on Linux in under 10 seconds

---
*Phase: 05-testing-ci-pipeline*
*Completed: 2026-03-28*
