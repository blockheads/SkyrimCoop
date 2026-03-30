# Domain Pitfalls

**Domain:** MinGW full client build with embedded host-authoritative server (v2.0 milestone)
**Researched:** 2026-03-30
**Confidence:** HIGH for ABI and threading issues (confirmed from codebase), MEDIUM for enet6 update semantics

> **Note:** v1.0 pitfalls (basic MSVC extension compatibility, MinHook feasibility smoke test,
> CEF/DirectXTK exclusion, XMake MinGW platform config) are resolved. See
> `.planning/archive/v1.0-phases/` for that history. This document covers v2.0-specific
> risks: full client compilation, embedded server in DLL, Skyrim struct layout at scale,
> threading, and host self-routing.

---

## Critical Pitfalls

### Pitfall 1: Itanium Tail-Padding Reuse Silently Corrupts Skyrim Struct Overlays

**What goes wrong:**

GCC (MinGW) uses the Itanium C++ ABI, which allows derived classes to reuse unused "tail padding" of base classes. MSVC never does this. Every Skyrim struct in `Games/Skyrim/` that has a base class with trailing unused bytes is laid out differently under MinGW than under MSVC. The code already disables `static_assert` layout checks on MinGW via `SKYRIM_STRUCT_ASSERT` in `TiltedOnlinePCH.h` line 27, with a TODO comment: "Upgrade to GCC 12+ and use `-flayout-compat=ms`." The toolchain is GCC 14.3 — the upgrade has happened. The TODO is stale. The flag must be added now.

Without `-flayout-compat=ms`, reading `Actor::flags`, `TESObjectREFR::loadedData`, `TESNPC::baseData`, or any struct field in the `Games/Skyrim/` hierarchy may access wrong memory offsets. This produces data corruption that looks like "Skyrim state weirdness" during testing, not a crash or assertion, making it extremely difficult to diagnose.

**Why it happens:**

The compat layer was added defensively in v1.0 to unblock compilation. The `-flayout-compat=ms` flag requires GCC 12+, which wasn't confirmed at the time. Now that GCC 14.3 is the toolchain, the flag is available but the TODO was never converted to a task.

**How to avoid:**

1. Add `-flayout-compat=ms` to `xmake.lua` for all MinGW targets:
   ```lua
   if is_plat("mingw") then
       add_cxflags("-flayout-compat=ms", {force = true})
   end
   ```
2. Change `SKYRIM_STRUCT_ASSERT` in `TiltedOnlinePCH.h` to:
   ```cpp
   #define SKYRIM_STRUCT_ASSERT(...) static_assert(__VA_ARGS__)
   ```
   (Remove the `#ifndef _MSC_VER` guard entirely — with `-flayout-compat=ms`, GCC lays out structs identically to MSVC, so the assertions are valid on both.)
3. Add the flag first, then re-enable assertions one header file at a time, fixing any that fail.

**Warning signs:**

- Actor health shows wrong value even though server sent the correct value.
- Position offsets are off by 4-8 bytes, causing actors to appear shifted in space.
- `static_assert(sizeof(Actor) == EXPECTED_SIZE)` passes in MSVC build but you don't know if MinGW agrees because the assert is suppressed.
- Any "Skyrim struct weirdness" where the game does something unexpected but no crash occurs.

**Phase to address:** Phase immediately before the first full client DLL integration test. Must be added before any `Games/Skyrim/` code runs inside Skyrim's process.

---

### Pitfall 2: Embedded Server `Update()` Blocks the Skyrim Game Thread

**What goes wrong:**

`HostService::OnUpdate()` calls `m_pGameServer->Update()` synchronously on the Skyrim game thread. `Update()` in `TiltedPhoques::Server` calls `OnUpdate()`, which: (1) computes delta time, (2) triggers `UpdateEvent` to all service handlers (CharacterService, PlayerService, etc.), and (3) calls `EnforceProximity()`. Each service handler iterates `entt::view`s and may do significant work (movement broadcasting, world state sync at 1-second intervals, faction updates). If any of this takes more than ~3ms, Skyrim's frame pacing hitches.

