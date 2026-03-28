---
phase: 07-native-linux-build-with-skse-tcp-relay
plan: 01
subsystem: networking
tags: [tcp-protocol, spsc-queue, lock-free, relay-dll, catch2]

requires:
  - phase: none
    provides: "standalone foundational plan"
provides:
  - "Shared TCP binary protocol header (protocol.h) with 30 hook, 15 command, 3 control opcodes"
  - "Lock-free SPSC command queue (command_queue.h/.cpp) for game-thread dispatch"
  - "Catch2 unit tests for protocol round-trips and queue operations"
affects: [07-02, 07-03, 07-04, 07-05, 07-06, 07-07, 07-08]

tech-stack:
  added: []
  patterns: [flat-binary-protocol, spsc-ring-buffer, zero-dependency-shared-headers]

key-files:
  created:
    - Code/relay_dll/protocol.h
    - Code/relay_dll/command_queue.h
    - Code/relay_dll/command_queue.cpp
    - Code/tests/relay_protocol_tests.cpp
    - Code/tests/command_queue_tests.cpp
  modified:
    - Code/tests/xmake.lua

key-decisions:
  - "Protocol uses flat binary C structs with no dependencies beyond cstdint per D-01"
  - "SPSC queue uses power-of-2 ring buffer (256 slots) with std::atomic for lock-free synchronization per D-03"

patterns-established:
  - "Relay protocol: opcode(u16)+length(u16)+payload wire format for all DLL-native IPC"
  - "Command dispatch: SPSC ring buffer pattern for TCP-thread to game-thread command passing"

requirements-completed: [RELAY-01, RELAY-03]

duration: 4min
completed: 2026-03-28
---

# Phase 07 Plan 01: Shared Protocol and Command Queue Summary

**Flat binary TCP protocol with 48 opcodes and lock-free SPSC command queue for relay DLL game-thread dispatch**

## Performance

- **Duration:** 4 min
- **Started:** 2026-03-28T16:18:26Z
- **Completed:** 2026-03-28T16:22:44Z
- **Tasks:** 2
- **Files modified:** 6

## Accomplishments
- Defined complete wire format: 30 HookOpcodes (DLL->Native), 15 CommandOpcodes (Native->DLL), 3 ControlOpcodes (bidirectional)
- Implemented lock-free SPSC ring buffer with 256-slot capacity, overflow detection, and templated drain
- All 11 Catch2 tests pass (6 protocol, 5 queue) covering round-trips, distinct opcodes, capacity, overflow, and interleaved operations
- Both protocol.h and command_queue.h compile cleanly under system GCC and MinGW cross-compiler

## Task Commits

Each task was committed atomically:

1. **Task 1: Create shared protocol.h and SPSC command queue** - `4ac05679` (feat)
2. **Task 2: Create Catch2 unit tests for protocol and command queue** - `f57cd2ea` (test)

## Files Created/Modified
- `Code/relay_dll/protocol.h` - PacketHeader, HookOpcode (30), CommandOpcode (15), ControlOpcode (3), HookEventPacket, CommandPacket, ControlPacket
- `Code/relay_dll/command_queue.h` - CommandSlot struct and CommandQueue class with Enqueue/Drain/Size/IsFull
- `Code/relay_dll/command_queue.cpp` - Enqueue, Size, IsFull implementations with proper memory ordering
- `Code/tests/relay_protocol_tests.cpp` - 6 test cases for protocol round-trips and opcode uniqueness
- `Code/tests/command_queue_tests.cpp` - 5 test cases for queue operations (single, full, overflow, empty, interleaved)
- `Code/tests/xmake.lua` - Added relay_dll include path and command_queue.cpp source

## Decisions Made
- Protocol uses flat binary C structs with only cstdint dependency, ensuring MinGW and system GCC compatibility per D-01/D-13
- SPSC queue uses 256-slot power-of-2 ring buffer with acquire/release memory ordering for correct lock-free synchronization per D-03
- HookEventPacket and CommandPacket both use 7-byte padding after argCount to align args[] to 8-byte boundary

## Deviations from Plan

None - plan executed exactly as written.

## Issues Encountered
- xmake was configured for mingw platform from a previous session, causing TPTests target to be excluded. Reconfigured to linux platform which includes the test target.

## Known Stubs

None - all protocol opcodes and queue operations are fully implemented and tested.

## User Setup Required

None - no external service configuration required.

## Next Phase Readiness
- protocol.h and command_queue.h ready for use by Plan 02 (relay DLL skeleton) and Plan 03 (hook trampolines)
- Wire format established as the data contract between all subsequent relay plans

---
*Phase: 07-native-linux-build-with-skse-tcp-relay*
*Completed: 2026-03-28*
