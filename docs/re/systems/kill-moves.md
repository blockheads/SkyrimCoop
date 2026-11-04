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
- A copy in a pair takes no plain action of its owner.

## Engine facts

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
on a copy already in that pair. That fired stray `KillActor` events through the clip, and a victim could die at 0.43, 0.99
or 1.38 s of a 2.4 s `BackStab`, the "instant" kill move with one attacker. Measured on one setup (attacker running at the
victim's back): 8 of 8 clean with the rule, early or stray in most rounds with it off. `SettlePairedReplay` now skips it.

**2. A copy's owner moved as the pair began.** The owner's `moveStart`, sent before its game heard of the pair, took the
copy out of the clip. `PairEnd` came at 0.28 s, the blow never landed, and with no `KillMoveEnd` both actors stayed flagged
for 10 s. `HeldByPair` keeps a copy in a pair from taking a plain action, checked where the action plays
(`PlayReplayedAction`): one staged before the pair began gets there too.

**3. Two players on each other.** Each started a pair on its own game, then each game replayed the other's onto the same
two actors. By 32803 that is "the same pair": both clips ended at 0.32 s and a player dropped with a plain `DeathAnim` at
0.35 s. Now they clash (`SettlePairedReplay`): both end their kill move (38614), nobody is hurt, both can swing again. It is
symmetric on purpose: no game has to know who swung first. `ActorExtension::LatestPairAction` keeps the pair, because
`LatestAnimation` is overwritten by the next `moveStop`.

**4. The victim's game refused the pair.** 32792's sweep runs on each game's own positions, so it could fail on the victim's
after passing on the attacker's: the attacker's kill move ran 5 s alone and the victim fell to the plain forwarded blow at
1.4 s. 4 of 10 rounds in one layout (victim facing east, attacker to its south-west). Now a replayed pair passes the sweep
(`PairMarkClear`, `HookTryMoveTo`): the attacker's game judged it. Same layout: 4 sweeps blocked, 0 refused, 10 of 10 halves
played. An early counter read 0 here only because those rounds never used this layout.

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
- **`kill-move-kills-guest`** fails on the host's copy of the guest: its body never comes to rest (`turned back`). 3 of 3
  with these rules and 3 of 3 with them off, so older than this work.
- **Clip loads (PR #12, not merged).** Copies may wait for an action's clips; check the paired branch against that wait.
