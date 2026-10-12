# 2026-10-12: crash on `coc` while a wolf fights (two threads in one combat behaviour tree)

**Symptom:** the host crashed at the end of `dive-client-player` (14:24:47), when the scenario's
cleanup ran `coc QASmoke` from the river cell (Tamriel 7,-8). CrashLoggerSSE: access violation
reading 0x0 at `SkyrimSE.exe+85DC69` (`mov rax,[rsi]`) in `CombatBehaviorThread::Update` (47483),
from `CombatBehaviorController::Abort` (47364) for the wild wolf `0x000DC8E1` the host owns, while
`coc` detached its cell. The two `coc`s of the scenario's earlier rounds (14:24:21, 14:24:34) did not
crash: a race.

**Verdict:** an engine race that the test harness opens, not our hooks and not the copy. The
harness runs console lines while the game is live (no menu open). Such a line executes in
`Main::Update` while the frame's actor and combat jobs still run on worker threads; a typed line runs
with the console open, in menu mode, when they do not. `coc` then aborts the wolf's combat behaviour
tree on the main thread while a worker is updating the same tree.

## Mechanism

1. `Console::ExecuteCommand` (442672), which the harness calls, only compiles the line into a queue
   (`0x1431ACD78`). `Console::RunQueuedCommands` (442673, named here) runs the queue from
   `Main::Update` (36564) at `+0xA8F`, after `Main::Update` kicked the AI stages
   (`SetJobTaskEvent` 1-4 at `+0x82F`). The stage `Job_Combat_and_magic` (39065) queues a
   `CombatUpdateJob` (46902) for every combat group member: `Actor::UpdateCombat` (38565) →
   `CombatController` update (33178) → `CombatBehaviorController::Update` (47362).
2. On the main thread, `coc` → `TES::SetCurrentCell` (13316) → cell detach (19130, 41292, 41468) →
   `Character::sub_37501` → `Actor::Func244_37624` → 38567 → 33219, which calls
   `CombatBehaviorController::Abort` (47364) on the wolf's controller (`CombatController+0x20`) when
   the actor goes inactive.
3. `Abort` and `Update` share the controller's state with no lock: the `queued` array (`+0x58`),
   the threads' `failState`/`state`/`currentNode`, the root thread (`+0x00`, which `Update` deletes
   and recreates when it finished, `0x858a2a`-`0x858aa5`), and the TLS "thread being updated"
   (`+0x6A0`). `CombatController`'s cached attacker/target are counted non-atomically too
   (`handleCount` `+0xC8`, 0x55b8a0/0x55b900). The engine keeps the two apart by phase only.
4. `Abort` checks `root->currentNode` (`+0x138`) non-null, queues the root, marks the whole tree
   aborted (`CombatBehaviorThread::Abort` 47493 → `SetAborted` 47499, `failState` `+0x148` = 2),
   then updates the queue. The worker, in the middle of the same controller's `Update`, sees
   `failState` 2, runs the nodes' abort handlers and finishes the root (`currentNode` = 0). The main
   thread's `UpdateThread` → `Update` then reads `currentNode` = 0 with `state` (`+0x14C`) = 1 and
   flags (`+0x150`) clear, and calls through it: `rsi` = 0 at `+85DC69`.

## Evidence

