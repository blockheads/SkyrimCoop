# Project Research Summary

**Project:** SkyrimCoop v2.0 — MinGW full client + embedded host-authoritative server
**Domain:** Cooperative multiplayer game mod (SKSE DLL, host-P2P, Linux-native toolchain)
**Researched:** 2026-03-30
**Confidence:** HIGH (direct codebase analysis across all four files)

## Executive Summary

SkyrimCoop v2.0 converts the existing STR codebase into a single MinGW-compiled SKSE DLL that embeds a `GameServer` in-process. The host player's `HostService` owns a `GameServer` instance, ticks it each frame from the Skyrim game thread, and routes host-side messages through an in-process `LoopbackTransport` (serialize to buffer, call `GameServer::InjectMessage()`) rather than a localhost enet6 socket. Remote peers connect to the host's enet6 port normally. All four research files converge on the same core recommendation: **loopback bridge first, validate two-player sync, then refactor to direct authority.** Attempting the direct-authority World merge before two players synchronize successfully is the primary scope-creep risk.

The hard technical work is the character spawn pipeline — `TESNPC::Create` from `AppearanceBuffer`, AI suppression via `SetRemote`, and `WorldStateSync` reconciliation. Everything downstream of that (actor values, combat, inventory, quest sync) depends on the spawn pipeline being correct. Three safety issues must be resolved before the first in-Skyrim integration test: (1) the `-flayout-compat=ms` flag status (disputed between researchers — see Gaps), (2) removal of the `try/catch` block in `HostService::StartHosting`, and (3) confirmation that enet6 packet callbacks fire on the calling thread and not a background thread.

The stack is stable — all required packages are already in the XMake dependency graph and build on MinGW. No new libraries are needed. CEF, DirectXTK, Discord SDK, Steam SDK, and sentry-native are correctly excluded from the MinGW target. The project's v1.0 phases resolved basic MSVC extension compatibility, MinHook, Address Library, and enet6 transport. The remaining work is connecting already-proven pieces into a working two-player session.

---

## Key Findings

### Recommended Stack

The stack is fully specified and proven from Phases 1-11. XMake with `-p mingw` builds all required targets. The only v2.0-specific concern is that `sol2`, `lua`, `sqlite3`, and `nlohmann_json` are declared in `Code/server/xmake.lua` without `is_plat` guards — they must resolve correctly for the `mingw` platform at configure time before compilation begins. Run `xmake f -p mingw` and watch for package fetch failures before starting compilation.

**Core technologies:**
- **xPack MinGW-w64 GCC 14.3.0** — cross-compiles Windows DLL from Linux; all Tier 1-3 libs proven
- **XMake 2.8.5+** — build orchestration; `add_deps("SkyrimTogetherServer")` already in client xmake.lua, no build changes required
- **enet6** — UDP transport for remote peers; proven on both Linux server and MinGW client
- **EnTT v3.10.0** — dual `entt::registry` instances (client World + server World); must never share entity handles across registry boundaries
- **sol2 v3.3.0 + Lua 5.1** — server-side scripting; header-only, builds trivially on MinGW via xmake package
- **MinGWCompat.h + `-mms-bitfields`** — MSVC ABI compatibility layer; calling conventions confirmed working

Three `__declspec(align(N))` uses must be replaced with `alignas(N)` in game headers (STACK.md Gap 1). Three `__declspec(noinline)` uses should use `__attribute__((noinline))` guards instead (STACK.md Gap 2, low priority — warnings only, not failures). The stale `-flayout-compat=ms` TODO in `TiltedOnlinePCH.h` is disputed between research files — see Gaps.

### Expected Features

The MVP for a valid two-player session proof requires exactly six things: direct IP connect, remote player spawns at correct position with AI suppressed, movement syncs in real-time, correct appearance (race/face/equipment), disconnect cleanup, and 1-second `WorldStateSync` reconciliation. Nothing else is required to validate the architecture.

**Must have (table stakes for a working session):**
- Remote player spawns with correct appearance — `TESNPC::Create` from `AppearanceBuffer` (Phase 16-17 in existing roadmap)
- Movement sync with interpolation — `ServerReferencesMoveRequest` + `InterpolationSystem` (works under loopback)
- AI suppression on remote actor — `ActorExtension::SetRemote(true)` with `WaitingFor3D` guard (Phase 16)
- Disconnect cleanup — `NotifyRemoveCharacter` + reconciliation (Phase 16)
- Direct IP connect — `HostService` starts server; client connects to host IP (already works)
- WorldStateSync reconciliation — `BroadcastWorldStateSync` 1-second heartbeat (Phase 16)

