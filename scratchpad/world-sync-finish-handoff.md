# Handoff: finish world sync

You are continuing SkyrimCoop's world-state sync. The goal of this run: finish world sync so it can be called done
(every object ends where the host's is; live physics stays its own later plan), before the team moves on to AI
state on copies and dialogue.

Read first, in this order:
1. `CLAUDE.md` (engineering rules: host/guest split, typed engine structs, test code out of production, short
   comments, glossary words).
2. `docs/progress.md`, section **"World sync: finishing it (plan 2026-10-21)"**: the five steps below, in order.
3. `docs/design/world-state-sync.md`, **"As built"** and **step 7**: how reference sync works now.
4. `docs/re/systems/form-state.md`, **"Change records"**: engine facts the work below leans on.

## Working rules (the user's)

- Never commit or push; the user commits. Ask before anything outward-facing.
- Builds with `-j8` (DLL: `scripts/xmake-msvc.sh build -j8 SkyrimTogetherClientDLL`; tests:
  `scripts/xmake-linux.sh build -j8 TPTests` / `BridgeTests`, one target per build).
- Run only the scenarios a change affects (`scripts/coop_test.py scenario <names>` or `--tag objects`); offer the
  full suite, never start it unasked.
- Oracle first: when unsure what is wrong, add a harness readout so the failure explains itself; no screenshots.
- Explain designs with short ASCII diagrams and pseudocode; keep replies concise.
- Update `docs/progress.md` (and the design doc's "As built") with each change; engine findings go in
  `docs/re/systems/`, IDA names in `docs/re/ida_notes.tsv`, then `python3 scripts/re/build_re_data.py`.

## What exists now

```
host funnel (Games/Skyrim/Forms/FormChanges):
  TESForm::AddChange 14607, forced 14608, RemoveChange 14609
  + a non-actor's repeat moves re-marked (TESObjectREFR::RecordMove, in the
    SetPosition / SetAngle / SetRotationX-Z hooks)
HostEngine watcher -> changed refs -> ReferenceStates::Read each update
  rows (Games/ReferenceStates.cpp), applied on the guest with the engine's call:
    enabled, lock, open state (SetOpenState 14287), move (22053/22054),
    scale (SetScale 19665), owner (SetOwner 20198, GameId)
  last row: save data (FormState) -- OFF (cSaveDataFallback), for mods later
  not sent: game-only extras, promoted, cell changed, inventory (kItems events)
  a flag with no row: logged once ("Reference sync: change flags ... have no row")
guest: GuestEngine::ApplyObjectChange -> ReferenceStates::Apply
```

The oracle (harness + `scripts/coop_test.py`):
- `world_state` harness command: every reference in the loaded cells: state, change record (35577), 3D pose,
  physics bodies, graph variables, effects. Actors are left to `desync`.
- `coop_test.py world-diff`: the two games' loaded worlds compared (created refs paired by base and position, idle
  animation skipped by sampling each game over a second, billboards not read).
- Scenario `world-changes-reach-guest`: one host change per kind (disable, enable, move, rotate, scale, lock,
  open, add items, own, create), each game's object events compared, the put-back checked. Today everything but
  `create` passes.
- `coop_test.py unsynced`: the change flags the host had with no row since the plugin loaded. After the object
  scenarios: havok moved `0x4`, activating children `0x4000000`, animation `0x10000000`.
- Games: `scripts/coop_test.py duo --keep-running` (QASmoke), `reload` after a DLL build, `stop`.

## The plan (docs/progress.md), each step ending with its scenarios and the oracle passing

1. **World diff after every scenario.** In `command_scenario`, compare both games' loaded worlds after each
   scenario (`world_differences(*sample_worlds())`) and fail one that leaves them different, naming the
   references and fields. Run `--tag objects` and the travelling scenarios first; expect failures: each is a
   sync gap (fix or add a row), or a scenario that leaves the world changed (fix its clean-up). Grow `unsynced`'s
   list from the whole suite (offer the full run to the user; don't start it).
2. **State at cell load.** When a guest's cell loads, the host sends the current state of every changed
   reference in that cell through the same rows; covers changes a guest missed while away (dropped today with
   "its object is not in this game") and leveled containers (the host's contents, containers slice 2: see
   `docs/re/systems/guest-world-events.md`, "Containers"). Decide how the host learns which references in that
   cell changed (its change records: `BGSSaveLoadGame::GetChange` 35577, or the stored changes of an unloaded
   cell). Scoreboard kind: guest leaves the cell, host changes something, guest comes back and matches.
3. **Created objects** (`0xFF` ids differ per game): dropped items, placed objects, loot. A mapping from the
   host's created id to each guest's, made when the host creates one; the scoreboard's `create` kind must pass.
   Also unblocks quest fills of created objects (progress.md loose ends).
4. **The rest of `unsynced`:** animation `0x10000000` (check whether the animation events already cover it) and
   activating children `0x4000000`: a row each, or documented as covered.
5. **Physics objects' rest positions** (havok moved `0x4`): when the host's body comes to rest (deactivated), send
   its position and angle through the existing move row; a guest's own push (its player bumping something) is a
   proposal of the rest position that the host's game takes (like a guest's lock or container change). Each game
   simulates the flight itself; the resting spot is the host's. Today the havok flag fires every frame while
   something moves (2,761 times in the travel scenarios): only the rest matters. After step 3 (a dropped item is
   created and physics both). Scoreboard kind: the host knocks an object over; it rests in the same spot on both.

Then world sync is done. Live physics (the flight itself, ragdolls, a guest's pushes as impulses) is NOT this
run's: it is planned after phase 5 (`docs/design/host-authority-migration.md`, "After: physics sync").

## Pitfalls already met (don't rediscover them)

- The engine records a loaded reference's move once (3D moved bit): repeat moves are re-marked in our hooks.
- A state back to its default calls `RemoveChange`, not `AddChange` (a door closing).
- `MoveTo_Impl` (56626) disables and re-enables a non-actor: crashed the guest on a placeable water. Moves use
  22053/22054 per axis; 22054 takes degrees and needs `NiAVObject::Update` (70251) after.
- The console's `setangle` leaves the 3D's world transform stale until something updates it (the scenario
  refreshes with a `setpos`).
- A door ignores `SetOpenState` while it swings; closing mid-swing left the host's own record open across a cell
  reload. Scenarios wait for rest (`at_rest`) and check the put-back.
- Loading save data runs `FinishLoadGame`, which re-inits alias data under the quests' locks: a lock-order
  inversion with a cell load's alias fills. Another reason the fallback is off.
- Save data for promoted / cell changed is the location header `FormState` doesn't write: a half-promoted
  reference on the guest (a broken shrine for the rest of that game session; restart the duo).
- `form-state-round-trips` fails on a fresh duo (known, progress.md); not this work's.
- Open question for the user, not yours to decide: whether the guest's world scripts should get object events
  (OnLockStateChanged, OnOpen, OnItemAdded) when it applies the host's changes. Today they do.

Start with step 1. Before writing code, show the user a short plan for it (diagram + pseudocode) and ask nothing
you can answer from the code or these docs.
