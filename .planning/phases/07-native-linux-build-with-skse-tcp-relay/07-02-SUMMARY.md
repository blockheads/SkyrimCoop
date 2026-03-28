---
phase: 07-native-linux-build-with-skse-tcp-relay
plan: 02
subsystem: infra
tags: [xmake, mingw, elf, cross-compile, build-system]

# Dependency graph
requires:
  - phase: 02-mingw-tier1-tier2-libraries
    provides: MinGW toolchain and package declarations in root xmake.lua
provides:
  - SkyrimCoopHooksDLL XMake target (MinGW relay DLL with MinHook + winsock2)
  - SkyrimCoopNative XMake target (Linux ELF binary with full native stack)
  - Flat binary TCP protocol header (protocol.h)
affects: [07-03-relay-dll-implementation, 07-04-native-client-implementation]

# Tech tracking
tech-stack:
  added: []
  patterns: [platform-guarded-targets, protocol-header-sharing]

key-files:
  created:
    - Code/relay_dll/xmake.lua
    - Code/relay_dll/relay_main.cpp
    - Code/relay_dll/protocol.h
    - Code/native_client/xmake.lua
    - Code/native_client/main.cpp
  modified:
    - Code/xmake.lua

key-decisions:
  - "Relay DLL includes MinGWCompat.h from ../client for calling convention compat"
  - "Native client includes game struct headers from ../client/Games/Skyrim per D-14"
  - "Both targets use internal platform guards (mingw/linux) with unconditional includes in Code/xmake.lua"

patterns-established:
  - "Platform-guarded targets: each new target wraps itself in is_plat() guard, included unconditionally"
  - "Protocol header sharing: relay_dll/protocol.h is the single source of truth for DLL-to-native IPC"

requirements-completed: [RELAY-09]

# Metrics
duration: 4min
completed: 2026-03-28
---

# Phase 07 Plan 02: XMake Build Targets Summary

**Two new XMake targets -- MinGW relay DLL (MinHook + winsock2 only) and native Linux ELF binary (full EnTT/spdlog/encoding stack) -- coexisting with existing build**

## Performance

- **Duration:** 4 min
- **Started:** 2026-03-28T16:18:27Z
- **Completed:** 2026-03-28T16:22:42Z
- **Tasks:** 2
- **Files modified:** 6

## Accomplishments
- SkyrimCoopHooksDLL target builds skyrim_coop_hooks.dll under MinGW with only MinHook + winsock2 (per D-13)
- SkyrimCoopNative target builds skyrim-coop ELF binary on Linux linking EnTT, spdlog, encoding, server natively (per D-12)
- Flat binary TCP protocol header (protocol.h) defines PacketHeader, hook events, commands, control messages (per D-01)
- Both targets coexist without breaking existing SkyrimTogetherClient build

## Task Commits

Each task was committed atomically:

1. **Task 1: Create relay DLL XMake target (MinGW only)** - `b5a5e8b1` (feat)
2. **Task 2: Create native client XMake target and wire both into root** - `5ddea19e` (feat)

## Files Created/Modified
- `Code/relay_dll/xmake.lua` - SkyrimCoopHooksDLL target: shared DLL, MinHook + winsock2, static MinGW runtime
- `Code/relay_dll/relay_main.cpp` - Stub source file (replaced in Plan 03)
- `Code/relay_dll/protocol.h` - Flat binary TCP protocol structs for DLL-to-native IPC
- `Code/native_client/xmake.lua` - SkyrimCoopNative target: ELF binary, full native package set, -mms-bitfields
- `Code/native_client/main.cpp` - Stub entry point accepting --pid and --port args (replaced in Plan 04)
- `Code/xmake.lua` - Added includes for relay_dll and native_client

## Decisions Made
- Relay DLL reuses MinGWCompat.h from `../client` directory for MSVC calling convention compatibility rather than duplicating it
- Native client xmake.lua includes `../relay_dll` for shared protocol.h and `../client/Games/Skyrim` for game struct headers
- Both new includes in Code/xmake.lua are unconditional (each target has its own internal platform guard)

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 3 - Blocking] Added Code/xmake.lua includes in Task 1 instead of Task 2**
- **Found during:** Task 1 (relay DLL build verification)
- **Issue:** Task 1 verification requires `xmake build SkyrimCoopHooksDLL` but the include wasn't wired yet (Task 2)
- **Fix:** Added both includes to Code/xmake.lua during Task 1, with a placeholder native_client xmake.lua
- **Files modified:** Code/xmake.lua
- **Verification:** `xmake build SkyrimCoopHooksDLL` succeeds
- **Committed in:** b5a5e8b1 (Task 1 commit)

---

**Total deviations:** 1 auto-fixed (1 blocking)
**Impact on plan:** Minor ordering change to unblock verification. No scope creep.

## Issues Encountered
None

## User Setup Required
None - no external service configuration required.

## Next Phase Readiness
- Relay DLL target ready for Plan 03 (relay_main.cpp, tcp_server.cpp, hook_trampolines.cpp, command_queue.cpp implementation)
- Native client target ready for Plan 04 (main.cpp, proc_memory reader, GameBridge implementation)
- protocol.h defines the IPC contract both sides will implement against

---
*Phase: 07-native-linux-build-with-skse-tcp-relay*
*Completed: 2026-03-28*