The deeper risk is enet6 polling. `TiltedPhoques::Server`'s `Update()` path calls enet6's `enet_host_service()` to process incoming packets. If a client sends a large burst (full state baseline on connect), `enet_host_service()` may process many packets synchronously and invoke message handlers, all on the game thread. This scales with player count and state size.

**Why it happens:**

The single-threaded listen server model (Borderlands/UE3 style) is the simplest correct approach. The danger is that Skyrim's game thread budget is ~16ms total and the embedded server's work is now part of that budget.

**How to avoid:**

1. Profile `m_pGameServer->Update()` call time in the first integration test. Instrument with `std::chrono`:
   ```cpp
   const auto t0 = std::chrono::high_resolution_clock::now();
   m_pGameServer->Update();
   const auto elapsed = std::chrono::high_resolution_clock::now() - t0;
   if (elapsed > std::chrono::milliseconds(3))
       spdlog::warn("[HostService] Server update took {}ms", elapsed.count() / 1'000'000);
   ```
2. Move heavy non-frame-critical work (world state sync broadcast) to a time-sliced budget. CharacterService's `BroadcastWorldStateSync()` sends a 1KB snapshot every second — it can be scheduled off-frame if it needs >1ms.
3. Do NOT offload `Update()` to a background thread without careful registry locking (see Pitfall 4). Start single-threaded, measure, then optimize.

**Warning signs:**

- Frame rate drops to 55-60fps from 120fps when a second player connects.
- Skyrim's in-game "show fps" counter shows periodic dips exactly 1 second apart (world state sync timing).
- `spdlog` output showing server update times >5ms.

**Phase to address:** Phase covering initial two-player integration test. Add timing instrumentation at the same time as the first full run.

---

### Pitfall 3: Exception Handling in `HostService::StartHosting` Is Undefined Behavior

**What goes wrong:**

`HostService::StartHosting()` has a `try/catch(const std::exception&)` block (line 33-68 of HostService.cpp). This is present despite the project's explicit "no exceptions" code guideline. In a MinGW DLL loaded into an MSVC-compiled process (Skyrim), C++ exception unwinding crosses a compiler ABI boundary. MinGW uses SEH-based exception unwinding on Windows (since GCC 4.8 with `--enable-sjlj-exceptions` replaced by SEH), but the exception tables and personality functions differ from MSVC's. A `std::exception` thrown by MinGW-compiled code caught in a MinGW `catch` block may work in isolated tests but is undefined behavior when the stack includes MSVC-compiled frames (SKSE, Skyrim itself).

More practically: if `GameServer` constructor throws (e.g., enet6 fails to bind the port), the stack may already contain MSVC frames from Skyrim's DLL loading path. Unwinding through those frames with MinGW's SEH tables is UB and can corrupt Skyrim's stack or leave resources unreleased.

**Why it happens:**

The `try/catch` was added as defensive coding during HostService development. It looks reasonable in isolation but violates both the project's "no exceptions" rule and the ABI constraint of MinGW-in-MSVC-process.

**How to avoid:**

Replace with explicit error-code returns:
```cpp
bool HostService::StartHosting(uint16_t aPort, uint8_t aMaxPlayers) noexcept
{
    if (m_isHosting)
    {
        spdlog::warn("[HostService] Already hosting");
        return true;
    }

    auto pConsole = std::make_unique<ServerConsole::ConsoleRegistry>("EmbeddedServer");
    auto pServer = std::make_unique<Server::GameServer>(*pConsole);

    if (!pServer->IsListening())
    {
        spdlog::error("[HostService] GameServer failed to bind port {}", aPort);
        return false;
    }

    pServer->Initialize();
    m_pConsole = std::move(pConsole);
    m_pGameServer = std::move(pServer);
    m_isHosting = true;
    return true;
}
```

Mark all `HostService` methods `noexcept` — which they already are in the header declaration. The body must match.

**Warning signs:**

