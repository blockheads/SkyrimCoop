# What drifts between two games, and how often the engine records a change

Findings of steps 1 and 2 of `docs/design/world-state-sync.md` (2026-10-19, branch
`clang-cl-port-e2e-tests` after `731e49d3`). No sync code: these measure what a world-state sync has
to carry, so step 3 onwards builds for the real list.

## Short answer

- **Quests drift from the first second.** Each game runs its own start-game and story-manager quests
  and fills their aliases with its own random picks: 13 to 62 quests differ at the duo baseline (two
  boots), plus the story-manager nodes' `TimeLastRun`. The client's own story manager also starts
  quests the host never ran (`WE09`, a world encounter, ran only on the host after fast travel). A
  join's full sync fixes the start; the client starting its own quests afterwards is a new open
  question (below).
- **Global variables never go through the change funnel.** They are saved whole, as one table (1121
  globals here). `set` on a global records no change at all; pushing globals needs a diff of that
  table, not the funnel. Two differ at baseline (`WEEnabled` 1 / 0, `WINextUseMagic`).
- **Containers' leveled loot rolls per game.** About 30 containers differ at baseline, and every cell
  loaded on both games adds more (`LeveledInventory`, `Inventory`). A host-only `additem` shows up as
  the expected `REFR Inventory` drift.
- **What we already sync stays identical in the save**: the door and lock of the object scenarios carry
  equal change forms on both games (`OpenState`, `LockExtra`, `GameOnlyExtra`).
- **Rates are small outside cell loads.** Idle, the engine records about 100 changes a second, about 95%
  of them actors simulating off screen (`ACHR Moved`, `PackageExtraData`); everything else is under one
  change a second. A cell load bursts to about 1700 calls a second, most of which the funnel ignores
  (it ignores changes while a load runs).
- **Side findings** (loose ends in `docs/progress.md`): the client's `GameDaysPassed` misses the host's
  time jumps; copy spawns write the NPC base's factions; a host crash in the Runner queue 2 s after a
  save (`docs/re/crashes/2026-10-19-runner-queue-after-save.md`).

## Tools

- `scripts/re/save_diff.py HOST.ess CLIENT.ess [--noise A B] [--json]`: parses two saves (UESP layout,
  LZ4 body) and compares them block by block. Change forms are keyed by their real form id (each save's
  RefIDs resolved through its own form id array); labels carry editor ids read from the plugins.
- `coop_test.py save-diff [--label X] [--noise S]`: saves both running games (`save coopdiff-X`), waits
  until both files are written, then compares; `--noise S` saves the host again S seconds later and marks
  what differs between those two as noise.
- `coop_test.py change-rate [scenarios] [--seconds S]`: counts the engine's change records on both games
  over a window (idle, or the named scenarios), per form type and flag, with the distinct forms
  recorded and example ids. The hook is `Code/client/TestHarness/ChangeLog.cpp` (test builds only).

### How a difference is classified

| Class | Meaning |
|---|---|
| drift | world state that differs between the two games |
| actors | an NPC's own state (`ACHR`): the copy contract syncs loaded ones; unloaded ones simulate on each game |
| noise | also differs between two saves of one game seconds apart (`--noise`) |
| expected | per player by design: the player (`0x14`, `0x7`), player location, menus, controls, favourites, stats |

