---
phase: 07-native-linux-build-with-skse-tcp-relay
plan: 03
subsystem: relay-dll
tags: [skse, winsock2, tcp, mingw, wine, process-launcher, relay]

# Dependency graph
requires:
  - phase: 07-01
    provides: "protocol.h shared types, command_queue.h/cpp SPSC queue"
  - phase: 07-02
    provides: "xmake build target SkyrimCoopHooksDLL, MinGW platform config"
provides:
  - "TcpServer class: Winsock2 localhost TCP server with ephemeral port and thread-safe send"
  - "ProcessLauncher class: CreateProcess wrapper with Wine Z: path mapping and getpid() PID discovery"
  - "Full SKSE plugin entry point (SKSEPlugin_Version/Load) with TCP + spawn + monitor thread"
  - "Auto-restart loop: native process respawned on TCP disconnect"
affects: [07-04, 07-05, 07-06]

# Tech tracking
tech-stack:
  added: [winsock2, ws2tcpip]
  patterns: [wine-z-path-conversion, getpid-for-linux-pid, monitor-thread-with-auto-restart]

key-files:
  created:
    - Code/relay_dll/tcp_server.h
    - Code/relay_dll/tcp_server.cpp
    - Code/relay_dll/process_launcher.h
    - Code/relay_dll/process_launcher.cpp
  modified:
    - Code/relay_dll/relay_main.cpp

key-decisions:
  - "getpid() via extern C for real Linux PID under Wine instead of GetCurrentProcessId()"
  - "TCP_NODELAY enabled on client socket for low-latency hook event forwarding"
  - "OutputDebugStringA for logging instead of spdlog (per D-13 no heavy deps)"
  - "DrainCommandQueue placeholder marked [[maybe_unused]] until Plan 05+ hooks game update"

patterns-established:
  - "Wine Z: path conversion: strip Z: prefix, swap backslashes to forward slashes for Linux paths"
  - "Monitor thread pattern: accept -> receive loop -> auto-restart on disconnect"
  - "CRITICAL_SECTION for thread-safe TCP send from multiple hook trampolines"

requirements-completed: [RELAY-02, RELAY-04]

# Metrics
duration: 3min
completed: 2026-03-28
---

# Phase 07 Plan 03: Relay DLL Core Summary

**Winsock2 TCP server on ephemeral localhost port + CreateProcess native binary launcher + SKSE entry point with monitor thread and auto-restart**

## Performance

- **Duration:** 3 min
- **Started:** 2026-03-28T16:25:24Z
- **Completed:** 2026-03-28T16:28:36Z
- **Tasks:** 2
- **Files modified:** 5

## Accomplishments
- TcpServer class with ephemeral port binding, single-client accept, thread-safe send via CRITICAL_SECTION, and header+payload receive protocol
- ProcessLauncher class with Wine Z: path conversion, getpid() for real Linux PID, and CreateProcess with CREATE_NEW_CONSOLE
- Full relay_main.cpp SKSE plugin: starts TCP, spawns native process, runs MonitorThread that receives commands into CommandQueue and auto-restarts on disconnect

## Task Commits

Each task was committed atomically:

1. **Task 1: Create TCP server and process launcher for relay DLL** - `61e81a81` (feat)
2. **Task 2: Create relay DLL SKSE entry point with auto-restart loop** - `80b0c33e` (feat)

## Files Created/Modified
- `Code/relay_dll/tcp_server.h` - TcpServer class declaration with Start/AcceptClient/Send/Receive/Stop
- `Code/relay_dll/tcp_server.cpp` - Winsock2 TCP server implementation, ephemeral port via bind(0)+getsockname
- `Code/relay_dll/process_launcher.h` - ProcessLauncher class declaration with Launch/IsAlive/Kill
- `Code/relay_dll/process_launcher.cpp` - CreateProcess wrapper with Wine Z: path mapping and getpid()
- `Code/relay_dll/relay_main.cpp` - SKSE plugin entry point, MonitorThread, auto-restart loop, command queue integration

## Decisions Made
- Used getpid() (extern "C") for real Linux PID under Wine instead of GetCurrentProcessId() which returns Wine-internal PID (per Pitfall 7)
- Enabled TCP_NODELAY on accepted client socket for low-latency hook event forwarding
- Used OutputDebugStringA for relay DLL logging to avoid spdlog dependency (per D-13)
- DrainCommandQueue function kept as [[maybe_unused]] placeholder until Plan 05+ installs game update hook

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 1 - Bug] Fixed winsock2.h include order warning**
- **Found during:** Task 2 (relay_main.cpp)
- **Issue:** winsock2.h must be included before windows.h to avoid redefinition warnings
- **Fix:** Added `#include <winsock2.h>` before `#include <windows.h>` in relay_main.cpp
- **Files modified:** Code/relay_dll/relay_main.cpp
- **Verification:** Clean build with no warnings
- **Committed in:** 80b0c33e (Task 2 commit)

**2. [Rule 1 - Bug] Fixed unused function warning for ExecuteGameCommand**
- **Found during:** Task 2 (relay_main.cpp)
- **Issue:** Standalone ExecuteGameCommand was unused; refactored to DrainCommandQueue with inline lambda
- **Fix:** Replaced standalone function with [[maybe_unused]] DrainCommandQueue that wraps the lambda
- **Files modified:** Code/relay_dll/relay_main.cpp
- **Verification:** Clean build with no warnings
- **Committed in:** 80b0c33e (Task 2 commit)

---

**Total deviations:** 2 auto-fixed (2 bugs)
**Impact on plan:** Minor code quality fixes, no scope change.

## Issues Encountered
None

## User Setup Required
None - no external service configuration required.

## Next Phase Readiness
- Relay DLL has full TCP server + process launcher + SKSE entry point infrastructure
- Ready for Plan 04 (native client TCP connector) to implement the other side of the TCP connection
- Plan 05+ will install actual hook trampolines that send events over TCP and wire DrainCommandQueue to game update

---
*Phase: 07-native-linux-build-with-skse-tcp-relay*
*Completed: 2026-03-28*