**Should have (post-validation additions):**
- Actor values (health/magicka/stamina) — `ActorValueService` wired (Phase 18)
- Equipment visible during play — `InventoryService` spawn data (Phase 18)
- Death and respawn sync — `ActorValueService` death state (Phase 18)
- Combat sync (projectiles, hit events) — `CombatService` wired (Phase 18)
- Weather and time follows host — `CalendarService` + `WeatherService` (Phase 18, works as-is)

**Defer (v2+):**
- Quest sync — `QuestService` is commented out ("currently not in use"), needs full resurrection with host-authoritative semantics
- Direct authority refactor (remove loopback) — after two-player session validates; fixes `StringCacheService` triple-update bug
- Steam invites / lobby — direct IP is sufficient for MVP
- ImGui connect overlay — Phase 6 in existing roadmap
- Container/loot sync (`ObjectService`) — table stakes gap not yet scheduled; will be requested before first real playtest
- Host migration — impossible for host-as-simulation model; document and reject explicitly

**Hard anti-features to refuse:**
- Direct authority as an MVP requirement — loopback is correct for the first session; the refactor is post-validation
- More than 4 players — Skyrim's engine degrades above 4; hard cap
- Dedicated server support — contradicts the P2P model; STR already does this
- PvP combat — explicitly abandoned in STR; adds anti-cheat surface with no co-op value

### Architecture Approach

The architecture is an in-process loopback bridge. `HostService` owns a `GameServer` via `unique_ptr`, ticks it each frame via `HostService::OnUpdate()` on the Skyrim game thread, and connects `LoopbackTransport` to `GameServer::InjectMessage()` for host-to-server message delivery. `TransportService` gains a `m_pLoopback` pointer and a `SetHostMode()` method; when non-null, `Send()` routes through the loopback. Remote peers use the existing enet6 path unchanged. Two separate `entt::registry` instances coexist — they must never share entity handles.

**Major components:**
1. **`LoopbackTransport`** — in-process message bridge; port from `Code/native_client/transport/` to `Code/client/transport/`; remove `TcpClient` dependency; keep `InjectMessage` and callback pattern unchanged
2. **`ServerMessageRouter`** — deserialize raw server bytes to typed `ServerMessage` objects; port unchanged from `Code/native_client/transport/`
3. **`TransportService` (dual-mode)** — add `m_pLoopback` + `SetHostMode()`; all callers unchanged; mode-routing is transport-layer-only (no per-service host-mode guards)
4. **`HostService::StartHosting()`** — construct `ConsoleRegistry` + `GameServer`, create `LoopbackTransport`, register loopback route callback, call `SetHostMode()`, send `AuthenticationRequest`
5. **`GameServer::SendToPlayers(msg, excludeHostPlayer)`** — exclusion parameter already exists; host must be excluded from every broadcast to prevent self-echo corruption of `InterpolationComponent` targets

Build system changes: none required. `add_deps("SkyrimTogetherServer")` and `add_includedirs("../server")` are already in `Code/client/xmake.lua`.

### Critical Pitfalls

1. **`-flayout-compat=ms` status is disputed and must be resolved empirically** — STACK.md (Gap 3) says this flag does not exist in GCC 14.3 and the TODO should be deleted. PITFALLS.md (Pitfall 1) says the flag is available since GCC 12.1 and must be added or struct layout is silently wrong. Run: `x86_64-w64-mingw32-g++ -flayout-compat=ms -x c++ /dev/null -fsyntax-only` to resolve before Phase 1 is declared complete. If accepted: add to xmake.lua and re-enable `SKYRIM_STRUCT_ASSERT`. If rejected: STACK.md is correct, existing mitigations (`-mms-bitfields`, Address Library offsets) are the only available approach.

