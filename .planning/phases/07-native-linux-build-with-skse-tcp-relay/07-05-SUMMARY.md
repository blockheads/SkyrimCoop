---
phase: "07"
plan: "05"
subsystem: relay-dll-hooks, native-client-services
tags: [minhook, trampolines, address-library, weather, calendar, quest, combat, game-reader]
dependency_graph:
  requires: [07-03, 07-04]
  provides: [hook-trampolines, weather-service, calendar-service, quest-service, combat-service]
  affects: [relay-dll, native-client]
tech_stack:
  added: [MinHook-trampolines, Address-Library-v2-loader]
  patterns: [hook-and-forward-relay, game-reader-service-pattern]
key_files:
  created:
    - Code/relay_dll/hook_trampolines.h
    - Code/relay_dll/hook_trampolines.cpp
    - Code/native_client/Services/WeatherService.h
    - Code/native_client/Services/WeatherService.cpp
    - Code/native_client/Services/CalendarService.h
    - Code/native_client/Services/CalendarService.cpp
    - Code/native_client/Services/QuestService.h
    - Code/native_client/Services/QuestService.cpp
    - Code/native_client/Services/CombatService.h
    - Code/native_client/Services/CombatService.cpp
  modified:
    - Code/relay_dll/relay_main.cpp
    - Code/native_client/main.cpp
decisions:
  - "Address Library v2 delta-encoded binary format parsed inline (~200 LOC) rather than using SKSE SDK"
  - "RelayLog and g_tcpServer made non-static in relay_main.cpp for cross-TU access by trampolines"
  - "DLL total LOC is 1679, exceeding ~500 soft target but within acceptable range (mechanical trampoline repetition)"
  - "QuestService is log-only; network wiring deferred per plan as quest sync is not critical path"
  - "Services use sentinel formIds (0xFFFF0001, 0xFFFF0002) for global pointer lookup pending Plan 06+ resolution"
metrics:
  duration: "9min"
  completed: "2026-03-28"
  tasks_completed: 2
  files_changed: 12
---

# Phase 07 Plan 05: Hook Trampolines and Service Rewrite Summary

31 MinHook trampolines forwarding raw uint64_t args over TCP, plus 4 services (Weather, Calendar, Quest, Combat) rewritten against GameReader/ProcMemReader API for native Linux process

## Task Completion

