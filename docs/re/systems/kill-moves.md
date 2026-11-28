# Kill moves, end to end

Reviewed and reproduced 2026-11-04 on 1.7.104. Scenarios `kill-move-runs-full-length`, `kill-moves-cross`,
`kill-move-drops-at-blow`, `kill-moves-by-weapon`; GameTests
`[kill-move]`, UnitTests `[replay-state]`.

## Claimed pairs (one-simulator.md piece 4b, 2026-11-20)

One game runs a pair. A kill move a game's own actor starts on an actor it does not own is an action claim
(`HostClaims`, `KillMoveService`; one-simulator.md, "Action claims"): its attacker locked, its NPC victim taken or its
player victim locked; the pair plays on meanwhile.

```
claimer's game                         host                          other games
its actor's pa_Kill... accepted
(38949: StartPair 32803 began it)
  ClaimRequest{kill move, attacker, --> one claim per actor: both free
    tick, members}                     an NPC victim's owner = claimer
                                       NotifyClaim ----------------> the host's game: its NPC is a copy
claimer: the NPC is its own (TakeOwnership),                         every non-claimer game plays the pair itself:
  the copy's scripted deferred kill kept                             an instance between its two copies (the NPC's
its clip's KillActor (42838): only counted (37896),                  copy has no stream: below)
  its pose held; graph ops sent                                      a player partner's game: plays its half
  each member's give-back: the       --> NPC: the host's again          from the replay, as before
  attacker's, the victim's{tick, killed} NotifyActorGiveBack ------> the host's game: once its own copy's clip sent
                                                                     KillActor (or 1.5 s), TakeOwnership, the hold
                                                                     ended, KillImpl with the claim's actor
refused (NotifyClaimRefused, to every game): the claimer ends its pair (the yield below);
  kHeld (either actor in another claim: crossed kill moves): its blow dropped
  kCannotTake (the victim dead or kProcessMe clear): the same action again with no target (42350's second try)
  every other game never plays that pair's replay
a claimed pair's forwarded blow on the victim's game: held; dropped once the pair plays there, a plain hit if it ends first
```

