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

- **Predicted place** (`Actor::GetPredictedPosition`, our name, 47309): the movement controller's answer if it gives one
  (`IMovementQueryPathingState` through +0x150), else position + velocity x t. At t <= 0 the real position.
- **Margin** (50643): from the combat style (defensive minus offensive), zero unless defensive; dropped when the
  target's reach (47336) is outside the margin of the fighter's own, or 10 s after `CombatState+0x28`, the last attack
  on it (`Actor::DoDamage`, the attacked handler). Measured 0 for Delphine and Orgnar, also 1.6 s after a hit.

Measured: R is 174 for Orgnar, 121 for Delphine; they stop just inside it, as vanilla. Stopping at 150 units is correct.

## The half-stop: a copy's stale predicted place

A copy never runs `MovementControllerNPC::Update` (41717: `actor-movement.md`), so what its controller remembers goes
stale. After the copy was placed far off (`CopyMove::Place`, past `CopyStep::kSnap`, 64 units, or off the ground):

```
combat_targets, Delphine, target = the guest's copy, 1,300 units away:
  target_position   -264, -340       where it stands
  target_predicted  -1313, 325       where it stood before the warp, beside her
  behavior          In Range Movement Selector > Close Movement Idle
```

She stands still believing she is in range, turning to face it (`CombatTrackTargetAimController` reads the real place),
and hits only when the guest comes to her: the 2026-11-04 report. A 350-unit walk afterwards left the prediction frozen,
so the state lasts. The host's death is not the cause; the host's own player is never the stale target because the
player's controller updates.

The fix: `HookGetPredictedPosition` asks for t = 0 for a copy, so the engine gives its real place. A copy loses the
half-second lead, which only matters to arrows and spells aimed at it.

### What is not known

- Which of the controller's fields answers: the query's slot resolves through `MovementPathManagerArbiter`, not followed
  to the end. Measured from outside only: frozen at the place before the warp.
- Why a walk after the warp does not clear it while steps before the warp tracked.
- That the playtest's NPCs were in this state: its log has no such readout. The same symptoms, in the same cell,
  are what is known. Its trigger would be any `Place`: a jump, a stagger past 64 units, a late frame.
- `Character::sub_47310` asks the same controller for a place and heading (one caller), left as it is.

### Wrong turns, for whoever reads the older notes

Bleedout, the host's death, who the fight started on, and the anger registry were each suspected and each ruled out with
the chase working (2026-11-05, all in the inn): real hits landed, guest mortal or in god mode, host alive or dead. The
earlier holds that seemed to need a bleedout came from `setpos` and `moveto` moves in the same runs. A hold of 6 s proves
nothing: NPCs pause in `Close Movement Idle` for some seconds in a normal chase. Read the tree and R, not the distance.

The spot a `coc` into the inn leaves the players, (-264, -340), took vanilla's Delphine more than 20 s to reach, if at all.

## An essential NPC's recovery

`Character::UpdateAI` (0x140679D50): when `AIProcess`'s essential down timer runs out (0x14067A642) it calls vfunc 0xE5
(`StopCombat`), sets life state 0 and restores health. She gets up at full health, so damage cannot end her fight, and
console `stopcombat` lasts about 25 ms. A guest in a fight with an essential NPC has no way to end it but to leave or die.

Measured with the chase working: the host stayed dead 3 minutes and more on this fight, the respawn waiting
(`HostDeath`). This is the playtest's 3.5 minutes (`docs/design/death.md`, "A fight holds the respawn"), a second bug
beside the half-stop: fixing the chase alone leaves it.

A host with only townsfolk fighting it: they clear combat when it dies and it respawns in 10 s, as the playtest's first
death did (a copy is no candidate of theirs until it attacks them).

## Not yet known

- What sets an actor's process level. `ProcessLists__RebuildNearbyActorList` (0x14077E220, on a timer from the player's
  update 0x14075FA50) builds a nearby-actor list, not levels.
- What calls the engine's equip on a copy about once a second (`Copy ... equip ...: the engine's, refused`, `12EB6`,
  `13790`): starts when NPCs turn on the copy, before any death.
- Why console `stopcombat` on an NPC fighting a copy is undone at once, and whether vanilla does the same.
