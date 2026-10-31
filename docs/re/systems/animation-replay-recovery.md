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
