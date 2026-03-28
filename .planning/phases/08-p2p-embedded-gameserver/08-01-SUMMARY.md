---
phase: 08-p2p-embedded-gameserver
plan: 01
subsystem: networking
tags: [loopback-transport, gameserver, p2p, enet6, catch2]

# Dependency graph
requires:
  - phase: 05-testing-ci-pipeline
    provides: shared GameServer test fixture, MockClient, Catch2 integration test infrastructure
  - phase: 07-native-linux-build-with-skse-tcp-relay
    provides: native_client target structure, xmake.lua build configuration
provides:
  - LoopbackTransport class for in-process ClientMessage delivery to GameServer
  - ServerMessageRouter for deserializing loopback ServerMessage responses
  - GameServer::InjectMessage and SetLoopbackRoute public API for P2P host mode
  - GameServer::Send loopback intercept (bypasses enet6 for host ConnectionId)
  - 4 Catch2 [Loopback] tests proving round-trip works
affects: [08-02-PLAN, 08-03-PLAN, native_client host mode]

# Tech tracking
tech-stack:
  added: []
  patterns: [loopback-transport-pattern, server-message-callback-routing]

key-files:
  created:
    - Code/native_client/transport/LoopbackTransport.h
    - Code/native_client/transport/LoopbackTransport.cpp
    - Code/native_client/transport/ServerMessageRouter.h
    - Code/native_client/transport/ServerMessageRouter.cpp
    - Code/tests/loopback_transport_tests.cpp
  modified:
    - Code/server/GameServer.h
    - Code/server/GameServer.cpp
    - Code/tests/xmake.lua
    - Code/native_client/xmake.lua

key-decisions:
  - "std::function callback approach for loopback routing avoids cross-target include dependencies between GameServer and LoopbackTransport"
  - "ServerMessageRouter skips packet-type byte (byte 0) before using ServerMessageFactory, matching Server::HandleMessage strip behavior"
  - "kHostConnectionId = 0xFFFF'FFFF'0000'0001 chosen to never collide with enet6 peer pointer-based ConnectionIds"
  - "Added sol2, lua, sqlite3 packages and server include dirs to native_client xmake.lua for GameServer.h transitive header chain"

patterns-established:
  - "Loopback transport pattern: serialize ClientMessage -> skip packet-type byte -> InjectMessage -> OnConsume"
  - "Server loopback callback: GameServer::Send checks m_loopbackConnectionId before enet6 path"
  - "Server PCH include preamble: 14-line include block replicating server Pch.h for files outside server target"

requirements-completed: [P2P-01, P2P-02]

# Metrics
duration: 14min
completed: 2026-03-28
---

# Phase 08 Plan 01: Loopback Transport Summary

**In-process LoopbackTransport bridges native client to embedded GameServer without enet6, with ServerMessageRouter deserializing responses and 4 Catch2 tests proving auth round-trip**

## Performance

- **Duration:** 14 min
- **Started:** 2026-03-28T21:05:55Z
- **Completed:** 2026-03-28T21:20:19Z
- **Tasks:** 1
- **Files modified:** 9

## Accomplishments
- LoopbackTransport serializes ClientMessages and delivers to GameServer::OnConsume via InjectMessage, achieving zero-latency in-process message delivery
- GameServer::Send intercepts the loopback ConnectionId (kHostConnectionId) and routes to callback instead of enet6 socket path
- ServerMessageRouter deserializes ServerMessages from loopback callback bytes using ServerMessageFactory
- All 4 Catch2 [Loopback] tests pass; full 42-test suite passes with zero regressions

## Task Commits

Each task was committed atomically:

1. **Task 1: Create LoopbackTransport, ServerMessageRouter, modify GameServer, and write Catch2 tests** - `8e60104a` (feat)

