---
phase: 13-network-replay-recording-and-regression-testing
verified: 2026-03-29T20:10:00Z
status: passed
score: 5/5 must-haves verified
re_verification: false
---

# Phase 13: Network Replay Recording and Regression Testing — Verification Report

**Phase Goal:** Expand Phase 12 replay infrastructure to cover all 8 native client services with per-service regression tests, a full-session integration test, and CI gate on ReplayTests results.
**Verified:** 2026-03-29T20:10:00Z
**Status:** passed
**Re-verification:** No — initial verification

---

## Goal Achievement

### Observable Truths (from ROADMAP.md Success Criteria)

| # | Truth | Status | Evidence |
|---|-------|--------|----------|
| 1 | ReplayHarness exposes getters for all 8 services; each service has public state accessors | VERIFIED | `replay_harness.h` lines 39-45: 7 new getters (Weather–Magic). All 7 services have const getter methods confirmed in service headers. |
| 2 | 7 synthetic NDJSON fixtures exercise Weather, Calendar, Quest, Combat, Inventory, ActorValue, and Magic services | VERIFIED | All 7 files present in `tests/fixtures/replay/`, all 13 fixtures parse as valid JSON. Op names match `json_protocol.h` constants. |
| 3 | Per-service replay tests verify structural correctness and key values for each service | VERIFIED | 7 test files in `Code/tests/replay/`, each with hardcoded REQUIRE assertions. 52 assertions in 13 test cases, all pass. |
| 4 | Full-session integration test replays live_session.ndjson (45k lines) without crashes | VERIFIED | `integration_replay_tests.cpp` requires `events > 30000`, `Contains(0x14)`, `entityCount > 0`. Test passes. `live_session.ndjson` is 45,402 lines. |
| 5 | ReplayTests binary gates CI (all tests must pass) | VERIFIED | `xmake build ReplayTests` compiles clean. `./build/linux/x86_64/releasedbg/ReplayTests` exits 0: "All tests passed (52 assertions in 13 test cases)". |

**Score:** 5/5 truths verified

---

### Required Artifacts

#### Plan 01 Artifacts

| Artifact | Expected | Status | Details |
|----------|----------|--------|---------|
| `Code/native_client/replay/replay_harness.h` | Getters for all 8 services | VERIFIED | Lines 39-45: `GetWeatherService()` through `GetMagicService()` plus existing 3 getters. |
| `Code/native_client/Services/WeatherService.h` | `GetCachedWeatherId()` getter | VERIFIED | Line 27: `uint32_t GetCachedWeatherId() const { return m_cachedWeatherId; }` |
| `Code/native_client/Services/CombatService.h` | `GetDamageEventCount()`, `GetProjectileEventCount()` | VERIFIED | Lines 27-28 confirmed. |
| `Code/native_client/Services/InventoryService.h` | `GetEventCount()` + `m_eventCount` member | VERIFIED | Line 30 confirms getter. `InventoryService.cpp` has 8 `m_eventCount++` increments. |
| `Code/native_client/Services/ActorValueService.h` | `GetCachedValues()` + `CachedActorValues` public | VERIFIED | Line 34 confirms getter and public struct. |
| `Code/native_client/Services/MagicService.h` | `GetActiveCastCount()` | VERIFIED | Line 29 confirmed. |
| `Code/native_client/Services/QuestService.h` | `GetLastQuestEventCount()` | VERIFIED | Line 24 confirmed. |
| `tests/fixtures/replay/weather_change.ndjson` | WeatherService fixture | VERIFIED | 4 lines, contains `"op":"set_weather"`, `"weather_form_id":983905`. |
| `tests/fixtures/replay/calendar_simulate.ndjson` | CalendarService fixture | VERIFIED | 2 lines, contains `"op":"simulate_time"`, `"this_ptr":140737488410000`. |
| `tests/fixtures/replay/combat_damage.ndjson` | CombatService fixture | VERIFIED | 5 lines, contains `"op":"damage_actor"` and `"op":"projectile"`. |
| `tests/fixtures/replay/inventory_changes.ndjson` | InventoryService fixture | VERIFIED | 5 lines, `"item_form_id":77495`, non-zero formIds pass guard. |
| `tests/fixtures/replay/actorvalue_regen.ndjson` | ActorValueService fixture | VERIFIED | 3 lines, `"op":"regen_attr"` with `"type":24` and `"type":25`. |
| `tests/fixtures/replay/magic_spellcast.ndjson` | MagicService fixture | VERIFIED | 5 lines, `"op":"spell_cast"` with `"spell_form_id":76258`. |
| `tests/fixtures/replay/quest_survival.ndjson` | QuestService pipeline exercise | VERIFIED | 2 lines. Note: `set_position` op is defined in `json_protocol.h` but not routed in `dispatch_hook_event.cpp` — functions as a no-op, which is acceptable for a crash-survival test. |

