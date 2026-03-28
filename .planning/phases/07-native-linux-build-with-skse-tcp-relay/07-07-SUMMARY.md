---
phase: 07-native-linux-build-with-skse-tcp-relay
plan: 07
subsystem: client
tags: [entt, ecs, interpolation, animation, character-sync, glm, native-linux]

requires:
  - phase: 07-native-linux-build-with-skse-tcp-relay (plans 05, 06)
    provides: "Game bridge (GameReader, ProcMemReader, PointerTable), 7 existing services, TCP client"
provides:
  - "CharacterService: actor lifecycle, 100ms movement sync, animation variable sync"
  - "InterpolationSystem: pure-math position lerp for remote actors"
  - "AnimationSystem: CMD_PLAY_ANIMATION dispatch for remote actor animations"
  - "Full main loop wiring all 8 services + 2 systems with frame pacing"
  - "ECS components: LocalComponent, RemoteComponent, InterpolationComponent, AnimationComponent"
affects: [07-08, network-transport-integration]

tech-stack:
  added: []
  patterns:
    - "ECS components defined in CharacterService.h, shared across service and systems"
    - "InterpolationSystem is pure math (no game API), operates on InterpolationComponent"
    - "Frame-paced main loop at ~60 FPS with delta time clamping"

key-files:
  created:
    - Code/native_client/Services/CharacterService.h
    - Code/native_client/Services/CharacterService.cpp
    - Code/native_client/Systems/InterpolationSystem.h
    - Code/native_client/Systems/InterpolationSystem.cpp
    - Code/native_client/Systems/AnimationSystem.h
    - Code/native_client/Systems/AnimationSystem.cpp
  modified:
    - Code/native_client/main.cpp

key-decisions:
  - "ECS components (Local/Remote/Interpolation/Animation) defined in CharacterService.h rather than separate component files, keeping native_client self-contained"
  - "Frame pacing with sleep_for instead of poll()-based non-blocking receive since TcpClient does not expose raw socket fd"
  - "Position data packed into uint64 args for CMD_SET_POSITION (3 floats position + 2 floats rotation in 4 args)"

patterns-established:
  - "Service+System pattern: services handle hook events and network messages, systems do pure computation on ECS views"
  - "All 8 services dispatched uniformly via DispatchHookEvent helper in main.cpp"

requirements-completed: [RELAY-08]

duration: 4min
completed: 2026-03-28
---

# Phase 07 Plan 07: CharacterService Rewrite + Interpolation/Animation Systems Summary

**CharacterService rewritten for native Linux process (510 LOC) with 100ms movement sync via /proc/pid/mem reads, plus InterpolationSystem (pure-math lerp) and AnimationSystem (CMD_PLAY_ANIMATION dispatch) wired into full 8-service main loop**

## Performance

- **Duration:** 4 min
- **Started:** 2026-03-28T16:44:31Z
- **Completed:** 2026-03-28T16:49:13Z
- **Tasks:** 2
- **Files modified:** 7

## Accomplishments
- CharacterService handles full actor lifecycle (added/removed/ctor/spawn/death), 100ms movement polling, and animation variable sync -- all via GameReader memory reads, zero direct pointer dereferences
- InterpolationSystem uses pure glm::mix lerp math on InterpolationComponent, no game API dependency
- AnimationSystem sends CMD_PLAY_ANIMATION commands to DLL for remote actor animation application
- Main loop wires all 8 services + 2 systems with frame pacing at ~60 FPS, periodic heartbeats, and clean shutdown

## Task Commits

Each task was committed atomically:

1. **Task 1: Rewrite CharacterService for native process** - `e9cfef33` (feat)
2. **Task 2: Move InterpolationSystem and AnimationSystem to native, wire into main loop** - `e8eaa514` (feat)

## Files Created/Modified
- `Code/native_client/Services/CharacterService.h` - ECS components + CharacterService declaration (110 LOC)
- `Code/native_client/Services/CharacterService.cpp` - Actor lifecycle, movement sync, animation, remote actor management (510 LOC)
- `Code/native_client/Systems/InterpolationSystem.h` - Pure math interpolation system interface
- `Code/native_client/Systems/InterpolationSystem.cpp` - glm::mix lerp on InterpolationComponent view
- `Code/native_client/Systems/AnimationSystem.h` - Animation command dispatch interface
- `Code/native_client/Systems/AnimationSystem.cpp` - CMD_PLAY_ANIMATION packet building for dirty remote actors
- `Code/native_client/main.cpp` - Full main loop with 8 services, 2 systems, frame pacing, heartbeat

## Decisions Made
- ECS components (LocalComponent, RemoteComponent, InterpolationComponent, AnimationComponent) defined in CharacterService.h for simplicity -- separate component files can be extracted later
- Frame pacing via sleep_for instead of poll()-based non-blocking receive since TcpClient's socket fd is private
- Position/rotation packed into uint64 args for CMD_SET_POSITION command packets (3 floats + 2 floats across 4 args)

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 3 - Blocking] Removed unused TryReceivePacket and poll.h include**
- **Found during:** Task 2 (main.cpp wiring)
- **Issue:** TryReceivePacket was written for poll()-based non-blocking receive, but TcpClient doesn't expose raw socket fd
- **Fix:** Removed dead code, used blocking receive with frame pacing instead
- **Files modified:** Code/native_client/main.cpp
- **Verification:** Clean build with no warnings
- **Committed in:** e8eaa514 (Task 2 commit)

---

**Total deviations:** 1 auto-fixed (1 blocking)
**Impact on plan:** Minor cleanup. No scope change.

## Issues Encountered
None

## Known Stubs
- `CharacterService::SyncMovement()` has TODO for sending ClientReferencesMoveRequest via network transport (logged, not sent) -- will be wired in Plan 08+ when network transport layer is integrated
- `CharacterService::SyncMovement()` animation variable sync logged but not sent -- same dependency on network transport

## Next Phase Readiness
- All 8 native services now exist and compile
- InterpolationSystem and AnimationSystem provide the core movement/animation pipeline
- Ready for Plan 08 (network transport integration) to wire actual message sending/receiving
- Binary compiles and runs (exits cleanly on expected connection failure)

---
*Phase: 07-native-linux-build-with-skse-tcp-relay*
*Completed: 2026-03-28*
