---
phase: 14-enet6-network-traffic-replay
plan: 02
subsystem: testing
tags: [ndjson, recording, replay-harness, enet6, catch2]

requires:
  - phase: 14-enet6-network-traffic-replay
    provides: MessageSerializer for ClientToJson/ServerToJson conversion
provides:
  - EnetRecorder class for per-peer NDJSON recording in GameServer
  - ServerReplayHarness for offline server-side message injection and capture
  - WorldTestMode and GameServerTestMode tag constructors for headless server creation
affects: [14-03-replay-engine-regression-tests]

tech-stack:
  added: []
  patterns: [tag-struct test-mode constructors for skipping I/O in tests]

key-files:
  created:
    - Code/server/Recording/EnetRecorder.h
    - Code/server/Recording/EnetRecorder.cpp
    - Code/tests/replay/server_replay_harness.h
    - Code/tests/replay/server_replay_harness.cpp
  modified:
    - Code/server/GameServer.h
    - Code/server/GameServer.cpp
    - Code/server/World.h
    - Code/server/World.cpp

key-decisions:
  - "Tag-struct constructors (WorldTestMode, GameServerTestMode) for test mode instead of bool flags or subclasses"
  - "Recorder taps placed at application layer (Send/OnConsume) not transport layer, per D-03"
  - "Per-peer NDJSON files with dir/peer/ts metadata fields, per D-04"

patterns-established:
  - "Tag-struct pattern for test-mode constructors that skip heavy I/O (ESLoader, enet6 port binding)"
  - "Loopback route + ServerMessageFactory::Extract for capturing outgoing messages in tests"

requirements-completed: [NET-REC-02, NET-REC-03]

duration: 3min
completed: 2026-03-29
---

# Phase 14 Plan 02: Recording Layer and Server Replay Harness Summary

**EnetRecorder taps GameServer Send/OnConsume for per-peer NDJSON recording; ServerReplayHarness creates headless server for offline c2s message injection with s2c capture via loopback**

## Performance

- **Duration:** 3 min
- **Started:** 2026-03-29T21:05:35Z
- **Completed:** 2026-03-29T21:08:50Z
- **Tasks:** 2
- **Files modified:** 8

## Accomplishments
- EnetRecorder writes per-peer NDJSON files with c2s/s2c direction, peer ID, and timestamp fields
- GameServer recording taps in Send() and OnConsume() with zero overhead when recorder is null
- WorldTestMode constructor emplaces all 15 services but skips ESLoader and ScriptService
- ServerReplayHarness replays c2s NDJSON lines via InjectMessage and captures s2c responses via loopback

## Task Commits

Each task was committed atomically:

1. **Task 1: EnetRecorder + GameServer recording taps** - `f3ba8af9` (feat)
2. **Task 2: ServerReplayHarness with stubbed server World** - `827bc653` (feat)

## Files Created/Modified
- `Code/server/Recording/EnetRecorder.h` - Per-peer NDJSON recorder class with mutex-protected file map
- `Code/server/Recording/EnetRecorder.cpp` - Implementation: ClientToJson/ServerToJson, per-peer file management, timestamped writes
- `Code/tests/replay/server_replay_harness.h` - ServerReplayHarness public API for offline server replay
- `Code/tests/replay/server_replay_harness.cpp` - Test-mode GameServer creation, NDJSON c2s replay, loopback s2c capture
- `Code/server/GameServer.h` - Added EnetRecorder member, StartRecording/StopRecording, GameServerTestMode constructor
- `Code/server/GameServer.cpp` - Recording taps in Send/OnConsume, test-mode constructor, StartRecording/StopRecording
- `Code/server/World.h` - Added WorldTestMode tag struct and test constructor declaration
- `Code/server/World.cpp` - Test constructor emplaces all services, skips ESLoader and ScriptService

## Decisions Made
- Used tag-struct pattern (WorldTestMode, GameServerTestMode) instead of bool flags or subclasses. Tag structs are self-documenting, cannot be accidentally passed as true/false, and make test code explicit about intent.
- Recording taps at application layer (Send/OnConsume) rather than transport layer, following D-03 decision from research. This captures deserialized messages with full type information.
- Per-peer NDJSON files with dir/peer/ts metadata fields per D-04. Each peer gets its own file for easier filtering and smaller file sizes.

## Deviations from Plan

None - plan executed exactly as written.

## Issues Encountered
None

## User Setup Required
None - no external service configuration required.

## Known Stubs
None - all functionality is fully wired. ServerReplayHarness creates a real GameServer with real services.

## Next Phase Readiness
- EnetRecorder ready for live session recording (call GameServer::StartRecording)
- ServerReplayHarness ready for Plan 03 regression tests using recorded NDJSON fixtures
- All existing replay tests pass (52 assertions in 13 cases)

---
*Phase: 14-enet6-network-traffic-replay*
*Completed: 2026-03-29*
