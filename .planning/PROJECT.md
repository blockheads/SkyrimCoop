# SkyrimCoop

## What This Is

A peer-to-peer cooperative multiplayer mod for Skyrim Special Edition, forked from Tilted Online (Skyrim Together Reborn). Converts the original MMO-style client-server architecture to a host-based co-op model (2-4 players, drop-in via direct IP or Steam invite). The full STR client builds under MinGW from Linux, producing a single SKSE plugin DLL that loads into Skyrim under Proton.

## Core Value

Two friends can drop into Skyrim together on Linux with zero server setup — host clicks "host," friend joins via direct connect, and it just works.

## Current Milestone: v2.0 MinGW Full Client with Loopback Server

**Goal:** Get the existing STR client and server compiling under MinGW as a single SKSE DLL, embed the server via loopback bridge, and verify existing sync features work between two players.

**Target features:**
- Full STR client (Code/client/) compiles under MinGW as a single SKSE DLL
- Embedded GameServer via LoopbackTransport (host mode) / enet6 (client mode)
- Existing STR sync verified through loopback (character, inventory, disconnect/reconnect)
- Two-player co-op session via direct IP connect
- Linux cross-compile + debug workflow (MinGW build, gdb with DWARF symbols)

**NOT in this milestone (future):**
- Direct authority refactor (v3.0+) — host reads engine directly, bypasses loopback
- Full game state sync (v4.0+) — physics, AI, scripting, weather made host-authoritative

## Long-Term Vision

The ultimate goal is to make Skyrim fully host-authoritative for co-op: the host's Skyrim engine IS the single simulation, clients are pure renderers receiving state. This requires rewriting every game system (physics, AI, scripts, weather, etc.) to be multiplayer-aware. The pattern is always: host simulates → state captured → sent to clients → clients interpolate/apply.

**Milestone roadmap:**
- **v2.0 (current):** MinGW build + loopback bridge + verify existing STR features
- **v3.0:** Direct authority rewrite — host services read engine directly (see NETWORK-ARCHITECTURE.md, WHY_DIRECT_AUTHORITY_IS_REQUIRED.md)
- **v4.0+:** Full game state sync — physics/Havok, AI, Papyrus scripts, weather/lighting, cell transitions (likely multiple milestones)

## Requirements

### Validated

- ✓ MinGW cross-compilation produces working SKSE plugins — Phase 1 GATE-01/GATE-02
- ✓ MinHook produces working x64 function hooks under MinGW — Phase 1 GATE-02
- ✓ Address Library v2 resolves game addresses at runtime — Phase 7
- ✓ MSVC ABI compatibility layers work (MinGWCompat.h, link_stubs.cpp, -mms-bitfields) — Phase 2-3
- ✓ enet6 UDP networking with GameServer — Phase 5
- ✓ Message encoding/serialization (58 client / 61 server opcodes) — existing STR
- ✓ EnTT-based ECS entity management — existing STR
- ✓ Lua server-side scripting via Sol2 — existing STR
- ✓ Server builds on Linux — existing STR

### Active

- [x] Full Code/client/ compiles under MinGW as SKSE DLL (CEF/DirectXTK/Discord excluded) — Validated in Phase 2: Full Client Compilation
- [x] SKSE abstraction layer: all services use ISkyrimBridge, ReplayBridge enables headless testing — Validated in Phase 2.5: SKSE Abstraction Layer
- [x] ABI safety fixes (alignas, try/catch UB, struct layout verification) — Validated in Phase 1: ABI Safety and Source Cleanup
- [x] Code/relay_dll/ removed from source tree, native_client removed from build system — Validated in Phase 1: ABI Safety and Source Cleanup
- [ ] LoopbackTransport for host-mode message delivery (in-process, no network)
- [ ] Dual-mode TransportService (loopback for host, enet6 for clients)
- [ ] HostService::StartHosting() with GameServer lifecycle management
- [ ] Two-player co-op via direct IP — both players visible with existing STR sync
- [ ] Existing STR character, inventory, disconnect/reconnect sync verified through loopback
- [ ] Linux cross-compile + debug workflow (MinGW build, gdb with DWARF symbols)

### Out of Scope

- Native Linux SKSE port — SKSE stays Windows, runs under Proton
- Dedicated server hosting / server browser — co-op only, no MMO infrastructure
- More than 4 players — keep it intimate co-op, not MMO scale
- Mobile or console clients — PC only
- Custom game engine modifications — mod only, don't touch Skyrim's engine
- Real-time voice chat — use Discord/Steam voice
- CEF overlay UI — deferred, not needed for core multiplayer
- DirectXTK debug UI — deferred
- Discord SDK integration — deferred
- Steam invite system — direct IP connect first, Steam invites later
- Relay DLL architecture — abandoned in v1.0, replaced by direct MinGW DLL
- Native Linux client (ELF binary) — abandoned in v1.0, all logic in SKSE DLL

