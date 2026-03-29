# Phase 12: NDJSON Replay Capture and Integration Tests - Research

**Researched:** 2026-03-29
**Domain:** NDJSON replay harness, Catch2 integration testing, service stubbing
**Confidence:** HIGH

## Summary

Phase 12 builds a replay harness that reads pre-recorded NDJSON files and feeds them through the existing `DispatchHookEvent()` dispatch loop, then creates Catch2 integration tests that assert on entt::registry state after replay. The core challenge is constructing the service graph (CharacterService depends on GameReader, TcpClient, and entt::registry) without live TCP or /proc/pid/mem connections.

All 8 services share the same constructor signature: `Service(GameReader&, TcpClient&)` except CharacterService which additionally takes `entt::registry&`. GameReader depends on ProcMemReader and PointerTable. The stubbing strategy must provide dummy implementations for ProcMemReader (no-op reads returning zero) and TcpClient (no-op sends, no socket), while PointerTable and entt::registry work as-is since they are pure data structures with no I/O.

The existing codebase already has the `--record` flag writing NDJSON lines (main.cpp line 674-678), `DispatchHookEvent()` as a standalone free function (line 140-192), and all services using a uniform `OnHookEvent(const nlohmann::json&)` interface. The replay harness is structurally simple: read lines from file, parse JSON, call DispatchHookEvent, call service.Update(1/60.0f) between events.

**Primary recommendation:** Create a `NullProcMemReader` subclass (or initialize ProcMemReader without calling Open()) and a `NullTcpClient` wrapper that satisfies the reference interface without I/O. Build `ReplayHarness` as a helper class that owns these stubs plus all 8 services. Create a separate `ReplayTests` xmake target linking native_client service .cpp files + Catch2.

<user_constraints>
## User Constraints (from CONTEXT.md)

### Locked Decisions
- D-01: Add `--replay <file>` flag to the existing `skyrim-coop` binary. Reuses `DispatchHookEvent()` and all services but skips TCP/GameReader initialization. Same binary, no new build target for the CLI tool.
- D-02: Fast-forward by default -- replay feeds events as fast as the dispatch loop can consume them, no artificial delays. `--real-time` flag available for manual debugging at recorded speed.
- D-03: Synthetic fixed deltaTime (1/60s = 16.6ms) for all `service.Update()` calls between dispatched events. Deterministic: same file always produces the same result. No timestamp parsing needed.
- D-04: Curated golden files checked into the repo under `tests/fixtures/replay/`. Small, focused captures. Version-controlled, always available for CI.
- D-05: Manual trim + commit workflow. Record a live session with `--record`, trim to the interesting part using grep/head/tail on NDJSON lines, commit to `tests/fixtures/replay/`.
- D-06: Service state assertions after replay. Tests are Catch2 test cases that call replay programmatically via a `ReplayHarness` helper class, then assert on the entt::registry state.
- D-07: `ReplayHarness` class sets up stub services (no GameReader, no TCP), replays a file through `DispatchHookEvent()`, and exposes `entt::registry` plus individual services for assertions. Linked into the test binary, not the main binary.
- D-08: Phase 12 scope limited to CharacterService + position tests only. Test cases: actor spawn (entities created in registry), actor remove (entities cleaned up), movement sync (positions update correctly), multiple actors (3+ actors tracked simultaneously).
- D-09: Hardcoded assertions in test cases with explicit expected values. A regression is a test failure. No baseline files or snapshot comparison.
- D-10: Replay tests are a separate Catch2 binary (`ReplayTests`) in the same CI pipeline.

### Claude's Discretion
- How to stub GameReader and PointerTable in replay mode (null implementations, mock objects, or compile-time exclusion)
- ReplayHarness internal architecture (how it initializes services without the full main() setup)
- Whether `--replay` in the main binary shares code with the ReplayHarness test class or they're independent implementations
- Exact fixture file format conventions (whether to include metadata headers or keep pure NDJSON)
- How to handle unknown/malformed ops in replay (skip with warning vs fail)

### Deferred Ideas (OUT OF SCOPE)
None -- discussion stayed within phase scope.
</user_constraints>

## Standard Stack

