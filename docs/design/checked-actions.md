# Checked actions: auditing OpenMW's tests against ours (2026-11-02)

Status: **the slim version is built (2026-11-02, "As built" below); the rest of section 5 waits for need.** This
comes before the MCP layer (`docs/ideas.md`, Testing): the MCP's tools are these actions.

## As built (2026-11-02)

The user chose a slim version, because the bugs that day came from short scenarios and checks of one game, more than
from open-loop actions. Built: the runner, turning, buttons, cross-game watches, and the physics scenario on them.
Not built: `walk_to`/`push_toward`, scene control, matchers, the broad migration.

```
TestHarness/Actions.{h,cpp}   act / act_state / act_cancel; ticked in Poll once a frame, in the world
  ready   all hold, else waits (ready_seconds, real time), else not_ready
  run     a frame counts only when the game takes input (no menu, fade or kill cam) unless allow_menus
          keep    any breaks -> failed "frame N (s): <condition> broke: <what it read>"
          until   all hold -> done; past `seconds` -> timeout naming the one not reached
          turn    {yaw, pitch | face: ref, rate deg/s}: mouse counts through the input dispatch (ActionSteer::Axis)
          buttons held through PlayerInput for the run, released at its end
          watch   distance | distance_xy | z_between [a, b], z a: min/max with frames; series: wall ms per frame
  conditions  on_ground, takes_input, holding id (0: nothing), aimed, for s, near/far(_xy) [a, b, d], moved_less [a, d]
TestHarness/ActionSteer.h     pure: AngleError, Axis (probes 1, 2, 4... counts until a turn shows, then measures
                              the sensitivity each frame; never past the goal; capped per frame), Extremes
                              ClientTests [action-steer]
actions/play.py              ctx.act, ctx.act_both (several games at once, series lined up by ctx.series_at),
                              turn_to, grab, carry
scenarios/physics.py         drags_into_npc (guest/host-drags-into-npc, guest/host-holds-into-npc) on them: no
                              sleeps, no setangle turns; the other game's bucket watched during the press
```

**The problem.** A scenario says "press forward 1.5 s" and hopes. The game runs on between Python's calls, so an
action can silently fail its intent:

- the carry step of `guest-holds-into-npc` dropped the bucket;
- a walk measured the carrier's own capsule instead of the bucket;
- `setangle` turns drop held objects.

The user's words: "you say an action, it happens, other state changes, and the thing you wanted to happen didn't."

## 1. OpenMW's test system, all of it

Read from the source: `scripts/integration_tests.py`, `scripts/data/integration_tests/{testing_util,test_lua_api}`,
`scripts/data/morrowind_tests`, `CI/run_integration_tests.sh`, `.gitlab-ci.yml`.

```
integration_tests.py (one OpenMW process per suite, headless under xvfb, 640x480, 60 fps cap)
  writes test_config.lua {filter, list}  ->  OpenMW boots the suite's content
  menu script  : discover tests (events with a timeout), for each test: newGame(), run it, print the result
  global script: world setup (teleport, createObject, addScript idle.lua), then runLocalTest(player, step, arg)
  player script: the steps: drive controls each frame, coroutine.yield(), expect...
  stdout protocol  TEST_START n name / TEST_OK / TEST_FAILED msg / TEST_ERROR / TEST_FOUND
  runner: per-test timing, "unexpected termination", exit code, crash log, osg frame stats -> artifacts
```

