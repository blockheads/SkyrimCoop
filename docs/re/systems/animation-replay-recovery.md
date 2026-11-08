# Recovering a refused copy action

Native `follower-switches-anchor` still reproduced after the idle-query capture fix. Full graph snapshots immediately before and after native calls, matched by action/idles/tick, showed Lydia's owner in StandingIdlesBehavior's DialogueGeneric (44), while her copy had returned to MotionDrivenDialogue (43), with different IdleBaseA/IdleBaseB clip phases. The copy refused IdleStopInstant and retained the old latest gesture. Other samples accepted the same action without changing the owner's graph; a refusal count alone does not establish a visible fault.

The vanilla behavior files confirm that clip annotations advance these machines independently (MotionDrivenDialogueNextClip). `bForceIdleStop` differed too, but BSIsActiveModifier writes it in idlebehavior/mt_behavior; it is not a missing engine input to force. Additional native action-output result, sequence and index fields were zero on both sides in the matched dialogue capture; they do not explain it. Evidence: ignored `build/branch-actions.json`, `build/output-actions.json`, their full host/guest logs and original diagnostic DLL/PDB.

Every accepted unpaired owner action now carries its resulting saved graph. When its copy cannot perform the event, it loads that exact result through LoadAnimationGraphState, as spawns do, and records the action only if loading succeeded. This avoids guessing which event to synthesize from a mismatched state, forcing graph-written variables, or blocking the queue on a permanently refused event. Paired actions retain their existing partner handling; they have no fallback snapshot. The body's own knock state is preserved when loading, as during replay.

This adds an opaque graph block to the action differential (including empty/reset handling). Transport already compresses payloads with Snappy. Graphs are captured only for accepted actual unpaired actions, never idle queries, refused owner actions, or our own replays. There is additional capture and bandwidth cost per action; native acceptance must check movement, transformations and death as well as dialogue. Accepted events that produce different states are not detected by this fallback.

Native 38953 (RVA6CD4A0) already calls 39004 (RVA6CF760), passing success in R8b. ForceAction also called it a second time through an inherited two-argument signature, leaving success unspecified. That duplicate call is removed; the native action runs its own aftermath once. The hook signature for 38949 remains uint8(ActorMediator*, TESActionData*), checked against the disassembly and CommonLib BGSActionData layout.

Fast evidence: GameTests [replay-graph] initially fails when a local clip leaves the action's source state; the owner accepted the action, its copy cannot update LatestAnimation. Recovery unit tests initially fail three assertions. Both must pass in scripts/check.sh. Native combined acceptance remains pending.

The first combined attempt ran 27 scenarios before being stopped for offline fixture and equipment work. Anchor switch, saved anchor and host load pass; there are no unrecovered unpaired replay refusals in the retained native logs; successful graph recoveries are logged separately. Host kill move passes. The guest kill move has one refused wolf-half replay, but the native timeline establishes that this copy already began that same half with its local player at 137 ms; the owner's half reaches it later at 825 ms while it is already paired. This refusal is not a stuck old pose. The scenario nevertheless retains a separate timing failure: its wolf copy enters dying 951 ms after its local KillActor annotation, following the host's delayed death action. Neither that failure nor the walking checker (owners and copies slide) is declared fixed.

Both Beast Form scenarios stopped before transforming because reporting.race_of's refactored conditional always returned None. A harness regression fails on the original and passes after reading the requested game's status directly. They must be rerun, as must the remainder of the 104-scenario union. The placed guard name matches but its scenario retains the already-recorded 17a479/e82682 versus e85437/16b61d native lock-order family (also in the prior mod-profile report); it is not allowlisted by this batch.

The next combined turn passes anchor switch, saved anchor, trade, host load, both Beast Form transformations and both kill moves. It completed 21 cases before yielding the duo to the waiting agent; 83 remain, including the final dead-guest travel. The first guest kill timing failure is retained despite this passing rerun. `dialogue-peer-starts` again has a restrained/frozen Lydia owner with head82 versus copy122, but identical saved state machines/variables (only TargetLocation and TimeDelta differ), both latest JumpLand. FreezeActor clears kProcessMe; a paused owner's landing pose is a fixture hypothesis, not a confirmed fix. No comparison is relaxed.

