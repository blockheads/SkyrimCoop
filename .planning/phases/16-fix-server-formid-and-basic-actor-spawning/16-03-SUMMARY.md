---
phase: 16-fix-server-formid-and-basic-actor-spawning
plan: 03
subsystem: relay-dll
tags: [skse, mingw, actor-spawning, iformfactory, tesnpc, ai-suppression, address-library]

requires:
  - phase: 07-native-linux-build-with-skse-tcp-relay
    provides: relay DLL architecture with hook trampolines and binary protocol
provides:
  - Dual-path actor spawning in relay DLL (formId-based + TESNPC::Create appearance path)
  - AI suppression on all spawned remote actors (flags1 bit 0x02000000)
  - IFormFactory table resolution (addr 400508) with vtable[1] Create() call
  - 7 new address library resolver functions for game function pointers
  - SpawnAppearanceHeader binary protocol for variable-length appearance payloads
  - Thread-safe appearance spawn queue (mutex + vector, drained on game thread)
affects: [16-04, 16-05, native-client, actor-lifecycle]

tech-stack:
  added: []
  patterns:
    - "Dual-path spawn: formId lookup vs IFormFactory::Create for player characters"
    - "Variable-length command payloads via SpawnAppearanceHeader struct in binary protocol"
    - "Game function resolver pattern: static pointer resolved during InstallAllHooks()"

key-files:
  created: []
  modified:
    - Code/relay_dll/hook_trampolines.h
    - Code/relay_dll/hook_trampolines.cpp
    - Code/relay_dll/relay_main.cpp
    - Code/relay_dll/protocol.h

key-decisions:
  - "Used existing binary protocol (protocol.h) instead of JSON protocol -- plan assumed json_protocol.h from 16-02 which runs in parallel"
  - "BGSLoadFormBuffer Deserialize deferred to future iteration -- MVP spawns with player defaults, appearance buffer stored for later"
  - "IFormFactory::Create at vtable[1] confirmed from STR IFormFactory.h -- destructor at [0], Create at [1]"
  - "AI suppression via flags1 bit 0x02000000 in both spawn paths per D-06"

patterns-established:
  - "Game function resolver: ResolveAddress() during hook init, static pointer + getter function"
  - "Variable-length command: SpawnAppearanceHeader + trailing data, parsed in MonitorThread, queued for game thread"

requirements-completed: [SPAWN-08]

duration: 8min
completed: 2026-03-30
---

# Phase 16 Plan 03: TESNPC Create Pipeline Summary

**Dual-path actor spawning in relay DLL: IFormFactory::Create for player characters (FormId=0) with AI suppression, and TESForm::GetById for world NPCs**

## Performance

- **Duration:** 8 min
- **Started:** 2026-03-30T04:46:36Z
- **Completed:** 2026-03-30T04:54:28Z
- **Tasks:** 2
- **Files modified:** 4

## Accomplishments
- Resolved 7 new address library IDs for the TESNPC::Create and Actor::Create pipelines (FormById, GameHeapAllocate, GameHeapInstance, SpawnNewREFR, ModManagerInstance, SetLevelMod, IFormFactoryTable)
- Implemented full Actor::Create pipeline for both formId-based world NPCs and custom player characters via IFormFactory::Create at vtable[1]
- Added AI suppression (flags1 |= 0x02000000) to both spawn paths preventing remote actors from executing Skyrim's default AI
- Extended binary protocol with CMD_SPAWN_ACTOR, CMD_SPAWN_ACTOR_APPEARANCE, and HOOK_SPAWN_RESULT opcodes

## Task Commits

Each task was committed atomically:

1. **Task 1: Resolve new address library IDs for TESNPC::Create pipeline** - `22ab34c4` (feat)
2. **Task 2: Extend CMD_SPAWN_ACTOR for TESNPC::Create appearance path with AI suppression** - `377631b3` (feat)