| # | Pattern | Where in their code |
|---|---|---|
| O1 | **Tests run inside the frame loop.** A coroutine resumed once per frame (`onFrame`/`onUpdate`), so a test and the game never drift apart. | `testing_util.updateLocal/updateGlobal` |
| O2 | **Paused world, paused test**: `if not core.isWorldPaused() then resume`. A menu or pause can't eat the test's time. | `updateLocal` |
| O3 | **Steering every frame.** `yawChange = normalizeAngle(target - yaw) * 0.5` each frame; the attack test re-reads the target, re-paths (`nearby.findPath`) and re-aims every frame. | `player.lua` `rotate`, the attack steps |
| O4 | **Invariants inside the loop.** `expectLessOrEqual(distance, 100, 'Target left melee reach')` every frame, NaN checks every frame. | the attack and spin steps |
| O5 | **A ready gate with its own deadline.** Loop until the target is valid, both on the ground, weapon equipped, stance set, else "Attack setup timed out". | "equipped weapon damages a stationary target" |
| O6 | **Wait for an outcome, not a duration.** "until health drops", else "Did not damage any targets in 10 seconds". | the attack steps |
| O7 | **Two clocks.** Gameplay deadlines in simulation time, setup deadlines in real time. | `getSimulationTime` / `getRealTime` |
| O8 | **The test is the only thing that moves.** | |
| | - the player's own controls and looking are off (`setControlSwitch`); | |
| | - the target's AI is off (a 3-line `idle.lua`: `enableAI(false)`); | |
| | - stats are boosted so a hit can't miss, then restored; | |
| | - spawned objects are removed. | all suites |
| O9 | **A fresh, fixed start.** Each global test begins with `menu.newGame()`, and a local test with a teleport to one spot. | `setSetupGlobalTest`, `initPlayer` |
| O10 | **A purpose-built world.** The example-suite content (a template game, "the hub" cell, a test creature, small addons) for engine tests; a separate suite on real Morrowind content, refused without `Morrowind.esm`. | `example-suite`, `morrowind_tests` |
| O11 | **Regression tests named by issue**: `[issues] Player should be able to walk up stairs in Ebonheart docks (#4247)`. | `global_issues.lua` |
| O12 | **Steps take arguments** (`registerLocalTestStep(name, fn(arg))`, `runLocalTest(obj, name, arg)`): the world side passes the target to the player side. | `testing_util` |
| O13 | **Matchers that explain themselves.** `expectThat(value, matcher)`, where a matcher returns `''` or why not: `closeToVector`, `elementsAreArray`, `equalTo` (deep, serialized), `isNotNan`, `isAny`. | `matchers.lua` |
| O14 | **A runner protocol.** Start, ok, failed with message, framework error, found. `--test_filter`, `--list_tests`, "no tests matched" is a failure. | `integration_tests.py` |
| O15 | **CI artifacts.** Logs, configs, screenshots, the crash log, per-suite frame-time stats (`osg_stats.py`), an ASAN run. | `.gitlab-ci.yml` |

## 2. Ours today (measured 2026-11-02)

```
scripts/coop_harness: 20k lines Python, 341 registered scenarios
  Session = Runtime + Commands + Actions + Reporting + Fixtures + Runner + Observations + Profiling (one ctx)
  a scenario: Python function -> ctx.* -> Game.rpc (JSON over TCP) -> TestHarness::HandleCommand (game thread)
  waiting: ctx.wait_for(desc, predicate, timeout): Python polls every 50 ms, each poll a full "status" RPC
  run_flow: setup -> action -> comparison (desync, dialogue trace, world diff, lock order) -> cleanup (baseline)
Code/client/TestHarness: 7.2k lines C++, Poll() once a frame: input resend, commands, traces, gait watch
```

How scenarios act, counted in `scenarios/` and `actions/`:

- 945 + 225 `wait_for`, 35 + 19 `wait_on`. Waiting on state is the norm, as CLAUDE.md asks.
- 467 + 80 `console_on`. Staging, but also actions: `setangle` turns 22 + 12, `setpos` 8 + 8.
- 48 + 25 `press`/`tap`/`release`, for player input through the engine's dispatch: 10 buttons, plus keys, chars and
  the mouse.
- 24 + 6 `time.sleep`, against the rule. They're the timed parts: carries, held buttons, turns.
- 135 + 15 `place_before` (checked placement with rays), 67 `spawn_host_npc`, 16 + 8 `freeze`.

**Where we're ahead of OpenMW** (keep it all):

- Two-game oracles after every scenario: desync, world diff, dialogue trace, lock-order reports.
- A baseline that is restored and then verified (`settle_baseline`): position, equipment, values, NPCs, copies,
  weather, calendar.