## Completed combined run and smoke audit

The 104-case run completed 89 PASS/15 FAIL, including both Beast Forms and both kill moves passing. One trade check retains a persistent all-zero copy action while its owner performs MotionDrivenIdle; later diagnostic capture had already converged. Aela's host-load failure is a torch-slot fault, not a zero animation. The complete failure table and retained evidence are in [the batch report](../../reports/2026-10-30-copy-desync.md). Earlier partial counts above describe those turns, not outstanding cases. No blanket animation closure is claimed.

The user's smoke audit exposed immediate replay in the fake and no animation comparison in Party::ActorDifferences. Red regressions now cover playback time, graph-job staging, graph readiness and the ten-update no-job fallback. The phase machine is shared with production. The existing guest-NPC conversation now performs actual queried/selected/stop gestures and checks actor agreement while talking and before cleanup. With recovery disabled it fails retaining DialogueGeneric instead of IdleStopInstant; recovery restored passes. The observer itself fails first on an erased action marker. These tests do not invent a cause for the intermittent native zero marker. A later short native phase trace passes 9/9; the persistent fault remains open.

The final-build first handoff completes45 cases (41 PASS/4 FAIL), including the controlled torch and all focused animation cases. The exact Lydia83/122 sample shows the owner still processing, so the earlier frozen-owner hypothesis does not explain this occurrence. Both latest actions and saved state machines agree; scalar saved variables differ only in TargetLocation/TimeDelta, while BSLookAtModifier runtime data differs. No specific cause is inferred from that. Optional fake pose readings now have a red/green observer regression for head/pelvis gaps with equal actions; a causal pose-flow regression is still required before fixing the native fault.

A later camera control resolves the Lydia head81–88/copy122 finding as a stale off-screen skeleton reading: looking at the owner changes head to124 without changing JumpLand. Scale matches and a graph reload does nothing. A new red `[culled-pose]` gameplay flow and red comparison units cover this observer fault; visible pose mismatches remain checked. See [the culling evidence](culled-npc-pose-readings.md). This does not explain the earlier persistent all-zero animation marker, which remains open.


## Historical weapon transitions, 2026-10-30

The next combined run captures Deer `846DE` with owner current weapon state0 and copy state5.
Both latest markers are the same moveStart (`959F8`, idle `A75C7`); both carry State2 `0x64E10A8`,
whose bits5–7 equal5. The owner's captured action began during sheathing; its current state later
finished. The copy replay wrote the historical State2 wholesale. Inventory convergence deliberately
waits through Drawing/Sheathing, so this old transition does not converge. A graph snapshot also
contains both actor-state words and previously reintroduced those bits on load.

Evidence: ignored `build/deer-replay-weapon-native.json` (36 exact samples),
`build/coop-test/reference-name-part1.log` and matching `build/reference-name-part1-evidence` DLL/PDB/logs.
The run is stopped and our duo released immediately for offline work.

The fake previously ignored the replayed actor-state words. It now captures weapon bits before an
owner's action, applies them during replay, and prefixes saved graphs with the native actor-state
header. `[replay-weapon]` fails with **5 != 0** through both an accepted move and graph recovery.
The general smoke observer also fails its new weapon-state fault: it compared only hand direction,
so Sheathing5 versus Sheathed0 was invisible. `[replay-state]` units fail three assertions first.

`ReplayState::Flags2` preserves the copy's current weapon bits and applies the other historical flags.
Both ordinary replay and copy graph loading use it. The live owner weapon-choice messages continue
to drive drawing and sheathing; historical movement does not restart a finished transition. Owner graph
loads retain their original exact-state semantics. The smoke observer now checks the raw weapon state,
as the native harness does. No per-event exceptions or transition timeout are added.


Final native acceptance (`build/coop-test/final-weapon-105.log`) completes105 cases with87 PASS/18 FAIL.
The recruitment case no longer reports NPC weapon-state or name differences; its remaining failure is the
revived guest's own lifted skeleton. Both Beast Forms and both kill-move replay cases pass. Five terminal
paired refusals remain logged (four kill moves and HorseExit). The earlier wolf trace proves that duplicate
was already playing, but this does not close every paired refusal, facing drift, or unrelated AI scene state.
Latest full gate:618 C++ cases /6,402 assertions, Python/UI checks and Windows builds pass.

