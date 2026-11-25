# Combat targets: how an NPC picks and keeps one

Read in IDA on 1.7.104 and measured on the duo, 2026-11-04 and 11-05 (the "host died, NPCs half-stop" bug, `docs/bugs.md`).

## Selection

```
CombatTargetSelectorStandard::SelectTarget (47191, vfunc 6)
  cached target still good? (CombatGroup__IsTargetStillValid, 0x14081CCE0, no Address Library name)
    actor exists, not deleted or disabled, not dead (vfunc 0x99), in the group's targets (CombatGroup__HasTarget 0x140818C50)
  no, or the re-pick timer ran out:
    pick (47195) -> cached handle, timer reset
```

- Read in IDA (47191, 47195, 2026-11-04/05): a dead cached target fails the check, so the selector re-picks at once, not at the timer.
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
               and predicted distance at 0.5 s (47321, read) > R
```

- **Predicted place** (read in IDA: `Actor::GetPredictedPosition`, our name, 47309): the path follower's answer if it gives one, else
  position + velocity x t. At t <= 0 the real position.
- **The path follower's answer** (`IMovementQueryPathFollowing`, 90724, slot 9, through the controller at +0x150): only
  while its state (+240 from that interface) is 1-8 or 13 and it holds a path with points; then a place along that path.
- **Margin** (read in IDA: `CombatUtilities__GetCloseRangeMargin`, 50643): from the combat style (defensive minus offensive); dropped when the target's reach (`CombatUtilities__GetActorReach` 47336, read) is
  far from the fighter's own, or 10 s after `CombatState+0x28`, the last attack on it (`Actor::DoDamage`, the attacked
  handler). Measured 0 for Delphine and Orgnar, also 1.6 s after a hit.

Measured: R is 174 for Orgnar, 121 for Delphine with a blade, and they stop inside it (vanilla's Delphine stopped about
105 from a real player). Stopping at 150 units is correct: read the tree and R, not the distance. NPCs also pause in
`Close Movement Idle` for some seconds in a normal chase. With a ranged weapon the tree is `Ranged Movement`, and
Delphine's R read 4,123: standing 970 units off and shooting is the engine's fight, not the half-stop.

## A copy's combat (2026-11-25)

Supersedes the refusal below: a copy holds a **real** controller and group, filled from its owner's fight, and nothing
on its own game decides for it (the copy pattern: state mirrored, deciders off, as for packages). Read in IDA 1.7.104
and measured on the duo (spike, 2026-11-25).

```
owner's game                                  copy's game: CopyCombatGroups::Hold (SetCopyCombat)
AI state: Combat, CombatTarget,               fights, no controller:
  CombatGroup (player session << 32 | id),      owner's group here? JoinCombatGroup (46874) : StartCombatGroup (46873)
  GroupTargets (session actors)                 the controller stored on the actor, as StartCombat does (0x1406C9755)
CombatGroup__Update (44703) hooked:           group targets := the owner's (AddTarget 44704, RemoveTarget 44705)
  targets changed -> members' state sent      controller target := the owner's (SetTarget 33235)
                                              group searchState := set while the owner searches (GetCombatState 2)
                                              fight over: StopCombat (38566)
