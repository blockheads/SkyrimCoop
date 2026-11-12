# Kill moves, end to end

Reviewed and reproduced 2026-11-04 on 1.7.104. Scenarios `kill-move-runs-full-length`, `kill-moves-cross`; GameTests
`[kill-move]`, ClientTests `[replay-state]`.

## The path

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

## Names given in IDA (1.7.104)

`BGSSynchronizedAnimationManager::StartPair` 32803, `::EndPairsOf` 32809, `::IsInInstance`, `::GetParticipants`,
`g_BGSSynchronizedAnimationManager` 401100; `BGSSynchronizedAnimationInstance::AddParticipant` 32782, `::LayoutParticipants` 32792,
`::AllAtMark` 32794, `::Activate` 32796, `::Deactivate` 32797 (ends the instance once any half deactivates),
`::BindGenerators` 32783, `::HasParticipant` 32784; `BSSynchronizedClipGenerator::Update` 63050, `::Activate` 63048,
`::RemapClipEvents` 63060 (`2_` events to the partner), `::GetClipRootTransform` 63058;
`BSRagdollContactListenerModifier::contactPointCallback` 63092, `::Modify` 63091, `::AddListeners` 63093, `::Activate` 63088,
`::Deactivate` 63089;
`Character::EndKillMove` 38614. Each has a comment in the database.

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
two actors. By 32803 that is "the same pair": both clips ended and a player dropped with a plain death. Now the first swing
wins (`SettlePairedReplay`). Both games hold the same two ticks, a tie going to the lower session id, so they agree with no
message: the winner's game skips the later pair; the loser's game yields, then plays the winner's pair as its victim.

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

`ActorExtension::LatestPairAction` keeps the pair: `LatestAnimation` is overwritten by the next `moveStop`.

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
hold, so its bodies' velocities are zeroed each update (`HoldCopyRagdollStill`). Its first ragdoll frame steers instead of
teleporting unless 16+ units off (`PlaceDistance`). Before: 0 of 3, up to 81-unit flickers. After: 6 of 6, 0 flickers, no
bone over 25 units off. This also explains the note in `progress.md` about a wolf's copy holding its pose after its blow:
that hold is now deliberate.

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

## The `host-kill-move` timeout

The scenario, not a kill move: the wolf is unrestrained and fighting, and in the captured failure it stood 113 units away and
about 60 degrees off the player's facing, so each swing missed. The scenario now waits for `kill_move_check`, as
`client-kill-move` does.

## Open

- **`ai_state.head_track`.** `host-kill-move`, `client-kill-move`, `spectator-watches-kill-move` and sometimes
  `kill-moves-cross` stop at the end comparison on a copy looking at a player: `bugs.md`, "Headtracking left over", deferred
  past v1.
- **A dead copy's bodies never sleep:** `docs/bugs.md`, "A dead copy's ragdoll never sleeps".