### Core
| Library | Version | Purpose | Why Standard |
|---------|---------|---------|--------------|
| Catch2 | 2.13.9 | Test framework | Already used in TPTests, header-only, established in project |
| nlohmann/json | (vendored) | JSON parsing | Already vendored in Code/native_client/third_party/ |
| entt | 3.10.0 | ECS registry for assertions | Already a project dependency |
| spdlog | 1.13.0 | Logging in harness | Already used throughout |
| glm | 0.9.9+8 | Vec3/Vec2 for position assertions | Already a project dependency |

### Supporting
| Library | Version | Purpose | When to Use |
|---------|---------|---------|-------------|
| hopscotch-map | 2.3.1 | PointerTable dependency | Used as-is, no stubbing needed |
| rpmalloc | (project ver) | Allocator init in test main | Required by Catch2 runner pattern |

No new dependencies needed. Everything required is already in the project.

**Installation:**
```bash
# No new packages -- all already available via xmake
```

## Architecture Patterns

### Recommended Project Structure
```
Code/
  native_client/
    replay/
      replay_harness.h        # ReplayHarness class declaration
      replay_harness.cpp       # ReplayHarness implementation
      null_tcp_client.h        # No-op TcpClient stub for replay mode
      null_tcp_client.cpp      # Stub implementation
  tests/
    replay/
      xmake.lua               # ReplayTests target definition
      replay_tests.cpp         # Catch2 test cases using ReplayHarness
      main.cpp                 # Catch2 runner (same pattern as tests/main.cpp)
tests/
  fixtures/
    replay/
      actor_spawn.ndjson       # Golden file: single actor add
      actor_remove.ndjson      # Golden file: actor add then remove
      movement_sync.ndjson     # Golden file: position updates
      multi_actor.ndjson       # Golden file: 3+ actors simultaneously
```

### Pattern 1: Stubbing Strategy for GameReader and TcpClient

**What:** GameReader requires ProcMemReader& and PointerTable&. TcpClient requires a live socket. Both need no-op implementations for replay.

**Approach: Null TcpClient**

TcpClient has concrete member `int m_socket = -1` and `LineBuffer m_lineBuffer`. The simplest approach is a thin wrapper or a "null mode" flag:

```cpp
// null_tcp_client.h
// Option A: Standalone NullTcpClient that matches the TcpClient interface
// Since services take TcpClient& (concrete reference, not virtual), we need
// either:
//   1. Make TcpClient constructable in disconnected state (m_socket = -1)
//      and have Send/SendJsonLine silently succeed when disconnected
//   2. Create a NullTcpClient subclass (TcpClient has no virtual methods)
//
// Recommendation: Option 1 is cleanest. TcpClient already has m_socket = -1
// default. Modify Send/SendJsonLine to return true (no-op) when m_socket == -1
// instead of failing. This is a 2-line change in tcp_client.cpp.
```

**Approach: ProcMemReader in disconnected state**

ProcMemReader already has `m_fd = -1` by default and `IsOpen()` returns false. GameReader::Read() calls `m_mem.ReadValue()` which calls `m_mem.Read()` which returns -1 when fd is -1. In replay mode, GameReader reads are never called by CharacterService::OnHookEvent() -- all data comes from JSON fields. The only GameReader call in CharacterService is `m_reader.GetPointers()` and `m_reader.LookupPointer()`, both of which operate on PointerTable (pure data, no I/O).

**Conclusion:** ProcMemReader can be constructed normally (fd=-1), PointerTable works as-is. The only real stub needed is making TcpClient's Send methods no-op when disconnected.

### Pattern 2: ReplayHarness Class Design

**What:** Encapsulates the full service graph and replay loop.

```cpp
// replay_harness.h
class ReplayHarness {
public:
    ReplayHarness();

    // Replay all events from an NDJSON file
    // Returns number of events dispatched
    size_t ReplayFile(const std::string& aPath);

    // Access for assertions
    entt::registry& GetRegistry() { return m_registry; }
    CharacterService& GetCharacterService() { return m_character; }
    PointerTable& GetPointerTable() { return m_pointerTable; }

private:
    // Stubs (owned, constructed first)
    ProcMemReader m_memReader;      // Never opened -- fd=-1, all reads return 0
    PointerTable m_pointerTable;    // Pure data, works as-is
    GameReader m_gameReader;        // Wraps stubs
    TcpClient m_tcpClient;         // Never connected -- m_socket=-1, sends no-op

    // ECS
    entt::registry m_registry;

    // Services
    WeatherService m_weather;
    CalendarService m_calendar;
    QuestService m_quest;
    CombatService m_combat;
    InventoryService m_inventory;
    ActorValueService m_actorValue;
    MagicService m_magic;
    CharacterService m_character;

    static constexpr float kFixedDeltaTime = 1.0f / 60.0f;  // D-03
};
```