## Files Created/Modified
- `Code/relay_dll/hook_trampolines.h` - Added 8 getter function declarations for game function pointers
- `Code/relay_dll/hook_trampolines.cpp` - Added 7 AddressIds, 7 static resolved pointers, 8 getter implementations, address resolution in InstallAllHooks()
- `Code/relay_dll/relay_main.cpp` - Added SpawnActorByFormId(), SpawnActorFromAppearance(), DrainCommandQueue() with dual-path handling, appearance spawn queue, SendSpawnResult()
- `Code/relay_dll/protocol.h` - Added CMD_SPAWN_ACTOR, CMD_SPAWN_ACTOR_APPEARANCE, CMD_REMOVE_ACTOR, HOOK_SPAWN_RESULT opcodes, SpawnAppearanceHeader struct

## Decisions Made
- Adapted plan from assumed JSON protocol to existing binary protocol (protocol.h) since json_protocol.h from plan 16-02 runs in parallel and wasn't available yet
- Deferred BGSLoadFormBuffer Deserialize to future iteration -- constructing the exact struct layout requires deep SaveLoad.h analysis; MVP spawns with player defaults which is sufficient to verify the pipeline works
- Increased receive buffer to 16KB to accommodate AppearanceBuffer payloads over binary protocol

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 3 - Blocking] Adapted from JSON protocol to binary protocol**
- **Found during:** Task 2 (appearance spawn implementation)
- **Issue:** Plan assumed json_protocol.h with nlohmann/json and JsonOp/JsonField constants from plan 16-02, but the DLL uses a binary PacketHeader protocol and has no JSON dependency
- **Fix:** Implemented appearance spawn path using existing binary protocol: added SpawnAppearanceHeader struct to protocol.h, CMD_SPAWN_ACTOR_APPEARANCE opcode, and binary payload parsing in MonitorThread
- **Files modified:** Code/relay_dll/protocol.h, Code/relay_dll/relay_main.cpp
- **Verification:** xmake build SkyrimCoopHooksDLL succeeds
- **Committed in:** 377631b3 (Task 2 commit)

**2. [Rule 3 - Blocking] Created game function getters that plan assumed existed**
- **Found during:** Task 1 (address resolution)
- **Issue:** Plan's interfaces section listed existing getter functions (GetFormById, GetGameHeapAllocate, etc.) but these didn't exist in the DLL codebase
- **Fix:** Created all 8 getter functions with proper address library resolution in hook_trampolines.cpp
- **Files modified:** Code/relay_dll/hook_trampolines.h, Code/relay_dll/hook_trampolines.cpp
- **Verification:** xmake build SkyrimCoopHooksDLL succeeds
- **Committed in:** 22ab34c4 (Task 1 commit)

---

**Total deviations:** 2 auto-fixed (2 blocking)
**Impact on plan:** Both deviations were necessary adaptations to the actual codebase state vs plan assumptions. The binary protocol approach is cleaner than JSON for a performance-critical DLL. No scope creep.

## Known Stubs

- `SpawnActorFromAppearance()` line ~330: BGSLoadFormBuffer Deserialize is logged but not executed. Appearance data is stored but not applied to the TESNPC. Actor spawns with player default appearance. Future plan will implement full deserialization via SaveLoad.h struct layout.

## Issues Encountered
None -- plan executed with protocol adaptation.

## User Setup Required
None - no external service configuration required.

## Next Phase Readiness
- Relay DLL can now handle both spawn paths via binary protocol
- Native client (plan 16-02) needs to send CMD_SPAWN_ACTOR / CMD_SPAWN_ACTOR_APPEARANCE commands
- BGSLoadFormBuffer deserialization is the main remaining work for full appearance reconstruction
- AI suppression is in place; no further work needed for basic remote actor behavior

---
*Phase: 16-fix-server-formid-and-basic-actor-spawning*
*Completed: 2026-03-30*
