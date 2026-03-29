# Phase 13: Network Replay Recording and Regression Testing - Context

**Gathered:** 2026-03-29
**Status:** Ready for planning

<domain>
## Phase Boundary

Expand the Phase 12 NDJSON replay infrastructure to cover all 8 native client services with regression tests. Extract per-service fixture files from the existing live session recording, add a full-session integration test, and gate CI on replay test results. Scope is limited to the DLL → native client hook-event replay path (NDJSON). Server-side ENet message replay is a separate future phase.

</domain>

<decisions>
## Implementation Decisions

### Service Coverage
- **D-01:** All 7 remaining services get replay tests in this phase: Weather, Calendar, Quest, Combat, Inventory, ActorValue, Magic. The ReplayHarness already instantiates all 8 — just needs fixtures and assertions per service.
- **D-02:** Fixtures extracted from the existing 45k-line `tests/fixtures/replay/live_session.ndjson` using grep/filtering. No new game recordings needed for this phase.

### Regression Detection
- **D-03:** Hardcoded assertions (same as Phase 12). Explicit REQUIRE/CHECK with expected values. A regression = a test failure. No baseline files or snapshot comparison.
- **D-04:** Replay tests gate CI — ReplayTests must pass alongside existing encoding/network tests. Failure blocks merges.
- **D-05:** Catch2 default diagnostics on failure (file:line, section names, INFO() context). No custom event traces or state dumps.

### Recording & Fixtures
- **D-06:** Keep both the full `live_session.ndjson` and extracted per-service slices in `tests/fixtures/replay/`. Full file is used for integration testing; slices for per-service tests.
- **D-07:** Fixtures are game-sourced recordings, not hand-written. They should never be manually modified — they represent what the game actually produced.
- **D-08:** When protocol changes break fixture tests, that's expected and useful. Broken tests signal which fixtures need re-recording from a new game session. No backward-compatibility handling in the harness.

### Assertion Depth
- **D-09:** Structural + key values per service. Verify entities exist with correct components, plus 1-2 key values per service (e.g., weather type ID, quest stage, inventory item count). Balances regression coverage with brittleness.
- **D-10:** One full-session integration test that replays the entire `live_session.ndjson` and checks aggregate cross-service state: total entity count, services that processed events, no crashes.

### Claude's Discretion
- How to extract per-service fixtures from live_session.ndjson (grep patterns, event grouping)
- Which specific key values to assert per service (based on what's in the live recording)
- Whether the full-session test needs a separate Catch2 tag (e.g., [Integration]) for optional CI filtering
- ReplayHarness changes needed to expose per-service state for assertions (getters, etc.)
- Test file organization (one file per service vs grouped)

</decisions>

<canonical_refs>
## Canonical References

**Downstream agents MUST read these before planning or implementing.**

### Replay Harness (Phase 12 — foundation)
- `Code/native_client/replay/replay_harness.h` — ReplayHarness class with all 8 services, stub dependencies
- `Code/native_client/replay/replay_harness.cpp` — ReplayFile() implementation, dispatch loop
- `Code/native_client/dispatch_hook_event.h` — DispatchHookEvent() shared between main binary and harness

### Existing Replay Tests (Phase 12 — pattern to follow)
- `Code/tests/replay/replay_tests.cpp` — 5 CharacterService test cases (actor spawn, remove, movement, multi-actor, game startup)
- `Code/tests/replay/xmake.lua` — ReplayTests build target with FIXTURE_PATH define

### All 8 Services (test targets)
- `Code/native_client/Services/CharacterService.cpp` — Already tested in Phase 12
- `Code/native_client/Services/WeatherService.cpp` — Weather sync
- `Code/native_client/Services/CalendarService.cpp` — Time/date sync
- `Code/native_client/Services/QuestService.cpp` — Quest stage tracking
- `Code/native_client/Services/CombatService.cpp` — Combat events
- `Code/native_client/Services/InventoryService.cpp` — Item/equipment sync
- `Code/native_client/Services/ActorValueService.cpp` — Health/magicka/stamina
- `Code/native_client/Services/MagicService.cpp` — Spell casting

### Fixtures
- `tests/fixtures/replay/live_session.ndjson` — 45k-line full game session recording (source for extraction)
- `tests/fixtures/replay/` — Existing per-service fixtures (actor_spawn, actor_remove, movement_sync, multi_actor, game_startup)

### Protocol Schema
- `Code/relay_dll/hook_trampolines.cpp` — JSON field names for each hook event (authoritative schema for fixture format)

### Prior Phase Context
- `.planning/phases/12-ndjson-replay-capture-and-integration-tests/12-CONTEXT.md` — Phase 12 decisions (D-01 through D-10)

</canonical_refs>

<code_context>
## Existing Code Insights

### Reusable Assets
- `ReplayHarness` class: Already instantiates all 8 services with stub dependencies (ProcMemReader fd=-1, TcpClient socket=-1). No new stubs needed.
- `FixturePath()` helper in `replay_tests.cpp`: Resolves fixture paths via FIXTURE_PATH build define. Reuse for all new test files.
- `live_session.ndjson` (45k lines): Contains real game data for multiple services. Ready for extraction.

### Established Patterns
- Test structure: `TEST_CASE("description", "[Replay][ServiceName]")` with ReplayHarness instantiation, ReplayFile(), then REQUIRE/CHECK assertions.
- Fixtures: NDJSON files in `tests/fixtures/replay/`, one scenario per file.
- Build: `ReplayTests` xmake target links native_client service libs + Catch2, defines FIXTURE_PATH.

### Integration Points
- `ReplayHarness` may need new getters to expose service state for assertions beyond CharacterService (e.g., `GetWeatherService()`, `GetQuestService()`).
- New test files link into existing `ReplayTests` target — no new build targets needed.
- CI pipeline runs ReplayTests alongside existing test targets.

</code_context>

<specifics>
## Specific Ideas

- This phase should be relatively quick — the harness exists, the recording exists, it's mostly fixture extraction + writing assertions.
- User wants a new phase after this one for server-side ENet message recording and replay (different data flow entirely).
- Fixtures are sacred game recordings — never hand-edit them. Protocol breakage = re-record signal.

</specifics>

<deferred>
## Deferred Ideas

- **Server-side ENet message recording and replay** — Record and replay `ServerMessage`/`ClientMessage` traffic on the GameServer ↔ client path. Different protocol, different harness, different fixtures. Should be its own phase (Phase 14 or next available). User explicitly requested this as a follow-on phase.

</deferred>

---

*Phase: 13-network-replay-recording-and-regression-testing*
*Context gathered: 2026-03-29*