```

The deciders, each read and gated for copies:

- `Actor::UpdateCombat` (38565, per member from `CombatUpdateJob` 46902): `CombatController__UpdateTarget`
  (0x1405617A0, picks and sets the target), `CombatController__ShouldStop` (0x140561330, queues `StopCombat`), the
  combat head-track slot, and the only route to `CombatBehaviorController::Update` (through 0x1405609E0 /
  0x140560820). Skipped for a copy. The tree is built once by `CombatController__StartBehaviorTree` (0x140563850) and
  never stepped: measured, no equip attempt or path from a fighting copy.
- Its tail, `Actor::UpdateCombatSearchState` (37273), is bookkeeping and still runs for a copy: `+0x104` := the
  controller's target, boolBits (`+0xE8`) `kSearchingInCombat` := the group searching (0x140561310). Measured
  (duo, 2026-11-25, `kill_move_on` A/B against dev's build and with hook files off): skipping it too stopped every
  player's kill moves on both games (`kill-moves-every` 242 wrong); running it restored them. Which reader of the
  two fields the kill move pick needs is not yet read: on the host no copy held a controller at the swing, so the
  consumer is not the obvious one (open).
- `CombatGroup__Update` (44703, on the combat job through 46869): detection of the targets, and
  `CombatGroup__RemoveStaleTargets` (44803) drops a target LOST past its timeouts on this game's detection. Not run
  for a group whose members are all copies (`HeldByOwners`): its targets stay its owners'.
- `CombatManager__StartCombatGroup` (46873) / `__JoinCombatGroup` (46874): refused for a copy unless `Hold` calls
  them. Measured: with only `StartCombat` refused, a player's copy got a controller another way (the allies-join
  path, 46878, is the inferred route).
- `Actor::StartCombat` (38561) stays refused for a copy: it also ends packages, equips and leaves the scene.
- `CanAttackActor` (37532) reads the actor's cached knowledge (`ShouldAttack` 37534), which a copy (no AI) lacks:
  measured, 46873 refused every copy. Inside `Hold` it answers yes; the owner's fight decided it.

A player's copy holds its player's own group (`PlayerCharacter::combatGroup`, +0x9E0 on 1.7.104, the enemies 41261
adds): the player has no controller, so its copy reads in combat as its player does (the regen rate, conditions).

Copies of one owner group share one group (the key), so readers counting members see the owner's numbers.

Kept: the stealth eye's hooks (41408, 46877, stealth.md) answer the host's detection of a guest, which a guest's
own game does not compute; they never stood in for a missing controller.

What reads the real state now, with no hook of ours: `IsInCombat` (38562) and `GetCombatState` (38556) for every
caller, the player's in-combat flag (41238 through 41261), the compass (41240, compass.md), the regen rate (38460),
the swing target (`+0x104`, actor-state.md), Papyrus, conditions and plugins (PapyrusExtender, Acheron read groups).

## A copy's own combat: closed (2026-11-05, superseded 2026-11-25)

An attack on an actor runs `ActorProcess::unk_Assault_39359` then `AIProcess__StartAssault` (0x14072EB90), which calls
`Actor::StartCombat(victim, attacker)` (38561). 38561 refuses the real player at its top and nothing else, so a copy took a
real controller and group, and `CombatUpdateJob` (46902) ran its tree: the copy contract's "never given to a copy" had nothing
enforcing it.

- Measured (duo, 2026-11-05, probe on 38561): `StartCombat on copy ... from 14072EE52`, then `combat_targets` on the copy
  reads `Root - Combat`, `controller_target` Orgnar, its own candidates, `Equip weapon`, `Advance`, `Reposition`.
- That tree's `Equip weapon` is the "Copy ... equip 12EB6: the engine's, refused" line, about once a second. The 2026-11-04
  playtest's host log has it from 00:07:54 to 00:11:08, the whole wait, so its copy ran this tree all through.
- `CombatPath__StopMovingOwner` (0x1408D91D0, from 0x14085ED10, a `CombatPathT` dtor) ran `Actor::StopMoving` (37817) on the
  copy: its tree starts paths on its movement controller. A copy's `ApplyMovement` (37356) is cut, so it never walks them.
- `HookStartCombat` (`CopyAiState.cpp`, rule `CopyCombat::MayStart`) refuses copies. With it `combat_targets` on the copy
  reads no tree and no controller, and the equip line stops. Measured: a hit, the host's death and the chase still work.
  `AIProcess__StartAssault` still runs on the copy before that call (a combat package, `EndInterruptPackage`,
  `ClearMovementStopped`): read, not measured to matter; the copy's package reads `3EAB9` after a hit.

## The half-stop (2026-11-04 report): what is known

**Not proved closed, but not a copy's.** Measured on the duo, 2026-11-05, Sleeping Giant Inn, melee NPC Orgnar (R 174):

| Case | Result |
|---|---|
| host dead, target | `controller_target` becomes the copy (report suspects 1 and 3: out) |
| guest walks back by key, copy with its own tree | closes within 300, 5 of 5 |
| same, the refusal on | closes within 300, 5 of 5 |
| guest at (-127, 374), either build | `Advance` / `Circle Distant`, gap stuck at 350-470 |
| same spot, **the real host player** as target | the same, gap 389 for 20 s |
| scenario `host-dies-guest-still-fought` with the chase check, fix / control | 2 of 3 each; the failing fix run: combat over (state 0) during the walk back |

The failures come from detection. `combat_targets` now reads, per candidate, `Actor::RequestDetectionLevel` (37764),
`Actor::GetLightLevel` (37775) and the process light (39509; written by `ActorProcess__SetLightLevel` 0x1406FF490 from
0x140726250 in `ProcessActorAnimSG`'s per-actor update, not `UpdateAI`, so a copy gets it). With Orgnar fighting both
players at once, 80 units apart: light 31 (copy) and 33 (host), detection -7 and -7, then 33 and -10 as he turns: the copy
is seen as the player is. In one boot he started a fight 41 units from the copy at detection -5, `LOST`, `combat_state` 2,
and ended it in about 10 s with the host alive. `Actor::CalculateDetection` (37774) reads the target's light, line of sight
(37768), movement noise and equipped weight; in this dim inn a still or retreating target is lost, real player or copy.

Correction (2026-11-06): the light figures above came from 37775/39509; the console's `GetDetected` reads another
scale, and its floor in this inn is 60 for everyone (host, Delphine and copy alike in one spot). The copy is not darker.

### Measured 2026-11-06: vanilla, and three clean host deaths

Vanilla (`vanilla_start`, `console_watch`, the inn, god mode, `startcombat player`): Orgnar holds combat state 1 for 40 s.
Each hit pushes the player back 3-5 units. Orgnar stands while the gap is inside R (174) and steps in when it nears R, so
the gap runs a sawtooth from 135 to 174. The host's own player on the duo does the same (pushed 145 to 168, Orgnar
standing). Standing at 150-170 while a still target is inside R is the engine's fight, not a stall.

Three clean runs on the duo (both players by Orgnar, both his targets, then `player.kill` on the host): he stays in combat
with the copy, detects it (`GetDetected` 18-21) and hits it. With the guest's god mode off, health fell 13 a hit at 134
units. One run ran vanilla's sawtooth; one held 134 with the guest not moving back further; one held 150 (not explained).

What a copy changes (read and measured):

- **Hits on the guest are judged on the guest's game.** `HitRules`: an NPC copy's swing on the local player is applied
  there. `HitFrameHandler::Handle` (42828) -> 38603 -> `Actor_GetTarget` (38608) takes a forced target
  (`ExtraForcedTarget`, 0x9D, only UseWeapon procedures set it), else `Actor::sub_47272`: the actor's combat target
  (+0x104) if it is in the swing's cone and within `GetAttackReach` (38538) after both bodies (47273); with no combat
  target, the nearest actor in the cone within reach. With no combat target its swing would hit whoever is nearest in
  front; a copy has its owner's (`SetCopyAiFields`, kept past 37273). Measured 2026-11-06: the pick was the host's copy
  while the host's Orgnar fought the guest's copy. **Corrected 2026-11-18** (actor-state.md, read in IDA and measured):
  with `+0x104` set, 47272 takes only that actor or none, and on the duo a wolf fighting the guest's copy, with the
  host's copy in front of it and nearer (gap -37), picked the guest; its copy's `+0x104` was the guest. The 11-06
  reading is not explained (a target switch on the host at that moment is one candidate).
- **Reach**: 145 (mace reach x 141 x scale 1.03) on both games; once 98.9 on the copy, `fCombatBashReach` (96) x scale,
  which 38538 uses while the attack state (actorState1 bits 28-31) reads bash. Vanilla does the same during a bash.
- **Advance's speed** (`CombatBehaviorAdvance__Enter` 0x140882AB0, `CombatApproachTargetSpeedController__Update`
  0x1408EDF60): inside R + `fCombatApproachTargetSlowdownDistance` (96) the speed is lerped by squared distance from
  0 to the target's own speed (`Actor::GetLinearVelocity`) when the target is in front. A still target just outside R gives
  almost no speed: Orgnar sat 175-176 from the copy on `Advance` for 20 s once. A copy's linear velocity is not measured.

Seen but caused by the setup: Orgnar searched (state 2) and walked up to 825 units off for 16 s when re-aimed at a copy
that had been out of sight. A guest unseen when the host dies would get the same.

**The frozen prediction.** PR #21 saw once `target_predicted` frozen 1,300 units off, the NPC "in range". The prediction has one
source that can hold a place: the copy's path follower (47309 -> 90724, state 1-8 or 13 with a path). The copy's own tree is
what was seen starting paths on a copy, so the refusal should remove that source, but the state did not come back: the
follower read state 0 on every sample here (`target_path_state`, `combat_watch`), the copy's tree running. So the link is
inferred.

Also seen: `Actor__OnBumpedByActor` (0x1406A8620) -> `Actor__StartBumpPackage` (0x140680B40) ran `StopMoving` on the copy
about 90 times in 0.7 s (both players `moveto` one NPC, first session) and not on later tries. Only the host's player
bumps (`g_thePlayer` check). What it leaves on a copy's follower is not measured, and it is not refused.

### The melee swing start (read 2026-11-06)

`CombatBehaviorContextMelee__TryStartAttack` (49170, 0x1408B7D60; kaputt's AttackActionHook, SCAR hooks its SE caller):

1. No swing within `fCombatAttackAnimationDrivenDelayTime` of the last one while the NPC is pathing.
2. Centre distance < weapon reach + the longest root motion of its attack clips (`...UpdateAttackMotionReach` 49177)
   + a combat-state value + `fCombatAttackCheckTargetRangeDistance`.
3. Per attack data: chance, stamina, the strike cone (47297), then `...CheckAttackInRange` (0x1408B8390): the distance
   to the target's predicted place at the strike time (`Actor::GetPredictedPosition` 47309) within reach, the cone
   from the attacker's end place, a clear line (0x1408689E0). Then PerformAction.

So a stale prediction for the copy (the frozen prediction below) blocks every swing, not only close movement.

**A/B at 196 units (duo, one run each, 2026-11-06).** With the guest standing still after the host's death, Orgnar sat
196 from the copy for 20 s: detected, line of sight true, prediction exact, `Advance` reselected every ~0.5 s, no
movement. The host's player pushed to 175-196 the same way: Orgnar closed back to 115-136 within seconds. A still copy
600 off: closed to 136, fell back to 256, closed to 143. So a still copy is approachable; what held the 196 stall is
not known (the spot, or the state left by the search before it). `tdetect` is no way to leave the host out: it turns off
all AI detection.

**Host-death rounds (duo, 2026-11-06, `host_death_watch`, 5 each way, the survivor still).** Host dies: the target moves
to the copy and the state goes to search at once every time (2.5-7 s, 44-67 units off), twice walking 440-490 units
away first. Guest dies: the host's player is fought on, never searched for. While both live, the copy's candidate reads
line of sight false and detection about -6, the host's 42 with line of sight. Read and measured 2026-11-21 (stealth.md,
"Line of sight" and "A player's noise"): no ray to the copy was blocked; it was out of the NPC's view cone, and it made
none of its owner's noise (the host's player's bump noise is most of its 42). Its noise is its owner's since: no search.

## A fight with one player is the party's (2026-11-17)

Read in IDA (1.7.104) for the Solitude playtest (2026-11-16): Josh, a guest, stepped onto the execution platform; the
guards killed him and the host was never their target.

```
OpeningSceneInterruptTrigger (DAB6C) -> SolitudeOpening (B2FD9) stage 29, Fragment_16 (vanilla Papyrus source):
    PlatformGuard.StartCombat(Game.GetPlayer())     inferred: a guest's trigger makes that its copy (party-model.md)
