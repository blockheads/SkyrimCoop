# Combat targets: how an NPC picks and keeps one

Read in IDA on 1.7.104 and measured on the duo, 2026-11-04 (the "host died, NPCs half-stop" bug, `docs/bugs.md`).

## Selection

```
CombatTargetSelectorStandard::SelectTarget (47191, vfunc 6)
  cached target still good? (CombatGroup__IsTargetStillValid, 0x14081CCE0)
    actor exists, not deleted or disabled, not dead (vfunc 0x99), in the group's targets (0x140818C50)
  no, or the re-pick timer ran out:
    pick (47195) -> cached handle, timer reset
```

- A dead cached target fails the check, so the selector re-picks at once, not at the timer.
- **47195** walks the combat group's targets (`CombatTarget`, 0xA8 each). One counts if `detectLevel > 0`,
  `stealthPoints <= 0` and not `LOST`, or if it is the current target and `KNOWN`.
- It scores distance, recent hits and attackers. A dead target only loses points there, it is not excluded, and with a
  single target no score is computed at all.
- If nothing counts, it falls back to the group's other targets, by how long since each was seen.
- **Validity (47196)** tests only the encounter zone and the child flag. Nothing on this path tests for a
  `PlayerCharacter`, so the guest's copy, a plain `Character`, is an ordinary candidate.

## Measured with the host dead (duo, one bandit)

- The target moves to the guest's copy, the dead host leaves the group's targets, and the NPC chases and hits.
- This held in QASmoke, and outdoors with the corpse about 9,500 units away (an attack about every 6 s, staggers on the
  guest). The NPCs stayed at `process_level` 0 there, so distance from the host's body did not lower it at that range.

## A copy past the host's loaded cells

Outdoors, a guest walking on past the host's loaded cells (about 10,700 units from a dead host's body): the copy leaves
what the host's player sees nearby, its `CombatTarget` reads detection -1000, then `LOST` with `combat_state` 2 (search),
and the NPCs walk about searching. The respawn came late, after at least 60 s and within the next 30 s, once the
watching player had lost its spectate target. What ended the wait was not measured.

This is not the 2026-11-04 playtest's case: that fight was in an interior.

## Closing on a target

The combat tree runs in threads of named nodes; the harness's `combat_targets` prints them (`behavior`). Close movement:

```
Root - CloseMovement > Close Movement Context > Movement Selector
  Out of Range Movement Selector > Advance | Surround | ...      closes in
  Out of Position Movement Selector > Reposition | ...
  In Range Movement Selector > Close Movement Idle | Fallback/Circle
```

`CombatBehaviorContextCloseMovement::CheckOutOfRange` (0x140882350) picks the branch:

```
R = CombatInventory+0x19C (own extents + equipment range) + margin (50643)
out of range = (no target location, or real distance >= R, or outside its arc)
               and predicted distance at 0.5 s (0x14086A5F0) > R
```

- **Predicted place** (`Actor::GetPredictedPosition`, our name, 47309): the path follower's answer if it gives one, else
  position + velocity x t. At t <= 0 the real position.
- **The path follower's answer** (`IMovementQueryPathFollowing`, 90724, slot 9, through the controller at +0x150): only
  while its state (+240 from that interface) is 1-8 or 13 and it holds a path with points; then a place along that path.
- **Margin** (50643): from the combat style (defensive minus offensive); dropped when the target's reach (47336) is
  far from the fighter's own, or 10 s after `CombatState+0x28`, the last attack on it (`Actor::DoDamage`, the attacked
  handler). Measured 0 for Delphine and Orgnar, also 1.6 s after a hit.

Measured: R is 174 for Orgnar, 121 for Delphine with a blade, and they stop inside it (vanilla's Delphine stopped about
105 from a real player). Stopping at 150 units is correct: read the tree and R, not the distance. NPCs also pause in
`Close Movement Idle` for some seconds in a normal chase. With a ranged weapon the tree is `Ranged Movement`, and
Delphine's R read 4,123: standing 970 units off and shooting is the engine's fight, not the half-stop.

## The half-stop (2026-11-04 report): not closed

Seen once on the duo (2026-11-05, host dead, Orgnar dead, Delphine recovered from bleedout, guest walked, then `setpos`):

```
combat_targets, Delphine, target = the guest's copy, 1,300 units away:
  target_position   -264, -340       where it stands
  target_predicted  -1313, 325       near where it last stood, beside her
  behavior          In Range Movement Selector > Close Movement Idle
```

The prediction stayed there while the copy moved on, so she stood "in range": the report's symptoms (facing the guest,
hitting only up close, no chase). Holding a fixed place fits a path left in the copy's path follower, not velocity.

**What set it is not known.** With nothing correcting the prediction, the same steps in another run did not bring it back:
real hits, walking, `setpos` far off, bleedout, host dead, Orgnar dead, in that order. Neither did `setpos` alone
(`placed-copy-is-chased`, written and deleted: it passed with and without a correction). Whether the playtest's NPCs were
in this state is also not known: its log has no such readout.

What is in place: `HookGetPredictedPosition` replaces a copy's prediction with its real place only when it is farther off
than anything moves in that time (`CopyStep::PredictionStale`, 800 units/s, 400 at the 0.5 s the check asks), so aim keeps its
lead. It logs `Copy ...: combat predicted ...: stale path`. That line in a report dates the trigger; no line and the same
symptoms mean this is not the cause. It is a guard against a state seen once, not a proven fix. The bug stays in
`docs/bugs.md`.

Ruled out as causes, the chase working in each: the bleedout, the host's death, who the fight started on. The anger
registry: no `is now angry` line in any run or the playtest's log.

## The real player's chase, for comparison

Vanilla (the stand-in, no plugin, same inn, god mode): Delphine followed a teleporting player within a second before her
bleedout, got up after about 18 s, and closed 285 units within a second after it, twice. With the plugin loaded, the same
on the host's player (329 to 107 units in 4 s). The spot `coc` leaves the players, (-264, -340), took vanilla's Delphine
more than 20 s to reach, if at all: not a place to measure a chase.

## An essential NPC's recovery

`Character::UpdateAI` (0x140679D50): when `AIProcess`'s essential down timer runs out (0x14067A642) it calls vfunc 0xE5
(`StopCombat`), sets life state 0 and restores health. She gets up at full health, so damage cannot end her fight, and
console `stopcombat` lasts about 25 ms. A guest in a fight with an essential NPC has no way to end it but to leave or die.

Measured: the host stayed dead over two minutes on this fight, status `Party in combat`, until the run was stopped; only the
guest's death would have ended it (`HostDeath`). So the playtest's 3.5 minutes needs no half-stop, only an NPC that does not
go down for good: a second bug, fixed by a cap (`docs/design/death.md`). That Delphine held the playtest's wait is inferred:
she and Orgnar are the only NPCs its log names in the inn.

A host with only townsfolk fighting it: they clear combat when it dies and it respawns in 10 s, as the playtest's first
death did (a copy is no candidate of theirs until it attacks them).

## Not yet known

- What sets an actor's process level. `ProcessLists__RebuildNearbyActorList` (0x14077E220, on a timer from the player's
  update 0x14075FA50) builds a nearby-actor list, not levels.
- What calls the engine's equip on a copy about once a second (`Copy ... equip ...: the engine's, refused`, `12EB6`,
  `13790`): starts when NPCs turn on the copy, before any death.
- Why console `stopcombat` on an NPC fighting a copy is undone at once, and whether vanilla does the same.