2. **`try/catch` in `noexcept HostService::StartHosting()` is undefined behavior** — The existing body contains a `try/catch(const std::exception&)` block (HostService.cpp lines 33-68). Exception unwinding across MinGW-compiled code into MSVC stack frames (Skyrim, SKSE) is UB. Replace with explicit error-code returns (`if (!pServer->IsListening()) return false;`) before the first in-Skyrim run. Fix is approximately 20 lines.

3. **enet6 threading model must be confirmed before concurrent testing** — If `TiltedPhoques::Client`/`Server` fire `OnConsume()` callbacks on a background network thread rather than inside `enet_host_service()` on the calling thread, there is a data race on the `entt::registry` (which is not thread-safe). Add thread-ID logging in both `OnConsume` and `OnUpdate` at the start of two-player testing. If callbacks fire on a background thread, all registry access needs a mutex or a game-thread command queue.

4. **Two `entt::registry` instances — no cross-registry entity access** — Client `World` and `Server::World` both extend `entt::registry`. An entity handle from one registry silently accesses wrong data in the other. Never use `using namespace Server;` in files that also use the client `World`. Enable `ENTT_ASSERT=assert` in debug builds. Never call `GameServer::Get()` from client code — use `HostService::GetServer()` exclusively.

5. **Host's own broadcast echo causes interpolation jitter** — `GameServer::SendToPlayers()` must exclude the host player connection from every broadcast. The exclusion parameter already exists. If omitted, the host's `CharacterService` receives its own movement as a `RemoteComponent` update and teleports back to where it was one tick ago each frame.

---

## Implications for Roadmap

Based on combined research, the phase structure below reflects the hard dependencies: ABI safety before in-Skyrim code runs, loopback transport before service wiring, spawn pipeline before any sync, and actor state before combat.

### Phase 1: ABI Safety and HostService Correctness

**Rationale:** Pitfalls 1-3 are all silent corruption or UB that produces confusing gameplay bugs rather than clean crashes. They must be resolved before any code runs in Skyrim's process, or integration test results cannot be trusted.

**Delivers:** Clean MinGW build with verified struct layout semantics, no exception UB in `HostService`, stale TODO removed from `TiltedOnlinePCH.h`, `__declspec(align)` replaced with `alignas`.

**Addresses:** STACK.md Gaps 1-3, PITFALLS.md Pitfalls 1 and 3.

**Avoids:** Days of debugging wrong struct offsets or exception-induced stack corruption during integration testing.

### Phase 2: Port LoopbackTransport and ServerMessageRouter

**Rationale:** These are the only missing files in the transport layer. Both exist in `Code/native_client/transport/` and are proven working. The port is mechanical (remove `TcpClient` dependency), is unit-testable via the existing replay harness, and has no external dependencies. No other work can proceed until these files exist in `Code/client/transport/`.

**Delivers:** `Code/client/transport/LoopbackTransport.h/.cpp` and `ServerMessageRouter.h/.cpp` — in-process message bridge, standalone and testable.

**Addresses:** ARCHITECTURE.md "Port LoopbackTransport and ServerMessageRouter" integration point.

**Avoids:** Anti-Pattern 4 (localhost enet6 connection for host), Anti-Pattern 1 (premature World merge).

### Phase 3: Dual-Mode TransportService + HostService::StartHosting

**Rationale:** `TransportService::SetHostMode()` and `HostService::StartHosting()` are the integration glue. Depends on Phase 2. Threading model must be confirmed here via thread-ID logging before any concurrent test. Timing instrumentation on `m_pGameServer->Update()` must be added at this phase to detect frame budget violations early.

**Delivers:** Host can start server, authenticate with its own embedded `GameServer` via loopback, and receive `AuthenticationResponse`. Replay test: `auth_flow.ndjson` against loopback path validates end-to-end message delivery.

**Addresses:** FEATURES.md "Host embeds server in-process" and "Direct IP connect," ARCHITECTURE.md Pattern 2 (dual-mode TransportService), ARCHITECTURE.md host startup sequence.

**Avoids:** PITFALLS.md Pitfall 4 (enet6 threading — confirm before concurrent test), PITFALLS.md Pitfall 2 (add `Update()` timing instrumentation at this phase).

### Phase 4: Character Spawn Pipeline (TESNPC::Create + AI Suppression)