papyrus Actor.StartCombat (54768) -> BSTaskPool -> Actor::StartCombat (38561)            read: no crime, no faction
CombatManager__StartCombatGroup (46873): CanAttackActor (37532), a group, AddTarget(target)  read
CombatGroup__AddTarget (44704): refused unless every member CanAttackActor it (44708)        read
    CanAttackActor -> ShouldAttack (37534) -> the reaction (cached 37666, computed 37667)
```

How a fight already running takes another target (read in IDA): the NPC's detection listener.

```
StandardDetectionListener__ReactToDetection (39765): the detecting NPC reacts to a detected actor
  Character__ShouldAssaultDetected (39747): not a player teammate, target alive, not already its combat
      target, ShouldAttackAtOwnAggression (37534)
  -> assault (39359): a guard with an alarm package on the real player and no hostile crime faction raises
     the alarm instead; else AIProcess__StartAssault (40079) -> Actor::StartCombat (38561)
     -> CombatController__AddTarget (33228) -> CombatGroup__AddTarget (44704)
     a real-player target also sets kAngryWithPlayer on the NPC (37463)
  (Character__ShouldJoinAllysCombat 39753 -> 46878: an NPC joining an ally's fight)
```

So the guard's group held the guest's copy only. To a Solitude guard the host's player is a friend: 39747 and 44704
both said no. `HostAggro` counted a fight as the party's only for damage or a crime (`crime.md`), so the guard kept
Skyrim's pick, the copy. Every hostility the engine keeps for "the player" (crime, `kAngryWithPlayer`, faction enemy)
lives on the host's player and reached copies through `CrimeBridge`; hostility aimed at a copy (a script's
`StartCombat`, a friendly hit's anger) never reached the other players.

The party rule (`party-model.md`, Crime): an NPC whose combat target is a player is fighting the party.

- `HostAggro` counts every such NPC (`PartyThreat::State::PartyFights`), so `PartyThreat` gives the nearest player
  it sees, first pick at once.
- `PartyAggroNative::IsPartyEnemy`: the four reaction hooks in `Actor.cpp` (`IsHostileToActor` 37537,
  `GetFactionReaction` 37666, `ShouldAttack` 37534, `ComputeReaction` 37667) answer "enemy" for every player's body,
  for an NPC fighting a player or angry with one (the anger below, kept in `PartyAggro.cpp`). Then 39747 and 44704 let the detection listener add the host (above). When the NPC leaves combat it
  is no foe again.
- A guest's friendly-hit anger (`SkyrimBridge::ReactToAttack`: past the engine's friendly hits, `MarkAngryWithRemotePlayer`)
  is the party's too (2026-11-17, `PartyThreat::AngerCovers`): the angry NPC reads every player's body as an enemy,
  as `kAngryWithPlayer` already did for the host's over-hits through `CrimeBridge`, and the anger ends after a fight
  with any player. Before, it covered the one guest until the NPC's combat target was a player. Measured on the duo
  (`guest-angers-ally-party-enemy`, Lydia staged as an ally): after the guest's second punch Lydia went into combat on
  the copy before the anger was marked (the third hit read her no longer an ally), so the party-fight rule made the host
  her enemy (reaction 1, the host among her candidates). That run did not exercise the anger slot itself;
  ClientTests `[party-aggro]` hold its rule.
- Read, not measured: the host's admission through 39359 sets `kAngryWithPlayer` on the NPC, which `CrimeBridge` reads
  as a party grudge (`crime.md`, Who an NPC fights for a party crime). Whether vanilla clears it when the fight ends
  was not read.

Measured on the duo (`guest-trips-platform-guard`): before, the guard's candidates were the guest's copy only, the host
beside it; after, `Party aggro: NPC 0002E1D1 switches from FF000833 to 00000014` once the guest walked off, and Captain
Aldis (`41FB9`) went from the host to the guest's copy when the guest was the nearer one. That the host entered through
39765 was read, not traced in that run.

## The real player's chase, for comparison

Vanilla (the stand-in, no plugin, same inn, god mode): Delphine followed a teleporting player within a second before her
bleedout, got up after about 18 s, and closed 285 units within a second after it, twice. With the plugin loaded, the same
on the host's player (329 to 107 units in 4 s). The spot `coc` leaves the players, (-264, -340), took vanilla's Delphine
more than 20 s to reach, if at all: not a place to measure a chase.

## An essential NPC's recovery

`Character::UpdateAI` (0x140679D50): when `AIProcess`'s essential down timer runs out (0x14067A642) it calls vfunc 0xE5
(`StopCombat`), sets life state 0 and restores health. She gets up at full health, so damage cannot end her fight, and
console `stopcombat` lasts about 25 ms. A guest in a fight with an essential NPC has no way to end it but to leave or die.

Measured: the host stayed dead over two minutes on this fight, status `Party in combat`: the design (`docs/design/death.md`),
only the guest's death or leaving ends it.

A host with only townsfolk fighting it: they clear combat when it dies and it respawns in 10 s, as the playtest's first
death did (a copy is no candidate of theirs until it attacks them).

## Not yet known

- Whether the bare game's NPC loses a single player in this inn the same way (the stand-in, `vanilla_start`).
- Why detection of the same target in the same spot was -5 in one boot and 29-117 in others.
- What sets an actor's process level. `ProcessLists__RebuildNearbyActorList` (0x14077E220, on a timer from the player's
  update 0x14075FA50) builds a nearby-actor list, not levels.
- Why console `stopcombat` on an NPC fighting a copy is undone at once, and whether vanilla does the same.