Created forms (`0xFF......`) are counted and listed by type, never compared: the two games number them
independently (the host's spawned NPC was `FF000889`, its copy on the client `FF000946`).

Two things in the payloads are not real differences and are masked:

- **RefIDs inside a form's data** index each save's own form id array, which orders forms differently.
  Equal-length payloads whose differing 3-byte windows resolve to the same form id are equal.
- **Clock stamps.** The engine writes the same 4-byte values into many forms at once: a seconds timer
  (166.4 in one save, 172.0 in the next, in every actor), a few counters. The tool learns them from the
  pair (a 4-byte difference that recurs in 20+ forms) and ignores them: 120 to 280 patterns per pair.

Payloads are not parsed per form type, so a data difference says which form and from which byte, not
which field. Parsing the first live types is step 3's job (`BGSLoadFormBuffer`).

### Checks

- A save diffed with itself: no differences.
- Two saves of the host 5 s apart (the noise baseline): header time; global variables `GameHour` and
  `GameDaysPassed`; global data Weather, Process Lists, Combat, Papyrus; about 770 off-screen actors
  (position, AI package data, process timers); 4 QASmoke furniture references' `Animation` data.

## Step 1: what differs between host and client

Setup: `duo --keep-running`, both games booted fresh from the main menu into QASmoke (`coc`, no save),
vanilla plus the Creation Club plugins of the test install, no other mods.

### At the baseline

| Kind | Run 1 | Run 2 | What |
|---|---|---|---|
| QUST | 62 | 13 | start-game and radiant quests (`MS01`, `T03`, `DarkBrotherhood`, `CR04`-`CR08`, `MGR*`, `DA*`, `Freeform*`, `C00`, `MS06Start`, `SolitudeOpening`...): same size, data differs in the runtime and instance data (alias fills); 3 exist on one game only |
| REFR | 55 | 48 | containers whose inventory rolled differently (`Inventory`, `LeveledInventory`); references promoted (made persistent) on one game only (`Promoted`, `GameOnlyExtra`) |
| SMQN | 8 | 8 | story-manager nodes' `TimeLastRun` (`MGR*Node`, `Companions*Node`) |
| CELL | 5 | 7 | cell `Flags` of a few interiors, QASmoke's `SeenData` |
| NPC_ | 1 | 1 | `HousecarlWhiterun` `Factions` on the client only: our copy spawn (side findings) |
| ACHR | 1422 | 1420 | actors (class "actors") |
| Global variables | 2 | 2 | `WEEnabled` (1 / 0), `WINextUseMagic` |
| Global data | | | SkyCells, Detection Manager, Location MetaData, Quest Static Data |

### After scenarios

| What ran | New drift |
|---|---|
| `door-opens-for-both`, `lock-reaches-others`, `death-host-npc` | none. Door `0200CAAC` has equal change forms on both games (`Animation`+`GameOnlyExtra`, then `LockExtra`). The NPC and its copy are created forms |
| host `C2CE4/C2CE6.additem` (a chest in QASmoke) | `REFR Inventory` on the host only, as expected |
| host `set WINextUseMagic to 5` | the global (5 / 0.33) |
| `party-fast-travels` (to Riverwood) | about 100 forms: the loaded exterior cells (`ExteriorChar`, `DetachTime`, `SeenData`), interiors' `Flags`; containers' `LeveledInventory` rolled per game; physics objects that settled differently (`HavokMoved`); references promoted on one game; Riverwood NPCs' bases' `Factions` on the client (copies); `FreeformRiverwood01` data and story-manager nodes; `WE09` and `WESpecificWE09` on the host only; globals `CaravanIsCampedB/C` (0 / 1); global data Story Event Manager, Anim Objects. **`GameDaysPassed` 0.4376 / 0.3866** (side findings) |

A quirk: `additem` of gold into a chest with no change form yet left none in the save, though the
funnel recorded the change (step 2). The flag says what may have changed; what the save writes is the
form's own `SaveGame`. A sync that reads the funnel and serializes with `SaveGame` gets the same answer
as the save, so this does not change the design.

## Step 2: the change funnel

### Where the hook sits (IDA, 1.6.1170)

- The funnel `BGSSaveLoadGame::AddChangeFlags` (`0x140604320`, no Address Library ID) has exactly two
  callers:
  - `TESForm::AddChange` (14607, vfunc 0x0A). Overrides tail-call it, for example
    `TESActorBase::AddChange` (24385).
  - 14608, which is **not** a remove-change counterpart: it is the same call with the funnel's 4th
    argument (force) set, so it records even while a load runs. Its callers are
    `BGSDynamicPersistenceManager::PromoteReference`, `TESObjectREFR::FinishLoadGame`,
    `TESObjectREFR::Revert` and three others.
- The funnel returns 1 when it recorded the change. It returns 0 while a load runs (unforced), for
  ignored forms (form flag 0x4000), for forms without an id, and for temporary references that are not
  persistent.
- One path skips the funnel: `TESActorBase::AddChange` on a temporary (created) actor base keeps the
  change in its own actor-base data.
- The hook counts per (form type, flags, forced): calls, recorded, a few example ids, and the distinct
  forms recorded per type. Atomics only: the funnel runs on job threads.

### Rates

Idle in QASmoke (30 s, both games settled; the first window after a reload ran 5x higher on the
reconnecting client):

| Type | Host /s | Client /s | Flags | Distinct forms in 30 s |
|---|---|---|---|---|
| ACHR | 93 | 110 | `Moved` (76-93/s), `PackageExtraData` (10-12), forced `Promoted` (3.7), `CellChanged` (1-3) | 291 / 338 |
| REFR | 0.5 | 0.9 | `OpenState`, `LockExtra` (NPCs using doors off screen) | 14 / 21 |
| CELL | 0.3 | 0.7 | `Flags` | 4 / 7 |
| NPC_ | 0.07 | 0.17 | `Factions` (`DLC2Talvas`, a script) | 1 |
| QUST, GLOB | 0 | 0 | | |

Scenarios (each its own window):

| Scenario | What it adds (besides the idle actors) |
|---|---|
| `door-opens-for-both` | door `0200CAAC`: `OpenState`, `GameOnlyExtra`, `LockExtra`, the same on both games |
| `lock-reaches-others` | door `0200CAAC`: `LockExtra` on both |
| `death-host-npc` | the NPC and its copy (created forms): `HavokMoved`, `Inventory`, `Flags`, forced `Animation`; `LifeState` on the host only (the copy dies by its owner's action); `INFO SaidOnce` on both |
| `melee-client-hits-host-npc` | the NPC: `Inventory` (5/s); the client's NPC base `Factions` 13 times (copies) |
| `party-fast-travels` (22 s) | host 1683 calls/s, client 1207: REFR `Flags` 800/s of which 7% recorded (the load), `GameOnlyExtra`, `EncZoneExtra`, `HavokMoved`, forced `Promoted`/`Animation`; ACHR `Moved` 530/s on the host, `Inventory`, forced `LeveledInventory`; CELL `DetachTime`+`ExteriorChar` 14/s; QUST 3/s (`QuestFlags`, `Stages`, `AlreadyRun`, `RuntimeData`) on 3-5 quests; SMQN, LCTN `KeywordData`, SCEN `Active`, INFO; the player's `NPC_ Face+Race`; 168 `NPC_ Factions` on the client (copies) |
| console probes on the host | `setstage`: QUST `QuestFlags`, `Objectives`, `Stages`. `additem`: REFR `Inventory`. `lock`: REFR `LockExtra`. `set` on a global: nothing |

Distinct forms are what a dynamic sync would send. Outside cell loads, non-actor forms change at well
under one a second; a cell load records a few hundred to a few thousand forms at once, which suits the
design's "at the load screen" path.

## What this changes in the design

1. **Globals need their own detection.** They are not change forms and never reach the funnel: diff the
   host's global table (about 1100 floats, 8 KB) a few times a second and send what changed, minus the
   clock globals the calendar sync covers. Cheap, and safe to apply live.
2. **The client's own quest machinery is a drift source**, not only a receiver. The story manager and
   start-game quests run on every game: a full sync at join gives the client the host's quests, but the
   client then keeps starting its own (radiant, world encounters) and rolling its own aliases. New open
   question for the design: keep the client's story manager and quest starts off (as copies run no AI),
   or let them run and overwrite them.
3. **Leveled containers re-roll on every game** when their cell loads or resets. Containers in cells the
   party has loaded need the host's inventory at the client's load screen, not just a push of live
   changes.
4. **Off-screen actors are not world-state sync's job.** About 1400 differ; they are the copy contract's
   when loaded and irrelevant while unloaded (the host owns every NPC). Exclude `ACHR` from the dynamic
   sync, as actor positions, values and equipment already are.
5. **A compare sweep cannot hash raw blocks**: the clock stamps and the per-save form id array change
   the bytes of equal forms. Hash parsed fields per type, or mask as `save_diff.py` does.
6. **Forced changes (14608)** are recorded even while a load runs (promotion, `FinishLoadGame`,
   `Revert`). If applying the host's data on the client runs one of those, it records a change despite
   the load flag: the echo guard cannot rest on the load flag alone.

### Proposed order for the first live types

1. Global variables (table diff), without the clock globals.
2. Quest stages, objectives and flags (`QUST`), with the story-manager nodes (`SMQN TimeLastRun`), once
   the client's own quest starts are settled (point 2).
3. References' `LockExtra`, `OpenState`, ownership, enabled state (form flags) and `Inventory` of
   containers in unloaded cells; `LCTN Cleared`/`KeywordData`; `FACT` crime counts.

At the client's load screen only: `CELL` (detach time, seen data, exterior data), references with 3D
(`Moved`, `HavokMoved`, `Animation`), leveled inventories of the destination cell, anything created
(needs the id remap of step 3).

Never: the players' own forms (`0x14`, `0x7`), `ACHR`.

## Side findings

- **`GameDaysPassed` misses host time jumps.** `CalendarService::HandleUpdate` adds each tick's delta to
  the client's `GameDaysPassed`, so a jump on the host (fast travel, wait, sleep) never arrives:
  0.4376 on the host, 0.3866 on the client after `party-fast-travels` (1.2 game hours), with `GameHour`
  equal. Quest timers, cell resets and anything else keyed on days passed drift with it.
- **Copy spawns write the NPC base.** `SetActorFactions` (`RemoveFromAllFactions`, then `SetFactionRank`
  per faction) changes the shared `TESNPC`'s factions, so the client records `NPC_ Factions` the host
  never did (168 times in one fast travel, 13 in one melee scenario).
- **Host crash in `TaskQueue::Drain`** 2 s after two saves, not reproduced: open,
  `docs/re/crashes/2026-10-19-runner-queue-after-save.md`.
- **`coop_test.py stop` missed games launched from another worktree** (their environment spells the
  prefix through the resolved `build/` symlink, and Steam rewrote `STEAM_COMPAT_DATA_PATH`): fixed, it
  matches either spelling of either variable.
- Not run: popular quest or world mods. The test install has none; worth a run once one is installed,
  with `save-diff` and `change-rate` as they are.