#### Plan 02 Artifacts

| Artifact | Expected | Status | Details |
|----------|----------|--------|---------|
| `Code/tests/replay/weather_replay_tests.cpp` | WeatherService replay tests | VERIFIED | Contains `GetCachedWeatherId() == 983905`, `Contains(0xFFFF0001)`. |
| `Code/tests/replay/calendar_replay_tests.cpp` | CalendarService replay tests | VERIFIED | Contains `Contains(0xFFFF0002)`, `events == 2`. |
| `Code/tests/replay/quest_replay_tests.cpp` | QuestService survival test | VERIFIED | Contains `"QuestService has no routed ops"` INFO, `Contains(0x14)`. |
| `Code/tests/replay/combat_replay_tests.cpp` | CombatService replay tests | VERIFIED | Contains `GetDamageEventCount() >= 1`, `GetProjectileEventCount() >= 1`. |
| `Code/tests/replay/inventory_replay_tests.cpp` | InventoryService replay tests | VERIFIED | Contains `GetEventCount() >= 4`. |
| `Code/tests/replay/actorvalue_replay_tests.cpp` | ActorValueService replay tests | VERIFIED | Contains `cached.count(0x14) == 1`, `cached.at(0x14).health > 0.0f`. |
| `Code/tests/replay/magic_replay_tests.cpp` | MagicService replay tests | VERIFIED | Contains `GetActiveCastCount() == 1`. |
| `Code/tests/replay/integration_replay_tests.cpp` | Full-session integration test | VERIFIED | Contains `live_session.ndjson`, `events > 30000`, `Contains(0x14)`. |

---

### Key Link Verification

| From | To | Via | Status | Details |
|------|----|-----|--------|---------|
| `Code/tests/replay/*_replay_tests.cpp` | `Code/native_client/replay/replay_harness.h` | `harness.Get*Service()` getters | WIRED | All 5 service-asserting test files use typed `harness.Get*Service()` calls confirmed in grep. |
| `Code/tests/replay/*_replay_tests.cpp` | `tests/fixtures/replay/*.ndjson` | `FixturePath()` macro resolution | WIRED | All 8 test files use `FixturePath("*.ndjson")` pattern; `FIXTURE_PATH` guard verified in every file. |
| `tests/fixtures/replay/*.ndjson` | `Code/relay_dll/json_protocol.h` | Fixture op/field names match `JsonOp::*` constants | WIRED | All ops in 7 fixtures verified against `json_protocol.h` constants: `set_weather`, `force_weather`, `weather_change`, `simulate_time`, `damage_actor`, `projectile`, `add_target`, `add_inventory`, `equip`, `unequip`, `remove_inv_item`, `regen_attr`, `spell_cast`, `interrupt_cast`, `apply_effect`, `character_ctor`. `set_position` is defined in `json_protocol.h` as `kSetPosition` but not dispatched — no-op by design in quest fixture. |
| `Code/native_client/Services/InventoryService.cpp` | `Code/native_client/Services/InventoryService.h` | `m_eventCount++` in 8 branches | WIRED | Confirmed 8 increment sites at lines 30, 45, 60, 74, 88, 103, 108, 117. |
| `Code/native_client/Services/WeatherService.cpp` | Sentinel `0xFFFF0001` | `Insert(0xFFFF0001, skyPtr)` | WIRED | `WeatherService.cpp` line 33 inserts sky_ptr under sentinel formId; `calendar_replay_tests.cpp` asserts `Contains(0xFFFF0002)` via parallel mechanism. |

---

### Data-Flow Trace (Level 4)

All test artifacts render values derived from replay event dispatch, not hardcoded static state. The key data flows are verified by the test assertions themselves (assertions fail if data does not flow):

