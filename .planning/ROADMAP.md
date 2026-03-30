# Roadmap: SkyrimCoop

## Milestones

- Abandoned **v1.0 Linux Toolchain & Relay Architecture** - Phases 1-18 (abandoned 2026-03-30; proved MinGW feasibility, relay arch abandoned)
- Working **v2.0 MinGW Full Client with Loopback Server** - Phases 1-5 + 2.5 (in progress)

## Overview

Milestone v2.0 takes the existing STR codebase from the abandoned relay architecture to a single MinGW-compiled SKSE DLL with an embedded GameServer in-process. The critical path runs from ABI safety fixes and source cleanup, through full client compilation under MinGW, to loopback transport integration and finally a verified two-player co-op session via direct IP. All v1.0 learnings (MinHook, Address Library, enet6, replay infrastructure) carry forward — the work is connecting proven pieces into a working session.

## Phases

**Phase Numbering:**
- Integer phases (1, 2, 3): Planned milestone work
- Decimal phases (2.1, 2.2): Urgent insertions (marked with INSERTED)

Decimal phases appear between their surrounding integers in numeric order.

- [ ] **Phase 1: ABI Safety and Source Cleanup** - Fix silent ABI hazards and delete v1.0 relay code before any compilation begins
- [ ] **Phase 2: Full Client Compilation** - Compile all ~6200 LOC of Code/client/ under MinGW as a single SKSE DLL
- [ ] **Phase 2.5: SKSE Abstraction Layer** (INSERTED) - Abstract SKSE interaction behind testable ISkyrimBridge interface for headless replay
- [ ] **Phase 3: Loopback Transport and Host Mode** - Port LoopbackTransport, wire dual-mode TransportService, implement HostService::StartHosting()
- [ ] **Phase 4: Sync Verification via Loopback** - Validate character, inventory, and disconnect/reconnect sync through the loopback bridge
- [ ] **Phase 5: Two-Player Session and Workflow** - Two live Skyrim instances see each other via direct IP; full Linux build-deploy-debug workflow confirmed

## Phase Details

### Phase 1: ABI Safety and Source Cleanup
**Goal**: All known ABI hazards are resolved and v1.0 relay code is gone before any compilation attempt
**Depends on**: Nothing (first phase)
**Requirements**: BUILD-02, BUILD-03
**Success Criteria** (what must be TRUE):
  1. `x86_64-w64-mingw32-g++ -flayout-compat=ms -x c++ /dev/null -fsyntax-only` result is recorded — flag either added to xmake.lua (and SKYRIM_STRUCT_ASSERT re-enabled) or the stale TODO in TiltedOnlinePCH.h is deleted and the finding documented
  2. All `__declspec(align(N))` uses in game headers are replaced with `alignas(N)`
  3. The `try/catch` block in `HostService::StartHosting()` is replaced with explicit error-code returns (`if (!pServer->IsListening()) return false;`)
  4. Code/relay_dll/ and Code/native_client/ directories are deleted and removed from the XMake build system
  5. `xmake f -p mingw` completes without errors after the deletions
**Plans**: TBD

### Phase 2: Full Client Compilation
**Goal**: The complete Code/client/ codebase compiles under MinGW as a single SKSE DLL with CEF, DirectXTK, and Discord excluded
**Depends on**: Phase 1
**Requirements**: BUILD-01
**Success Criteria** (what must be TRUE):
  1. `xmake -y` produces SkyrimTogetherClient.dll from Code/client/ with zero errors (warnings acceptable)
  2. The DLL loads into Skyrim SE under Proton without crashing at startup (SKSE log confirms plugin loaded)
  3. No MSVC-isms remain in Code/client/ that prevent compilation — all `__declspec` non-align uses, MSVC-specific pragmas, and Windows-only headers are either guarded or replaced
**Plans**: TBD

