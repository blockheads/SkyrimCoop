# Animation action queries reaching copies (2026-10-30)

A native action query is not a performed animation. `ActorMediator::PerformAction` (38949,
1.6.1170 RVA `0x6CD0D0`) resolves the idle/event with flag 1, changes the flags to 2, and returns
before dispatching the event (`+0xB6` through `+0xD6`). `HookPerformAction` checked for flag 1
**after** the engine call. The query therefore became an `ActionEvent`, changed `LatestAnimation`,
and was replayed on copies. `ForceAction` dispatches the resolved event through 38953, which does
not take the native query-only branch.

The query can select a different idle from the later performed action. It can also start the
copy's transition early, so the real event has no transition left when it arrives. Retrying the
same event does not restore the state the premature event changed.

## Skyrim evidence

Worktree `v1-copy-desync`, based on dev `6088c863`, isolated standard duo on displays `:99`/`:100`:

- `follower-switches-anchor` fails mid-conversation: owner `MotionDrivenDialogueGeneric`, copy
  `IdleStopInstant` or `MotionDrivenIdle`; another run has owner idle `10DEB8`, copy `10DEB6`.
- Temporary diagnostics saved the named graph machines and variables before/after owner actions
  and copy attempts. The owner's queries show initial flags 1, final flags 2, success, and no
  graph transition. The later actual action has flags 0. A copy prematurely in `DialogueGeneric`
  refuses the repeated `MotionDrivenDialogueGeneric` event; the owner can still be in
  `MotionDrivenDialogue` before its actual event.
- `bForceIdleStop` differs in some snapshots. It is bound to `BSIsActiveModifier.bIsActive1` in
  `IdleBehavior`, a graph output; adding it to the variable descriptor would hide the cause.
- Baseline `follower-trade-to-picker` passes once while still logging four refused dialogue
  replays. A refusal count alone does not establish a visible mismatch.

Original logs and matching diagnostic DLL/PDB: ignored `build/animation-original-evidence/`.
Scenario runs and failure graph snapshots: `build/coop-test/animation-*.log` and
`animation-graphs.jsonl`. Parsed action snapshots: `build/animation-actions.json`.
The diagnostic instrumentation is removed from the source.

The action layouts were checked against CommonLibSSE-NG `ActionOutput`, `BGSActionData` and
`TESActionData`; the query branch and existing hook signature were checked in the unpacked
1.6.1170 executable. Behaviour transitions were read with `scripts/re/behavior_dump.py`.

## Regression and correction

`ReplayCapture::Run` extracts the existing capture decision without changing it first. The fake
learns the native query's flags changing from 1 to 2 without a graph transition, and ordinary
events that only have transitions from the expected state. The GameTests flow `[replay-query]`
then fails: the copy takes the query's idle and refuses the actual gesture. The duo was stopped
and released immediately after that failure.

ClientTests `[replay-capture]` fails on the resolved queries (two assertions). The correction
remembers whether the action began as a query before calling the engine, and suppresses its
capture regardless of the flags the engine returns. Performed and refused actions retain their
native results. Both suites pass afterward (9 flow assertions and 16 unit assertions).

Full gate and combined Skyrim acceptance are recorded in the v1 issue index when completed.

## Separate finding

Native 38953 already calls the aftermath function 39004 with three arguments, including its
success flag in `r8b`. The inherited `ForceAction` calls it again with a two-argument declaration.
That call is invalid and is separate from the query regression. It is removed in the subsequent
[graph replay recovery](animation-replay-recovery.md), whose red flow and units cover the native
replay path. The native action now performs its own aftermath once.