**Rationale:** This is the hard phase and the dependency root for everything else. `TESNPC::Create` from `AppearanceBuffer`, `WaitingFor3D` guard for AI suppression, `NotifyRemoveCharacter` cleanup, and `WorldStateSync` reconciliation are all required here. This is Phases 15-17 in the existing roadmap — they map to a single logical goal: two players see each other.

**Delivers:** Two-player session proof — remote player spawns, moves, has correct appearance, AI stays suppressed, and disconnects cleanly.

**Addresses:** FEATURES.md MVP definition (all six items), FEATURES.md table stakes P1 features (all four), ARCHITECTURE.md character spawn and position update data flow pipelines.

**Avoids:** PITFALLS.md "Looks Done But Isn't" checklist items (host's own position echo, server shutdown on DLL unload, client registry cleared on disconnect).

**Research flag:** This phase requires a research-phase during planning. `TESNPC::Create` from `AppearanceBuffer` is rated HIGH complexity with no prior validation in the current codebase. Needs research on: (1) `IFormFactory` resolution pattern, (2) `WaitingFor3D` callback sequence for deferred state application, (3) completeness of appearance serialization in `AssignCharacterRequest`.

### Phase 5: Actor Values, Equipment, and Combat

**Rationale:** After two players can see each other and move, the session needs to feel like a game. Actor values (health/magicka/stamina), equipment visible on spawn, death/respawn, and combat sync all depend on Phase 4 producing a working spawn. Each service (`ActorValueService`, `InventoryService`, `CombatService`) works under loopback without modification — this phase is primarily wiring, not new engineering.

**Delivers:** Full two-player combat session. Host adjudicates damage. Equipment visible. Death and respawn sync. Weather and time follow host. Matches Phase 18 scope in existing roadmap.

**Addresses:** FEATURES.md "Should have" features (all five), FEATURES.md "Combat feels right" dependency chain.

**Avoids:** PITFALLS.md Performance Trap (world state sync CPU spike — verify differential sync is active, not serializing all entities every second).

### Phase 6: Direct Authority Refactor (Post-Validation)

**Rationale:** Only after Phase 4 proves two-player sync is working. Direct authority removes the loopback serialization round-trip and fixes the documented `StringCacheService` triple-update bug. It is explicitly an optimization, not an MVP requirement. Attempting it before sync is proven is the primary scope-creep risk identified across multiple research files.

**Delivers:** Host messages bypass serialization entirely; `StringCacheService` triple-update bug resolved; approximately 50% reduction in host-side per-message overhead.

**Addresses:** FEATURES.md `StringCacheService` direct-authority requirement, `WHY_DIRECT_AUTHORITY_IS_REQUIRED.md` rationale.

**Avoids:** Anti-Pattern 3 (per-service host-mode guards — all routing in `TransportService` only).

**Research flag:** This phase needs targeted research on all services that require `IsHost()` routing changes and the full `StringCacheService` refactor path before changes begin.

### Phase Ordering Rationale

- **ABI safety before any in-Skyrim code** — struct layout corruption produces non-obvious gameplay bugs that are expensive to diagnose. Fix it once before integration begins.
- **Loopback before services** — no service needs to change for loopback; the transport layer is the only integration point. Services are proven unchanged under loopback.
- **Spawn pipeline before combat** — you cannot sync health on an actor that does not exist. `TESNPC::Create` is the dependency root for all downstream sync.
- **Loopback before direct authority** — validate that message protocol and service logic are correct before removing the serialization round-trip that makes both sides visible in logs.

### Research Flags

Phases needing deeper research during planning:
- **Phase 4 (Character Spawn Pipeline):** `TESNPC::Create` from `AppearanceBuffer` is HIGH complexity (FEATURES.md) with no prior codebase validation. Needs research phase for `IFormFactory` resolution, `WaitingFor3D` callback sequence, and appearance serialization completeness.
- **Phase 6 (Direct Authority Refactor):** `StringCacheService` refactor and `IsHost()` routing changes span multiple services. Needs targeted research on cross-service dependencies before changes begin.

Phases with standard patterns (skip research-phase):
- **Phase 2 (Port LoopbackTransport):** Source exists in `Code/native_client/transport/`, pattern is mechanical, replay harness validates it.
- **Phase 3 (Dual-mode TransportService):** Pattern documented in ARCHITECTURE.md with code samples. Well-understood null-pointer-check routing.
- **Phase 5 (Actor Values/Combat):** All services work under loopback without modification. Standard message-handler wiring.

---

## Confidence Assessment

| Area | Confidence | Notes |
|------|------------|-------|
| Stack | HIGH | Direct XMake + compiler validation. One gap: `-flayout-compat=ms` existence is disputed between STACK.md and PITFALLS.md — requires empirical test before Phase 1 complete. |
| Features | HIGH | Direct codebase analysis. Service-by-service loopback compatibility table is authoritative. QuestService "currently not in use" confirmed from source. |
| Architecture | HIGH | `HostService`, `GameServer`, `LoopbackTransport` all exist and APIs confirmed from source. Build dependency graph already complete in XMake. |
| Pitfalls | HIGH (ABI, exceptions, cross-registry) / MEDIUM (threading) | ABI, exception, and cross-registry pitfalls confirmed from code. enet6 threading model is MEDIUM — standard enet behavior but not verified with thread-ID logging in this codebase. |

**Overall confidence:** HIGH

### Gaps to Address

- **`-flayout-compat=ms` flag existence (CRITICAL — resolve before Phase 1 complete):** STACK.md and PITFALLS.md directly contradict each other. STACK.md says the flag does not exist in GCC 14.3 and the TODO should be deleted. PITFALLS.md says the flag is available since GCC 12.1 and must be added immediately or struct layout is silently wrong. Do not pick either conclusion without the empirical test: `x86_64-w64-mingw32-g++ -flayout-compat=ms -x c++ /dev/null -fsyntax-only`. If the command exits 0, PITFALLS.md is correct. If it errors with "unrecognized command line option", STACK.md is correct.

- **enet6 threading model (MEDIUM — verify before Phase 3 concurrent test):** Whether `OnConsume()` fires on the game thread (inside `enet_host_service()`) or a background thread determines whether `entt::registry` access is safe without locks. Add thread-ID logging in `OnConsume` and `OnUpdate` as part of Phase 3 instrumentation. Not blocking Phase 2 (transport port).

- **`ObjectService` container/loot sync not scheduled:** FEATURES.md flags this as a table-stakes gap — players expect to share loot from the same chest. `ObjectService` exists on both client and server but is absent from all milestone requirements. It will be requested before the first real playtest. Flag for prioritization after Phase 5.

---

## Sources

### Primary (HIGH confidence)
- `Code/client/Services/HostService.h/.cpp` — embed lifecycle, `try/catch` block confirmed, `OnUpdate` server tick
- `Code/server/GameServer.h/.cpp` — `InjectMessage()`, `SetLoopbackRoute()`, `SendToPlayers()`, `OnUpdate()` implementations
- `Code/native_client/transport/LoopbackTransport.h` — proven loopback pattern, `kHostConnectionId` constant
- `Code/native_client/transport/ServerMessageRouter.h/.cpp` — proven message routing
- `Code/client/xmake.lua` — `add_deps("SkyrimTogetherServer")` confirmed present
- `Code/client/TiltedOnlinePCH.h` — `SKYRIM_STRUCT_ASSERT` no-op on MinGW, stale `-flayout-compat` TODO
- `Code/client/Services/QuestService.h` line 14 — "This service is currently not in use"
- `Code/client/Games/Overrides.cpp` — 14+ `thread_local` refcount guards
- `.planning/PROJECT.md` — canonical milestone scope
- `.planning/NETWORK-ARCHITECTURE.md` — authority model, two-layer sync
- `P2P_ARCHITECTURE.md` — loopback vs merged-World trade-off rationale
- `WHY_DIRECT_AUTHORITY_IS_REQUIRED.md` — `StringCacheService` triple-update bug

### Secondary (MEDIUM confidence)
- GCC 14.3 documentation — `-flayout-compat` flag status (disputed; needs empirical test with actual toolchain)
- enet6 event model — callbacks fire inside `enet_host_service()` on calling thread (standard enet behavior; not confirmed with thread-ID logging in this specific codebase)
- MinGW Thread-Local Storage — xPack 14.x defaults to native Windows TLS, not `__emutls`

### Tertiary (LOW confidence)
- DRG host migration quote — "too complex to transfer" (community attribution, not official dev statement; supports anti-feature rejection of host migration)

---
*Research completed: 2026-03-30*
*Ready for roadmap: yes*