- Crash inside `HostService::StartHosting` that produces a non-standard exception backtrace in GDB.
- Port binding failure (EADDRINUSE from a previous run) causes a hard crash instead of a clean error message.
- Any `catch(...)` or `catch(std::exception&)` in a `noexcept` function.

**Phase to address:** Before the first integration test run. This is a correctness fix, not an optimization.

---

### Pitfall 4: enet6 Message Handlers Touch `entt::registry` from the Wrong Thread

**What goes wrong:**

`TiltedPhoques::Client` (base of `TransportService`) and `TiltedPhoques::Server` (base of `GameServer`) both have internal network threads that receive packets. The pattern is: network thread receives bytes, decodes packet, calls `OnConsume()`, which invokes the registered message handler. If the server's enet6 receive path calls handlers on a background network thread while `OnUpdate()` (game thread) is iterating the same `entt::registry` via `entt::view`, there is a data race.

`entt::registry` is not thread-safe. Concurrent access — even read-only views — during structural modification (entity creation/deletion in a handler) causes undefined behavior: iterator invalidation, wrong component data, corrupted free lists.

The same risk applies symmetrically: the client's `TransportService::OnConsume()` runs on the TiltedPhoques network thread, but `CharacterService::OnUpdate()` iterates the client registry on the game thread.

**Why it happens:**

The original STR codebase was designed with TiltedPhoques' threading model in mind — but that model's documentation is sparse. Developers assume service handlers run on the same thread as `Update()`. This may or may not be true depending on whether TiltedPhoques serializes packet callbacks onto the update thread or fires them directly from the receive thread.

**How to avoid:**

1. Verify TiltedPhoques' threading model: does `OnConsume()` fire on the calling thread (inside `Update()` / `enet_host_service()`) or on a background thread?
   - Check: add `spdlog::info("OnConsume thread: {}", std::this_thread::get_id())` in both client and server OnConsume. Compare with game thread ID logged at `OnUpdate()`.
2. If callbacks fire inside `enet_host_service()` (most likely — this is standard enet behavior), they run on the game thread via the synchronous `Update()` call. No data race.
3. If callbacks fire on a background thread: wrap all `entt::registry` access with a mutex, or use a thread-safe command queue (post lambdas to process on the game thread):
   ```cpp
   // In OnConsume (network thread):
   m_gameThreadQueue.Push([this, msg = std::move(pMessage)]() {
       OnCharacterSpawn(*static_cast<CharacterSpawnRequest*>(msg.get()));
   });

   // In OnUpdate (game thread):
   m_gameThreadQueue.DrainAll();
   ```

**Warning signs:**

- Intermittent crashes inside `entt::view::begin()` or `entt::registry::emplace()` that only occur with two players connected.
- Entity component data appears to change mid-iteration of a view.
- `ASAN` reports "heap-use-after-free" in entt internals.
- Thread sanitizer (TSAN) reports data races on registry internals.

**Phase to address:** Before the second-player connection test. Must be verified before any concurrent server-client interaction.

---

### Pitfall 5: Two `entt::registry` Instances — Accidental Cross-Registry Entity Access

**What goes wrong:**

The embedded server (`Server::World`) has its own `entt::registry`. The client (`World`) has a separate `entt::registry`. Both live in the same process. An `entt::entity` from one registry is just a 32-bit integer — if accidentally passed to the other registry, it either silently accesses the wrong entity's components or hits an assertion (if ENTT_ASSERT is enabled). In a single-DLL embedded server setup, this is easy to do accidentally through:

- A service callback that captures `World&` from the client context but runs in the server's `UpdateEvent` handler.
- A message handler in the server that calls a utility function expecting `Server::World&` but receives `World&` via a reference capture.
- `GameServer::Get()` static accessor returning the server's world when client code accidentally calls it.

The server and client are both in scope in `HostService.cpp`. As more services are added around host mode, the chance of a cross-registry reference grows.

**Why it happens:**

Both `World` and `Server::World` extend `entt::registry` and have similar APIs. Without strong type differentiation, they look identical at the call site. The namespace `Server::` prefix is easy to drop accidentally in a translation unit that has `using namespace Server;`.