## Context

**Forked from:** Tilted Online / Skyrim Together Reborn (GPLv3)
- Original architecture: dedicated server + multiple clients (MMO-style)
- Target architecture: host player runs embedded server in-process + clients connect (co-op)

**v1.0 learnings (abandoned relay architecture):**
- Proved MinGW can produce valid SKSE plugins (GATE-01/GATE-02)
- Proved MinHook, Address Library, and game hooking all work under MinGW
- The relay DLL + native client split added unnecessary complexity (TCP/JSON IPC, /proc/pid/mem reads, command queue)
- The full STR client CAN build under MinGW — the codebase already has extensive compat layers
- Debugging MinGW DLLs under Wine works well (DWARF symbols + gdb attach)

**Host-authoritative architecture (canonical docs):**
- `.planning/NETWORK-ARCHITECTURE.md` — "Single Simulation, Multiple Renderers" design
- `P2P_ARCHITECTURE.md` — direct authority model, no localhost loopback
- `LISTEN_SERVER_ARCHITECTURE.md` — embedded server in Skyrim process
- `WHY_DIRECT_AUTHORITY_IS_REQUIRED.md` — why direct authority is mandatory
- `P2P_REFACTOR_PLAN.md` — conversion strategy

**SKSE source reference:** `/home/bighass/Downloads/skse64_2_02_06/`
- Plugin API is pure C-linkage (extern "C" exports via GetProcAddress)
- All interfaces are function-pointer structs, not C++ vtables
- x64 calling convention identical between MSVC and MinGW

**Existing codebase state:**
- ~12k LOC across client (6.2k) and server (5.3k) in C++20
- XMake build system with MinGW platform support
- MinGW compat already in place: MinGWCompat.h, link_stubs.cpp, -mms-bitfields
- Basic Catch2 test suite + 208 replay test assertions
- Angular 16 + CEF overlay UI (deferred for this milestone)

**Key architectural change for co-op:**
- Actor ownership: host always owns NPCs/world state (simplifies from distributed ownership)
- Cell management: host's loaded cells are authoritative (removes handoff logic)
- Quest progression: syncs to host's state (removes voting/conflict resolution)
- Combat resolution: host adjudicates (removes lag compensation complexity)

## Constraints

- **Build system**: Must cross-compile Windows DLL from Linux via MinGW
- **SKSE dependency**: Client DLL loads into Skyrim via SKSE — cannot modify SKSE itself
- **Wine/Proton**: Client always runs under Wine/Proton on Linux, debug via gdb with DWARF symbols
- **License**: GPLv3 — all modifications must remain open source
- **Mod compatibility**: Host's mod list is authoritative; clients must match to connect
- **Platform**: Linux-first development, Windows compatibility maintained for end users
- **No relay**: Single DLL architecture — no TCP IPC, no separate native process

## Key Decisions

| Decision | Rationale | Outcome |
|----------|-----------|---------|
| MinGW single DLL (not relay+native split) | Relay added complexity (TCP/JSON/proc_memory) for marginal debug benefit; MinGW DLLs debuggable via gdb+DWARF | — Pending |
| Host-authoritative (single simulation) | Skyrim is closed-source, can't recreate engine in separate server; host's engine IS the simulation | — Pending |
| Embedded server in DLL process | No separate server binary; host's DLL runs GameServer in-process, clients connect via enet6 | — Pending |
| Loopback bridge first, direct authority later | Loopback preserves all existing STR service code unchanged; direct authority is a rewrite for v3.0+ | — Pending |
| CEF/UI deferred | Core multiplayer works without overlay; add UI after sync is solid | — Pending |
| Direct IP connect first (not Steam) | Simpler to implement; Steam invites are polish, not core | — Pending |
| Delete relay_dll/ and native_client/ | Clean break from v1.0 architecture; code preserved in git history | — Pending |
| v2.0 = build+verify, not rewrite | Get existing STR features working under MinGW first; rewrite for host authority is v3.0+ | — Pending |

## Evolution

This document evolves at phase transitions and milestone boundaries.

**After each phase transition** (via `/gsd:transition`):
1. Requirements invalidated? → Move to Out of Scope with reason
2. Requirements validated? → Move to Validated with phase reference
3. New requirements emerged? → Add to Active
4. Decisions to log? → Add to Key Decisions
5. "What This Is" still accurate? → Update if drifted

**After each milestone** (via `/gsd:complete-milestone`):
1. Full review of all sections
2. Core Value check — still the right priority?
3. Audit Out of Scope — reasons still valid?
4. Update Context with current state

---
*Last updated: 2026-03-30 — Milestone v2.0 started (abandoned v1.0 relay architecture, pivoting to MinGW full client with loopback server)*
