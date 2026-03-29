# Phase 12: NDJSON Replay Capture and Integration Tests - Context

**Gathered:** 2026-03-29
**Status:** Ready for planning

<domain>
## Phase Boundary

Build a replay harness that feeds recorded NDJSON capture files into the native client's `DispatchHookEvent()` loop without Skyrim running, and create integration tests that validate service behavior against real game data. Enables fast iteration and regression detection without launching the game.

</domain>

<decisions>
## Implementation Decisions

### Replay Harness Design
- **D-01:** Add `--replay <file>` flag to the existing `skyrim-coop` binary. Reuses `DispatchHookEvent()` and all services but skips TCP/GameReader initialization. Same binary, no new build target for the CLI tool.
- **D-02:** Fast-forward by default — replay feeds events as fast as the dispatch loop can consume them, no artificial delays. `--real-time` flag available for manual debugging at recorded speed.
- **D-03:** Synthetic fixed deltaTime (1/60s = 16.6ms) for all `service.Update()` calls between dispatched events. Deterministic: same file always produces the same result. No timestamp parsing needed.

### Capture Workflow
- **D-04:** Curated golden files checked into the repo under `tests/fixtures/replay/`. Small, focused captures (e.g., `actor_spawn.ndjson`, `movement_sync.ndjson`). Version-controlled, always available for CI.
- **D-05:** Manual trim + commit workflow. Record a live session with `--record`, trim to the interesting part using grep/head/tail on NDJSON lines, commit to `tests/fixtures/replay/`. Human judgment on what matters.

### Integration Test Strategy
- **D-06:** Service state assertions after replay. Tests are Catch2 test cases that call replay programmatically via a `ReplayHarness` helper class, then assert on the entt::registry state (entity counts, component values, positions).
- **D-07:** `ReplayHarness` class sets up stub services (no GameReader, no TCP), replays a file through `DispatchHookEvent()`, and exposes `entt::registry` plus individual services for assertions. Linked into the test binary, not the main binary.
- **D-08:** Phase 12 scope limited to CharacterService + position tests only. Test cases: actor spawn (entities created in registry), actor remove (entities cleaned up), movement sync (positions update correctly), multiple actors (3+ actors tracked simultaneously). Other services get replay tests in future phases.

### Regression Detection
- **D-09:** Hardcoded assertions in test cases with explicit expected values (entity count, final positions, component states). A regression is a test failure. No baseline files or snapshot comparison needed.
- **D-10:** Replay tests are a separate Catch2 binary (`ReplayTests`) in the same CI pipeline. Runs alongside existing encoding/network tests. One CI step, one report.

### Claude's Discretion
- How to stub GameReader and PointerTable in replay mode (null implementations, mock objects, or compile-time exclusion)
- ReplayHarness internal architecture (how it initializes services without the full main() setup)
- Whether `--replay` in the main binary shares code with the ReplayHarness test class or they're independent implementations
- Exact fixture file format conventions (whether to include metadata headers or keep pure NDJSON)
- How to handle unknown/malformed ops in replay (skip with warning vs fail)

</decisions>

<canonical_refs>
## Canonical References

**Downstream agents MUST read these before planning or implementing.**

### NDJSON Wire Protocol (Phase 11 — prerequisite)
- `Code/native_client/main.cpp` — `DispatchHookEvent()` function (line ~140), main loop with NDJSON receive + dispatch (line ~644), `--record` flag implementation (line ~106)
- `Code/native_client/game_bridge/tcp_client.h` — `ReceiveJsonLine()` and `SendJson()` NDJSON interface
- `Code/native_client/game_bridge/tcp_client.cpp` — NDJSON line buffering implementation

### Services (test targets)
- `Code/native_client/Services/CharacterService.cpp` — `OnHookEvent()` handler, primary test target
- `Code/native_client/Services/CharacterService.h` — Service interface

### JSON Protocol Definition
- `Code/relay_dll/protocol.h` — Opcode enums (deprecated binary but documents all ops)
- `Code/relay_dll/hook_trampolines.cpp` — JSON field names for each hook event (authoritative schema)

### Existing Test Infrastructure
- `Code/tests/encoding.cpp` — Existing Catch2 tests (pattern to follow)
- `Code/tests/main.cpp` — Catch2 entry point
- `Code/tests/integration/fake_player.cpp` — Mock client pattern reference

### Prior Phase Context
- `.planning/phases/11-json-wire-protocol/11-CONTEXT.md` — JSON protocol decisions (D-01 through D-12)
- `.planning/phases/09-in-game-p2p-integration-test/09-CONTEXT.md` — `--record` flag decision (D-14), testing pyramid (D-01)

</canonical_refs>

<code_context>
## Existing Code Insights

### Reusable Assets
- `--record` flag in `main.cpp` (line 106-128, 336-343, 674-678): Already writes NDJSON lines to file. Replay reads the same format.
- `DispatchHookEvent()` in `main.cpp` (line 140-180): Single dispatch function that routes JSON messages to services by `op` string. Replay feeds directly into this.
- All 8 services have uniform `OnHookEvent(const nlohmann::json& aMsg)` interface — replay doesn't need special per-service handling.
- `nlohmann/json.hpp` already vendored in `Code/native_client/third_party/` — no new dependencies.

### Established Patterns
- Services are constructed with references to `entt::registry`, `TcpClient`, `GameReader`, `PointerTable`. Replay mode needs stubs for TcpClient and GameReader.
- Main loop structure: receive → dispatch → service.Update(deltaTime) → systems.Update(). Replay mirrors this with file reads instead of TCP receives.
- Catch2 tests in `Code/tests/` follow `TEST_CASE("name")` pattern with `REQUIRE`/`CHECK` assertions.

### Integration Points
- `ReplayHarness` needs to instantiate the same services as `main.cpp` but without TCP/GameReader/PointerTable live connections.
- `--replay` flag in main binary shares the dispatch path but replaces the TCP receive loop with file reading.
- New `ReplayTests` xmake target links against native_client service libraries + Catch2.

</code_context>

<specifics>
## Specific Ideas

- The replay harness is essentially `main.cpp`'s main loop with the TCP receive replaced by file reading — the dispatch and service update paths are identical.
- Fixed deltaTime makes replay fully deterministic, which is critical for regression detection via hardcoded assertions.
- Starting with CharacterService-only keeps scope tight. The `ReplayHarness` class is the real deliverable — once it exists, adding tests for other services is trivial.

</specifics>

<deferred>
## Deferred Ideas

None — discussion stayed within phase scope

</deferred>

---

*Phase: 12-ndjson-replay-capture-and-integration-tests*
*Context gathered: 2026-03-29*