**How to avoid:**

1. Never use `using namespace Server;` in any file that also uses the client `World`. Always qualify: `Server::World`, `Server::GameServer`.
2. Add a static compile-time tag to prevent implicit conversion:
   ```cpp
   // In server/World.h
   namespace Server {
   struct World : entt::registry {
       struct ServerTag {}; // Marker type, never used at runtime
   };
   }
   ```
   Functions that take `Server::World&` cannot accept `World&` even if both extend `entt::registry`.
3. Prohibit `GameServer::Get()` from being called in client code — assert in `Get()` if the call happens from a thread that isn't the server's context.
4. Run with ENTT_ASSERT enabled in debug builds: `add_defines("ENTT_ASSERT=assert")`. entt's debug assertions will catch entity provenance violations in some cases.

**Warning signs:**

- NPC position updates applied to the wrong actor — host sees one actor move, client sees a different one move.
- `entt::registry::get<ComponentType>(entity)` returning garbage data.
- Entity IDs appear valid but component lookup always returns default-initialized data.
- HostService code review finds `World&` and `Server::World&` in the same function.

**Phase to address:** Phase covering embedded server integration. Before first host-mode service handler is written.

---

### Pitfall 6: `thread_local` Variables in MinGW DLL May Not Initialize in New Threads

**What goes wrong:**

The codebase has 14+ `thread_local` variables, including critical state guards:
- `ScopedReferencesOverride::s_refCount` (Overrides.cpp/h) — guards re-entrant reference operations
- `ScopedEquipOverride::s_refCount`, `ScopedUnequipOverride::s_refCount` — guards equipment operations
- `s_autoSucceedEffectCheck`, `s_applyHealPerkBonus` in MagicTarget.cpp — magic system guards
- `s_cancelAnimationWaitEvent`, `s_cancelAnimationEvent` in TESObjectREFR.cpp — animation guards
- `ScratchAllocator s_allocator` in TransportService.cpp — per-thread scratch memory

When a MinGW-compiled DLL is loaded into an MSVC process, `DllMain` with `DLL_THREAD_ATTACH` is called for each new thread. MSVC's TLS implementation uses TLS callbacks registered in the PE header. MinGW's `thread_local` may use `__emutls` (emulated TLS) rather than native Windows TLS, depending on the MinGW runtime version. With `__emutls`, `thread_local` variables may not be initialized when first accessed from a thread created by Skyrim (not by the MinGW runtime).

An uninitialized `s_refCount` starting at garbage (not 0) means `ScopedReferencesOverride` never releases the guard, causing all reference operations to silently no-op. An uninitialized `ScratchAllocator` on a background thread causes a null-pointer dereference.

**Why it happens:**

MinGW 14.3 with xPack toolchain uses native Windows TLS (not `__emutls`) for `thread_local`. This is correct behavior. However, the initialization order guarantee only applies to TLS slots initialized before the thread starts. For threads created by Skyrim before the DLL loads, TLS variables are initialized on first access via the TLS callback — but only if the PE's TLS directory is correctly populated by the MinGW linker.

**How to avoid:**

1. Verify the compiled DLL has a TLS directory: `x86_64-w64-mingw32-objdump -h SkyrimTogetherClient.dll | grep -i tls`
2. For the most critical guards (`ScopedReferencesOverride`), add an explicit check:
   ```cpp
   // In Overrides.h:
   static thread_local uint32_t s_refCount;
   static thread_local bool s_initialized;

   // In access code:
   if (!s_initialized) { s_refCount = 0; s_initialized = true; }
   ```
   (This is a workaround for the edge case, not the normal path.)
3. The `ScratchAllocator` in `TransportService` should be tested by triggering `OnConsume()` from multiple threads and verifying it doesn't crash.
4. Use `x86_64-w64-mingw32-nm SkyrimTogetherClient.dll | grep __emutls` — if this symbol appears in the output, the toolchain is using emulated TLS. Switch to a toolchain that uses native TLS.

**Warning signs:**