- Phase-tagged failure evidence (`at-failure.json`, both games' status, the log trail digest) and crash detection.
- Checked placement (rays: floor, headroom, line of sight) and staging controls (`freeze`, stage spots).
- Per-frame recorders: body, animation, input and update traces, gait watch (sliding actors), the sampler.
- A fake-engine tier below it all (GameTests), and hot reload.

**The gaps, against each OpenMW pattern:**

| OpenMW | Ours | Gap |
|---|---|---|
| O1 frame loop | Python, between frames; 50 ms polls of a full status | **Missing.** The cause of the gripe. |
| O2 pause-aware | real-time deadlines; `takes_input` only before a press | Missing during an action |
| O3 steering | open loop: timed presses; turns by `setangle` (a teleport of the angle, which drops held objects) | **Missing** |
| O4 invariants in the loop | checks before and after; oracles only after the scenario | **Missing** |
| O5 ready gate | partial: `takes_input`, `place_before` waits for the ground, `hands_settled` | Ad hoc, per helper |
| O6 outcome waits | mostly yes (`wait_for` on state); the timed parts aren't | Partial |
| O7 two clocks | real time only | Missing (a kill cam's 0.2 time scale, `timescale`) |
| O8 only the test moves | `freeze`, restrained NPCs, stage spots; nothing for the player's own input or other AI | Partial |
| O9 fresh start | baseline reset and check (a new game per test would take minutes in Skyrim) | Ours fits Skyrim; keep |
| O10 test world | QASmoke stage, bundled start, home cell; real content | Fine; a test cell plugin later, if needed |
| O11 named by issue | names by feature; playtest ids in docstrings | Convention only |
| O12 step arguments | Python passes ids around freely | Not needed |
| O13 matchers | hand-written failure strings, mostly good | Missing as a library; a few would help (near, within, elements) |
| O14 runner protocol | `run_flow` phases, a summary table, `--repeat`, `--until-fail`, `--tag` | Ours is richer |
| O15 CI artifacts | evidence folders, `profile` | Ours is richer; no frame-time stats per run |

## 3. What we port

1. **An action runner inside the game** (O1, O2, O7). Python sends one declarative action; the plugin ticks it once
   a frame in `TestHarness::Poll` and returns one outcome.
2. **Steering primitives** (O3): turn to an angle, a point or a reference by closed-loop mouse deltas through the
   input dispatch (what a player's mouse does, so a held object follows). Walk to a point, steering as it goes.
   Push toward a reference.
3. **Ready, keep and until conditions, evaluated each frame** (O4, O5, O6). A small JSON predicate set, not code.
4. **Watch values**: the minimum or maximum of a measure over the action, with the frame it happened.
5. **Simulation-time deadlines next to real time** (O7). The action also pauses while the game is paused or a menu
   is open, unless the action is about menus.
6. **Scene control** (O8): an input lock (only the harness drives the player during a scenario), AI off for named
   actors (`freeze` generalised), and normalized stats for a run.
7. **A few matchers** (O13) in Python, for the comparisons after an action: `near`, `within`, `elements`.
8. **Issue-named regression scenarios** (O11) as a convention: a playtest id in the name or tags (`s1`, `c2`).

Not ported: Lua and coroutines (C++ actions are the equivalent), a new game per test (our baseline is the
equivalent at a Skyrim cost), and the example-suite world (QASmoke and the stage spots do that job).

## 4. The design

```
Python (scenario or MCP tool)
  ctx.act("client", {
      "ready": [{"on_ground": "player"}, {"holding": "bucket"}, "takes_input"],
      "do":    {"push_toward": "lydia", "speed": "walk"},
      "until": {"held_for": 10},
      "keep":  [{"holding": "bucket"}, {"moved_less": ["lydia", 5]}],
      "watch": [{"min_distance_xy": ["bucket", "lydia"]}],
      "timeout": {"ready": 5, "action": 20}})
     |  rpc "act" -> id; then rpc "act_wait" {id} (blocks up to N s, answers when done: no status polling)
TestHarness::Actions (game thread, once a frame from Poll)
  ready phase: all ready conditions true, else wait (deadline in real time)
  run phase  : each frame
                 paused or a menu open (unless allowed) -> no tick, no clock
                 keep false -> FAILED {frame, sim_time, condition, values}
                 until true -> DONE
                 do: steer (mouse deltas toward the aim), buttons held through PlayerInput
                 watch: update the minimum or maximum, with its frame
  outcome    : {status: done|failed|not_ready|timeout, frame, sim_s, real_s, reason, values, watched}
Python
  done -> returns the watched values; failed -> ctx.failure("[client] frame 412 (6.9 s): not holding the bucket
  (its grab ended; turned 9 degrees that frame)")
```

**Names** resolve in Python before sending (handles such as "bucket" or "lydia" become form ids), so the plugin only
sees ids, `player`, and points.

**Conditions** (first set, grown by need, each one an engine read through `EngineCalls`): `on_ground`,
`takes_input`, `holding`, `not_holding`, `near`/`far` (3D or xy), `moved_less`, `awake`, `yaw_within`, `menu_open`,
`graph_bool`, `health_below`.

**Actions** (first set): `wait` (nothing pressed, for watches), `hold_buttons`, `turn_to`, `walk_to`,
`push_toward`, `look_at`, then `grab` and `release` as checked actions (Activate held until `holding`).

**Across games.** The other game runs a watch-only action started in the same call (`ctx.act_both`). Both stamp
the wall clock, since the games run on one machine. The result merges them, which gives checks such as "the
guest's bucket never fell below the host's held one".

## 5. The refactor, in steps

| Step | What | Size |
|---|---|---|
| 1 | `TestHarness/Actions.{h,cpp}`: the runner, conditions, watches, the `act`/`act_wait`/`act_cancel` commands. Its pure halves (steering error to mouse delta, the condition tree, clocks) in a header with ClientTests. | new, ~600 lines |
| 2 | Steering: `turn_to`, `walk_to`, `push_toward` through `PlayerInput` and the mouse path. | ~200 lines |
| 3 | Python `ctx.act` / `ctx.act_both`, with `walk_to`, `turn_to`, `grab`, `carry`, `hold_against` built on it; failures through `ctx.failure`, with the action's reason and frame. | ~250 lines |
| 4 | **Migrate the open-loop sites**: the 30 `time.sleep`, the `setangle` turns in scenarios (22) and actions (12), and the timed `press`/`release` pairs. The physics scenarios first (`drags_into_npc`, `bucket_on_npc_head`, `sets_down_and_watches`), then movement and combat. Other `console_on` staging stays: it's setup, not play. | ~60 sites |
| 5 | Scene control: an input lock during a scenario, `freeze` for named actors as an action option, stat normalization in the baseline. | small |
| 6 | Python matchers (`near`, `within`, `elements`) where failure strings repeat. A naming convention for playtest regressions. | small |
| 7 | Then the MCP server: tools are `act` presets plus observe (`around`, `compare`, readouts) and stage (spawn, place), with handles and a recorder that writes `ctx.act` calls into a scenario. | later |

**What stays:** `run_flow`'s phases and oracles, the baseline, the evidence, the traces, `place_before`, the
registry, GameTests below.

**One structural note.** `Session` is one context built from eight mixins (20k lines). It works, and splitting it
isn't needed for this. The MCP needs a small, documented verb set: that's the `act` presets plus a handful of
observe and stage calls in one module (`actions/play.py`). Scenarios and the MCP both use it, so a recorded MCP
session becomes scenario code one to one.

## 6. Tests for the tooling itself

- ClientTests: the steering math (angle wrap, a gain that doesn't overshoot), the condition tree, the
  simulation-time clock while paused.
- `scripts/test_coop_harness.py`: name resolution, building outcomes into failures, and the recorder later.
- In the duo:
  - `turn_to` reaches a yaw within 0.5 degrees while holding a bucket, and keeps it held;
  - `walk_to` reaches a point and stops on a wall with "blocked", not a timeout;
  - the carry step of `guest-holds-into-npc` passes 10 of 10, where today's open-loop carry sometimes drops the
    bucket.