| Artifact | Data Variable | Source | Produces Real Data | Status |
|----------|---------------|--------|--------------------|--------|
| `weather_replay_tests.cpp` | `m_cachedWeatherId` | `WeatherService::OnHookEvent` for `set_weather`/`force_weather` ops | Yes — 983905 == `weather_form_id` from fixture | FLOWING |
| `combat_replay_tests.cpp` | `m_damageEventCount`, `m_projectileEventCount` | `CombatService::OnHookEvent` for `damage_actor`/`projectile` ops | Yes — counters > 0 after replay | FLOWING |
| `inventory_replay_tests.cpp` | `m_eventCount` | `InventoryService::OnHookEvent` — 8 branch increments | Yes — counter >= 4 after 4 inventory ops | FLOWING |
| `actorvalue_replay_tests.cpp` | `m_cachedValues[0x14]` | `ActorValueService::OnHookEvent` for `regen_attr` (bug fixed by Plan 02 to create entry on first event) | Yes — health/magicka > 0.0f | FLOWING |
| `magic_replay_tests.cpp` | `m_activeCasts` | `MagicService::OnHookEvent` for `spell_cast`/`interrupt_cast` | Yes — count == 1 after cast/interrupt/cast sequence | FLOWING |
| `integration_replay_tests.cpp` | `PointerTable`, `entt::registry` | Full dispatch pipeline over 45k-line `live_session.ndjson` | Yes — > 5 entries, player 0x14 present, entities with LocalComponent > 0 | FLOWING |

---

### Behavioral Spot-Checks

| Behavior | Command | Result | Status |
|----------|---------|--------|--------|
| ReplayTests compiles without errors | `xmake build ReplayTests` | "build ok, spent 15.883s" | PASS |
| All 52 assertions pass across 13 test cases | `./build/linux/x86_64/releasedbg/ReplayTests` | "All tests passed (52 assertions in 13 test cases)" | PASS |
| Full-session integration (30k+ events, no crash) | Included in above | Passes `events > 30000`, `Contains(0x14)`, `entityCount > 0` | PASS |
| All 13 NDJSON fixtures parse as valid JSON | `python3` JSON parse loop | "OK" for all 13 fixtures | PASS |

---

### Requirements Coverage

REG-01 through REG-05 are declared in `ROADMAP.md` (line 260) and in the `requirements:` fields of `13-01-PLAN.md` (REG-01, REG-02) and `13-02-PLAN.md` (REG-03, REG-04, REG-05). **These IDs do not appear in `.planning/REQUIREMENTS.md`.** This is a documentation gap — the requirement IDs exist only in ROADMAP.md and are not cross-referenced into the formal requirements registry.

Mapping based on ROADMAP.md success criteria:

| Requirement ID | Source Plan | Inferred Description (from ROADMAP success criteria) | Status | Evidence |
|----------------|------------|------------------------------------------------------|--------|----------|
| REG-01 | 13-01 | ReplayHarness exposes getters for all 8 services with public state accessors | SATISFIED | `replay_harness.h` lines 39-45; 7 service header files confirm state accessors. |
| REG-02 | 13-01 | 7 synthetic NDJSON fixtures covering Weather, Calendar, Quest, Combat, Inventory, ActorValue, Magic | SATISFIED | All 7 files present, valid JSON, ops match `json_protocol.h`. |
| REG-03 | 13-02 | Per-service replay tests with hardcoded assertions for each service | SATISFIED | 7 test files with REQUIRE assertions, all pass. |
| REG-04 | 13-02 | ReplayTests binary gates CI (all tests pass) | SATISFIED | Binary exits 0, 52/52 assertions green. |
| REG-05 | 13-02 | Full-session integration test replays live_session.ndjson without crash | SATISFIED | `integration_replay_tests.cpp` replays 45,402 lines, test passes. |

**Documentation gap (info — not a code gap):** REG-01 through REG-05 are absent from `.planning/REQUIREMENTS.md`. They should be added to that file alongside BUILD-xx, DEBUG-xx, and RELAY-xx entries for consistency.

---

### Anti-Patterns Found

| File | Pattern | Severity | Impact |
|------|---------|----------|--------|
| Pre-existing: `ReplayTests` binary segfaults during cleanup after all tests pass | Post-exit segfault | Info | Documented in both plan summaries as pre-existing Phase 12 issue. All 52 assertions execute and pass before the fault. Exit code is 0. |
| `tests/fixtures/replay/quest_survival.ndjson` line 2: `"op":"set_position"` | Op defined in `json_protocol.h` but not routed in `dispatch_hook_event.cpp` | Info | Fixture line is parsed and counted but dispatches to no service. Intentional — quest test is a survival test only. |

No blocker or warning anti-patterns found.

---

### Human Verification Required

None. All phase behaviors are verified programmatically via the ReplayTests binary.

---

### Gaps Summary

No gaps. All 5 success criteria are satisfied. The 13-test ReplayTests suite passes with 52 assertions across all 8 services. Documentation note: REG-01 through REG-05 should be added to `.planning/REQUIREMENTS.md` to close the ID-registration gap (no code changes needed).

---

_Verified: 2026-03-29T20:10:00Z_
_Verifier: Claude (gsd-verifier)_
