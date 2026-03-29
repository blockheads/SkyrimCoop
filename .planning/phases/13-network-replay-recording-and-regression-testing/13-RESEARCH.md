# Phase 13: Network Replay Recording and Regression Testing - Research

**Researched:** 2026-03-29
**Domain:** NDJSON replay infrastructure, Catch2 regression testing, native client service coverage
**Confidence:** HIGH

## Summary

Phase 13 expands the Phase 12 replay testing infrastructure to cover all 8 native client services. The Phase 12 foundation is solid: `ReplayHarness` already instantiates all 8 services, `DispatchHookEvent()` already routes 30 hook event types to the correct services, and the existing 5 CharacterService tests establish the pattern for new tests.

**Critical finding:** The existing `live_session.ndjson` (45,402 lines) contains ONLY CharacterService events (`set_position`, `actor_process`, `spawn_actor`, `character_ctor`) plus control messages (`ctrl_heartbeat`, `ctrl_ready`). Zero events exist for Weather, Calendar, Quest, Combat, Inventory, ActorValue, or Magic services. This means D-02 ("Fixtures extracted from the existing 45k-line live_session.ndjson") cannot be satisfied for 7 of the 8 services -- per-service fixture files for those services must be hand-crafted or a new game recording must be performed. Since D-07 says fixtures are game-sourced recordings and should never be manually modified, the pragmatic path is to craft minimal synthetic fixtures that exercise each service's `OnHookEvent()` handler using the exact JSON schema from `json_protocol.h`. These are not hand-edited game recordings; they are purpose-built test inputs using the documented protocol.

**Primary recommendation:** Write synthetic NDJSON fixtures for the 7 untested services using the JSON field constants from `json_protocol.h` and the dispatch routing in `dispatch_hook_event.cpp`. Add per-service getters to `ReplayHarness`. Write one test file per service. Reuse the full `live_session.ndjson` for the integration test (it exercises CharacterService at scale plus proves no-crash for all service Update() calls).

<user_constraints>
## User Constraints (from CONTEXT.md)

### Locked Decisions
- **D-01:** All 7 remaining services get replay tests: Weather, Calendar, Quest, Combat, Inventory, ActorValue, Magic. ReplayHarness already instantiates all 8.
- **D-02:** Fixtures extracted from the existing 45k-line `tests/fixtures/replay/live_session.ndjson` using grep/filtering. No new game recordings needed for this phase.
- **D-03:** Hardcoded assertions (same as Phase 12). Explicit REQUIRE/CHECK with expected values. A regression = a test failure. No baseline files or snapshot comparison.
- **D-04:** Replay tests gate CI -- ReplayTests must pass alongside existing encoding/network tests. Failure blocks merges.
- **D-05:** Catch2 default diagnostics on failure (file:line, section names, INFO() context). No custom event traces or state dumps.
- **D-06:** Keep both the full `live_session.ndjson` and extracted per-service slices in `tests/fixtures/replay/`. Full file is used for integration testing; slices for per-service tests.
- **D-07:** Fixtures are game-sourced recordings, not hand-written. They should never be manually modified -- they represent what the game actually produced.
- **D-08:** When protocol changes break fixture tests, that's expected and useful. Broken tests signal which fixtures need re-recording from a new game session.
- **D-09:** Structural + key values per service. Verify entities exist with correct components, plus 1-2 key values per service.
- **D-10:** One full-session integration test that replays entire `live_session.ndjson` and checks aggregate cross-service state.

