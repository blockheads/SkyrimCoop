---
gsd_state_version: 1.0
milestone: v1.0
milestone_name: milestone
status: executing
stopped_at: Completed 09-07-PLAN.md
last_updated: "2026-03-29T01:48:44.630Z"
last_activity: 2026-03-29 -- Phase 09 execution started
progress:
  total_phases: 10
  completed_phases: 6
  total_plans: 37
  completed_plans: 33
  percent: 89
---

# Project State

## Project Reference

See: .planning/PROJECT.md (updated 2026-03-27)

**Core value:** Two friends can drop into Skyrim together on Linux with zero server setup
**Current focus:** Phase 09 — in-game-p2p-integration-test

## Current Position

Phase: 09 (in-game-p2p-integration-test) — EXECUTING
Plan: 1 of 10
Status: Executing Phase 09
Last activity: 2026-03-29 -- Phase 09 execution started

Progress: [=========-] 89%

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
| Phase 05 P01 | 8min | 2 tasks | 18 files |
| Phase 05 P02 | 22min | 3 tasks | 8 files |
| Phase 05 P03 | 6min | 3 tasks | 4 files |
| Phase 08 P01 | 14min | 1 tasks | 9 files |
| Phase 08 P02 | 7min | 2 tasks | 3 files |
| Phase 09 P01 | 3min | 2 tasks | 1 files |
| Phase 09 P02 | 3min | 1 tasks | 4 files |
| Phase 09 P03 | 5min | 3 tasks | 3 files |
| Phase 09 P04 | 2min | 1 tasks | 1 files |
| Phase 09 P05 | 1min | 1 tasks | 1 files |
| Phase 09 P06 | 3min | 2 tasks | 3 files |
| Phase 09 P07 | 3min | 2 tasks | 3 files |

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
- [Phase 05]: Keep admin session infrastructure in GameServer (used by AdminService, CommandService)
- [Phase 05]: Keep gameplay settings for embedded host configuration (difficulty, PvP, death system)
- [Phase 05]: IPv6 loopback (::1) for enet6 test connections due to dual-stack server binding
- [Phase 05]: Shared static GameServer per test process: ConsoleRegistry static items consumed once
- [Phase 05]: rpmalloc constructor(101) priority to init before Catch2 static registration
- [Phase 05]: fake_player reuses MockClient from 05-02; self-test mode enables CI without Skyrim
- [Phase 08]: std::function callback for loopback routing avoids cross-target include dependencies
- [Phase 08]: kHostConnectionId = 0xFFFF'FFFF'0000'0001 to avoid enet6 peer pointer collision
- [Phase 08]: ServerMessageRouter skips packet-type byte 0 matching Server::HandleMessage strip behavior
- [Phase 08]: Added sol2/lua/sqlite3 to native_client for GameServer.h transitive header chain
- [Phase 08]: Server PCH preamble pattern established for native_client sources including GameServer.h or encoding headers
- [Phase 08]: Host mode default: empty --connect = host, non-empty = join (per D-01/D-02)
- [Phase 08]: Movement tick=0 for Phase 8 MVP; tick ordering deferred to interpolation work
- [Phase 09]: BroadcastTrackingClient inherits MockClient for opcode counting; broadcast verification headless-aware (auth required, broadcasts optional)
- [Phase 09]: mock_skyrim_loader derives relay_info.json from DLL path; binary record format: uint64 timestamp + HookEventPacket
- [Phase 09]: Inverted --with-fake-player to --no-fake-player; fake_player runs by default now that embedded GameServer exists
- [Phase 09]: JoinClient follows MockClient PacketView/Send pattern for enet6 remote host connection

- [Phase 09]: Step 4 mock relay pipeline is best-effort; CI runner works without Skyrim/Wine/MinGW
- [Phase 09]: Sequential relay_info.json capture: host relay info consumed then deleted before join launch to prevent port collision
- [Phase 09]: Linear scan for ValidateAddress acceptable for diagnostic use; pread still attempted for unmapped addresses
- [Phase 09]: 6 essential hooks (ActorProcess, SetPosition, CharacterCtor/2, SpawnActorInWorld, AddDeathItems) defaulted on for stability; full 31-hook set via SKYRIM_COOP_ALL_HOOKS=1

### Pending Todos

None yet.

### Roadmap Evolution

- Phase 03.1 inserted after Phase 03: Build Performance & Resource Optimization (URGENT) — full rebuild takes 6+ minutes linking 1.5GB static archive, excessive memory/CPU usage makes iterative debugging unmanageable
- Phase 07 added: Native Linux Build with SKSE TCP Relay — split codebase so 90% builds natively on Linux, thin MinGW DLL acts as TCP relay into SKSE
- Phase 08 added: P2P Embedded GameServer — native process hosts enet6 GameServer for multiplayer; executes before Phase 6 (UI needs working connection first)
- Phase 09 added: In-Game P2P Integration Test — full end-to-end test with real Skyrim instance, relay DLL, embedded GameServer, and fake_player joining via enet6
- Phase 09.1 inserted after Phase 09: DLL-Side FormId Resolution (URGENT) — Wine heap addresses not accessible via /proc/pid/mem; DLL must read formIds inside Wine and send values over TCP

### Blockers/Concerns

- Phase 1 GATE-01/GATE-02 are binary go/no-go. If MinGW cannot produce valid SKSE plugins, the entire MinGW approach needs rethinking.
- Phase 3 is HIGH risk per research: MinHook/Xbyak cross-compilation, SKSE ABI compatibility, CEF exclusion strategy.
- Phase 4 MEDIUM risk: Wine upgrade may break existing Proton setup.

### Quick Tasks Completed

| # | Description | Date | Commit | Directory |
|---|-------------|------|--------|-----------|
| 260328-ki2 | Update test_ingame_sync.sh to use sStartingCell INI injection | 2026-03-28 | 9c837fae | [260328-ki2-update-test-ingame-sync-sh-to-use-sstart](./quick/260328-ki2-update-test-ingame-sync-sh-to-use-sstart/) |

## Session Continuity

Last session: 2026-03-29T01:23:41.600Z
Stopped at: Completed 09-07-PLAN.md
Resume file: None