- **Read in IDA (2026-11-20).**
  - `KillActorHandler` 42838 kills through `Actor::QueueKill` 37316 with the pair's other participant as killer
    (null when its lookup finds none), after ending a non-scripted deferred kill only. Crediting the claim's own actor
    always is our choice, from that: the done message carries only "killed", and the host names the killer.
  - The victim's `Character::EndKillMove` 38614 (`KillMoveEndHandler` 42871) also queues a kill, by its recorded
    killer (+0x108), with no `KillActor` event. Under the lent NPC's hold either kill only counts: `DeferKill` 38617
    raises `MiddleHigh +0x2F8` above 1 (`MiddleProcess::deferredKill`, 0 none, 1 held). So "killed" is that count
    (`Actor::KillCounted`), and releasing a hold drops it (`Actor::HoldKill`): 40036 would queue it for
    `g_thePlayer`, against the hand-back's word (an alive hand-back after the host's copy counted its own clip's kill).
  - Every engine caller of `Actor::EndDeferredKill` 38618 passes scripted 0 (`Set3D`, `MoveToLow`, `MoveToMiddleLow`,
    `Revert`, 37947, 37972, 42838, `FlightLandEndHandler`, 42907, `Actor::SetGettingOnOffMount` 37462: a rider's non-scripted hold through a get-on); only Papyrus passes 1, and 40036
    acts only on a matching flag. So a copy's scripted hold (`UpdateCopyAI`) outlives the copy: `TakeOwnership` now
    ends it (`Actor::HoldKill`), and a lend for a pair keeps it on purpose. Inferred, not read: the hold goes with
    the middle-high process data when that is dropped.
  - `KillImpl` 37896, after the deferred-kill test: `IsDead` (vfunc 0x99) returns at once.
  - 38949: `started = StartPair(32803) && the graph event sent`; our hook sends only accepted actions, so an accepted
    action with a target event is a pair begun. (32803 also returns 1 for two actors already in one instance, but the
    selection checks 39002 refuse an unforced action of an actor in an instance first.)
  - **The engine's fallback for a refused pair** is in the player's controls, not the idle tree:
    `PlayerControls` 42350 (from `AttackBlockHandler` 42435, action 45) performs the action with the swing target
    (`Func5` 41557); refused with a target, it clears the event names and the target (`BGSActionData_SetTarget` null)
    and performs it again in the same call: the tree then picks a plain swing. `coop_PickIdleForAction` (from 37147)
    makes one pick per perform, no retry. `Actor::PerformPlainly` is that second try.
- **Why the kill rides the done message, not a forwarded hit** (the plan's first idea): a hit on a lent NPC is applied
  on the claimer (`HitRules`: its own actor), and the host's game drops hits on a copy until it took the NPC back.
- **A replay with a partner that is this game's player** waits for the host's answer (`ActionClaims`, by the action's
  tick): claimed, it plays, and a player in its own kill move yields to it; refused, it is never played.
- **Learned on the duo (2026-11-20), each now a GameTests flow that failed first:**
  - *The lent NPC's copy has nothing to replay.* On the claimer the NPC was still a copy when 32803 played its half
    (held off its stream, `HookSendEvent`), so its half never went out as an action; owned now and in an instance, it
    gives no sync point (`GraphStreams::HoldPointsWhile`). The host's copy showed nothing and its `KillActor` never
    came (`client-kill-move`: "[host] wolf's graph sends KillActor" timed out). So a non-claimer game pairs the two
    copies itself (`ReplayState::LentPartner`, `PairsHere`, `ReplayStep::PairHere`), as the host's game did with its
    own NPC before.
  - *Our hook refused that pair.* 32803 plays the partner's half through `Func5` (41557) -> 38949; `HookPerformAction`
    refused every copy's own action but the local player's partner's half, and 32803 then fails (`v12`): "Copy refused
    replayed action 'pa_KillMove1HMWolfA'". A copy's half of a pair replayed here is taken and never sent
    (`ReplayState::CopyTakesAction`; inside `ForceAction`, 38953 is not hooked, so only that half reaches 38949).
  - *The plain swing bypassed our hook.* `ActorMediator::PerformAction` calls the trampoline: the swing was not sent and
    the yield's blow guard (`ClashUntil`) dropped its blow ("14's blow on FF00080D dropped"). `PerformPlainly` enters
    38949 at its own address, as 42350's `Func5` does. Then (`frozen-wolf-kill-move`, 3 of 3): refused `kCannotPair`,
    the yield, `attackStart` taken, its blow forwarded about 430 ms later, the wolf dead.
  - *A short winner's blow came first* (bugs.md's entry, now closed): the guest's refusal at 04.451, the host's
    `ShortD` hit applied at 04.812 as a plain death, the host's pair reaching the guest at 04.895 behind the yield's wait.
    The claim reaches the victim's game ahead of the blow on the same ordered channel, so the blow is held
    (`ActorValueService::RunHeldBlows`, `ActionClaims::Holds`): a kill move's blow is never applied on its victim
    (`SkyrimBridge::DamageActorHealth` refuses it once the kill move holds it); applied only if the pair ended
    without ever playing there. A claim that ended makes a late replay of it skip (`ActionClaims::End`).
  - `StartDeferredKill` 40033 (read): one hold per actor, its scripted flag OR'd, so `EnterKillMove`'s non-scripted
    start never clears the lent NPC's scripted hold: its clip's `KillActor` only counts.
- The kill move is a generic action claim (2026-11-20): its NPC victim a taken member with its death kept for the
  give-back (`ClaimMember::DeathOnGiveBack`), given back by `ClaimService::GiveBack` as a ridden horse is, killed or
  alive. The kill move's own rule (`HostAuthority`): a taken victim can pair (`HostGame::CanPair`).
- Not measured yet: what a host NPC shows when it becomes a copy mid-swing (the scenarios' wolves are restrained or
  frozen). Seen, not read: a frozen wolf (AI off) at -24 health after two plain hits, not dead.

## A victim half cut short (2026-11-28, playtest "the guest's model stuck somewhere he was not")

Crossed kill moves left the winner's copy on the loser's game with its skeleton frozen where the pair was, for good,
while the copy (its reference, capsule and name) walked on with its owner. Scenario `kill-moves-cross-frozen-copy`
(every crossed exchange: 3 of 3 on the duo); GameTests "Crossed kill moves: the winner's copy on the loser's
game walks with its skeleton"; UnitTests `[copy-ragdoll]`.

```
loser's game: its player's own pair on the winner's copy
  the copy plays the victim half: AddRagdollToWorld 42873 (graphs' ragdolls reset, the 3D in the world, layer 8)
    its bones' blend strength 0 in that frame (measured; the writer inside 42873 not read)
    driveToPose 58291, the next frame: bodies not keyframed set dynamic (hkpRigidBody::setMotionType 60908, read)
  the host refuses the loser's claim (kHeld): the yield (EndPairsOf) cuts the victim half short, nobody dies
  out of its kill move, the copy mirrors its owner's layer 0 (SetCopyPhysics -> Actor::SetBodiesLayer: was 36924 only)
    bodies out of the world, still dynamic: nothing moves them
  each node update: bhkBlendCollisionObject::UpdateBoneFromBody 78910, blend 0 -> each bone = its body (78382,
    local too): the skeleton stays at the cut, the COM's local recomputed each frame to keep it there
```

- **Read in IDA.** `bhkBlendCollisionObject` (vtable ID 239339): `SetMotionType` 78912 (slot 0x2E) sets blend strength
  (+0x28) 0 for a dynamic motion type, 1 otherwise; `UpdateBoneFromBody` 78910 (slot 0x2B) blends the bone's world
  between its animation and its body by it, with no test that the body is in a world. `driveToPose` 58291 sets each
  body's motion straight on Havok (60908 at `0x140AC79FE`), not through 78912.
- **Read in IDA and the behaviours (`behavior_dump.py`).** `1hm_behavior`'s `VictimState` leaves on `PairEnd` for
  `1HM_Ready_State` and sends nothing; only `0_master` sends `RemoveRagdollFromWorld`. So a living vanilla victim keeps
  its ragdoll in the world, where `driveToPose` 58291 runs each step and sets each body keyframed or dynamic by its
  bone's track (the 4/1 loop at `0x140AC79AC`). What vanilla does with a victim whose pair is cut was not played. The
  only engine end of a living actor's ragdoll read is `AIProcess::EndKnockState` 39897: every graph's
  `RemoveRagdollFromWorld`, then the 3D keyframed (`SetMotionType` 4).
- **Measured (duo, the trace's `blend` and `body_motion`).** The copy's COM blend 1, body motion 4 until
  `AddRagdollToWorld`; blend 0 in its frame, the first body dynamic (motion 2) the next; then our mirror took the bodies
  out (layer 0) and they stayed dynamic.
- **Measured (duo, harness `skeleton`, `ragdoll`).** The graph's `rootNode` is the drawn 3D, and the four nodes read
  (NPC Root, COM, Pelvis, L Foot) are among its 99 `boneNodes`, which `WritePoseToBoneNodes` 63856 writes;
  world-from-model on the copy. COM's world identical to the decimal across reads while the copy moved 300 units, its
  local changing; the foot's local fixed. Resetting the ragdoll driver (BSIRagdollDriver vfunc 5) changed nothing, and
  63564 said the graph was not eligible for the physics step's list at each read (a sample, not the listener's record:
  physics.md); `reset_3d` (fresh collision objects, blend 1) freed the skeleton at once.
- `StartPair` 32803 also calls `Actor::EnterPairedSim` 32847 on both actors (boolFlags 0x800000, controller `kNoSim`,
  filter bit 0x8000), undone by `Deactivate` 32797: not involved here.
- **Where we diverge.** Two engine removals are read: `EndKnockState` 39897 (a knocked actor's: every graph's
  `RemoveRagdollFromWorld`, then the 3D keyframed) and the graph event's `RemoveRagdollHandler` 42877 (36924, or 77864
  at once, with no motion type change; `0_master` sends it, from which states was not traced). What vanilla does with a
  victim whose pair is cut is inferred, not traced: its `VictimState` exit sends nothing. Our mirror took a living
  copy's dynamic ragdoll out with 36924 alone, where nothing moves it again: that is the bug, wherever the bodies came
  from.
