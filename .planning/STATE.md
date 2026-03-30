---
gsd_state_version: 1.0
milestone: v1.0
milestone_name: milestone
status: verifying
stopped_at: Phase 15 context gathered
last_updated: "2026-03-30T01:45:32.647Z"
last_activity: 2026-03-29
progress:
  total_phases: 17
  completed_phases: 12
  total_plans: 56
  completed_plans: 54
  percent: 90
---

# Project State

## Project Reference

See: .planning/PROJECT.md (updated 2026-03-27)

**Core value:** Two friends can drop into Skyrim together on Linux with zero server setup
**Current focus:** Phase 14 — enet6-network-traffic-replay

## Current Position

Phase: 14
Plan: Not started
Status: Phase complete — ready for verification
Last activity: 2026-03-29

Progress: [=========] 90%

## Performance Metrics

**Velocity:**

- Total plans completed: 0
- Average duration: -
- Total execution time: 0 hours

**By Phase:**

| Phase | Plans | Total | Avg/Plan |
|-------|-------|-------|----------|
| - | - | - | - |

**Recent Trend:**

- Last 5 plans: -
- Trend: -

*Updated after each plan completion*
| Phase 01 P01 | 4min | 2 tasks | 4 files |
| Phase 01 P02 | 3min | 1 tasks | 1 files |
| Phase 02 P01 | 3min | 2 tasks | 5 files |
| Phase 02 P02 | 4min | 2 tasks | 12 files |
| Phase 02 P03 | 14min | 2 tasks | 10 files |
| Phase 03 P01 | 2min | 2 tasks | 4 files |
| Phase 03 P02 | 6min | 2 tasks | 20 files |
| Phase 03 P05 | 21min | 2 tasks | 2 files |
| Phase 03.1 P02 | 4min | 2 tasks | 12 files |
| Phase 03.1 P02 | 3min | 2 tasks | 10 files |
| Phase 07 P01 | 4min | 2 tasks | 6 files |
| Phase 07 P02 | 4min | 2 tasks | 6 files |
| Phase 07 P03 | 3min | 2 tasks | 5 files |
| Phase 07 P04 | 4min | 2 tasks | 12 files |
| Phase 07 P05 | 9min | 2 tasks | 12 files |
| Phase 07 P07 | 4min | 2 tasks | 7 files |
| Phase 07 P08 | 5min | 1 tasks | 2 files |
| Phase 11 P02 | 6min | 2 tasks | 4 files |
| Phase 11 P04 | 8min | 2 tasks | 17 files |
| Phase 11 P05 | 15min | 2 tasks | 8 files |
| Phase 12 P01 | 8min | 2 tasks | 11 files |
| Phase 12 P02 | 4min | 2 tasks | 8 files |
| Phase 13 P02 | 10min | 2 tasks | 9 files |
| Phase 14 P01 | 8min | 2 tasks | 6 files |
| Phase 14 P02 | 3min | 2 tasks | 8 files |
| Phase 14 P03 | 34min | 2 tasks | 10 files |

## Accumulated Context

### Decisions

Decisions are logged in PROJECT.md Key Decisions table.
Recent decisions affecting current work:

- Roadmap: GATE-01/GATE-02 placed in Phase 1 as go/no-go gates before full migration investment
- Roadmap: Phases 4-5 (debug, testing) can parallelize with Phase 3 since they depend on Phase 2 only
- Roadmap: UI port (Phase 6) deferred to last since it requires client DLL structure from Phase 3
- [Phase 01]: XMake v3.0.8 installed, MinGW platform block uses static linking to prevent runtime DLL deps
- [Phase 01]: GATE-01 uses inline C-compatible SKSE structs, GATE-02 hooks GetTickCount for initial validation
- [Phase 01]: XMake v3 requires add_requires() in root scope; MinGW needs set_prefixname('') for SKSE-compatible DLL names
- [Phase 02]: rpmalloc replaces mimalloc as default allocator (MinGW/GCC compatible)
- [Phase 02]: All MSVC build blocks deleted from root xmake.lua (clean break per D-02)
- [Phase 02]: sentry-native removed with HAS_SENTRY conditionals for future re-enablement
- [Phase 02]: ThreadUtils uses platform-native APIs: SetThreadDescription (Win10+) and pthread_setname_np (Linux)
- [Phase 02]: Tier 3 client targets gated behind is_plat windows|mingw in libraries/xmake.lua
- [Phase 02]: rpmalloc requires explicit init in test main (unlike mimalloc)
- [Phase 02]: Windows-only packages (minhook, xbyak) gated to windows|mingw in root xmake.lua
- [Phase 02]: External vendored libs (DirectXTK, imgui) re-added with platform gate after Plan 02 removal
- [Phase 03]: imgui kept unconditional for Phase 6 readiness; _initterm_e hook guarded behind _MSC_VER
- [Phase 03]: OverlayService stub returns void* from GetOverlayApp() to avoid CEF dependency; DiscordService emplace unconditional via no-op stub
- [Phase 03]: RipAllocateN uses 1MB static fallback pool instead of immersive_launcher highrip section
- [Phase 03.1]: component() helper modified centrally for 5 component targets instead of individual files
- [Phase 07]: Relay DLL and native client use internal platform guards with unconditional includes in Code/xmake.lua
- [Phase 07]: protocol.h in relay_dll/ is single source of truth for DLL-to-native TCP IPC protocol
- [Phase 07]: Protocol uses flat binary C structs with cstdint only, compiles under both MinGW and system GCC
- [Phase 07]: SPSC command queue: 256-slot power-of-2 ring buffer with acquire/release memory ordering
- [Phase 07]: getpid() via extern C for real Linux PID under Wine instead of GetCurrentProcessId()
- [Phase 07]: OutputDebugStringA for relay DLL logging to avoid spdlog dependency (per D-13)
- [Phase 07]: ProcMemReader adapted from LethalInjection read-only; PointerTable uses hopscotch_map; GameReader stubs health read for service rewrite
- [Phase 07]: Address Library v2 parser inline in DLL (~200 LOC), no SKSE SDK dependency
- [Phase 07]: DLL total 1679 LOC (exceeds 500 soft target, mechanical trampoline repetition)
- [Phase 07]: ECS components defined in CharacterService.h for native_client self-containment
- [Phase 07]: Frame pacing via sleep_for; poll()-based receive deferred until TcpClient exposes socket fd
- [Phase 07]: Catch2 comma-separated tag syntax for OR-matching multiple test groups in smoke tests
- [Phase 07]: xmake show projectdir parsing for worktree-safe build artifact location in smoke tests
- [Phase 11]: g_dirtyCache non-static for cross-TU access between hook_trampolines.cpp and relay_main.cpp
- [Phase 11]: JSON command parsing on receive thread, packed into CommandSlot binary for game-thread SPSC queue safety
- [Phase 11]: ReadFormId/ReadPosition/ReadRotation helpers for typed field extraction in JSON trampolines
- [Phase 11]: AnimationSystem converted from binary CommandPacket to JSON SendJsonLine (Rule 3: blocking reference)
- [Phase 11]: Legacy binary tests preserved with [Legacy] tag; post-build auto-deploy added to xmake.lua
- [Phase 12]: ReplayHarness uses binary HookEventPacket dispatch (not JSON), with NDJSON-to-binary conversion layer
- [Phase 12]: nlohmann_json v3.11.3 added for NDJSON parsing in replay harness only (excluded from production binary)
- [Phase 12]: SkyrimTogetherServer not needed as ReplayTests dependency (native_client services are self-contained)
- [Phase 12]: FIXTURE_PATH macro resolves from os.projectdir() for worktree-safe absolute paths
- [Phase 12]: Replay sources included in production binary since nlohmann_json already available via third_party
- [Phase 13]: ActorValueService regen_attr creates cache entry on first event (not just update existing)
- [Phase 14]: nlohmann_json added to server packages for Recording/ module; DEF_MSG_NAME macro for Visit()-based type name registry
- [Phase 14]: Tag-struct constructors (WorldTestMode, GameServerTestMode) for test mode instead of bool flags or subclasses
- [Phase 14]: EnetRecorder taps at application layer (Send/OnConsume) per D-03, per-peer NDJSON files with dir field per D-04
- [Phase 14]: Programmatic fixture generation via MessageSerializer for enet6 replay tests
- [Phase 14]: DualReplayHarness wraps both ReplayHarness and ServerReplayHarness for combined client+server replay

### Pending Todos

None yet.

### Roadmap Evolution

- Phase 03.1 inserted after Phase 03: Build Performance & Resource Optimization (URGENT) — full rebuild takes 6+ minutes linking 1.5GB static archive, excessive memory/CPU usage makes iterative debugging unmanageable
- Phase 07 added: Native Linux Build with SKSE TCP Relay — split codebase so 90% builds natively on Linux, thin MinGW DLL acts as TCP relay into SKSE
- Phase 12 added: NDJSON Replay Capture and Integration Tests — record live sessions, replay without Skyrim, regression tests against real game data
- Phase 13 added: Network Replay Recording and Regression Testing — extend replay system to capture full enet6 network traffic (auth, AssignCharacter, CharacterSpawn, movement) alongside DLL hook events; dual host+client recording for offline regression tests with real game data
- Phase 15 added: Remote Actor Spawning & Game Integration — bridge server CharacterSpawnRequest to in-game actor creation via relay DLL commands, position sync for remote actors, INI auto-configuration in launch scripts
- Phase 14 added: enet6 Network Traffic Replay — deferred from Phase 13 scope; capture and replay full enet6 network traffic (auth, AssignCharacter, CharacterSpawn, movement) alongside DLL hook events with dual host+client recording for offline regression testing

### Blockers/Concerns

- Phase 1 GATE-01/GATE-02 are binary go/no-go. If MinGW cannot produce valid SKSE plugins, the entire MinGW approach needs rethinking.
- Phase 3 is HIGH risk per research: MinHook/Xbyak cross-compilation, SKSE ABI compatibility, CEF exclusion strategy.
- Phase 4 MEDIUM risk: Wine upgrade may break existing Proton setup.

## Session Continuity

Last session: 2026-03-30T01:45:32.645Z
Stopped at: Phase 15 context gathered
Resume file: .planning/phases/15-remote-actor-spawning-game-integration/15-CONTEXT.md