**Key insight:** The member initialization order matters. `m_gameReader` must be constructed after `m_memReader` and `m_pointerTable` since it takes references. `m_character` must be constructed after `m_gameReader`, `m_tcpClient`, and `m_registry`.

### Pattern 3: --replay Flag in Main Binary (D-01)

**What:** Add `--replay <file>` to the existing skyrim-coop binary's argument parsing.

```cpp
// In main.cpp ParseArgs or post-parse:
if (replayPath is set) {
    ReplayHarness harness;  // Or lightweight inline setup
    size_t count = harness.ReplayFile(replayPath);
    spdlog::info("Replayed {} events from {}", count, replayPath);
    return 0;  // Exit after replay
}
```

**Decision point (Claude's Discretion):** The main binary's `--replay` path can simply instantiate `ReplayHarness` and call `ReplayFile()`. This shares the same class used by tests. The main binary already links all service code. However, `ReplayHarness` is described as "linked into the test binary, not the main binary" (D-07). Resolution: the harness _class_ lives in native_client/replay/ (compiled into both targets), but the test assertions live only in the test binary. The main binary's --replay just runs the replay and prints summary stats.

### Pattern 4: NDJSON Fixture File Format

**What:** Pure NDJSON, one JSON object per line, same format as `--record` output.

```json
{"op":"actor_added","form_id":20,"ptr":140234567890}
{"op":"set_position","form_id":20,"x":100.0,"y":200.0,"z":300.0,"rx":0.0,"rz":1.57}
{"op":"actor_added","form_id":256,"ptr":140234567900}
{"op":"set_position","form_id":256,"x":150.0,"y":250.0,"z":300.0,"rx":0.0,"rz":0.0}
```

No metadata header. Pure NDJSON. The `--record` flag already writes this exact format (main.cpp line 676: `msg.dump() + "\n"`).

### Anti-Patterns to Avoid
- **Virtual TcpClient interface:** Don't refactor TcpClient to have virtual methods just for testing. The no-op-when-disconnected approach is simpler and doesn't change the production interface.
- **Linking the full main.cpp into tests:** The test binary should link individual service .cpp files, not main.cpp (which has its own main()).
- **Timestamp-dependent replay:** D-03 explicitly mandates fixed deltaTime. Do not parse timestamps from NDJSON lines.
- **Complex fixture generators:** D-05 mandates manual trim+commit. Do not build tooling to auto-generate fixtures.

## Don't Hand-Roll

| Problem | Don't Build | Use Instead | Why |
|---------|-------------|-------------|-----|
| JSON parsing | Custom NDJSON parser | nlohmann::json + getline | Already vendored, battle-tested, used everywhere |
| Test framework | Custom test runner | Catch2 2.13.9 | Already in project, test patterns established |
| ECS assertions | Manual entity iteration | entt::view + Catch2 REQUIRE | entt views are the idiomatic query mechanism |
| File I/O | Custom buffered reader | std::ifstream + std::getline | NDJSON is line-oriented, stdlib handles it perfectly |

## Common Pitfalls

### Pitfall 1: Member Initialization Order
**What goes wrong:** ReplayHarness members must be initialized in declaration order. If GameReader is declared before ProcMemReader, the reference will bind to an uninitialized object.
**Why it happens:** C++ initializes members in declaration order, not initializer list order.
**How to avoid:** Declare stubs (ProcMemReader, PointerTable, TcpClient) before services that depend on them. Use `-Wreorder` warning.
**Warning signs:** Segfault or garbage reads on first test run.

### Pitfall 2: TcpClient::Send Fails When Disconnected
**What goes wrong:** CharacterService calls `m_client.SendJsonLine()` during Update() for remote position commands. If TcpClient::Send returns false (socket == -1), it may trigger error handling that disrupts replay.
**Why it happens:** TcpClient::Send currently returns false on error, services may log warnings or bail out.
**How to avoid:** Modify TcpClient::Send and SendJsonLine to return true (no-op) when m_socket == -1. This is a controlled 2-line change.
**Warning signs:** spdlog warnings about failed sends during replay tests.

### Pitfall 3: PointerTable Insert in DispatchHookEvent
**What goes wrong:** `DispatchHookEvent` inserts into PointerTable when it sees actor_added/spawn_actor events (main.cpp line 150-161). The harness must pass its own PointerTable reference, not a separate one.
**Why it happens:** DispatchHookEvent takes PointerTable& as a parameter, separate from GameReader's internal pointer table.
**How to avoid:** Ensure ReplayHarness passes the same PointerTable instance to both DispatchHookEvent and GameReader. GameReader accesses `m_ptrs` which is the same object.
**Warning signs:** `LookupPointer()` returns 0 for formIds that were added via events.

### Pitfall 4: rpmalloc Initialization in Test Binary
**What goes wrong:** Catch2 allocates during static initialization (TEST_CASE registration). If rpmalloc is the global allocator, it must be initialized before any allocation.
**Why it happens:** Project uses AllocatorSymbolOverride to route all allocations through rpmalloc.
**How to avoid:** Use the `__attribute__((constructor(101)))` pattern from existing tests/main.cpp.
**Warning signs:** Segfault before any test output.

### Pitfall 5: Fixture File Path Resolution
**What goes wrong:** Tests run from the build directory, not the repo root. Relative paths to `tests/fixtures/replay/` won't resolve.
**Why it happens:** xmake runs test binaries from `build/<mode>/tests/`.
**How to avoid:** Use `CATCH_CONFIG_RUNNER` with a custom main that accepts a `--fixtures` path, or use CMake-style configure to embed the absolute path. Simplest: use `__FILE__` macro to compute the source tree root at compile time.
**Warning signs:** "File not found" on first test run.

## Code Examples

### ReplayHarness::ReplayFile Implementation Pattern
```cpp
// Source: Derived from main.cpp main loop (line 644-681)
size_t ReplayHarness::ReplayFile(const std::string& aPath)
{
    std::ifstream file(aPath);
    if (!file.is_open()) {
        spdlog::error("ReplayHarness: cannot open {}", aPath);
        return 0;
    }

    size_t count = 0;
    std::string line;
    while (std::getline(file, line)) {
        if (line.empty()) continue;

        auto msg = nlohmann::json::parse(line, nullptr, false);
        if (msg.is_discarded()) {
            spdlog::warn("ReplayHarness: skipping malformed line {}", count + 1);
            continue;
        }

        const auto& op = msg[JsonField::kOp].get_ref<const std::string&>();
        if (op.starts_with("ctrl_")) continue;  // Skip control messages

        DispatchHookEvent(msg, m_gameReader, m_pointerTable,
                          m_weather, m_calendar, m_quest, m_combat,
                          m_inventory, m_actorValue, m_magic, m_character);

        // Fixed deltaTime update after each event (D-03)
        m_weather.Update(kFixedDeltaTime);
        m_calendar.Update(kFixedDeltaTime);
        m_quest.Update(kFixedDeltaTime);
        m_combat.Update(kFixedDeltaTime);
        m_inventory.Update(kFixedDeltaTime);
        m_actorValue.Update(kFixedDeltaTime);
        m_magic.Update(kFixedDeltaTime);
        m_character.Update(kFixedDeltaTime);

        InterpolationSystem::Update(m_registry, kFixedDeltaTime);

        count++;
    }
    return count;
}
```

### Catch2 Test Case Pattern
```cpp
// Source: Follows Code/tests/json_protocol_tests.cpp pattern
TEST_CASE("Actor spawn creates entity in registry", "[Replay][CharacterService]")
{
    ReplayHarness harness;
    size_t events = harness.ReplayFile(FIXTURE_PATH "/actor_spawn.ndjson");

    REQUIRE(events > 0);

    auto& reg = harness.GetRegistry();
    auto view = reg.view<LocalComponent>();

    // Actor with formId 0x14 (player) should exist
    bool found = false;
    for (auto entity : view) {
        auto& comp = view.get<LocalComponent>(entity);
        if (comp.formId == 0x14) {
            found = true;
            REQUIRE(comp.gamePtr != 0);
        }
    }
    REQUIRE(found);
}

TEST_CASE("Actor remove cleans up entity", "[Replay][CharacterService]")
{
    ReplayHarness harness;
    harness.ReplayFile(FIXTURE_PATH "/actor_remove.ndjson");

    auto& reg = harness.GetRegistry();
    auto view = reg.view<LocalComponent>();

    // After remove, no entities with the removed formId should remain
    for (auto entity : view) {
        auto& comp = view.get<LocalComponent>(entity);
        REQUIRE(comp.formId != 0x100);  // The removed actor
    }
}

TEST_CASE("Movement sync updates position cache", "[Replay][CharacterService]")
{
    ReplayHarness harness;
    harness.ReplayFile(FIXTURE_PATH "/movement_sync.ndjson");

    auto& reg = harness.GetRegistry();
    // Position assertions via InterpolationComponent or position cache
    // depend on whether the actor is local (position cache) or remote
    // (InterpolationComponent). For local actors, check the position cache
    // hasn't been cleared and contains expected values.
}
```

### DispatchHookEvent Linkage

`DispatchHookEvent` is currently a `static` free function in main.cpp (line 140). To share it with ReplayHarness, it needs to be:
1. Moved to a shared header/cpp (e.g., `native_client/replay/dispatch.h`)
2. Or declared non-static and externed

**Recommendation:** Extract DispatchHookEvent into its own file (`dispatch_hook_event.h` / `dispatch_hook_event.cpp`) since both the main binary and the test binary need it.

### xmake Target for ReplayTests
```lua
-- Code/tests/replay/xmake.lua
target("ReplayTests")
    set_kind("binary")
    set_group("Tests")
    set_languages("cxx20")

    add_files("*.cpp")

    -- ReplayHarness source
    add_files("../../native_client/replay/*.cpp")

    -- Service sources (all 8 + systems)
    add_files("../../native_client/Services/*.cpp")
    add_files("../../native_client/Systems/*.cpp")

    -- Shared dispatch function
    add_files("../../native_client/dispatch_hook_event.cpp")

    -- Game bridge stubs/real (PointerTable works as-is, TcpClient needs no-op mode)
    add_files("../../native_client/game_bridge/pointer_table.cpp")
    add_files("../../native_client/game_bridge/tcp_client.cpp")
    if is_plat("linux") then
        add_files("../../native_client/game_bridge/proc_memory.cpp")
    end

    add_includedirs(
        "../../native_client",
        "../../native_client/third_party",
        "../../relay_dll",
        "../../native_client/game_bridge",
        "../../native_client/transport",
        "../../encoding",
        "../../server",
        "../../common", "../../components", "../../base",
        "../../admin_protocol",
        "../../libraries/networking",
        "../../external"
    )

    add_deps("SkyrimEncoding", "SkyrimTogetherServer")
    add_packages("catch2", "glm", "spdlog", "hopscotch-map",
                 "enet6", "entt", "sqlite3", "lua", "sol2")
```

## State of the Art

| Old Approach | Current Approach | When Changed | Impact |
|--------------|------------------|--------------|--------|
| Binary TCP protocol | NDJSON wire protocol | Phase 11 | Makes replay trivial -- human-readable, line-oriented |
| No recording capability | `--record` flag writes NDJSON | Phase 11 | Capture already works, replay just reads the same format |
| Integration tests via fake_player only | Replay tests from recorded data | Phase 12 | Test against real game data without Skyrim running |

## Open Questions

1. **AnimationSystem::Update depends on TcpClient for cmd_play_animation sends**
   - What we know: AnimationSystem::Update takes TcpClient& and sends commands for dirty AnimationComponents
   - What's unclear: Will the no-op TcpClient cause any issues beyond suppressing sends?
   - Recommendation: Should work fine -- SendJsonLine returning true (no-op) is sufficient. AnimationSystem only sends; it doesn't read responses.

2. **DispatchHookEvent is static in main.cpp**
   - What we know: The function is declared `static` at file scope (main.cpp line 140)
   - What's unclear: Whether to move it or duplicate it
   - Recommendation: Extract to a shared file. Duplication would be a maintenance hazard since the dispatch routing logic is the core contract.

3. **FIXTURE_PATH macro for test file resolution**
   - What we know: Tests run from build directory, fixtures live in repo under tests/fixtures/replay/
   - What's unclear: Best way to resolve paths across different build configurations
   - Recommendation: Define `FIXTURE_PATH` as a compile-time macro in xmake.lua using `add_defines("FIXTURE_PATH=\"" .. path.absolute("tests/fixtures/replay") .. "\"")` or use `__FILE__`-based path computation.

## Environment Availability

| Dependency | Required By | Available | Version | Fallback |
|------------|------------|-----------|---------|----------|
| Catch2 | Test framework | Available via xmake | 2.13.9 | -- |
| nlohmann/json | JSON parsing | Vendored | (latest) | -- |
| xmake | Build system | Available | 2.8.5+ | -- |

**Missing dependencies with no fallback:** None.

## Validation Architecture

### Test Framework
| Property | Value |
|----------|-------|
| Framework | Catch2 2.13.9 |
| Config file | Code/tests/main.cpp (CATCH_CONFIG_RUNNER) |
| Quick run command | `xmake build ReplayTests && xmake run ReplayTests` |
| Full suite command | `xmake build TPTests ReplayTests && xmake run TPTests && xmake run ReplayTests` |

### Phase Requirements -> Test Map
| Req ID | Behavior | Test Type | Automated Command | File Exists? |
|--------|----------|-----------|-------------------|-------------|
| D-06 | Service state assertions after replay | integration | `xmake run ReplayTests "[CharacterService]"` | Wave 0 |
| D-08-a | Actor spawn creates entities | integration | `xmake run ReplayTests "Actor spawn creates entity"` | Wave 0 |
| D-08-b | Actor remove cleans up entities | integration | `xmake run ReplayTests "Actor remove cleans up entity"` | Wave 0 |
| D-08-c | Movement sync positions update | integration | `xmake run ReplayTests "Movement sync"` | Wave 0 |
| D-08-d | Multiple actors (3+) tracked | integration | `xmake run ReplayTests "Multiple actors"` | Wave 0 |
| D-01 | --replay flag in main binary | smoke | `echo '{"op":"actor_added","form_id":20,"ptr":1234}' > /tmp/test.ndjson && xmake run SkyrimCoopNative --replay /tmp/test.ndjson` | Wave 0 |
| D-03 | Deterministic fixed deltaTime | unit | Assert count/state identical across two runs of same fixture | Wave 0 |

### Sampling Rate
- **Per task commit:** `xmake build ReplayTests && xmake run ReplayTests`
- **Per wave merge:** `xmake build TPTests ReplayTests && xmake run TPTests && xmake run ReplayTests`
- **Phase gate:** Full suite green before `/gsd:verify-work`

### Wave 0 Gaps
- [ ] `Code/tests/replay/main.cpp` -- Catch2 runner with rpmalloc init
- [ ] `Code/tests/replay/replay_tests.cpp` -- test cases for D-08 scenarios
- [ ] `Code/tests/replay/xmake.lua` -- ReplayTests target definition
- [ ] `tests/fixtures/replay/*.ndjson` -- golden fixture files (hand-crafted for initial tests)

## Sources

### Primary (HIGH confidence)
- `Code/native_client/main.cpp` -- DispatchHookEvent function, --record flag, main loop structure
- `Code/native_client/Services/CharacterService.h` / `.cpp` -- constructor signature, OnHookEvent handler, Update method
- `Code/native_client/game_bridge/tcp_client.h` -- TcpClient interface, m_socket default
- `Code/native_client/game_bridge/game_reader.h` -- GameReader constructor taking ProcMemReader& and PointerTable&
- `Code/native_client/game_bridge/proc_memory.h` -- ProcMemReader default state (m_fd = -1)
- `Code/native_client/game_bridge/pointer_table.h` -- PointerTable is pure data, no I/O
- `Code/relay_dll/json_protocol.h` -- JsonOp and JsonField constants (shared between relay_dll and native_client)
- `Code/tests/main.cpp` -- Catch2 runner pattern with rpmalloc constructor priority
- `Code/tests/xmake.lua` -- existing TPTests target structure
- `Code/native_client/xmake.lua` -- SkyrimCoopNative target structure and dependencies

### Secondary (MEDIUM confidence)
- `Code/tests/integration/fake_player.cpp` -- integration test pattern reference

## Metadata

**Confidence breakdown:**
- Standard stack: HIGH -- all libraries already in project, no new dependencies
- Architecture: HIGH -- DispatchHookEvent, service constructors, and existing patterns fully audited from source
- Pitfalls: HIGH -- identified from direct code inspection of constructor dependencies and I/O paths

**Research date:** 2026-03-29
**Valid until:** 2026-05-29 (stable -- no external dependency changes expected)