- **Fix.** `Actor::SetBodiesLayer` runs `BodiesLayer::For`'s steps (shared with the fake): out (layer 0), removed then
  keyframed, 39897's order; a layer, keyframed, set, added; bodies out of the world but one still dynamic count as not
  done. So one call at the change, no polling. The copy's bodies stay its own (no mirror) while it lies, gets up (knock
  state 5 or 6: 37491 leaves 6 out of "lying", while 58291 still simulates its limbs), is in its kill move, or is dying
  (`ReplayState::TakesOwnerBodies`): dead, or holding its pose for its owner's death. That last matters: a person's victim clip sends `KillMoveEnd` with
  `KillActor`, so its copy is out of its kill move and alive until its owner's death, and keyframing then made that
  death's fall flicker (`kill-move-kills-guest`, once, under an earlier version of this fix).
- **After** (duo, the shared-steps build): `kill-moves-cross-frozen-copy` 3 of 3 (2 of 2 red, 342 units off, on a build
  whose layer 0 did not keyframe), `kill-move-kills-guest`, `kill-moves-cross`, `knockdown-lies-as-host`,
  `guest-knockdown-lies-on-host` 1 of 1, `furniture-client-player` and `guest-rides-host-horse` 3 of 3 each after one
  guest crash in a graph stream replay (bugs.md, "Seen once in the run of 2026-11-22"; nothing of this change on its
  stack).
- **The race itself works.** Both orders (host first, guest first) ended with one winner and every copy at its owner's
  place once the skeleton is set aside (`kill_move_exchange`, now with `first`): the host's claim wins a simultaneous
  pair (its own claim is in-process), the loser yields and plays the winner's pair.
- **The sword of freezing.** A vanilla "of Freezing" enchantment slows its victim (the guest's `SpeedMult` 100 to 75
  after three hits) and does not paralyze; plain hits with it left the copy on its owner (13 units). Not involved: the
  frozen skeleton needs only the cut-short victim half.
- The playtest's logs (2026-11-26 bug reports) hold no kill move line; the session it happened in was not among them.

## A blow and its clip's end in one update (the frozen victim, 2026-11-22)

A guest's kill move on a person left the victim standing in its clip's last pose for 1.5 s after the blow, on every
game, before it dropped. A second swing in that time started another kill move on it (refused: `kCannotTake`).
Scenario `kill-move-drops-at-blow`; GameTests `[kill-move]` "A kill move whose clip ends in the update of its blow...".

```
guest (claimer): the taken victim's graph update, one update
  clip events PairEnd, KillMoveEnd, KillActor
    KillMoveEnd -> 38614: flag off, QueueKill 37316 (a killer is recorded)
    KillActor   -> 42838: QueueKill 37316 (a task: killQueued +0x331 set), our hook starts the pose hold
  KillMoveService, same frame: the victim out of its pair, its kill not counted yet -> "done, alive"
    -> the give-back waits for the victim's next graph update (ClaimService: its last ops go first)
    -> the pose hold gives its graph no update: 1.5 s (cPoseHoldMs), then the give-back, "killed" by then
  the task queue, a frame later: KillImpl 37896 clears killQueued, then counts the held kill (DeferKill 38617)
```

- **Read in IDA.** `Actor::QueueKill` 37316 only records the killer and queues task 10 unless a kill is queued
  (`ActorProcess::IsKillQueued` 40011, MiddleHigh `+0x331`; CommonLib `killQueued`). `KillImpl` 37896 clears that
  byte first (`ActorProcess::SetKillQueued` 40012, our name), then tests the deferred kill and counts. So a clip's
  kill is counted when the task runs. `QueueKill` goes through `TaskQueueInterface::QueueKillActor` 36911 to
  `TaskQueueInterface::Push` 36993, which runs the task inline when the task pool is off (`gEnableTaskPoolThread`)
  or its queue refuses it: later than the graph update is what the duo showed (below), not what the code forces.
