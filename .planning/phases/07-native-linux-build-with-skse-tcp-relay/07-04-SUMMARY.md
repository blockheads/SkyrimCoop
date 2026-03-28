---
phase: 07-native-linux-build-with-skse-tcp-relay
plan: 04
subsystem: native-client
tags: [proc-memory, ptrace, tcp, game-reader, pointer-table, hopscotch-map, posix-sockets]

# Dependency graph
requires:
  - phase: 07-01
    provides: "shared protocol.h with PacketHeader, HookOpcode, ControlOpcode definitions"
  - phase: 07-02
    provides: "native client XMake target (SkyrimCoopNative) and stub main.cpp"
provides:
  - "ProcMemReader: /proc/pid/mem reader with ptrace SEIZE for Yama bypass"
  - "PointerTable: formId-to-game-pointer mapping via hopscotch_map"
  - "TcpClient: POSIX TCP client connecting to DLL server"
  - "GameReader: typed memory reads with game struct offsets (position, formId, health)"
  - "Native main event loop: arg parsing, init, CTRL_READY, hook/control dispatch"
affects: [07-05, 07-06, 07-07, 07-08]

# Tech tracking
tech-stack:
  added: [hopscotch-map (PointerTable), ptrace SEIZE, pread /proc/pid/mem, POSIX TCP sockets]
  patterns: [proc-memory-reader, hook-driven-pointer-table, game-bridge-api, tcp-packet-protocol]

key-files:
  created:
    - Code/native_client/game_bridge/proc_memory.h
    - Code/native_client/game_bridge/proc_memory.cpp
    - Code/native_client/game_bridge/pointer_table.h
    - Code/native_client/game_bridge/pointer_table.cpp
    - Code/native_client/game_bridge/tcp_client.h
    - Code/native_client/game_bridge/tcp_client.cpp
    - Code/native_client/game_bridge/game_reader.h
    - Code/native_client/game_bridge/game_reader.cpp
    - Code/tests/proc_memory_tests.cpp
    - Code/tests/pointer_table_tests.cpp
  modified:
    - Code/native_client/main.cpp
    - Code/tests/xmake.lua

key-decisions:
  - "ProcMemReader adapted from LethalInjection with read-only support (no write needed per D-04)"
  - "PointerTable uses tsl::hopscotch_map for O(1) formId lookup"
  - "GameReader stubs ReadActorHealth (requires vtable traversal, deferred to service rewrite)"
  - "Main loop is single-threaded -- separate /proc/pid/mem read thread can be added later if needed"

patterns-established:
  - "GameBridge API: services use GameReader for typed memory reads instead of direct pointer access"
  - "Hook-driven pointer table: DLL hook events populate formId-to-pointer mappings"
  - "TCP packet dispatch: opcode ranges determine packet type (hook < 0x8000, command 0x8000-0xEFFF, control >= 0xF000)"

requirements-completed: [RELAY-05, RELAY-06, RELAY-07]

# Metrics
duration: 4min
completed: 2026-03-28
---

# Phase 07 Plan 04: Native Client Core Summary

**GameBridge subsystem with /proc/pid/mem reader (ptrace SEIZE), TCP client, pointer table, and typed GameReader API -- native Linux ELF binary receives hook events and maintains actor pointer table**

## Performance

- **Duration:** 4 min
- **Started:** 2026-03-28T16:25:27Z
- **Completed:** 2026-03-28T16:29:54Z
- **Tasks:** 2
- **Files modified:** 12

## Accomplishments
- ProcMemReader reads process memory via /proc/pid/mem with ptrace SEIZE for Yama ptrace_scope bypass, adapted from LethalInjection
- PointerTable maintains formId-to-game-pointer mappings using hopscotch_map with insert/remove/lookup/clear/foreach
- TcpClient provides reliable POSIX TCP communication with DLL server (ReadExact loop, control packet helpers)
- GameReader wraps ProcMemReader with typed reads at game struct offsets (position at 0x54, formId at 0x14)
- Native main loop parses --pid/--port args, initializes proc_memory + TCP, sends CTRL_READY, dispatches hook/control events
- 12 unit tests covering proc_memory self-reads and pointer table operations, all passing

## Task Commits

Each task was committed atomically:

1. **Task 1: Implement ProcMemReader and PointerTable with tests** - `e1138d12` (feat)
2. **Task 2: Implement TcpClient, GameReader, and native main loop** - `d233fb9c` (feat)

## Files Created/Modified
- `Code/native_client/game_bridge/proc_memory.h` - ProcMemReader class with Open/Read/Close/ReadValue
- `Code/native_client/game_bridge/proc_memory.cpp` - ptrace SEIZE + pread implementation adapted from LethalInjection
- `Code/native_client/game_bridge/pointer_table.h` - PointerTable with hopscotch_map, ForEach template
- `Code/native_client/game_bridge/pointer_table.cpp` - Insert/Remove/RemoveByPointer/Lookup/Clear
- `Code/native_client/game_bridge/tcp_client.h` - TcpClient with Connect/Send/Receive/SendControl
- `Code/native_client/game_bridge/tcp_client.cpp` - POSIX socket connect, ReadExact loop, MSG_NOSIGNAL
- `Code/native_client/game_bridge/game_reader.h` - GameReader with Read/ReadChain/ReadActorPosition/ReadFormId
- `Code/native_client/game_bridge/game_reader.cpp` - Offset 0x54 position, 0x14 formId, stubbed health
- `Code/native_client/main.cpp` - Full native entry point with event loop
- `Code/tests/proc_memory_tests.cpp` - 5 ProcMem tests (self-read, invalid addr, open/close, ReadValue, not-open)
- `Code/tests/pointer_table_tests.cpp` - 7 PointerTable tests (insert/lookup, remove, removeByPtr, clear, forEach, overwrite)
- `Code/tests/xmake.lua` - Added game_bridge sources and spdlog/hopscotch-map packages to TPTests

## Decisions Made
- ProcMemReader is read-only (no OpenForWrite/Write methods) per D-04 design decision
- PointerTable uses hopscotch_map for fast O(1) amortized lookups on formId
- GameReader::ReadActorHealth stubbed (returns 0.0f) since it requires ActorValueOwner vtable traversal -- deferred to service rewrite
- Main loop is single-threaded for simplicity; can add /proc/pid/mem read thread later if latency requires

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 3 - Blocking] Added missing cstddef include in tcp_client.h**
- **Found during:** Task 2 (build verification)
- **Issue:** `size_t` type used in ReadExact declaration without `<cstddef>` include
- **Fix:** Added `#include <cstddef>` to tcp_client.h
- **Files modified:** Code/native_client/game_bridge/tcp_client.h
- **Verification:** Build succeeded after fix
- **Committed in:** d233fb9c (Task 2 commit)

---

**Total deviations:** 1 auto-fixed (1 blocking)
**Impact on plan:** Trivial missing include, no scope impact.

## Known Stubs
- `GameReader::ReadActorHealth()` returns 0.0f -- requires vtable traversal, intentionally deferred to Plan 05+ service rewrite

## Issues Encountered
None beyond the missing include.

## User Setup Required
None - no external service configuration required.

## Next Phase Readiness
- GameBridge subsystem is complete and ready for service rewrites (Plan 05+)
- Native binary builds, runs, and exits cleanly on connection failure
- All 12 unit tests pass
- Services in Plan 05+ will use GameReader API for all game memory access

---
*Phase: 07-native-linux-build-with-skse-tcp-relay*
*Completed: 2026-03-28*