| Source | Shows |
|---|---|
| CrashLoggerSSE, thread 384 (main) | `47483+0xC9` ← `47363+0x58` ← `47364+0x5B` ← `33219+0xB7` ← `37624+0x6D0` ← ... ← `13316` ← `40437`/`22873` (CenterOnCell) ← `442673+0xCA` ← `36564+0xA94` ← `HookMainLoop`; `Character "Wolf" 0xDC8E1` at `RSP+98` |
| Our VEH (first access violation in the process, `tp_client.1.log` 14:24:47.049) | **another thread** faulted first, at `SkyrimSE.exe+8604E1` (root node abort handler 47568, reading the thread's stack) ← `47483+0xE3` (the `[rax+28h]` aborted-node call) ← 47363 ← `47362+0xB8` (`CombatBehaviorController::Update`) ← 33178 ← `38565+0x1D7` (`Actor::UpdateCombat`) ← `46902` (`CombatUpdateJob`) ← `JobList`/`BSJobs::JobThread` (69355, 69343, 68445) |
| Breadcrumbs, same dump | `HookUpdateAI Character 000DC8E1` on worker `[444]` 1 ms before the crash; `HookVMUpdate`, `HookApplyMovement`, `HookShouldAttack 000DC8E1` (122 ms) on workers; `HookCheckForNewPackage` on `[384]` at 0 ms (the call in `Func244` at `0x683C4D` during the detach); `HookMainLoop` last 3 ms: the main thread entered the frame, queued `coc` (log 14:24:47.046) and the actors were updated meanwhile |
| `repro qasmoke-exit-console` breadcrumbs (2026-10-11, console open 3 s before `coc`) | the per-frame actor hook stopped the moment the console opened (`HookActorProcess` last 4699 ms = the `show Console` time): in menu mode no actor work runs while a line executes |

`HookCheckForNewPackage` passes host-owned actors through; the copy (`FF002655`) only kept the wolf
in combat, as the player does in the single-game runs. No other hook of ours is on either stack.

Nothing in Engine Fixes, po3 Tweaks, SCN-Crash-fix or the CrashLogger reports found by search
touches `CombatBehaviorThread`/`CombatBehaviorController`: in normal play the console is open when
lines run, and the engine's own cell changes happen at its safe points.

## Repro (not run yet)

The window is a few microseconds per `coc`, so it needs combat jobs in flight: more fighters, more
runs. Two games on one machine slow the AI stage and widen it, which is why the duo hit it.

```
# our plugin, harness as today: expect some runs to crash here
python3 scripts/coop_test.py repro --cell Riverwood --runs 10 --wait 10 \
    "tgm" "player.placeatme 0001E7A1 8" "coc QASmoke"
# A/B 1: the same with the console open, as typed: expect no crash
python3 scripts/coop_test.py repro --cell Riverwood --runs 10 --wait 10 \
    "tgm" "player.placeatme 0001E7A1 8" "@show Console" "coc QASmoke"
# A/B 2: the engine without our plugin, same harness path: expect it to crash like the first
python3 scripts/coop_test.py repro --vanilla --cell Riverwood --runs 10 --wait 10 \
    "tgm" "player.placeatme 0001E7A1 8" "coc QASmoke"
```

Adding `--probe 47364,47362` puts the main thread's `Abort` and the workers' controller `Update`
into the breadcrumbs with their threads, so a crashing run shows the overlap directly.

## Fix (test harness only)

Run harness console lines as a typed line runs: with the console open. The harness's `console`
command opens the Console menu (as `SetMenuShown` does now), waits until the UI reports it open,
executes the line, and closes the console once `Console::RunQueuedCommands` has emptied the queue
(`0x1431ACD88` back to 0). Each step waits for game state, no sleeps. `SKYRIMCOOP_CONSOLE_PHASE=ui`
does not help: `ConsoleTask::Run` only queues the line too, which still runs at `Main::Update+0xA8F`.

No production change: players type console lines with the console open, so they never run this way.

`tcai` around the dive (added to the scenario after this crash) does not close the window: the
cleanup turns combat AI back on right before `coc`.

## Open questions

- Our client update runs inside `HookVMUpdate`, on a job thread (`[436]` here), not the main thread.
  Its engine changes (copy spawn and delete, `MoveTo`, package and AI calls) may run alongside other
  AI stage jobs too. In this crash, "Owner ... refused by the engine" lines were logged at .047,
  between the `coc` (.046) and the crash (.049). It played no part here, but it needs its own check of
  which stage jobs run in parallel with "VM update".
- Not verified in IDA: which `WaitForJobTask` in `Main::sub_36555` (`0x643F43`, stage 0 event 2)
  joins `Job_Combat_and_magic`. The breadcrumbs above show that the jobs overlap the console's run.

## Fix (2026-10-12, test harness only)

`EngineCalls::RunConsoleCommand` raises `UI::numPausesGame` (UI +0x160) before queuing a line and
`EndConsolePauses` lowers it at the next harness poll, as an open console does. The harness polls before
`Main::Update`, which sets `gIsInMenuMode1 = (UI::numPausesGame != 0)` at `0x645f78` and skips
`SetJobTaskEvent` (the AI stages, `+0x82F`) when it is set, so the line runs at `+0xA8F` with no actor or
combat jobs alongside it.

The recipe above did not crash on one game, with or without the fix (vanilla 0 of 10, paused 0 of 10,
8 wolves and `coc`): the window needs the two-game load. The fix rests on the code path above.

Pausing every line broke `equip-host-npc` and `late-join-client` (3 of 3; the control without the pause
passed): an NPC's `equipitem` run in menu mode never shows on the NPC, even on the host
(`AIProcess::Update3DModel_Impl` 0x6e3b70 is one of the readers of `gIsInMenuMode1`; not traced). The
harness now pauses only cell changes (`coc`, `cow`, `centeroncell`, `centeronworld`), the lines that
unload actors.