## Files Created/Modified
- `Code/native_client/transport/LoopbackTransport.h` - Transport class declaration with SendToServer, SetServerMessageCallback, DeliverToHost
- `Code/native_client/transport/LoopbackTransport.cpp` - Message serialization matching MockClient::SendGameMessage pattern, InjectMessage call
- `Code/native_client/transport/ServerMessageRouter.h` - Router class for dispatching server messages back to native services
- `Code/native_client/transport/ServerMessageRouter.cpp` - ServerMessageFactory-based deserialization with auth response handling
- `Code/server/GameServer.h` - Added InjectMessage, SetLoopbackRoute, m_loopbackConnectionId, m_loopbackCallback
- `Code/server/GameServer.cpp` - Loopback check in Send() before enet6 path, InjectMessage and SetLoopbackRoute implementations
- `Code/tests/loopback_transport_tests.cpp` - 4 Catch2 tests: auth round-trip, send routing, non-host skip, router deserialize
- `Code/tests/xmake.lua` - Added transport includedirs and source files to TPTests
- `Code/native_client/xmake.lua` - Added server include dirs and sol2/lua/sqlite3 packages for GameServer.h header chain

## Decisions Made
- Used std::function callback approach for loopback routing to avoid cross-target include dependencies between GameServer and LoopbackTransport
- ServerMessageRouter skips packet-type byte (byte 0) before using ServerMessageFactory, matching Server::HandleMessage strip behavior
- kHostConnectionId = 0xFFFF'FFFF'0000'0001 chosen as large constant that cannot collide with enet6 peer pointer-based ConnectionIds
- Added sol2, lua, sqlite3 packages to native_client xmake.lua because GameServer.h -> World.h -> ScriptService.h -> ScriptService.inl requires sol/sol.hpp

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 3 - Blocking] Added server transitive dependencies to native_client xmake.lua**
- **Found during:** Task 1 (SkyrimCoopNative build)
- **Issue:** LoopbackTransport.cpp includes GameServer.h which transitively pulls ScriptService.inl -> sol/sol.hpp; native_client lacked sol2 package
- **Fix:** Added server include dirs (../server, ../common, etc.) and packages (sol2, lua, sqlite3) to native_client/xmake.lua
- **Files modified:** Code/native_client/xmake.lua
- **Verification:** `xmake build -P . SkyrimCoopNative` succeeds
- **Committed in:** 8e60104a (part of task commit)

**2. [Rule 1 - Bug] Fixed auth round-trip test assertion checking GetLastOpcode()**
- **Found during:** Task 1 (test execution)
- **Issue:** Server sends multiple messages after auth (AuthenticationResponse then StringCacheUpdate), so GetLastOpcode() returns StringCacheUpdate opcode (8), not kAuthenticationResponse (0)
- **Fix:** Removed GetLastOpcode() check from auth round-trip test, rely on IsAuthenticated() which is set specifically by auth response handler
- **Files modified:** Code/tests/loopback_transport_tests.cpp
- **Verification:** All 4 [Loopback] tests pass
- **Committed in:** 8e60104a (part of task commit)

**3. [Rule 3 - Blocking] Added missing includes for native_client transport compilation**
- **Found during:** Task 1 (worktree build)
- **Issue:** ServerMessageRouter.cpp and LoopbackTransport.cpp needed glm, optional, entt, sol2 includes that server PCH normally provides
- **Fix:** Added explicit includes matching the server PCH include pattern in both cpp files
- **Files modified:** Code/native_client/transport/LoopbackTransport.cpp, Code/native_client/transport/ServerMessageRouter.cpp
- **Verification:** Both TPTests and SkyrimCoopNative build successfully
- **Committed in:** 8e60104a (part of task commit)

---

**Total deviations:** 3 auto-fixed (1 bug, 2 blocking)
**Impact on plan:** All auto-fixes necessary for compilation and test correctness. No scope creep.

## Issues Encountered
- Segfault at process exit after tests pass -- known static destruction order issue with shared GameServer fixture + enet6 cleanup. Does not affect test results (all assertions pass before shutdown).

## User Setup Required
None - no external service configuration required.

## Next Phase Readiness
- LoopbackTransport and GameServer loopback API ready for 08-02 (HostService integration)
- ServerMessageRouter ready for 08-03 (wiring native services to server responses)
- SkyrimCoopNative binary builds cleanly with all transport code included

---
*Phase: 08-p2p-embedded-gameserver*
*Completed: 2026-03-28*