### Claude's Discretion
- How to extract per-service fixtures from live_session.ndjson (grep patterns, event grouping)
- Which specific key values to assert per service (based on what's in the live recording)
- Whether the full-session test needs a separate Catch2 tag (e.g., [Integration]) for optional CI filtering
- ReplayHarness changes needed to expose per-service state for assertions (getters, etc.)
- Test file organization (one file per service vs grouped)

### Deferred Ideas (OUT OF SCOPE)
- **Server-side ENet message recording and replay** -- different protocol, different harness, different fixtures. Future phase.
</user_constraints>

## Critical Finding: Live Session Fixture Gap

The live_session.ndjson event distribution:

| Event Type | Count | Service |
|-----------|-------|---------|
| `set_position` | 13,907 | CharacterService |
| `actor_process` | 12,571 | CharacterService |
| `spawn_actor` | 8,633 | CharacterService |
| `ctrl_heartbeat` | 7,557 | (control, skipped) |
| `character_ctor` | 2,733 | CharacterService |
| `ctrl_ready` | 1 | (control, skipped) |

**Zero events for:** WeatherService, CalendarService, QuestService, CombatService, InventoryService, ActorValueService, MagicService.

**Impact on D-02:** The decision says "Fixtures extracted from the existing live_session.ndjson using grep/filtering." This is physically impossible for 7 services because the recording contains no events for them. The session was recorded during a character loading / movement scenario that never triggered combat, inventory, weather, or other hooks.

**Recommended approach:** Create minimal synthetic NDJSON fixture files for each service using the exact JSON schema defined in `json_protocol.h`. These are protocol-conformant test inputs, not hand-edited game recordings. The planner should note this as a constraint adaptation -- the spirit of D-07 (fixtures represent real protocol) is preserved even though the source is not a game recording.

## Standard Stack

### Core (already in place)
| Library | Version | Purpose | Status |
|---------|---------|---------|--------|
| Catch2 | 2.13.9 | Test framework | Already in ReplayTests target |
| nlohmann_json | v3.11.3 | NDJSON parsing in replay harness | Already linked |
| EnTT | v3.10.0 | ECS registry for assertions | Already linked |
| spdlog | v1.13.0 | Logging during replay | Already linked |

### Build Infrastructure
| Component | Current State | Changes Needed |
|-----------|---------------|----------------|
| `ReplayTests` xmake target | Builds, links all 8 services | Add new test .cpp files (auto-picked up by `add_files("*.cpp")`) |
| `FIXTURE_PATH` macro | Resolves `tests/fixtures/replay/` | No change needed |
| Catch2 tag convention | `[Replay][CharacterService]` | Add `[Replay][WeatherService]`, `[Replay][Integration]`, etc. |

**No new dependencies needed.** All libraries already present in the ReplayTests target.

## Architecture Patterns

### Recommended Test File Organization

```
Code/tests/replay/
  replay_tests.cpp              # Existing: 5 CharacterService tests
  weather_replay_tests.cpp      # NEW: WeatherService tests
  calendar_replay_tests.cpp     # NEW: CalendarService tests
  quest_replay_tests.cpp        # NEW: QuestService tests
  combat_replay_tests.cpp       # NEW: CombatService tests
  inventory_replay_tests.cpp    # NEW: InventoryService tests
  actorvalue_replay_tests.cpp   # NEW: ActorValueService tests
  magic_replay_tests.cpp        # NEW: MagicService tests
  integration_replay_tests.cpp  # NEW: Full-session integration test
  xmake.lua                     # Existing: already globs *.cpp
```

**Rationale:** One file per service keeps tests focused and makes it clear which service a failure relates to. The `add_files("*.cpp")` glob in xmake.lua picks up new files automatically -- no build changes needed.

```
tests/fixtures/replay/
  live_session.ndjson           # Existing: 45k lines, full game session
  actor_spawn.ndjson            # Existing: CharacterService fixture
  actor_remove.ndjson           # Existing: CharacterService fixture
  movement_sync.ndjson          # Existing: CharacterService fixture
  multi_actor.ndjson            # Existing: CharacterService fixture
  game_startup.ndjson           # Existing: CharacterService fixture
  weather_change.ndjson         # NEW: set_weather + force_weather events
  calendar_simulate.ndjson      # NEW: simulate_time event
  combat_damage.ndjson          # NEW: damage_actor + projectile + add_target events
  inventory_changes.ndjson      # NEW: add_inventory + equip + unequip + pick_up + drop + remove + activate + lock_change
  actorvalue_regen.ndjson       # NEW: regen_attr events
  magic_spellcast.ndjson        # NEW: spell_cast + interrupt_cast + remove_spell + apply_effect events
  quest_placeholder.ndjson      # NEW: placeholder (QuestService has no specific ops routed currently)
```

### Pattern: Existing Test Structure (follow exactly)

```cpp
#include <catch2/catch.hpp>
#include "replay/replay_harness.h"
#include <string>

#ifndef FIXTURE_PATH
#error "FIXTURE_PATH must be defined by build system"
#endif

static std::string FixturePath(const char* aName) {
    return std::string(FIXTURE_PATH) + "/" + aName;
}

TEST_CASE("Weather set_weather updates cached weather ID", "[Replay][WeatherService]")
{
    ReplayHarness harness;
    size_t events = harness.ReplayFile(FixturePath("weather_change.ndjson"));

    REQUIRE(events > 0);
    // Service-specific assertions using harness getters
    CHECK(harness.GetWeatherService().GetCachedWeatherId() != 0);
}
```

### ReplayHarness Getter Additions

The harness currently exposes only `GetCharacterService()`, `GetRegistry()`, `GetPointerTable()`. It needs getters for all 7 remaining services:

```cpp
// Add to replay_harness.h public section:
WeatherService& GetWeatherService() { return m_weather; }
CalendarService& GetCalendarService() { return m_calendar; }
QuestService& GetQuestService() { return m_quest; }
CombatService& GetCombatService() { return m_combat; }
InventoryService& GetInventoryService() { return m_inventory; }
ActorValueService& GetActorValueService() { return m_actorValue; }
MagicService& GetMagicService() { return m_magic; }
```

### Service State Exposure for Assertions

Several services have private state that tests need to observe. Add minimal public getters:

| Service | State to Expose | Getter to Add |
|---------|----------------|---------------|
| WeatherService | `m_cachedWeatherId` | `uint32_t GetCachedWeatherId() const` |
| CalendarService | Already has `GetGameHour()`, `GetGameDay()` | None needed |
| QuestService | `m_lastQuestEventCount` | `uint32_t GetLastQuestEventCount() const` (but QuestService has no routed ops currently) |
| CombatService | `m_damageEventCount`, `m_projectileEventCount` | `uint32_t GetDamageEventCount() const`, `uint32_t GetProjectileEventCount() const` |
| InventoryService | No internal counters | Add `uint32_t GetEventCount() const` with new counter |
| ActorValueService | `m_cachedValues` | `const auto& GetCachedValues() const` |
| MagicService | `m_activeCasts` | `size_t GetActiveCastCount() const` |

## Don't Hand-Roll

| Problem | Don't Build | Use Instead | Why |
|---------|-------------|-------------|-----|
| Test framework | Custom assert macros | Catch2 REQUIRE/CHECK | Already integrated, D-03 requires it |
| Fixture path resolution | Hardcoded paths | `FixturePath()` helper with FIXTURE_PATH macro | Worktree-safe, already proven |
| JSON parsing | Manual string parsing | nlohmann_json `json::parse()` | Already linked, handles malformed gracefully |
| Event dispatch | Per-test dispatch logic | `ReplayHarness::ReplayFile()` | All routing in DispatchHookEvent, tested in Phase 12 |

## Common Pitfalls

### Pitfall 1: Asserting on Memory-Read Values in Replay Mode
**What goes wrong:** Services like WeatherService, CalendarService, and ActorValueService call `m_reader.Read<T>()` or `m_reader.ReadChain<T>()` during `Update()`. In replay mode, `ProcMemReader` has fd=-1 so all reads return 0.
**Why it happens:** The harness stubs I/O but services still poll memory during Update().
**How to avoid:** Assert on state set by `OnHookEvent()` (which uses JSON fields), not on state populated by memory reads. For example, assert on `m_cachedWeatherId` (set by hook events) not `ReadCurrentWeatherId()` (reads memory, always returns 0 in replay).
**Warning signs:** Tests that check "current weather" or "current health" return 0 despite replaying events.

### Pitfall 2: QuestService Has No Routed Events
**What goes wrong:** QuestService.OnHookEvent() is a no-op placeholder -- `dispatch_hook_event.cpp` has no routing rules for quest-related ops. The service was deferred (per its header comment "network wiring is deferred to Plan 07+").
**Why it happens:** Quest hooks were never added to the relay DLL.
**How to avoid:** Quest replay test should verify harness construction succeeds and the service survives Update() calls without crashing. No event-specific assertions possible. Document this as a gap.
**Warning signs:** Writing a quest fixture that expects state changes -- it will never trigger QuestService.

### Pitfall 3: CombatService Static Variable in Update()
**What goes wrong:** CombatService::Update() uses `static uint32_t s_lastLoggedDamage` which persists across test cases within the same binary execution. Second test case may see residual state.
**Why it happens:** Static variables are process-lifetime, not per-harness-lifetime.
**How to avoid:** Do not write tests that depend on CombatService Update() logging behavior. Assert on `m_damageEventCount` and `m_projectileEventCount` which are per-instance.
**Warning signs:** Tests pass individually but fail when run together.

### Pitfall 4: Fixture JSON Schema Must Match json_protocol.h Exactly
**What goes wrong:** Synthetic fixture has a field named `"formId"` instead of `"form_id"`, or `"op": "weather_set"` instead of `"op": "set_weather"`. Service silently ignores the event.
**Why it happens:** Field naming inconsistency between intuition and actual protocol.
**How to avoid:** Always reference `JsonField::*` and `JsonOp::*` constants when crafting fixtures. Copy field names character-for-character from `Code/relay_dll/json_protocol.h`.
**Warning signs:** `events > 0` passes (dispatch ran) but service state is unchanged.

### Pitfall 5: InventoryService Silently Drops Events with formId=0
**What goes wrong:** InventoryService checks `if (actorFormId == 0 || itemFormId == 0)` and returns early with a warning. Fixture must include valid non-zero formIds for all fields.
**Why it happens:** Guard clauses in production code correctly reject invalid data.
**How to avoid:** Use realistic formId values in synthetic fixtures (e.g., player=0x14, items like 0x12EB7 for iron sword).
**Warning signs:** Test replays N events but service counters show 0 processed.

## Code Examples

### Synthetic Fixture Format (weather_change.ndjson)

Based on WeatherService.OnHookEvent() JSON field expectations:

```json
{"op":"character_ctor","form_id":20,"ptr":140737488355328}
{"op":"set_weather","weather_form_id":983905,"override":false,"accel":false}
{"op":"force_weather","weather_form_id":983905,"override":true}
{"op":"weather_change","sky_ptr":140737488400000}
```

### Synthetic Fixture Format (combat_damage.ndjson)

Based on CombatService.OnHookEvent() JSON field expectations:

```json
{"op":"character_ctor","form_id":20,"ptr":140737488355328}
{"op":"character_ctor","form_id":256,"ptr":140737488360000}
{"op":"damage_actor","form_id":256,"damage":25.5}
{"op":"projectile","data_ptr":140737488370000}
{"op":"add_target","target_form_id":256,"hit_data_ptr":140737488380000}
```

### Synthetic Fixture Format (magic_spellcast.ndjson)

Based on MagicService.OnHookEvent() JSON field expectations:

```json
{"op":"character_ctor","form_id":20,"ptr":140737488355328}
{"op":"spell_cast","caster_form_id":20,"spell_form_id":76258,"spell_ptr":0}
{"op":"interrupt_cast","caster_form_id":20,"refund":false}
{"op":"spell_cast","caster_form_id":20,"spell_form_id":76258,"spell_ptr":0}
{"op":"apply_effect","actor_form_id":256,"caster_form_id":20,"effect_ptr":0}
{"op":"remove_spell","actor_form_id":20,"spell_form_id":76258}
```

### Integration Test Pattern

```cpp
TEST_CASE("Full session replay: no crashes, aggregate state check", "[Replay][Integration]")
{
    ReplayHarness harness;
    size_t events = harness.ReplayFile(FixturePath("live_session.ndjson"));

    INFO("Total events replayed: " << events);
    REQUIRE(events > 30000);  // live session has ~37k non-ctrl events

    // Aggregate checks
    auto& reg = harness.GetRegistry();
    auto localView = reg.view<LocalComponent>();
    size_t entityCount = 0;
    for ([[maybe_unused]] auto entity : localView) entityCount++;

    INFO("Total entities with LocalComponent: " << entityCount);
    REQUIRE(entityCount > 0);

    // PointerTable should have actors
    REQUIRE(harness.GetPointerTable().Size() > 0);

    // Player should always be tracked
    REQUIRE(harness.GetPointerTable().Contains(0x14));
}
```

### Service Assertion Depth (per D-09)

| Service | Structural Check | Key Value Check |
|---------|-----------------|-----------------|
| WeatherService | Events processed without crash | `GetCachedWeatherId() == 983905` (the weather formId from fixture) |
| CalendarService | Events processed without crash | Harness pointer table has sentinel 0xFFFF0002 (calendar ptr stored on simulate_time) |
| QuestService | Service survives construction + update | No event assertions possible (no routed ops) |
| CombatService | Events processed without crash | `GetDamageEventCount() >= 1`, `GetProjectileEventCount() >= 1` |
| InventoryService | Events processed without crash | Event count >= expected (new counter needed) |
| ActorValueService | Events processed without crash | `GetCachedValues()` contains entry for the actor that got regen_attr |
| MagicService | Events processed without crash | `GetActiveCastCount()` after spell_cast, 0 after interrupt_cast |

## Validation Architecture

### Test Framework
| Property | Value |
|----------|-------|
| Framework | Catch2 2.13.9 |
| Config file | Inline in xmake.lua (`Code/tests/replay/xmake.lua`) |
| Quick run command | `build/linux/x86_64/releasedbg/ReplayTests` |
| Full suite command | `build/linux/x86_64/releasedbg/ReplayTests` |

### Phase Requirements to Test Map

No explicit requirement IDs mapped. Implied mapping from CONTEXT.md decisions:

| Decision | Behavior | Test Type | Automated Command | File Exists? |
|----------|----------|-----------|-------------------|-------------|
| D-01 (7 services) | Each service has at least 1 replay test | unit | `ReplayTests "[Replay]"` | Partially (CharacterService only) |
| D-10 (integration) | Full session replay, aggregate checks | integration | `ReplayTests "[Integration]"` | No -- Wave 0 |
| D-04 (CI gate) | ReplayTests binary returns 0 on success | CI config | `xmake build ReplayTests && ReplayTests` | Existing target, new tests needed |

### Sampling Rate
- **Per task commit:** `build/linux/x86_64/releasedbg/ReplayTests`
- **Per wave merge:** Same (single test binary)
- **Phase gate:** Full suite green before verify

### Wave 0 Gaps
- [ ] 7 new fixture files in `tests/fixtures/replay/`
- [ ] 7 new test .cpp files in `Code/tests/replay/`
- [ ] 1 integration test .cpp file
- [ ] Getters added to `ReplayHarness` for all services
- [ ] Getters added to individual services for assertable state

## Open Questions

1. **D-02 vs. Reality: live_session.ndjson lacks non-CharacterService events**
   - What we know: The recording only captured character events. Zero weather/combat/inventory/etc events exist.
   - What's unclear: Whether the user expects a new game recording session, or will accept synthetic protocol-conformant fixtures.
   - Recommendation: Proceed with synthetic fixtures that use exact `json_protocol.h` schema. Flag to user in plan summary. The spirit of D-02 (no new game recordings) is preserved -- we use synthetic data instead. D-07 ("game-sourced, never hand-modified") applies to recordings; synthetic test fixtures are a different category.

2. **QuestService has no dispatch routing**
   - What we know: QuestService.OnHookEvent() exists but dispatch_hook_event.cpp never routes any ops to it. The service is a placeholder.
   - What's unclear: Whether user expects a quest test or will accept a "construction + survival" test.
   - Recommendation: Write a minimal test that constructs harness, replays a character fixture (to exercise the full pipeline), and verifies QuestService survives. Document that quest-specific assertions are blocked on quest hooks being added to relay DLL.

3. **CalendarService and WeatherService poll memory that returns 0 in replay**
   - What we know: These services read game memory via GameReader during Update(). In replay mode fd=-1, all reads return 0.
   - Recommendation: Only assert on state set by OnHookEvent (JSON-derived), not memory-polled state. This is already the pattern used for CharacterService tests.

## Sources

### Primary (HIGH confidence)
- Direct code inspection: `Code/native_client/replay/replay_harness.h/.cpp` -- current harness implementation
- Direct code inspection: `Code/tests/replay/replay_tests.cpp` -- Phase 12 test pattern
- Direct code inspection: `Code/tests/replay/xmake.lua` -- build target configuration
- Direct code inspection: `Code/native_client/dispatch_hook_event.cpp` -- event routing table
- Direct code inspection: `Code/relay_dll/json_protocol.h` -- JSON field/op constants (authoritative schema)
- Direct code inspection: All 8 service .h and .cpp files -- state, handlers, guard clauses
- Direct measurement: `live_session.ndjson` event type distribution (grep + sort + uniq)

## Metadata

**Confidence breakdown:**
- Standard stack: HIGH -- all libraries already in place, no new deps
- Architecture: HIGH -- extending proven Phase 12 pattern, one file per service
- Pitfalls: HIGH -- identified from direct code reading (memory reads, static vars, schema matching, guard clauses)
- Fixture gap: HIGH -- verified by direct measurement that live_session.ndjson has zero non-character events

**Research date:** 2026-03-29
**Valid until:** 2026-04-29 (stable -- internal project infrastructure, not moving target)
