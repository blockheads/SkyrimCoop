---
phase: 10-two-player-co-op-session
plan: 02
subsystem: relay-dll
tags: [tcp, mingw, skse, game-thread, command-queue]

requires:
  - phase: 07-native-linux-build-with-skse-tcp-relay
    provides: relay DLL with TCP server, hook trampolines, command queue
provides:
  - CMD_SET_POSITION game-thread execution via DrainCommandQueue
  - SKYRIM_COOP_PORT env var for explicit TCP port binding
affects: [10-03, 10-04, two-player-session]

tech-stack:
  added: []
  patterns: [cross-TU function pointer callback via GetRealSetPosition(), per-frame command drain on player actor process]

key-files:
  created: []
  modified:
    - Code/relay_dll/relay_main.cpp
    - Code/relay_dll/tcp_server.h
    - Code/relay_dll/tcp_server.cpp
    - Code/relay_dll/hook_trampolines.cpp
    - Code/relay_dll/hook_trampolines.h

key-decisions:
  - "GetRealSetPosition() callback pattern to bridge file-static RealSetPosition across TUs"
  - "DrainCommandQueue called only for player formId 0x14 to avoid per-NPC overhead"

patterns-established:
  - "Command execution pattern: game-thread drain via HookActorProcess player filter"
  - "Env var pattern: SKYRIM_COOP_PORT follows existing SKYRIM_COOP_ALL_HOOKS getenv() convention"

requirements-completed: [D-07, D-08, D-09, D-11]

duration: 3min
completed: 2026-03-29
---

# Phase 10 Plan 02: CMD_SET_POSITION Execution and Port Binding Summary

**CMD_SET_POSITION decoded and executed on game thread via player actor process hook, with SKYRIM_COOP_PORT env var for dual-instance port assignment**

## Performance

- **Duration:** 3 min
- **Started:** 2026-03-29T05:11:01Z
- **Completed:** 2026-03-29T05:13:32Z
- **Tasks:** 2
- **Files modified:** 5

## Accomplishments
- TcpServer::Start(uint16_t) accepts explicit port with SO_REUSEADDR for quick restart
- SKYRIM_COOP_PORT env var read in SKSEPlugin_Load, enabling host=9667/client=9668 separation
- DrainCommandQueue decodes CMD_SET_POSITION packed float arguments and calls RealSetPosition
- HookActorProcess calls DrainCommandQueue once per frame for the player character (formId 0x14)
- RealSetPosition null-checked in DrainCommandQueue and nulled in RemoveAllHooks for shutdown safety

## Task Commits

Each task was committed atomically:

1. **Task 1: Add SKYRIM_COOP_PORT env var support** - `cd28ffae` (feat)
2. **Task 2: Implement DrainCommandQueue CMD_SET_POSITION** - `a508e524` (feat)

## Files Created/Modified
- `Code/relay_dll/tcp_server.h` - Start(uint16_t aPort) overload with default 0
- `Code/relay_dll/tcp_server.cpp` - Explicit port binding with SO_REUSEADDR
- `Code/relay_dll/relay_main.cpp` - SKYRIM_COOP_PORT env var reading, DrainCommandQueue implementation
- `Code/relay_dll/hook_trampolines.cpp` - GetRealSetPosition(), DrainCommandQueue call in HookActorProcess, RealSetPosition=nullptr in RemoveAllHooks
- `Code/relay_dll/hook_trampolines.h` - GetRealSetPosition() and DrainCommandQueue() declarations

## Decisions Made
- Used GetRealSetPosition() callback pattern (option c from research) to bridge file-static RealSetPosition across translation units without making it globally visible
- DrainCommandQueue filtered to player formId 0x14 only to avoid unnecessary per-NPC queue drain overhead

## Deviations from Plan

None - plan executed exactly as written.

## Issues Encountered
- Build target SkyrimCoopHooksDLL only available under mingw platform config; switched xmake config to -p mingw for DLL compilation verification, then back to linux for native client verification

## User Setup Required
None - no external service configuration required.

## Next Phase Readiness
- CMD_SET_POSITION execution pipeline complete: native process sends command over TCP, DLL enqueues, game thread drains and calls RealSetPosition
- Ready for Plan 03 (integration testing with two Skyrim instances)
- SKYRIM_COOP_PORT enables distinct ports for host and client DLL instances

---
*Phase: 10-two-player-co-op-session*
*Completed: 2026-03-29*