- **Measured (duo, `kill_move_on`, a bandit `3DE53`).** Every person's victim clip sent the three events in one
  update, 12 of 12 (`KillMoveI`, `D`, `ShortA`-`D`, `1HMBackStab`, `1HMGrappleStab`, `1HMStabDownChest`, `2HM3Slash`,
  `2HWChopKick`, `H2HBodySlam`); a wolf's (`KillMove1HMWolfA/B`) sends `KillActor` first and ends updates later,
  which is why the wolf scenarios never showed it. `pa_KillMoveI`: guest `KillActor` at 2.38 s, the host's take-back
  1.52 s after it (`HostClaims: 5 back from claim 1, killed`), the second `pa_KillMoveI` 20 ms after that.
- **Fix.** A taken victim that left its pair is not done while its kill is queued (`ReplayState::PairDone`,
  `IActorBridge::ActorKillQueued`): a frame later it is counted and given back killed, at once. After: the host's
  victim dies 0.01 to 0.07 s after its own clip's `KillActor`, 21 of 21 kill moves.

## The claimer's victim waits for the host's word

Its death is the host's (above), so on the claimer's game the victim holds its pose from its clip's `KillActor`
until the host's death action comes back. Measured after the fix above, guest `KillActor` to the guest's death: 0.27
to 0.44 s, and 0.69 and 0.93 s (`H2HBodySlam`, `1HMGrappleStab`). Two parts:

- The host kills at its own replay's `KillActor` (`HostTakeBacks`), 0.11 to 0.34 s behind the guest's.
- Then the guest, a copy again, took the death 0.18 to 0.52 s later. Inferred from its log, not traced: at its
  playback time, behind what its queue held (the host's half of the pair first, `paired action ... not played on
  14`), on a playback clock with no samples of this actor through the claim (`1HMGrappleStab`: host death at 4788 ms, guest's at 5313).

A copy holding its pose is ahead of its owner's timeline (its own clip reached the blow first), so its death is
overdue when heard: it is taken at once, the actions queued before it dropped (`ReplayState::TakeOverdueDeath`,
`AnimationSystem::Update`). After: 0.08 to 0.35 s on the guest (26 kill moves; 0.12 to 0.21 in the first six, 10 to 40 ms after the host's death). What
is left is the host's replay lag; killing on the claimer first would start its ragdoll before its owner's
(`docs/bugs.md`, "A claimed kill's victim holds its pose for the host's replay").

## Every kill move (2026-11-23, `kill-moves-every`)

The kill moves are idles (IDLE records) in the action idle tree, not engine code: a normal swing's pick walks
`AttackRightRoot > NonMountedCombatRight` (a power attack `PowerAttackRoot > NonMountedCombatRightPower`, the same
kill moves as twins without the `00` suffix), children in their `ANAM` sibling order, the first whose conditions
pass. `scripts/re/kill_moves.py <Data>` lists every one (145 `pa_Kill...` events in the base game and DLC, 344 idles
naming one); `--rows` walks the tree's conditions per victim race, weapon and side and writes the table
`scripts/coop_harness/kill_moves.json`: 112 rows, 84 kill moves a player's normal swing can pick. The 84 are
candidates: the walk's first-passing-child order and its fallback to the next sibling are inferred from the records, not
read at `TESIdleForm` 24570 (the pick, 37147 -> 24602 -> 24570, traced in `agent-piece-4b.tsv`).

```
NonMountedCombatRight (children in order)
  sprint attacks
  KillingMoveSneakBackA00   sneaking, undetected
  KillMoveFrontSideRoot00   GetRelativeAngle 90..270, GetTargetHeight -35..35, ShouldAttackKill (38559), KillMove global,
                            the player only on target.IsLastHostileActor, weapon drawn, not blocking or staggered
    <race roots>            target.GetIsRace (draugr, falmer, wolf, troll, sabre cat, bear, spider, spriggan, hagraven,
                            giant, centurion, dragon, DLC races), then by GetEquippedItemType
    KillMoveShortRoot00     target ActorTypeNPC: by weapon (blade, axe/mace, 2HM, 2HW, H2H, dual wield, decap perks)
  KillMoveBackSideRoot00    target ActorTypeNPC, GetTargetHeight -35..35: back stabs, 2HM/2HW from behind, suplex
  plain attacks
```

- Siblings carry `GetRandomPercent`: a row's swing picks one of its kill moves at random, so `kill_move_table` swings a
  row until each played (two tries per kill move). A group whose children all fail is taken to pass on to its next
  sibling: inferred from the tree's shape, the pick (37147) not read for it.
- The decapitations need `SavageStrike` (1H) or `DevastatingBlow` (2H): the rows with perks add them.
- Not in the table: dual wield (the verb equips one hand), bleeding-out victims, sneak kills, power attacks (the same
  kill moves), creature and NPC attackers (`KillMoveRootDragon` and the creatures' own trees), dragons as victims (`KillMoveDragonRoot00`:
  `pa_KillMove2HWDragonRodeoSlash` and others; the harness cannot stage a dragon).

## Slopes (2026-11-23)

Not settled. Playtest item 16. What limits a kill move here may be the idles' height window, the pair's start, or both.

- **Read in IDA.** `GetTargetHeight` 21551 (condition 720): `param.z - subject.z`, 0 when either is null. Every kill
  move root needs -35..35 (the suplex -20..20), and the records name no reference (CTDA parameter 1 is 0). Not read:
  what the condition item passes for an empty reference, so whether the window acts at all.
- **Read in IDA.** `LayoutParticipants` 32792 gives a support's mark the lead's height; `TryMoveTo` 78260 sweeps it
  there, `MoveCheckCountsLayer` 78300 picking layers (`0x01010000`: terrain, static, ground). What a contact's value
  means to its depth test is not read.
- **Measured** (duo, Riverwood's hillside, 24 degrees, a restrained bandit, a sword). The attacker moved to the victim's
  height: 25 below, raised 34; above, lowered 17 to 28 (so here the victim leads). Attacker above its victim by 18.4:
  no kill move; 23.2, 24.4, 24.5, 25.6, 40.2 and one unread: paired; 35.3, 35.6 and 43: none. The height is read before the
  swing. 40.2 pairing contradicts a strict window.
- The refused swings at 18.4 and 35.6 logged `PairEnd`/`pairedStop` inputs before the plain `attackStart`, those at 35.3
  and 43 none. Inferred: a pair begun and refused in the first two, never picked in the others. Which check refused is not read.

## By weapon (measured 2026-11-22, `kill-moves-by-weapon`)

A restrained bandit (`3DE53`) or wolf, one hit from death, `KillMoveRandom` 100. Each paired on both games, the
victim dead on both, nobody left in the kill move, the attacker walking after. The bodies at rest: alike in 10 of
14 rows, then 13 of 14 (`docs/bugs.md`, "A kill move's dead copy awake or lying apart").

- Guest: sword `KillMoveI`, `KillMoveD`, `ShortD`, `1HMStabDownChest`; from behind `1HMBackStab`; dagger
  `1HMGrappleStab`, `1HMStabUpFace`; greatsword `2HM3Slash`; battleaxe `2HWChopKick`; warhammer `2HWB`; mace and
  war axe `ShortC`; unarmed `H2HBodySlam`; a wolf `1HMWolfB`.
- Host: sword `ShortB`; mace `ShortA`; greatsword `2HMStabA`; unarmed `H2HSlamA`; a wolf `1HMWolfA`.
- Lydia (a follower) gets no kill move: `kill_move_check` says no, and a plain swing at 3 health does not kill her.

`KillImpl` 37896 with a ragdoll (read 2026-11-22): any graph's driver holding a ragdoll instance
(`IAnimationGraphManagerHolder::HasRagdoll` 38085, vfunc 1) means no animated death: every graph's ragdoll into the
world, motion type dynamic, then the death action again with type 2 (flags2 `0x20000000`; cleared if refused), or
`Character::PlayRagdollInstant` 37186 when told to ragdoll at once. An actor already knocked (states 1-5, 7) skips
that part. It also clears the kill move flag (flags2 `0x4000`) itself: a dead actor is in no kill move.

## The path (before claims: what each game did, kept for the player-victim half)

```
attacker's game                            victim's game
attack press: the idle tree picks
pa_KillMove... (ShouldAttackKill 38559)
PerformAction 38949
  -> 32803 starts the pair:
     32782 add each half
     32792 lay out, sweep support to mark
     Func5 plays the victim's half
         on the victim's copy
  action sent, with its target ----------> copy of the attacker replays pa_...
                                           (AnimationSystem::Update, game thread)
                                           -> 32803: the real victim plays its half
HitFrame on the attacker: a forwarded ---> DamageActorHealth: refused while the
hit                                        kill move holds the victim
                                           2_KillActor on the victim's clip:
                                           real death, credited to its partner
copy dies with its owner's <-------------- death action
death action
```

- Both directions work the same; an NPC victim's owner is always the host.
- A copy's half counts as at its mark (`HookUpdateSynchronizedClip`): its owner's game placed it.
- The victim's own half comes back to the attacker's game as an action. That copy is already in the pair: skipped.
- A replayed pair whose partner is a copy names no target event (`PlayReplayedAction`): 32803 makes no instance there,
  and each copy plays its half alone, the victim's by its owner's action (the half `TESActionData::Func5` played on the
  owner's game, sent as the victim's own action). Only the attacker's and the victim's own games hold an instance.
- An owner in an instance is recorded on but gives no sync point (`GraphStreams::HoldPointsWhile`): its synchronized
  clip waits for the mark on its instance (about 150 ms on the host, `KillMove1HMWolfA/B`, 2026-11-09), and its copies'
  clips take that wait as a pick (`HookUpdateSynchronizedClip`). A copy leaves the stream at the half's action only when
  its partner here is this game's own actor, and is back from a sync point after the pair (graph-input-stream.md, The
  one exception). Until 2026-11-11 every owner in an instance was off the stream.
- The instance ends at the victim's `PairEnd`; the attacker stays in its kill move (`IsInKillMove`, its clip running)
  until its own `KillMoveEnd`: 1820 against 3112 ms after the swing for `pa_KillMove1HMWolfB` (2026-11-10). A sync
  point restored inside a synchronized clip starts the clip over, so the owner gives none until it has left the kill
  move.
- A copy in a pair takes no plain action of its owner.

## Engine facts

- **A synchronized clip's scene (read 2026-11-11).** `BSSynchronizedClipGenerator::Update` (63050) asks its scene
  (`+0x70`) each update: slot 4 (`+0x20`) "every actor at its mark" into `+0x12C`, slot 3 (`+0x18`) "all in scene" into
  `+0x12B`; the inner clip updates only while `+0x12C` is set. `Activate` (63048) gives a generator with no scene a
  `hkbDefaultSynchronizedScene` (vtable `0x141a02da0`), whose two answers are always yes: a clip outside a pair's
  instance runs at once. These answers are a copy's pick on the graph input stream (graph-input-stream.md).
- **The kill move flag comes from the action, not the graph.** 39004 (the action's effects beside the graph), for an
  event starting "pa_Kill" (strnicmp 7) with an actor target: attack state 1, then `EnterKillMove` (38613) on the actor
  (no killer) and on the target (killer: the actor). 38613: a deferred kill, the flag (`Actor+0x204` 0x4000), the
  killer's handle.

- **32803 (the pair's start).** If either actor is already in an instance, it does not start one: when both are in the
  same instance it returns true, as if this were that pair. Replaying a second pair there plays a second clip into the
  first, which ends both clips.
- **32782** refuses an actor in ragdoll, knocked down, or a player under VATS slowdown, and one whose graph has no such
  synchronized clip.
- **32792** sweeps each supporting actor to its mark (`bhkCharacterController::TryMoveTo` 78260, a linear cast) and
  refuses the pair if one is blocked. Each game judges this on its own positions. A refused pair falls to the next idle.
- **38614** (`KillMoveEndHandler` 42871 calls it) clears the kill move flag (`flags2` 0x4000). If the actor has a recorded
  killer it queues the kill too. Nothing else clears the flag soon: without the `KillMoveEnd` event it stayed set for 10 s.
- **`2_` triggers** on a paired clip go to the partner's graph.
- **Why a copy never dies by its clip (read 2026-11-19, for piece 4b).** Every copy holds a *scripted* deferred kill
  (`UpdateCopyAI` each frame). `KillImpl` (37896) checks a deferred kill first and only counts the kill
  (`Actor::DeferKill` 38617, MiddleHigh `+0x2F8` += 1). `KillActorHandler` (42838) ends only a non-scripted one:
  `ActorProcess::EndDeferredKill` (40036) acts only when its argument matches the scripted flag (`+0x332`). Ending a
  scripted one later (`EndDeferredKill(true)`) queues the counted kill credited to `g_thePlayer`, so a game that ends it
  must kill with the real killer right after. A killer with `+0xE8` 0x4000000 is credited to `g_thePlayer` too
  (37896, inferred: the player's team; not read further).

## Names given in IDA (1.7.104)

`BGSSynchronizedAnimationManager::StartPair` 32803, `::EndPairsOf` 32809, `::IsInInstance`, `::GetParticipants`,
`g_BGSSynchronizedAnimationManager` 401100; `BGSSynchronizedAnimationInstance::AddParticipant` 32782, `::LayoutParticipants` 32792,
`::AllAtMark` 32794, `::Activate` 32796, `::Deactivate` 32797 (ends the instance once any half deactivates),
`::BindGenerators` 32783, `::HasParticipant` 32784; `BSSynchronizedClipGenerator::Update` 63050, `::Activate` 63048,
`::RemapClipEvents` 63060 (`2_` events to the partner), `::GetClipRootTransform` 63058;
`BSRagdollContactListenerModifier::contactPointCallback` 63092, `::Modify` 63091, `::AddListeners` 63093, `::Activate` 63088,
`::Deactivate` 63089;
`Character::EndKillMove` 38614. Each has a comment in the database. Added 2026-11-28: `Actor::EnterPairedSim` 32847,
`bhkBlendCollisionObject::SetMotionType` 78912, `::UpdateBoneFromBody` 78910, `BShkbAnimationGraph::WritePoseToBoneNodes`. Added 2026-11-22: `ActorProcess::IsKillQueued` 40011,
`ActorProcess::SetKillQueued` 40012, `Actor::DeferKill` 38617, `Character::PlayRagdollInstant` 37186,
`IAnimationGraphManagerHolder::HasRagdoll` 38085, `::SetRagdollMotionType` 38084, and their visitors.

## The clip's own timing

`KillActor` comes from the victim's clip (`1hm_behavior.hkx`). Read with the behaviour files' `hkbClipTriggerArray`
(`hkbClipGenerator` 0x50, `hkbClipTrigger` 0x20 long: time 0, event id 8):

- `Paired_2HMKillMoveSlash` 0.339 s, `Paired_1HMKillMoveShortC` 0.372 s, `Paired_1HMKillMove_Short_A` 0.933 s,
  `Paired_1HMKillMoveDecapBleedOut` 0.967 s
- most others 1.2 to 3.1 s (`BackStab` 2.26 s, `GrappleStab` 3.07 s)

So `2HMSlash` dropping its victim at about 0.46 s from the action is the clip (the pair starts about 0.12 s in). A kill cam
can slow the clock, so wall times can run longer than the clip's. An early death on a long clip is a bug: see below.

## What broke kill moves

Four faults, all from each game running its half of one pair. The first three are fixed by `ReplayState` rules, used by
`AnimationSystem` and the fake's replay; the fourth by the hook on 78260.

**1. The pair's other half came back.** The victim's game sends its half as an action, and the attacker's game replayed it
on a copy already in that pair: by 32803 a second pair on two paired actors looks accepted and its graph event still goes out.
`SettlePairedReplay` now skips it. Earlier this was blamed for victims dying early; that was wrong (next section).

**2. A copy's owner moved as the pair began.** The owner's `moveStart`, sent before its game heard of the pair, took the
copy out of the clip. `PairEnd` came at 0.28 s, the blow never landed, and with no `KillMoveEnd` both actors stayed flagged
for 10 s. `HeldByPair` keeps a copy in a pair from taking a plain action, checked where the action plays
(`PlayReplayedAction`): one staged before the pair began gets there too.

**3. Two players on each other.** Each started a pair on its own game, then each game replayed the other's onto the same
two actors. By 32803 that is "the same pair": both clips ended and a player dropped with a plain death. Now the host's first claim wins (`HostPairs`; until 2026-11-20 both games compared the two swings' ticks): the loser's
claim is refused and its game yields, then plays the winner's pair as its victim (`SettlePairedReplay`).

Yielding has to stop a playing pair, which took four things, each measured:

- Clearing the flags (38614) is not enough: the clips run on and the first blow lands as a plain hit (PR #15 found this).
- `EndPairsOf` 32809 ends the instance, as the engine does when graphs change (`Actor::EndPairs`). `Deactivate` sends each
  half `PairEnd`, and `PairedStop` only if that went unanswered. The victim's state leaves on `PairEnd`; the attacker's
  `KillMoveState` only on `pairedStop`, so that is sent too.
- Entry transitions to `pa_KillMoveI` and `pa_KillMoveD` carry flags 0xd04: `FLAG_UNINTERRUPTIBLE_WHILE_PLAYING`. A
  `pairedStop` during that blend is dropped, so it is sent again for up to 30 updates while that clip plays
  (`ResendPairStop`), and the winner's pair waits for the local player's clip to leave. There is no engine event for a blend ending.
- The loser's own clip can still land its blow (`ShortD` at 0.34 s). Its game drops that blow at the source (`CancelledBlow`,
  `HookDamageActor`) until its player starts a new attack (`StartsAttack`).
- The loser's own kill cam ends at the yield (`VATS::End`, `SetMode` 44282). Left running, its end at 1.24 s cut the winner's
  `KillMoveI` clip short, 2 of 2 rounds, and nobody died. Why only that clip was not settled: the graph path was not reversed.

**4. The victim's game refused the pair.** 32792's sweep runs on each game's own positions, so it could fail on the victim's
after passing on the attacker's: the attacker's kill move ran 5 s alone and the victim fell to the plain forwarded blow at
1.4 s. 4 of 10 rounds in one layout (victim facing east, attacker to its south-west). Now a replayed pair passes the sweep
(`PairMarkClear`, `HookTryMoveTo`): the attacker's game judged it. Same layout: 4 sweeps blocked, 0 refused, 10 of 10 halves
played. An early counter read 0 here only because those rounds never used this layout.

## Early deaths: the victim touched a wall

A victim dying 0.4 to 1.4 s into a 2.4 s backstab is not its clip. Only two things send `KillActor`: the paired clips'
`2_KillActor` triggers and one node, `VictimState_RagdollListener` in `1hm_behavior.hkx` (a
`BSRagdollContactListenerModifier`: event 0x4A, ragdoll bones 0, 3, 6, 9). While the victim state is active its keyframed
ragdoll reports contacts (collidable quality 9), and 63092 fires on any penetrating contact with a body it does not watch.
The victim dies on the spot, crediting its partner (`KillActorHandler` 42838).

Measured with a contact hook: the guest's player touched static `402152B` 15 ms before its `KillActor` at 0.71 s, with
the clip at its normal time. A later death at 0.36 s had the same signature: `KillActor` alone, no `PairEnd`, the clip near
0.24 s against its trigger at 2.26 s, the victim beside that wall. Rare: 1 in 40 or so rounds on this stage.

Vanilla checks room for the supporting actor only. Across games the attacker's game judges the pair on its positions, and
the clip then carries the victim from wherever its own game has it. So with a copy in the pair the contact kill is held
back (`ContactKills`, `HookContactPoint`) and the clip's own `KillActor` kills it. Pairs of two actors of one game stay vanilla.
A real NPC standing at that wall got no kill move at all (10 of 10 normal swings): the room check refused the pair first.

## A copy's death at the end of its clip

The copy of a victim fired its clip's `PairEnd` and `KillActor` like its owner, but does not die from them (scripted deferred
kill): it waits for its owner's death action, about 115 ms later. Traced on `kill-move-kills-guest`:

1. `PairEnd` took its graph out of the victim state toward `1HM_PickIdle`: it blended to standing, pelvis rising, a body 10 units off.
2. The death action started its ragdoll from there: its reference jumped 51 units.
3. The next frame's owner data pulled every body back, up to 65 units: the flickers, on the copy only.

Now it holds that pose: `KillActorHandler` 42838 starts the hold, `HookUpdateAnimationGraphs` gives its graph no update until
the death action (or 1.5 s, `cPoseHoldMs`). Nothing drives a ragdoll with no graph update, and limbs free-fell 45 units in the
hold, so its bodies' velocities are zeroed each update (`HoldCopyRagdollStill`). Before: 0 of 3, up to 81-unit flickers.
After: 6 of 6, 0 flickers, no bone over 25 units off. Its first frames were then steered body by body unless 16+ units
off (`PlaceDistance`, replaced 2026-11-23: next section). This also explains the note in `progress.md` about a wolf's copy holding its pose after its blow:
that hold is now deliberate.

Its ragdoll is its own through the clip (2026-11-18): the victim state's `AddRagdollToWorld` puts it in the world on
the copy before its owner's stream does, and mirroring the owner's body layer had taken it out again until the death
(`docs/re/systems/physics.md`, "A kill move's victim copy keeps its ragdoll").

## A copy's ragdoll starts whole (2026-11-23)

The host's battleaxe kill moves seen on the guest (10 falls): up to 32 flickers, 4 to 23 units of pelvis jump (the host's
own screen 0.4 to 4.6). The log showed the first frames split the ragdoll: limbs 16+ units off were placed (`PlaceDistance`),
the rest steered, and (inferred: the trace shows the mixed placements and a burst in the same falls) the joints between
them threw the bodies about. That showed in 3 of 6 falls, all HeadButts, each at
its burst.

`RagdollStart` (`Code/sync/physics/RagdollStart.h`) plans the whole ragdoll at once: each body's gap at its first frame is
blended out over 0.2 s, and one body 64+ units off (`BodyTolerances::kPlaceFar`) puts them all on their frames. After: 0
flickers in 9 of 9 falls, and in 16 more on the authority scheme's base. Tests: GameTests and UnitTests `[ragdoll-start]`.

Not fixed by it: the host's HeadButts on the guest still jump about 18 units (4 of 5, 2026-11-23). The trace: every body of
the copy's ragdoll drops about 22 units in one frame at its death (the host's rises), while its frame is 100 ms behind;
`RagdollStart` then takes that as its gap and blends it out. Nothing of ours logged a placement there, and `KillImpl` 37896's
ragdoll part sets no body's position (`SetRagdollMotionType`: 63627 skips the ragdoll's own bodies; vfunc 12, 63631, sets its
constraints' motors). A probe around 58291 (duo, 2 HeadButt-free rounds of 5 with a death jump) showed the move inside
the engine's own `hkbRagdollDriver::driveToPose` on the copy's first update after its death: body 0 moved 10 and 9.6 units
in that one call, our steer after it nothing. Its callees read so far set no position (IDA comments at 58291); whether its
keyframed-to-dynamic `setMotionType` puts a body back to its swept transform's start is not read yet.
Left as it is (the user, 2026-11-23): watched in play, the kill moves look right; the death's drop is the engine's.

## `client-kill-move` timing

Misses were 2 in about 10 runs (blow 1737 ms against 2143, start lag 611 ms against 600). The blow miss fits an early contact
`KillActor` on one game, which the contact rule removed, but it was not traced. Since that rule: 11 of 11 `client-kill-move` runs within limits,
blows about 40 ms apart, lag about 160 ms. The lag miss by 11 ms was during a loaded batch and is unexplained.

## Attacker's own refusals

The sweep still judges the attacker's own pair, as in vanilla: with no room at the mark, the engine tries another idle and
then swings normally. Measured: 3 of 13 sweeps failed in the layout above, 2 of 12 rounds ended in a normal swing. Likely the
missed kill moves of S8. Sneak kill moves were refused only because the test's mark sat in a wall: with room behind the
victim, `KillMoveSneakBackA` played 4 of 4.

By weapon, on a player: sword, dagger, greatsword and battleaxe pair; unarmed plays `pa_KillMoveH2HSuplex` 3 of 3; a mace from
the front plays `ShortA` and `ShortC`. A mace or war axe from behind picks `pa_KillingBlow`, which the engine refuses, and
swings normally (6 of 6). A real NPC gets the normal swing too (mace, 4 of 4, where a sword paired 7 of 7), so this does not
come from copies. Bows were not tried.

Not settled: whether a copy's capsule blocks a sweep a real NPC's would not. A real NPC in open floor paired 7 of 7, but the
same-spot control never started a kill move, so it says nothing.

## A partner whose graph stops

Read in IDA and measured on the duo, 2026-11-18 (`frozen-wolf-kill-move`). A guest's kill move on a host NPC whose AI is
off (`kProcessMe` clear: `Actor::EnableAI(false)` 37504, a quest script's `EnableAI`, the harness's `freeze`) stuck: the
NPC stayed in its kill move and refused every forwarded hit after it (`IsHeldByKillMove`) for over 50 s.

```
guest: its copy of the NPC (processed, copies are) takes the victim's half: pair starts, sent
host:  the guest's copy replays pa_KillMove... with the host's own NPC as partner (ForceAction)
       StartPair 32803: the victim's half, an unforced action (Func5 41557), taken
       39004 with started: EnterKillMove 38613 on both (flag +0x204 0x4000, a deferred kill)
       the NPC's graph never updates (kProcessMe gates the graph job, 41466) -> no KillMoveEnd 38614 -> flag stays
```

- Read: `PerformAction` 38949 tests the graph holder (32877) first, forced or not; action flag 2 (`ForceAction`) then
  skips the selection checks, 39002 (`coop_ActionSelectionChecks`: ragdoll, already in an instance, per-action state)
  and `coop_ActionAttackStaminaCheck` 39003. None of them reads a partner, and the victim's half is a new unforced action either
  way, so forcing is not what let the pair start.
- Read: 39004 sets attack state and the `pa_Kill` kill move flags only for a started action (its third argument); its
  graph-variable writes run either way.
- Measured: the host's own pick on a frozen wolf was refused (the wolf's half `KillMove1HMWolfB` first, then the player's
  `pa_KillMove1HMWolfB`; the player swung normally), while the replayed pair on another frozen wolf was taken. What
  refused the wolf's half in the first case is not read.
- Measured: an unfrozen wolf under the same blow: the pair ran and the wolf died.
- So the fix rests on the part read: a pair whose partner's graph does not run can never reach `KillMoveEnd`. The host
  refuses the claim on an NPC without `kProcessMe` (`HostGame::CanPair`, `kCannotPair`; until 2026-11-20 the host's game
  skipped the replay, `PartnerAnimates`): no game plays the pair on it, and the guest swings plainly.

## The `host-kill-move` timeout

The scenario, not a kill move: the wolf is unrestrained and fighting, and in the captured failure it stood 113 units away and
about 60 degrees off the player's facing, so each swing missed. The scenario now waits for `kill_move_check`, as
`client-kill-move` does.

## Open

- **`ai_state.head_track`.** `host-kill-move`, `client-kill-move`, `spectator-watches-kill-move` and sometimes
  `kill-moves-cross` stop at the end comparison on a copy looking at a player: `bugs.md`, "Headtracking left over", deferred
  past v1.
- **A dead copy's bodies never sleep:** `docs/bugs.md`, "A dead copy's ragdoll never sleeps".