- `ScopedReferencesOverride` operations silently no-op in all cases (refCount never reaches 0).
- Crash in `ScratchAllocator` on a thread that Skyrim created (not the main thread).
- Equipment changes on remote actors never apply even though the message is received.
- TLS-related warnings from the linker: `warning: -enable-auto-import is not supported`.

**Phase to address:** Full client DLL integration test phase — requires testing on a thread created by Skyrim (not just the main thread from DllMain).

---

## Technical Debt Patterns

Shortcuts that seem reasonable but create long-term problems.

| Shortcut | Immediate Benefit | Long-term Cost | When Acceptable |
|----------|-------------------|----------------|-----------------|
| Keeping `SKYRIM_STRUCT_ASSERT` disabled on MinGW | Compilation unblocks fast | Silent wrong-offset Skyrim memory access; debugging takes days | Never — `-flayout-compat=ms` is available now |
| `try/catch` in `noexcept` HostService methods | Easy error handling | UB when exceptions cross MSVC→MinGW stack frames; crash in Skyrim's exception filter | Never in DLL code |
| Calling `m_pGameServer->Update()` without timing | Simple integration | Game thread budget unknown; hitches manifest as user-visible stutter | Acceptable for first smoke test only; add timing before any feature work |
| Passing `entt::entity` values in log messages across registry contexts | Easy debugging | Encourages treating cross-registry entities as interchangeable | Acceptable only in `spdlog::debug()` lines that never execute in release builds |
| Using `GameServer::Get()` static singleton from client code | Convenient access | Couples client services to server lifecycle; breaks when server is not running | Never — use `HostService::GetServer()` exclusively |
| Host connecting to itself via localhost UDP | Simple architecture | Unnecessary serialization/deserialization + enet6 round-trip for host's own messages | Acceptable for v2.0 MVP; optimize in v2.1 via direct call path |

---

## Integration Gotchas

Common mistakes when connecting the embedded server to the client plugin.

