# SkyrimCoop

## What This Is

A peer-to-peer cooperative multiplayer mod for Skyrim Special Edition, forked from Tilted Online (Skyrim Together Reborn). Converts the original MMO-style client-server architecture to a host-based co-op model (2-4 players, drop-in via Steam invite). The entire toolchain builds and debugs natively on Linux, cross-compiling the Windows client DLL via MinGW following the LethalInjection pattern.

## Core Value

Two friends can drop into Skyrim together on Linux with zero server setup — host clicks "host," friend joins via Steam invite, and it just works.

## Requirements

### Validated

- ✓ EnTT-based ECS entity management — existing
- ✓ Message-based networking with differential serialization (58 client / 61 server opcodes) — existing
- ✓ Character sync (position, movement, animation) — existing
- ✓ Inventory and equipment synchronization — existing
- ✓ Combat synchronization — existing
- ✓ Quest sync (optional, host-authoritative) — existing
- ✓ Angular/CEF overlay UI — existing
- ✓ Lua server-side scripting via Sol2 — existing
- ✓ SKSE plugin integration — existing
- ✓ Server builds on Linux — existing
- ✓ Unit tests for message encoding/serialization — existing

### Active

- [~] Full Linux build toolchain (MinGW cross-compile for client DLL, native server) — Tier 1-2 libraries compile under MinGW and natively on Linux (Phase 2); client DLL pending (Phase 3)
- [ ] Linux-native debug workflow (attach to Wine/Proton process, inspect DLL state)
- [ ] MMO-to-co-op architecture conversion (embedded server in host, simplified ownership)
- [ ] Drop-in 2-4 player sessions via Steam/EOS friend invite
- [ ] Host-authoritative world state (NPCs, quests, time, weather)
- [ ] Mod-list synchronization (host's mod list enforced, clients must match)
- [ ] Automated E2E testing framework (headless or simulated host+client sessions)
- [ ] Comprehensive unit and integration test suite for all sync services
- [ ] CI pipeline running all tests on Linux

### Out of Scope

- Native Linux SKSE port — SKSE stays Windows, runs under Proton
- Dedicated server hosting / server browser — co-op only, no MMO infrastructure
- More than 4 players — keep it intimate co-op, not MMO scale
- Mobile or console clients — PC only
- Custom game engine modifications — mod only, don't touch Skyrim's engine
- Real-time voice chat — use Discord/Steam voice

## Context

**Forked from:** Tilted Online / Skyrim Together Reborn (GPLv3)
- Original architecture: dedicated server + multiple clients (MMO-style)
- Target architecture: host player runs embedded server + clients connect (co-op)

**LethalInjection reference project** (`/media/bighass/4d2c4781-34de-41a5-8939-e35551bc9a5d5/Projects/LethalInjection`):
- Proven pattern for Linux-native tooling + MinGW cross-compiled Windows DLL
- Three-layer architecture: Linux frontend ↔ Linux middleware ↔ injected Windows DLL
- CMake + MinGW toolchain, debug via Wine/Proton process attachment
- This project should follow the same cross-compile and debug patterns

**Existing codebase state:**
- ~12k LOC across client (6.2k) and server (5.3k) in C++20
- XMake build system with some CMake legacy support
- Windows client builds with MSVC, server builds on both platforms
- Basic Catch2 test suite for encoding/serialization only
- Angular 16 + CEF overlay UI with TypeScript
- Significant technical debt from MMO assumptions throughout codebase

**Key architectural change for co-op:**
- Actor ownership: host always owns NPCs/world state (simplifies from distributed ownership)
- Cell management: host's loaded cells are authoritative (removes handoff logic)
- Quest progression: syncs to host's state (removes voting/conflict resolution)
- Combat resolution: host adjudicates (removes lag compensation complexity)

## Constraints

- **Build system**: Must cross-compile Windows DLL from Linux via MinGW (LethalInjection pattern)
- **SKSE dependency**: Client DLL loads into Skyrim via SKSE — cannot modify SKSE itself
- **Wine/Proton**: Client always runs under Wine/Proton on Linux, debug tooling must work with this
- **License**: GPLv3 — all modifications must remain open source
- **Mod compatibility**: Host's mod list is authoritative; clients must match to connect
- **Platform**: Linux-first development, Windows compatibility maintained for end users

## Key Decisions

| Decision | Rationale | Outcome |
|----------|-----------|---------|
| LethalInjection-style cross-compile | Proven pattern for Linux dev + Windows DLL, same developer has reference impl | — Pending |
| MinGW over MSVC for client DLL | Required for Linux cross-compilation, LethalInjection validates this works | — Pending |
| Host-embedded server (not dedicated) | Simplifies ownership, removes infrastructure burden, fits co-op model | — Pending |
| Milestone 1 = builds + debugs on Linux | Foundational — everything else depends on having a working Linux toolchain | — Pending |
| Mod-list sync (not graceful mismatch) | Simpler to enforce matching than handle mismatches, prevents desync bugs | — Pending |

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
*Last updated: 2026-03-27 after Phase 2 completion — Tier 1-2 libraries compile under MinGW and native Linux, all Catch2 tests passing*