### Phase 2.5: SKSE Abstraction Layer (INSERTED)
**Goal**: All client services interact with Skyrim through an ISkyrimBridge interface that can be swapped between real SKSE (in-game) and a ReplayBridge (headless testing)
**Depends on**: Phase 2
**Requirements**: BUILD-04
**Success Criteria** (what must be TRUE):
  1. ISkyrimBridge interface defined with methods covering all SKSE interactions used by client services (actor access, form lookups, event hooks, game state queries)
  2. SkyrimBridge implementation calls real SKSE APIs and game memory — existing service behavior unchanged
  3. ReplayBridge implementation replays recorded NDJSON events and returns canned data for headless execution
  4. Services reference ISkyrimBridge (not SKSE directly) — dependency injection via World context or constructor
  5. A headless test binary can instantiate ReplayBridge, feed recorded data, and exercise all services without Skyrim running
**Plans**: TBD

### Phase 3: Loopback Transport and Host Mode
**Goal**: Host player can start an embedded GameServer in-process, authenticate through loopback, and remote peers connect via enet6
**Depends on**: Phase 2
**Requirements**: HOST-01, HOST-02, HOST-03
**Success Criteria** (what must be TRUE):
  1. `Code/client/transport/LoopbackTransport.h/.cpp` and `ServerMessageRouter.h/.cpp` exist, ported from Code/native_client/transport/ with TcpClient dependency removed
  2. `TransportService::Send()` routes through LoopbackTransport when in host mode and through enet6 when in client mode — callers require no changes
  3. `HostService::StartHosting()` constructs a GameServer, binds LoopbackTransport, and receives an AuthenticationResponse from the embedded server
  4. Thread-ID logging in `OnConsume()` and `OnUpdate()` confirms enet6 callbacks fire on the expected thread (game thread or background thread documented)
  5. `auth_flow.ndjson` replay test passes against the loopback path
**Plans**: TBD

### Phase 4: Sync Verification via Loopback
**Goal**: Existing STR character sync, inventory sync, and disconnect/reconnect behavior all work correctly through the loopback bridge
**Depends on**: Phase 3
**Requirements**: VERIFY-02, VERIFY-03, VERIFY-04
**Success Criteria** (what must be TRUE):
  1. A remote actor spawns at the correct world position with AI suppressed (no wandering) when a fake second player connects through loopback
  2. Equipment and inventory visible on the remote actor match what the player character has equipped
  3. Movement updates from the connected player propagate through loopback to the host's CharacterService and update the remote actor's position in real time
  4. When the fake second player disconnects, the remote actor is removed from the host's world with no dangling entities in the registry
  5. Reconnect after disconnect produces a fresh state — no stale RemoteComponent data carried over from the previous session
**Plans**: TBD

### Phase 5: Two-Player Session and Workflow
**Goal**: Two real Skyrim instances on Linux connect via direct IP and both players are visible with working sync; the full Linux build-deploy-debug workflow is confirmed
**Depends on**: Phase 4
**Requirements**: VERIFY-01, VERIFY-05
**Success Criteria** (what must be TRUE):
  1. Host starts Skyrim, activates host mode — embedded GameServer begins accepting connections on a configurable port
  2. Second player starts Skyrim, enters host's IP:port — both players see each other's character in-game using existing STR sync (character, movement, animation)
  3. Full build-deploy cycle works from Linux: `xmake -y` produces the DLL, deploy to Proton prefix, launch Skyrim, mod loads — no Windows machine required
  4. `gdb` attaches to the Skyrim Wine process with DWARF symbols from the MinGW-built DLL — breakpoints and stack traces resolve to source lines
**Plans**: TBD

## Progress

**Execution Order:**
Phases execute in numeric order: 1 → 2 → 3 → 4 → 5

| Phase | Plans Complete | Status | Completed |
|-------|----------------|--------|-----------|
| 1. ABI Safety and Source Cleanup | 0/TBD | Not started | - |
| 2. Full Client Compilation | 0/TBD | Not started | - |
| 2.5. SKSE Abstraction Layer | 0/TBD | Not started | - |
| 3. Loopback Transport and Host Mode | 0/TBD | Not started | - |
| 4. Sync Verification via Loopback | 0/TBD | Not started | - |
| 5. Two-Player Session and Workflow | 0/TBD | Not started | - |