| Integration | Common Mistake | Correct Approach |
|-------------|----------------|------------------|
| HostService + GameServer init | Creating GameServer before spdlog logger "EmbeddedServer" exists | Create the logger first; HostService already does this but order must be preserved if other services create loggers in constructors |
| Server::World vs client World | Passing `World&` to a function expecting `Server::World&` — compiles if both use same `entt::registry` base | Always qualify `Server::World`. No `using namespace Server` in mixed files. |
| HostService shutdown | Calling `Reset()` on `m_pGameServer` immediately after `Kill()` | Wait for `IsListening()` to return false (HostService::OnUpdate already handles this correctly — don't shortcut it) |
| enet6 host + client same port | Host binds port 10578 for server; client attempts to connect to 127.0.0.1:10578 using same enet6 host | They are separate enet6 hosts. Server enet_host binds port. Client enet_host connects to that port. No conflict if they are separate enet_host instances. |
| Server message broadcasting on host | Server broadcasts `ServerReferencesMoveRequest` to all connections including localhost; host's client receives a move message for its own player and applies interpolation | Server must skip broadcast to the connection corresponding to the host player. Check `aPlayer->GetConnectionId() != m_hostConnectionId` before sending. |
| Static singleton `GameServer::Get()` in server code | Server code calls `GameServer::Get()` which may be nullptr during initialization if client-side code triggers a server event before server is fully started | Guard all `GameServer::Get()` call sites with null check. The static `s_pInstance` is set in the constructor — verify timing. |

---

## Performance Traps

Patterns that work in single-player testing but break under load.

| Trap | Symptoms | Prevention | When It Breaks |
|------|----------|------------|----------------|
| World state sync serializing all entities every 1 second | CPU spike every 1 second, frame time spike visible in overlay | Differential sync: only serialize entities with dirty flags; or cap at 50 entities/second serialized | At ~100 synced entities (50 actors x 50-byte state = 5KB/sec still ok, but CPU cost of iteration is not) |
| `entt::view` iteration inside game thread while enet6 processes packets | Depending on threading model, potential iterator invalidation | Verify threading model (see Pitfall 4); if safe, profile view iteration time | Always — verify before assuming safe |
| Host-to-self loopback: serialize → UDP → deserialize for host's own messages | ~0.5ms per position update for the host player's own position going through full enet stack | In host mode, detect when the destination is the local connection and call the handler directly: `OnReferencesMoveRequest(msg)` instead of `Send(msg)` | At 20 position updates/sec with serialization overhead — measurable but not critical for v2.0 |
| Both client and server spdlog loggers writing to stdout | spdlog contention on a single stdout sink when both EmbeddedServer and client loggers fire simultaneously | Give each logger its own sink, or use async spdlog with a single shared thread | At >100 log events/sec — logging should be async in production builds |
| `RipAllocateN` 1MB static pool shared between client hooks and server JIT | Pool exhaustion: `RipAllocateN` returns nullptr, JIT assembly generation silently fails | Track pool usage. In production, replace with `VirtualAlloc`-backed implementation that grows. 1MB is per-DLL, not per-hook. | At ~500 hooks (MinHook trampolines are 14 bytes each). Not a concern at current hook count, but never grows. |

---

## "Looks Done But Isn't" Checklist

Things that appear complete but are missing critical pieces.

- [ ] **`-flayout-compat=ms` flag added**: Compilation succeeds without it, but struct layout is wrong. Verify: add `static_assert(offsetof(Actor, flags) == EXPECTED_OFFSET)` for 5 different Skyrim structs and confirm they match IDA/SKSE header annotations.
- [ ] **Host's own position updates**: Host player moves, server receives the movement, broadcasts to all clients — including back to the host. Verify the host does NOT apply its own broadcast as a remote actor update (it would cause jitter as the host teleports to where it was 1 tick ago).
- [ ] **Server shutdown on DLL unload**: `HostService::StopHosting()` must be called during `DllMain(DLL_PROCESS_DETACH)`. If the DLL unloads with the server still running, enet6 sockets are leaked and the next DLL load will fail to bind the port. Verify: forcibly unload the DLL via SKSE console, reload, attempt to host. Should work without `WSAEADDRINUSE`.
- [ ] **Server constructor port binding failure**: If port 10578 is already in use (previous crash left socket open), `GameServer` constructor must fail gracefully. Verify: bind port 10578 with a test tool (`nc -lu 10578`), then try to start hosting. Should see an error log, not a crash.
- [ ] **Client registry cleared on disconnect**: When a client disconnects, the server sends `NotifyRemoveCharacter` for all entities. Client `CharacterService::OnRemoveCharacter` must process these before the registry is torn down. Verify: connect client, host has 3 synced actors, client disconnects. After reconnect, confirm 0 ghost actors in the registry.
- [ ] **Thread-local guards on non-main threads**: `ScopedReferencesOverride`, `ScopedEquipOverride` all rely on `thread_local` refcounts. Test by triggering equipment changes from a non-main thread (enet6 callback thread if threading model allows it) and confirm refcounts are 0 before and after.

---

## Recovery Strategies

When pitfalls occur despite prevention, how to recover.

| Pitfall | Recovery Cost | Recovery Steps |
|---------|---------------|----------------|
| Wrong struct layout (Pitfall 1 not addressed) | HIGH — days of debugging wrong offsets | Add `-flayout-compat=ms`, rebuild, rerun layout assertions, audit every struct field access that was passing wrong data |
| Game thread hitching from server update (Pitfall 2) | MEDIUM — requires threading model change | Profile first; if single-threaded budget exceeded, extract periodic sync work to a command queue that processes 1ms worth per frame |
| Exception UB crash (Pitfall 3) | LOW — fix is mechanical | Replace `try/catch` with error code returns throughout HostService; the fix is ~20 lines |
| entt data race (Pitfall 4) | HIGH — race conditions are non-deterministic | Add `std::mutex` around registry access; or switch to TiltedPhoques command queue pattern; reproduce with TSAN first to confirm the race |
| Cross-registry entity confusion (Pitfall 5) | MEDIUM — requires audit | grep for all `entt::entity` values passed between server and client code paths; add type wrappers if needed |
| TLS initialization failure (Pitfall 6) | MEDIUM — platform-specific | Switch to non-TLS alternatives for the affected variables (atomic refcounts, or explicit initialization on DLL attach) |

---

## Pitfall-to-Phase Mapping

How roadmap phases should address these pitfalls.

| Pitfall | Prevention Phase | Verification |
|---------|------------------|--------------|
| Itanium tail-padding (Pitfall 1) | Full client compilation phase — add `-flayout-compat=ms` before any `Games/Skyrim/` code runs | Re-enable `SKYRIM_STRUCT_ASSERT` and confirm all layout assertions pass in MinGW build |
| Game thread blocking (Pitfall 2) | Initial two-player integration test | Add timing instrumentation; confirm server `Update()` < 3ms under load |
| Exception UB (Pitfall 3) | Before first in-Skyrim test | Static analysis: grep for `try/catch` in `noexcept` methods; fix before running |
| Registry threading (Pitfall 4) | Before concurrent client+server test | Log thread IDs in `OnConsume` and `OnUpdate`; confirm they match |
| Cross-registry entity confusion (Pitfall 5) | Embedded server integration phase | Code review: no bare `entt::entity` values crossing server/client boundary without `ServerId` translation |
| TLS initialization (Pitfall 6) | Full client DLL integration test | Test on threads created by Skyrim (hook fire, SKSE event callbacks) not just DllMain thread |

---

## Sources

**Codebase direct analysis (HIGH confidence):**
- `Code/client/TiltedOnlinePCH.h` line 24-29: `SKYRIM_STRUCT_ASSERT` disabled on MinGW, TODO for `-flayout-compat=ms`
- `Code/client/Services/HostService.cpp` lines 33-68: `try/catch` in `noexcept` StartHosting
- `Code/client/Services/HostService.cpp` lines 90-123: synchronous `m_pGameServer->Update()` on game thread
- `Code/server/GameServer.cpp` lines 622-638: `OnUpdate()` implementation (DispatchEvent + EnforceProximity)
- `Code/client/Games/Overrides.cpp`: `thread_local` ScopedOverride refcounts (14 total across the client)
- `Code/client/Services/Generic/TransportService.cpp`: `static thread_local ScratchAllocator`

**GCC documentation (HIGH confidence):**
- [GCC `-flayout-compat` flag](https://gcc.gnu.org/onlinedocs/gcc/C_002b_002b-Dialect-Options.html) — available since GCC 12.1, controls Itanium ABI tail-padding behavior
- [MinGW Thread-Local Storage on Windows](https://sourceware.org/bugzilla/show_bug.cgi?id=13767) — documents native TLS vs `__emutls` in MinGW

**EnTT documentation (HIGH confidence):**
- [EnTT thread safety](https://github.com/skypjack/entt/wiki/Crash-Course:-entity-component-system#multithreading) — "Registry is not thread-safe. Its operations are safe only if called from the same thread."

**Practical experience (MEDIUM confidence):**
- enet6 event model: callbacks fire inside `enet_host_service()` on the calling thread — this is standard enet behavior across all versions. The return value is the number of events processed synchronously.
- MinGW DLL TLS in MSVC process: documented behavior divergence between `__emutls` (old MinGW) and native TLS (MinGW 14.x xPack). xPack toolchain defaults to native TLS.

**v1.0 archived research (resolved pitfalls):**
- `.planning/archive/v1.0-phases/02-msvc-compatibility-core-libraries/02-VERIFICATION.md` — MSVC extension compatibility confirmed working
- `.planning/archive/v1.0-phases/03-client-dll-cross-compilation/03-VERIFICATION.md` — MinHook and SKSE DLL loading confirmed under Proton
- `.planning/research/SUMMARY.md` (2026-03-27) — v1.0 research baseline

---
*Pitfalls research for: MinGW full client + embedded server (SkyrimCoop v2.0)*
*Researched: 2026-03-30*
