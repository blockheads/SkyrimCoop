---
phase: 11-json-wire-protocol
plan: 02
subsystem: relay-dll
tags: [json, ndjson, nlohmann, mingw, dirty-cache, wire-protocol]

requires:
  - phase: 11-01
    provides: json_protocol.h constants, dirty_cache.h, line_buffer.h, nlohmann/json.hpp vendor

provides:
  - NDJSON wire protocol for all DLL-to-native TCP communication
  - SendJson/ReceiveJsonLine API on TcpServer
  - JSON command parsing for all 15 command opcodes
  - DirtyCache integration for high-frequency hooks (SetPosition, ActorProcess)

affects: [11-03, 11-04, native-client]

tech-stack:
  added: []
  patterns: [NDJSON line framing, nothrow JSON parse, dirty cache suppression]

key-files:
  created: []
  modified:
    - Code/relay_dll/hook_trampolines.cpp
    - Code/relay_dll/tcp_server.h
    - Code/relay_dll/tcp_server.cpp
    - Code/relay_dll/relay_main.cpp

key-decisions:
  - "g_dirtyCache declared non-static in hook_trampolines.cpp, extern in relay_main.cpp for cross-TU access"
  - "JSON command parsing done on receive thread, packed into existing CommandSlot binary for SPSC queue (game thread safety)"
  - "ReadFormId/ReadPosition/ReadRotation helpers added for typed field extraction from raw pointers"

patterns-established:
  - "SendJson serializes inside CRITICAL_SECTION for thread safety from concurrent hook trampolines"
  - "ReceiveJsonLine uses LineBuffer for NDJSON framing with nothrow json::parse"
  - "DirtyCache suppresses unchanged SetPosition and ActorProcess events with epsilon thresholds"

requirements-completed: [JSON-08]

duration: 6min
completed: 2026-03-29
---

# Phase 11 Plan 02: Relay DLL NDJSON Conversion Summary

**All 31 hook trampolines converted from binary HookEventPacket structs to nlohmann::json objects with named fields; TcpServer speaks NDJSON lines; FloatToU64 eliminated entirely**

## What Changed

### TcpServer NDJSON API (tcp_server.h/.cpp)
- Added `SendJson(const nlohmann::json&)` -- serializes to `json.dump() + "\n"` inside CRITICAL_SECTION
- Added `ReceiveJsonLine(nlohmann::json&)` -- uses internal LineBuffer for line framing, nothrow parse
- Added `LineBuffer m_lineBuffer` member for buffered receive
- Kept raw `Send()` for internal use by SendJson
- LineBuffer cleared on AcceptClient and DisconnectClient

### Hook Trampolines (hook_trampolines.cpp)
- All 31 trampolines now build `nlohmann::json` objects with named fields from json_protocol.h
- `FloatToU64()` deleted -- JSON natively represents floats
- `SendHookEvent()` helper deleted -- replaced by direct `g_tcpServer.SendJson(msg)`
- `HookEventPacket` struct no longer referenced
- Added `ReadFormId()`, `ReadPosition()`, `ReadRotation()` helpers for typed field extraction
- DirtyCache integrated: `HookSetPosition` checks `g_dirtyCache.CheckPosition()`, `HookActorProcess` checks `g_dirtyCache.CheckActorProcess()`

### Relay Main (relay_main.cpp)
- MonitorThread receive loop uses `ReceiveJsonLine()` instead of binary `Receive()`
- `ParseJsonCommand()` translates all 15 JSON command ops to CommandSlot for SPSC queue
- Control messages (heartbeat, shutdown, ready) sent/received as JSON
- Ready signal sent as JSON on new client connection
- `g_dirtyCache.Clear()` called on client disconnect

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 2 - Missing functionality] Added ReadFormId/ReadPosition/ReadRotation helpers**
- **Found during:** Task 1
- **Issue:** Plan specifies JSON fields with formId values but original trampolines only passed raw pointers. JSON needs actual form IDs, not just pointer addresses.
- **Fix:** Added helper functions that read formId (offset 0x14), position (offset 0x54), and rotation (offset 0x40) from actor/refr pointers.
- **Files modified:** Code/relay_dll/hook_trampolines.cpp

**2. [Rule 3 - Blocking] g_dirtyCache linkage across translation units**
- **Found during:** Task 2
- **Issue:** Plan suggested file-static DirtyCache in hook_trampolines.cpp but relay_main.cpp needs to call Clear() on disconnect.
- **Fix:** Made g_dirtyCache non-static in hook_trampolines.cpp, declared extern in relay_main.cpp.
- **Files modified:** Code/relay_dll/hook_trampolines.cpp, Code/relay_dll/relay_main.cpp

## Commits

| Task | Commit | Description |
|------|--------|-------------|
| 1 | 04575dd3 | Convert TcpServer to NDJSON and all hook trampolines to JSON |
| 2 | 0fba839d | Convert DrainCommandQueue and MonitorThread to JSON protocol |

## Self-Check: PASSED