## Copies played clips before their files were in (2026-11-04)

An owner's game loads an action's clip files first: `AIProcess` 39625 asks `AnimationFileManagerSingleton` (43621) and
performs the action from `RunActionAnimationLoadedCallback`. A copy's replay went straight to `PerformComplexAction`
(38953), so its clip started with no animation. `hkbClipGenerator` then reads a duration of 0.01333 s (59263): over at
once, its end annotation fires and the graph runs on, one clip early. The graph, the actions and the clip list all agree,
which is why nothing compared caught it; only what plays differs (Lucan's `MT_IdleHHRightEnter`: 5 s on the host, 1 s
of blend on the guest).

Skyrim, teleported behind Lucan and talking (`npc-clips-match`, the trace's `clips` readout): before, 8 clips on the guest
read `t=0.01333 at_end` with no file and none on the host; after, none in three runs, and Enter clips match (4.74 s and
4.71 s). A clip's control object exists either way: only `animationControl->binding->animation` tells.

`ActionClips::Request` does the owner's half for a copy through the engine's own slots (callback at `high+0x430`, load at
the sets' `+0x60`, clips kept by 40055, cancelled by 39258, which process teardown also uses). The replay step waits in
`kClips`, at most 120 updates. Restrained NPCs perform no idles, so restrained scenarios cannot show this.

`npc-clips-match` needs a fresh duo: repeated in one session, Lucan can run out of topics without an idle, and it fails at
that step, with or without this fix (no idle means nothing shown, never a pass).

## A graph's random picks (2026-11-04)

Files loaded, a copy still played other clips: one action gave `DialogueNeutralExpressiveE` on the host and `...H` on the
guest, and at Lucan's counter his arms crossed 9 to 19 s apart with no action at all. The saved traces matched Lucan
exactly 90%, 62% and 23% of the time (`npc-clips-match` compared nothing then, only that files were in).

Every pick draws from one generator per behaviour graph, shared with its sub-behaviours (`hkbBehaviorGraph` +0x78, seed at
+4, one per actor in the trace's `random`): `seed * 1664525 + 1013904223`.

- `hkbStateMachine` random start (59406): 59 machines in `mt_behavior`, 31 in `idlebehavior`.
  `DialogueNeutralExpressive_Behavior` has 14 states and no transitions: the pick is all that chooses a gesture.
- `random()` in expressions (60025).
- `BSEventEveryNEventsModifier` (63213): Lucan's `AnimObjectCounterIdleLoopRight` loops 1 to 3 times, then
  `AnimObjectCounterRightToArm`. Host N=3, guest N=1, clips 0.45 s apart: that was the crossed arms. `OffsetArmsCrossed`,
  which the first pass chased, played on both games.

Two carriers, because some picks have no action:

```
pick an action made   the action's Variables.GraphRandom, set just before ForceAction
pick no action made   a sync point: an ActionEvent with ActionId 0
                      (seed, GraphState, GraphCounters), kept out of the action queue
```

A copy in step made the same pick itself, on its playback time, and ignores the point. Any other loads it, as a refused
action recovers. A pick starts a clip, so loading at the point's own moment puts the copy on its owner's time without clip
times (a saved graph has none). A copy whose seed cannot reach the point's in a few draws is out of step for certain and
loads at once. One a draw behind gets 500 ms; waiting and then loading leaves it that late, so the next point does not
wait (`GraphRandom::Judge`). A saved graph has no every-N counters either, so the point carries them, written once the
loaded graph's nodes are active.

A seed alone did not do: following it through the movement stream passed 3 of 5 runs. A copy a loop ahead kept being pulled
back to the owner's older seed and never settled. Sync points are not sent for players, actors dying or in a pair.

`npc-counter-matches` and `npc-counter-copy-falls-back-in` show this. The second loads the host's crossed arms into the
copy alone: a tethered guest cannot arrive late, and `sae` on a copy does nothing. Havok fields and flags: hkxcmd
`compat/`. Open Animation Replacer's Random condition has its own generator, so a seed does not cover it.

## Does a copy's graph drift? Measured 2026-11-05

Owner and copy active states (`graph_state`'s `states` section) sampled at 10/s, Lucan, fresh duo, nothing staged:

- `npc-counter-matches`: 60 s, 66 of 481 samples differ, every run 1.08 s or shorter.
- Counter then talk, per-side timeline: 150 s, the copy follows every transition within 150 to 300 ms.
- `npc-clips-match` alone: every action reaches the copy 150 to 300 ms later, in order.

No lasting divergence was seen. That is one NPC, so it says accepted actions landing elsewhere are rare here, not that
they never happen. Two false alarms to avoid: `stage` with `ref` on a living NPC left the host's Lucan and Camilla in
`JumpFall`/`JumpLand` for the session (a harness effect, cause not found), and a sampler that skips unreadable samples
stretches a difference over a scenario change (an 18.6 s "run" that a per-side timeline did not reproduce).

A save costs a median 62 to 112 us and 7.1 to 7.9 KB.

## Drift detector

Built 2026-11-05 in the plugin, against the action's `GraphState`; **replaced 2026-11-06 by a live read in the harness**.
The old one is why the entries "A copy walks while its owner turns" and "A copy's idle variant" existed.

Why the action's snapshot is the wrong thing to compare, read in IDA 1.7.104:

```
owner: PerformAction (38949)
         NotifyAnimationGraph (32901) -> BShkbAnimationGraph::SendEvent (63591)
           hkbBehaviorGraph::HandleEventOnActiveNodes (58377)
             every active node's handleEvent, then updateActiveNodes (60079) before returning
       -> accepted -> TESObjectREFR::SaveAnimationGraphState
         per graph BShkbAnimationGraph::SaveState (63601)
           hkbCharacter::SnapshotGraphState (65148, our name)
             root behaviour graph's active nodes (+152, 144-byte entries, node at +80)
             keep hkbStateMachine (hkbStateMachine::StaticClass, 58901)
             its clone: hkbBehaviorGraph::GetNodeClone (58359)
             current state: states[GetStateIndex(59372, +128 currentStateId)] name (+96)
           first section written: count, then (machine, u32, state, u32, u32)
```

So the action's `GraphState` is the owner mid-transition, with the outgoing state's machines still listed. It stays true
for a moment, and the old detector held the copy to it until the next action.

A machine resting in its start state with no transition under way is not written (65148 checks the current state against
+104, active transitions at +184, a byte at +134; or a global flag): both sides leave the same machines out, so the sets
compare.

Measured 2026-11-06 with `graph_watch` (owner and copy read live, Riverwood, 3 to 6 reads per actor per second):

- 75 s, 10 actors, 2570 samples: no run of 2 s apart, while the old detector called `105A25` apart for 45 s.
- 120 s, 6 actors, 4338 samples: it reported `1348A` for 44 s and `1348B` for 37 s; live, neither was ever apart. It
  reported the crab `8CACD` after `moveStart` for 3.4 s; live, the crab's only run was 2.0 s on a mandible idle.
- Only 2 of 53 lasting reports began at a graph recovery.
- [bugs-map.md](../../bugs-map.md), cluster A, has the rest.

**The check now** (`comparisons.GraphApart`, `Observations.graph_watch_start`): over each `coop_test.py scenario` flow a
background thread reads every NPC's owner and copy with the harness's `graph_state`, on connections of its own, about 4
passes a second, and logs a `copies' graphs, owner against copy:` line: the runs apart for 2 s or more, each with its two
sides. 2 s: about twice the longest following measured (1.08 s, a copy 150-300 ms late never counts). Players' copies are left
out: a player's graph holds its `FirstPerson` graph and first-person machines a copy never runs (measured 2026-11-05).
The verb `graph_watch` is the same watch on demand. It logs and does not fail a flow; it stops before the flow's comparison
checks, and `scenario --no-graph-watch` leaves it out. A run whose actor stops being read ends at its last reading apart and says
so (`lost`, "until no longer read"). The pairs refresh every 3 s, so up to 3 s of it can be a graph that stopped updating: it counts
only past 5 s. Read failures end a run the same way. With a `limit`, an NPC stays watched while it can be read.

## A machine's start state is each game's own (2026-11-07)

The live check's lasting shape on creatures, a wolf's copy turning on the spot while its owner idles (`9B0DD`, 4 to 20 s),
comes from where a state machine starts, not from an event. Read in IDA 1.7.104 (each ID below, named in this branch's
journal) unless marked measured (duo, Riverwood) or from the behaviour file (`scripts/re/behavior_dump.py` on
`meshes\actors\canine\behaviors\quadrupedbehavior.hkx`).

```
owner and copy get the same events (measured, trace "input": all through PerformAction 38949 / ForceAction 38953)
TurnCannedR180's clip ends: its trigger cannedTurnStop, no action (behaviour file; measured in the trace)
  DefaultBehavior CannedTurnState -> StandingState (flags 0, iSyncIdleLocomotion == 0; behaviour file)
  StandingBehavior activates: a new clone of its template (GetOrCreateNodeClone 58360 -> clone ctor 59330,
    which copies startStateId..selfTransitionMode, +0x68..+0x87, and resets +0xFC and isActive +0x88)
  hkbBindable::CopyVariablesToMembers (58522) -> hkbVariableBindingSet::CopyVariablesToMembers (59467) writes the bound
  variable into the clone's startStateId (+0x68): the write itself measured (watchpoint, RVA B1F4BF under 59467)
    StandingBehavior.startStateId <- iSyncTurnState   (behaviour file; DefaultBehavior <- iSyncIdleLocomotion,
    LocomotionBehavior <- iSyncForwardState, the turn clips' machines <- iTurnMirrored)
  Activate (59334) -> ChooseStartStateId (59405): override +0xFC if >= 0, else by startStateMode +0x86:
    0 startStateId, 1 sync variable, 2 random (59406), 3 chooser
owner: iSyncTurnState 1 -> StandingIdleState        copy: still 0 -> LoopingTurnRight, until the next action
```

- Measured (harness `graph_machines`, trace `start` and a write watchpoint, since removed): every new clone holds the
  template's start (1), and at ChooseStartStateId some hold 0 or 2 with no override and mode 0; the watchpoint caught the
  write at RVA `B1F4BF` under `hkbVariableBindingSet::sub_59467` each time.
- The owner's engine changes `iSyncTurnState` in the frame its clip ends; in the trace that frame also holds a `turnStop`
  the engine refused (so never sent). That refused action is the writer (next section).
- A copy's integer variables are its current movement snapshot's (`CharacterService`, the 10-a-second rate for NPCs not
  in a fight), and its clip ends on its own clock: the measured runs read the old value.
- Read, not on the path: StartTransition (59383) and the nested-state path (58380) write +0xFC only for transitions flagged
  0x2000; no canine transition targets `StandingBehavior`'s states. setInternalState (60010) does not touch +0x68.

The same race leaves a copy in `CannedTurnState`: `DefaultBehavior`'s exit needs `iSyncIdleLocomotion` 0 or 1 at
`cannedTurnStop`, and the copy can still hold another value (behaviour file; seen on the duo, 3.8 s, 2026-11-07).

## A refused action still writes its graph variables (2026-11-08)

Both shapes above have one writer: an action the owner's graph refused. Read in IDA 1.7.104 (names and comments in this
branch's journal):

```
owner's AI: PerformAction (38949, hooked: HookPerformAction)
  event sent to the graph; refused (CannedTurnState has no turnStop)
  not a query (flags & 1 == 0) -> 39004 (coop_ApplyActionGraphEffects, started = 0)
    its variable writes sit outside its started-only branch, so they run for a refused action too:
      MoveStart 97 / MoveStop 98         -> iSyncIdleLocomotion (FixedStrings +0x310) = 1 / 0
      TurnRight 99 / TurnStop 101 / TurnLeft 100 -> iSyncTurnState (+0x318) = 0 / 1 / 2
      MoveForward 102 / MoveBackward 103 -> +0x558 = 0 / 1;  MoveRight 105 / MoveLeft 104 -> +0x560 = 0 / 1
    (SetGraphVariableInt at 0x1406e2917)
ForceAction (38953, a copy's replays): the same 39004 call, started or not
```

- Nothing else in the executable or the canine behaviour files writes these two variables. The graphs only read them
  (bound `startStateId`, transition conditions), and so does `AIProcess::StopCurrentIdle` (39257).
- **Read (our code, before the fix):** `HookPerformAction` dropped a refused action ("refused by the engine, not sent"),
  so its write reached the copy only in a later movement snapshot. The copy's machine started, or its exit was judged,
  on the old value.
- **Measured (duo, 2026-11-08):** in the long runs apart, the copy's integers equalled its owner's snapshots, and the
  owner's log held a refused `turnStop` next to each one.
- **The fix:** a refused action that changed its owner's variables is sent as variables only (`kVariablesOnly` in
  `ActionEvent::Type`, `ReplayStep.h`), with the variables as they stand after 38949. The copy stages it on its action
  queue, at its owner's tick, and loads the variables in step with its graph (the step's variables phase), with no
  event: replaying the event could let the copy take what its owner refused. A refused action that changed nothing is
  still not sent.
- **GameTests `[replay-start]`:** the fake runs 39004's writes (`ActionWrites`) for PerformAction and ForceAction, taken
  or refused. The owner's AI performs a refused `turnStop` (and `moveStop` for the exit condition) just before its canned
  turn's clip ends, on the main thread before the graph update. Without the send, two turns over a slow line leave the
  copy in its canned turn. On a clean line the snapshot still comes in time in the fake. ClientTests
  `[refused-writes]`.

### Tried and parked: reading the owner's integers ahead

Before the writer was read, three fixes moved snapshot integers instead: integers read up to an interval ahead of
playback, at an estimated graph clock, then at a measured graph time (`CopyVariables.h`, commits up to `bcc96169` on
PR #38). Each guessed when to read a value whose write arrived with no timing of its own. On the duo the measured graph
time lagged the owner by 50 to 110 ms and did not explain the stuck runs. One fact from it still holds: 41453 passes
20123 the frame's seconds times VATS's time multiplier (read).

### Tried and parked: reconciling copies' machines to their owner's

The first fix sent the owner's start choices and matched them to copy activations by time. The second sent every state
an NPC's machines entered and moved a copy's machine that differed, after 250 ms, through GoToState 59369 (branch
`agent/copy-machine-reconcile`). Three Codex design reviews on PR #38 found it unsound to ship:
- the owner reports a state on entry while the copy was judged by a transition's destination, so a correct delayed
  transition could be force-cut, running entry and exit effects;
- name and state names alias distinct canine machines (`Head_Stand_Behavior` in both `noncombatidle.hkx` and
  `quadrupedbehavior.hkx`);
- history and baselines had holes;
- a catch-up clock truncated running actions.

What it established, and still holds:
- **GoToState (59369):** not active or `force` → it clears transitions (59384, which enters a delayed transition's
  destination first), sets +0x100 and the graph's +0x12E, then calls SetActiveState and 59399. Otherwise it starts an
  eligible transition (59377, 59381, 59382), or jumps when told to.
- **Inside a node's update:** the graph's update (58329) runs `updateActiveNodes` when +0x12E is set; 59387 calls
  GoToState inside Update.
- **State changes:** SetActiveState 59390 sees every state a machine enters, from Activate 59334, StartTransition 59383
  (at its start, or at its end via 59384 under flag 0x10) and GoToState. Deactivate 59337 ends a machine.
- **Running transitions:** at +0xB0 (count +0xB8, 40-byte entries, toStateId +0x20).
- **Clone lifetime:** the engine frees a clone when its node leaves the active graph (60079, 58363).
- **Measured (duo, Riverwood):**
  - of 46 corrections, 34 followed an owner change 250 to 281 ms after it (all the `ForwardLocomotionBehavior` ones),
    9 undid a copy-only change, and 3 fought;
  - machine states cost about 215 B/s for the cell, against 1.7 MB of graph points in the same window.
- **Not shown:** the locomotion corrections may come from the same race on speed integers. A delayed float,
  transition or graph phase would also explain them, and none of these was traced. Treat it as a hypothesis.