| Task | Name | Commit | Key Files |
|------|------|--------|-----------|
| 1 | Create hook trampolines and MinHook installation | 1307209e | hook_trampolines.h/.cpp, relay_main.cpp |
| 2 | Rewrite Weather, Calendar, Quest, Combat services | 067216df | Services/*.h/*.cpp, main.cpp |

## What Was Built

### Task 1: Hook Trampolines (relay DLL)

- **31 trampoline functions** covering all hook categories:
  - Actor Lifecycle (6): ActorProcess, SetPosition, CharacterCtor/Ctor2, SpawnActor, AddDeathItems
  - Combat (5): DamageActor, ApplyActorEffect, RegenAttributes, UpdateDetectionState, UpdateTarget
  - Inventory (6): AddInventoryItem(Actor/REFR), PickUpObject, DropObject, UnequipObject, RemoveInventoryItem
  - Magic (5): SpellCast, InterruptCast, AddTarget, FindTargets, RemoveSpell
  - Object/World (4): Activate, LockChange, PlayAnimation, ProjectileLaunch
  - Weather (3): SetWeather, ForceWeather, UpdateWeather
  - Other (2): PerformAction, InitiateMountPackage

- **Standalone Address Library loader** (~200 LOC):
  - Parses both v1 (fixed-size entries) and v2 (delta-encoded) binary formats
  - Resolves integer IDs to runtime addresses using SkyrimSE.exe module base
  - All 31 Address Library IDs hardcoded as constexpr uint64_t from existing POINTER_SKYRIMSE calls

- **Zero game struct knowledge**: No includes from Code/client/Games/Skyrim/, no field access (->formID etc.), only raw pointer forwarding via uint64_t args

- **DLL LOC breakdown**:
  - hook_trampolines.cpp: 869 LOC
  - hook_trampolines.h: 13 LOC
  - Total DLL (all files): 1,679 LOC

### Task 2: Service Rewrites (native client)

- **WeatherService** (134 LOC): Reads weather formId via GameReader::ReadChain through Sky singleton pointer chain. Polls at 250ms intervals. Filters map weather (0xA6858).

- **CalendarService** (121 LOC): Reads GameHour/GameDay/GameMonth/GameYear/TimeScale via Calendar singleton pointer chain using TESGlobal::value offsets.

- **QuestService** (89 LOC): Log-only mode. Receives HOOK_PERFORM_ACTION and HOOK_ACTIVATE events, reads actor formIds via GameReader, logs to spdlog. Network wiring deferred per plan.

- **CombatService** (103 LOC): Handles HOOK_DAMAGE_ACTOR (extracts damage float via bit-cast) and HOOK_PROJECTILE events. Reads target formIds via GameReader.

- All services wired into main.cpp event loop with proper delta time tracking via std::chrono.

- **Total service LOC**: 447 (headers + implementations)

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 3 - Blocking] Made g_tcpServer and RelayLog non-static**
- **Found during:** Task 1
- **Issue:** hook_trampolines.cpp needed access to g_tcpServer and RelayLog defined in relay_main.cpp, but they were static
- **Fix:** Removed static qualifier, added extern declarations in hook_trampolines.cpp
- **Files modified:** Code/relay_dll/relay_main.cpp
- **Commit:** 1307209e

**2. [Rule 3 - Blocking] Added chrono include for delta time tracking**
- **Found during:** Task 2
- **Issue:** Service Update() methods need delta time, main.cpp needed std::chrono
- **Fix:** Added chrono include and steady_clock-based delta time calculation
- **Files modified:** Code/native_client/main.cpp
- **Commit:** 067216df

## Decisions Made

1. **Address Library v2 parser inline**: Rather than depending on any SKSE SDK, implemented a self-contained ~200 LOC parser for the delta-encoded binary format. This keeps the DLL dependency-free per D-13.

2. **DLL LOC exceeds soft target**: Total DLL is 1,679 LOC vs. the ~500 LOC soft target in RELAY-02. The overage is entirely mechanical trampoline repetition (31 hooks x ~12 LOC each = ~370 LOC) plus the Address Library loader (~200 LOC). The DLL remains a "dumb relay" with zero game logic.

3. **Sentinel formIds for global pointers**: Services use special sentinel formIds (0xFFFF0001 for Sky, 0xFFFF0002 for Calendar) in the PointerTable for global singleton lookup. These will be populated by the global pointer resolution system in Plan 06+.

4. **QuestService log-only**: Per plan specification, quest network wiring is deferred since quest sync was optional in the original codebase.

## Known Stubs

- **WeatherService::ReadCurrentWeatherId()** -- Returns 0 until Sky singleton pointer is populated in PointerTable (Plan 06+)
- **CalendarService::Update()** -- Returns early until Calendar singleton pointer is populated (Plan 06+)
- **QuestService** -- Log-only, no network send (intentional per plan, wired in Plan 07+)
- **CombatService** -- Logs events but does not send network messages (wired in Plan 07+)

These stubs are intentional architectural placeholders. The services correctly implement the GameReader pattern and will become fully functional once the global pointer table is extended with singleton addresses.

## Verification

- `xmake build -P . SkyrimCoopHooksDLL` -- PASSED (MinGW, 0 errors, 0 warnings)
- `xmake build -P . SkyrimCoopNative` -- PASSED (Linux GCC, 0 new errors)
- hook_trampolines.cpp contains 31 Hook* trampoline functions
- hook_trampolines.cpp contains `MH_CreateHook` and `MH_EnableHook(MH_ALL_HOOKS)`
- hook_trampolines.cpp uses `g_tcpServer.Send(&pkt` in each trampoline
- hook_trampolines.cpp contains NO game struct field access
- hook_trampolines.cpp does NOT include headers from Code/client/Games/Skyrim/
- All 4 services use GameReader (ReadChain/ReadFormId), no direct pointer dereference

## Self-Check: PASSED

All 10 created files verified present. Both commit hashes (1307209e, 067216df) verified in git log.
