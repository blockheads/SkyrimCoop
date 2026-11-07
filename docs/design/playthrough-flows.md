# Playthrough flows (2026-11-05)

Scripted stretches of real play, as the main way to find what breaks the experience: the party walks, talks, plays a
quest, goes into a dungeon, fights and loots, the way players do, instead of one system at a time in QASmoke. The
user's decision: "what we need is just simply better tests ... some flows of playing through the game".

Started small: one flow that covers as much as it can and stays simple (`riverwood-day-one`). More flows (alternate
starts, a town loop with a purchase, death and rejoin) come once it has proved the shape.

## The shape

```
boot: the duo from a save (a late save, or a flow's own checkpoint with --from)
  │
  step  do: the party plays, through the session's verbs (go_to, talk, door, clear, take)
  │     checks: the state the next step needs (quest stage, items, cell)
  │     health: together, nobody stuck in a menu, nobody down, guest connected, no hang
  │     copy differences: new ones, split into what players see and cosmetic
  │     checkpoint: the host saves flow-<flow>-<step>
  v
  next step ... a failed verb or check stops the flow; the summary names the step
```

- **Verdict:** a step passes when the game state is right, not when the games are identical. A copy difference a
  player would see (`comparisons.classify_problem`: dead, missing or orphan actors, equipment, place, a body floating,
  name, race, health; within 3,000 units of a player) marks the step `SEEN` and the flow goes on; the run fails.
  Cosmetic ones (head tracking, rotation, level, far actors) are listed in the summary only.
- **No retries:** a flow fails loudly. `--from STEP` boots the checkpoint before STEP for whoever iterates on a fix.
- **Each step names what it uses** (`find`), so it runs the same after a `--from` boot.

## Running

```bash
python3 scripts/coop_test.py flows --dry-run                    # the flows and their steps
python3 scripts/coop_test.py flows riverwood-day-one            # boots the duo from the flow's save, plays it
python3 scripts/coop_test.py flows riverwood-day-one --from fight
```

The command holds the duo (the normal lock), boots it, plays, writes `build/coop-test/flows/<time>-<flow>/summary.md`
(and `summary.json`), and stops the games. A flow is a combined-run test (before a merge, or at a cadence), not the
push gate: `scripts/check.sh` runs only its Python units (`PlaythroughFlows` in `scripts/test_coop_harness.py`).

A late save comes into a fresh worktree's prefix by itself (`Runtime.bring_late_save`, from the main checkout's
`build/coop-test/late-saves/`, fetched by `scripts/fetch_late_saves.py`).

## Files

- `scripts/coop_harness/flows/flow.py`: `Step`, the checks (`together`, `quest_at`, `quest_completed`, `carries`),
  `health`, `run_flow`, the summary.
- `scripts/coop_harness/flows/<flow>.py`: one flow (NAME, DOC, BOOT, STEPS).
- Verbs: `door` (through a load door) and `clear` (fight every hostile near the party) in `actions/play_verbs.py`.

## riverwood-day-one

`scripts/coop_harness/flows/riverwood_day_one.py`, from `LateAfterDragonslayerBackinSkyrim`:

```
arrive   both players get a steel sword and +400 health (the save's character has 100), coc Riverwood  [checkpoint]
faendal  both walk to Faendal, the host talks: "problem with Sven", "hope" -> A Lovely Letter (74A83) at 10,
         the letter (5C846) in the host's pack                                                       [checkpoint]
trader   through the Riverwood Trader's door (1341F), Camilla: "a letter for you, from Sven" -> 210,
         the letter gone                                                                             [checkpoint]
reward   out (133E9), both walk to Faendal: "spending any more time with Sven" -> 310               [checkpoint]
lookout  a console trip to Embershard Mine's door (B6CCE) stands in for the road; the guest kills the
         lookout and loots him; sees: his corpse holds the same in both games                        [checkpoint]
enter    through the door                                                                            [checkpoint]
fight    the host clears the first room (the guest stays at the entrance)
loot     the host loots the bandit; sees: the corpse holds the same in both games
leave    the guest leads out, the tether brings the host
```

Runs on 2026-11-05 (resumed with `--from`: a boot from a checkpoint is under a minute): `arrive` to `loot` each
passed at least once (`loot` and `leave` as SEEN, below). What the flow
found, each repeated:

- **A looted corpse keeps items on the guest** (5 of 5, either player looting): after `take`, the host's corpse is
  empty and the guest's copy still holds 2 each of 37C1B, 4F78D and 10E0DD (once 4DEE0 too); neither the looter nor
  its copy got them.
- **The guest's copy of the host stands sunk 26 units in the floor** after the Trader's door and the walk back
  (`reward`), with its left hand empty where the host holds 12FCC (`state.hands`).
- **A tether arrival through a door stacks the players** on one spot (under a unit apart), so the one that didn't
  lead stands in the doorway; `door` lets that player lead instead (the crosshair check, below).

What playing it taught the verbs (each fixed, Python units where they are pure):

- `use`/`talk` press Activate only with the crosshair on the target: `blocked` (another player or NPC in the way)
  or `out_of_reach` otherwise; `talk` walks closer once when the NPC walked on; a corpse is aimed at its body.
- `use` on a door at the navmesh's edge runs the last stretch in a line (`straight`); `run_straight` turns first.
- `fight` clicks again when a swing doesn't start (staggered), and gives up with `no_path` after three approaches
  that never got there instead of swinging at the air.
- `leave_dialogue` presses Tab twice when the first only skipped a line.
- A pick by index replays whatever sits at that index: Faendal's third list held the invisible "..." in one run and
  "Can you train me" in the next, which opened the Training Menu and held the checkpoint's save. Picks are text now.

Open:

- Embershard's first bandit (B6DBB) and three more are behind an invisible wall (Skyrim.esm's CollisionMarker
  105C99 cuts the navmesh and blocks the player: `docs/re/systems/pathing.md`, `terrain.md`). `clear` fights who
  the engine's search reaches from the player, nearest along the way, and skips the rest: the 2026-11-06 run killed
  B6FE4 and B6FDB, skipped four, and ran `loot` and `leave` to the end (SEEN: the filed corpse and door bugs).
- The road: a console trip stands in for walking Riverwood to the mine (~16,500 units); objects are walked to at the
  engine's walk (80 units/s), so a `road` verb (run by input along recorded waypoints) comes first.
- `arrive`'s coc load is a 2.3 s frame: under the 3 s hang limit, close to it.
