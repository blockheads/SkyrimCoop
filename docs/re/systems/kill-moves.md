# Kill moves, end to end

Reviewed and reproduced 2026-11-04 on 1.7.104. Scenario `kill-moves-cross`, GameTests `[kill-move]`, ClientTests
`[replay-state]`.

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
- The victim's own half comes back to the attacker's game as an action. That copy is already in the pair.

## Engine facts

- **32803 (the pair's start).** If either actor is already in an instance, it does not start one: when both are in the
  same instance it returns true, as if this were that pair. Replaying a second pair there plays a second clip into the
  first, which ends both clips.
- **32782** refuses an actor in ragdoll, knocked down, or a player under VATS slowdown, and one whose graph has no such
  synchronized clip.
- **32792** sweeps each supporting actor to its mark (`bhkCharacterController::TryMoveTo` 78260, a linear cast) and
  refuses the pair if one is blocked. Each game judges this on its own positions.
- **38614** (`KillMoveEndHandler` 42871 calls it) clears the kill move flag (`flags2` 0x4000). If the actor has a recorded
  killer it queues the kill too. Nothing else clears the flag soon: without the `KillMoveEnd` event it stayed set for 10 s.
- **`2_` triggers** on a paired clip go to the partner's graph.

## The clip's own timing

`KillActor` comes from the victim's clip (`1hm_behavior.hkx`), so how long a kill move looks is the clip's. Read with the
behaviour files' `hkbClipTriggerArray` (`hkbClipGenerator` 0x50, `hkbClipTrigger` 0x20 long: time 0, event id 8):

- `Paired_2HMKillMoveSlash` 0.339 s, `Paired_1HMKillMoveShortC` 0.372 s, `Paired_1HMKillMove_Short_A` 0.933 s,
  `Paired_1HMKillMoveDecapBleedOut` 0.967 s
- most others 1.2 to 3.1 s (`BackStab` 2.26 s, `GrappleStab` 3.07 s)

So a victim dropping 0.35 s in is vanilla for those clips. Measured: every `2HMSlash` died at 0.46 to 0.49 s from the
action (the pair starts about 0.12 s in). A kill cam can slow the clock, so wall times run longer than the clip's.

`pa_KillMove1HMBackStab` rounds died at about 0.45 s, 0.94 s, 1.3 s and 2.4 to 3.8 s. Those times suggest the graph picks
among several clips, but the run did not record which clip played, so that is not proven. The games may pick differently
(`bugs.md`, "Idle variants differ between games"): extra `KillActor` events showed on a copy while its owner's game had
none. A copy ignores them (deferred kill).

## The bug: kill moves on each other

Two players swinging a killing blow within the network delay each start a pair on their own game, then each game
replayed the other's pair onto the same two actors. By 32803 that was "the same pair": both clips ended at 0.32 s with
`HitFrame` and `KillMoveEnd` together, and a player dropped with a plain `DeathAnim` at 0.35 s (2 of 4 mutual rounds).
That is the "kill move played instantly".

Skipping the replay alone is not enough: each copy goes on following its owner, leaves the pair, the clip ends at 0.25 s,
and with no `KillMoveEnd` both players stay flagged for 10 s.

Now a replayed pair on a partner already in a kill move does not play (`ReplayState::SettlePairedReplay`, called
by `AnimationSystem::Update` and the fake's replay):

- **Clash:** the partner's own pair is with this copy, and this is not that pair's other half. Both end their kill move
  (38614), nobody is hurt, and both can swing again. No blow lands: `HitFrame` comes later than the clips end.
- **Skip:** the pair's other half came back, or the partner is paired with someone else. Nothing changes. These were the
  "kill-move refusals" in the logs (`bugs.md`, Copy visuals/state).

`ActorExtension::LatestPairAction` keeps the pair: `LatestAnimation` is overwritten by the next `moveStop`.

A clash is symmetric on purpose: no game has to know who swung first, and no message is added.

## Found, not fixed

- **A victim's game can refuse the pair.** Once in about 50 rounds (both moving, the first round of the session after a
  kill) the guest logged `refused replayed action 'pa_KillMove1HMBackStab'`. The host's kill move ran 5 s alone while the
  guest fell to the plain forwarded `HitFrame` at 1.4 s. The cause is not known: a blocked mark sweep fits (the copy stood
  30 units and 66 degrees off the owner), but a temporary counter on `TryMoveTo` saw 0 replayed blocks in the ~30 rounds
  after it. Forcing the sweep was dropped as a guess. Next: read which of 32782 or 32792 refused when it happens again.
- **Missed kill moves (S8, likely).** The attacker's own pair can be refused by 32792: the same counter saw 2 blocked
  sweeps in the attacker's action, and the log shows `Owner 14 action ... 'pa_...' refused by the engine`, then a normal
  swing. `kill_move_check` read `should_attack_kill` true in those rounds. This is vanilla's check (no room at the mark).
- **`host-kill-move` timing out at `kill_move is True`.** Seen once in 5 runs on this build, and in the note from the
  dialogue-anim-desync work. Unexplained: this run's log has no refused `pa_` action, so it was not the refusal above.
  Next: keep the trace of a run that fails this way.
- **`pa_KillMoveSneakBackA` is refused on a copy.** Sneaking, 2 of 2 rounds ended in a normal swing with this refusal.
  Not compared with vanilla. Unarmed gave no kill move in 2 rounds; bows were not tried.
- **`kill-move-kills-guest` fails on the host's copy of the guest:** its body never comes to rest (`turned back`). 3 of 3
  with this fix and 3 of 3 with it off, so it is older than this work.
- **`ai_state.head_track` after a kill move.** `host-kill-move` and `client-kill-move` can stop at the end comparison on
  a copy looking at a player: `bugs.md`, "Headtracking left over", deferred past v1. With this fix both pass their action
  phase 4 of 4 and fail only there. It ends those scenarios before their later checks.
- **Clip loads (PR #12, not merged).** Copies may wait for an action's clips. A pair's replay runs at once on the game
  thread, so check `AnimationSystem`'s paired branch against that wait when it lands.
