# Testing

Use small game flows: create a world, perform meaningful actions, advance it, and check both the expected outcome
and agreement between owners and copies. The model comes from [Factorio's testing description](https://factorio.com/blog/post/fff-60).
Fake-engine and Skyrim coverage exercise different boundaries; neither replaces the other automatically.

## Choose a layer

| Layer | Location | Purpose | When |
|---|---|---|---|
| Unit | `Code/tests/client`, `host_authority`, encoding tests | Pure rules, state transitions, wire encoding | Every change |
| In-process games | `Code/tests/games`, `Code/tests/specs` | Real services and transport on a fake engine | Every change |
| Skyrim | `scripts/coop_harness/scenarios` | Real engine behavior, input, integration | Target affected flows |
| Soak | The same Skyrim flows with fresh launches and shuffle | Crashes and order dependence | Release acceptance |

## The bug-fix flow (2026-10-30, the user: the most important rule)

```
1 a coop scenario recreates the bug        tier 3, once
2 a GameTests flow fails on it             tier 2 (the "smoke test"); teach the fake first if needed
  -> free the duo: nothing below needs Skyrim
3 unit tests for the logic, then the fix   tier 1
4 the GameTests flow passes                tiers 1-2, iterate here
  ...later, combined runs: step 1's        tier 3, before a push or merge or at a cadence;
     scenarios with the others              possibly another agent
```

The duo is one shared pair of games and costs minutes a run; a fix iterated in tiers 1-2 costs seconds and leaves a
test that keeps catching the bug. Also in [CLAUDE.md](../../CLAUDE.md) and [AGENTS.md](../../AGENTS.md).

```bash
scripts/check.sh                  # Linux tests, harness checks, UI, Windows DLL and launcher
scripts/check.sh --no-dll         # same checks without the Windows build
```

A run takes about 1.5-3 minutes in a worktree. It first configures whichever xmake build has no `xmake.conf` yet, and
prints each step's name as it starts. Logs are in `build/check/`. GameTests runs in eight shards, with separate `GAMETESTS_PORT` values, and the gate
checks that every discovered case ran. Unit-test organization is unchanged by the flow refactor.

## Discover and select

These commands require no Skyrim processes:

```bash
python3 scripts/coop_test.py --help
python3 scripts/coop_test.py scenario --dry-run
python3 scripts/coop_test.py scenario --tag inventory,hands --dry-run
python3 scripts/coop_test.py affected --check
python3 scripts/coop_test.py affected --files Code/client/Services/Generic/InventoryService.cpp
python3 scripts/coop_specs.py --dry
build/linux/x86_64/debug/GameTests --list-test-names-only
build/linux/x86_64/debug/GameTests --list-tests '[spec]'
```

A flow has one registration in `scripts/coop_harness/registrations.py`: name, tags, fixture requirement and callable.
Multiple matching tags select it once. Explicit duplicate names also select it once; `--repeat` controls repetitions.
Unknown names or tags fail. The six former equip/draw/drift scenarios are replaced by:

| Canonical flow | Checkpoints |
|---|---|
| `player-equipment-host` | Equip, draw, sheathe, remove; observe owner and guest copy |
| `player-equipment-guest` | Equip, draw, sheathe, remove; observe owner and host copy |
| `npc-equipment-lifecycle` | Equip, remove, strip the copy, wait for snapshot repair on the same NPC |

There are no aliases for removed names. [The coverage ledger](test-coverage.md) records every old destination.
Late join, first creation, unloaded actors, message ordering and failure paths keep fresh fixtures.

The default Skyrim selection omits `input`, `late-save`, and scenarios requiring two accounts. A tag run omits
`restart` unless it is explicitly requested or a named scenario requires it. `late-save` flows require the saved
story fixture documented by that flow (`duo --save NAME`); they cannot run against the empty QASmoke baseline.

## Run targeted Skyrim flows

The duo is shared by every agent and worktree; its lock (below) is automatic. In a worktree, link the main checkout's
`build/coop-test/CHAT.md` and `DUO.md` individually, keeping prefixes and evidence isolated. Use `:99` / `:100`,
`stop` when Skyrim is no longer needed, and never stop games another checkout holds (`stop` refuses).

### The duo lock

`scripts/coop_harness/duo_lock.py`, in the main checkout's `build/coop-test/` (found through git, so every worktree
shares it): `duo.json` is the truth, guarded by an `flock` on `duo.lock`; `DUO.md` is a view it rewrites (Rules,
Now, Waiting, new Log lines first). Nobody edits `DUO.md` or asks the user.

```
command starts (duo, scenario, reload, repro, launch, play, profile, soak, quest-walk, affected --run,
                MCP duo_start, duo.py run)
  token = new file, flock'ed for this process's life (freed by the kernel however it ends)
  holder is this checkout, alive   -> join the hold, run
  free, nobody ahead               -> hold, run
  holder's tokens all free (stale) -> stop its games (its checkout's `stop`), log it, hold, run
  else                             -> join the queue's end, block on the token ahead (flock, no polling),
                                      re-check when it frees; COOP_DUO_WAIT (2 h) then fail
command ends
  its games still run (duo --keep-running) -> hand the hold to a keeper (`duo.py keep`): it frees
      the hold on `coop_test.py stop`, 1 min after the games end, or after 30 idle minutes
      (COOP_DUO_IDLE_MINUTES; it stops the games first)
  else -> release with a Log line; the next waiter's block on our token returns
```

- One checkout holds at a time; its other commands (and the MCP's `duo` subprocess) join that hold.
- The MCP server holds it from `duo_start` (waits `wait` seconds, then fails keeping its queue place for the next
  call) until `duo_stop` or the server's exit.
- `scripts/duo.py status` shows the holder, the queue and ages; `scripts/duo.py run -- <command>` waits, holds for
  the command, stops its games and frees the duo.
- Tests: `DuoLockTests` in `scripts/test_coop_harness.py` (contending processes, FIFO, a killed holder, a stale
  takeover, the MCP's hold, the keeper).

```bash
python3 scripts/coop_test.py duo --keep-running
python3 scripts/coop_test.py scenario player-equipment-host player-equipment-guest npc-equipment-lifecycle
python3 scripts/coop_test.py scenario --tag inventory --repeat 2 --shuffle --seed 42
python3 scripts/coop_test.py scenario npc-equipment-lifecycle --until-fail 10
python3 scripts/coop_test.py stop
```

`duo` deploys the current harness DLL and loader and records each player's home. Use `duo --save NAME --level N`
for a saved story. `reload` swaps a rebuilt DLL into a running duo and reconnects it; it needs the resident loader.
Games use their own Proton prefixes under `build/coop-test`, never ordinary player saves. `SKYRIM_GAME_DIR`,
`SKYRIM_PROTON_DIR`, `SKYRIM_RUNTIME_DIR` and `COOP_DLL` override installation paths. `--visible` shows the games on
the desktop. `watch --instance both`, `screenshot`, `look`, `status`, `desync` and `world-diff` inspect a live duo.

To play by hand, `play [--control host|guest] [--cell CELL]` (default the Sleeping Giant Inn) boots the duo with only
the controlled game on the desktop, in a window on the primary monitor, and the other off-screen with a live `watch`
view on the second monitor; chat `!watch` turns the other player to face yours. Only an uncovered window keeps
running (KWin stops completing a covered window's presents, and a game that missed one stays stuck), so keep the
played window in front; the off-screen game never stops. Playing the other side means `stop` and `play --control`
again. Tried and dropped (2026-11-01): both games visible (a window covered while booting hung for good) and VNC
views of the off-screen games (`x11vnc`: the mouse never reached Skyrim).

`play --vanilla [--cell CELL]` is the same window and cell for one solo game without our plugin, to compare by hand
how the game plays without us: `launch --vanilla`'s stand-in plugin (build `SkyrimCoopVanillaHarness` first), no guest
and no view. The game keeps running until `stop`, and the next normal launch redeploys ours.

The runner restores the baseline before the batch and after each flow. A baseline records placement, equipment,
level, maximum actor values, weather/calendar and actor population. Saved stories keep quest weather/time and
placed story actors; new test actors still fail cleanup. The same actor, world and lock comparisons run after actions.
Existing baseline world differences are recorded once. New differences left after cleanup fail that flow; the
runner does not expand that initial allowance to hide contamination. Instead it boots the duo again with the
arguments it was booted with (`boot.json` beside the host's `home.json`, written by `duo --keep-running`), records the
fresh baseline and goes on; the summary names each flow that broke the baseline. The same happens when the games are
off the baseline before the first flow. `--no-restart` stops the batch there instead.

Failures are reported as `setup`, `action`, `comparison` or `cleanup`. The original error stays first if cleanup
also fails. Named waits and spec steps emit checkpoints. Evidence is written under `build/coop-test/results/`
before runner restoration, with both games' status captured at a harness failure. A failed restoration stops the
batch even without `--fail-fast`. `--until-fail N` stops at the first failure, requests both games' bug reports and
skips batch restoration; a flow's own `finally` cleanup still runs. Reports retain at-failure snapshots even if a
flow cleaned up its actors before returning. `report`, `crash` and the original diagnostic commands remain available.

### Quitting to desktop

The solo process-lifecycle scenarios launch a fresh game, use native menu input to select Quit and confirm it,
and require Skyrim itself to exit without a new crash report. Wine helpers are stopped during cleanup:

```bash
python3 scripts/coop_test.py repro quit-main-menu
python3 scripts/coop_test.py repro quit-in-game
```

They use the same duo reservation, off-screen display and prefix ownership rules. The corresponding GameTests
flow is `[quit-desktop]`; `[native-lifetime]` checks deletion of independently registered VM natives.

### Two Steam accounts

Since 2026-10-31 (the user: keep the player's Steam free) both duo games run as Linux user `coop2` (`COOP_DUO_USER`)
with its own Steam account in netns `coop2`, and from the duo's own game folder `build/coop-test/game` (the Steam
install's files linked, its own `Data/SKSE/Plugins`): the player's Steam and Skyrim folder are never used. The
client joins the host over loopback inside the namespace. `COOP_DUO_USER=` runs them as this user, on its Steam.
As `coop2` a game has no sound server, and Wine's ALSA driver took this user's sound card itself (2026-11-01). With
no audio driver at all, though, Skyrim queues voice lines that never start, and every scenario waiting on one fails
(`dive-*`, `copy-ends-voice-line`, 2026-11-03). So Wine's Pulse driver is off and its ALSA driver plays to ALSA's
null device (`ALSA_CONFIG_PATH`, `/tmp/skyrimcoop-alsa-null.conf`, `game_env`): it discards the sound and keeps
time, and never touches a sound card. The games are muted anyway.
The ordinary duo uses one account and the session network. Invite/lobby flows need `COOP_CLIENT_USER=coop2`, that
user's Steam account, a client prefix it owns and the existing `coop2` network namespace setup. The harness prepares
the namespace when needed. Restore prefix ownership and test launch options afterwards. Those flows run only when
named or when a second-account configuration is present; they are not part of an ordinary smoke test.

### On Windows

Since 2026-11-01 the duo also runs on Windows, natively (`scripts/coop_harness/windows.py`). Setup:
- Python 3.12 with `psutil`.
- The `clang+llvm-*-x86_64-pc-windows-msvc` release unpacked into `build/toolchains/LLVM`.
- Visual Studio's build tools; `scripts/xmake-msvc.sh` from Git Bash.

Both games run as the user, on the user's Steam, in 960x540 windows side by side, with `bBackgroundMouse` set so
they do not grab the cursor.

**Skyrim version:** Steam ships 1.7 on Windows, which our Address Library IDs and SKSE do not support. The duo links
a 1.6.1170 copy instead (`SKYRIM_STEAM_GAME_DIR`, with `SKYRIM_SKSE_DIR` and `SKYRIM_ADDRESS_LIBRARY_DIR`).

**Unlike Linux:**
- Both games share the user's `Documents/My Games/Skyrim Special Edition` (INIs, saves, SKSE and crash logs) and
  `Plugins.txt`. Skyrim reads them from the user's known folders, so there is no prefix per game.
- A crash log belongs to the game that is gone when it settles (`build/coop-test/crash-owners.json`).
- Each game is the process recorded at launch (`prefix-<game>/game.json`).
- The `xdotool` input actions, `watch`, `screenshot` and the two-account flows are Linux only.

## Repro with the MCP (2026-11-02)

Play a repro as `scene`, goal verbs, `observe`/`why`, `expect`, `record_export`: [repro-mcp.md](repro-mcp.md), "The play
flow". A recording with a scene away from the baseline exports with the tag `scene`: run it by name, on a duo booted for
it, one per boot. In a fresh worktree a late save must be copied into `build/coop-test/prefix-host`'s Saves first.

`scripts/coop_mcp.py` is an MCP server (`.mcp.json`, server `skyrim-coop`) holding one harness `Session` open, so a
bug can be set up step by step in the duo instead of in a scenario rerun from the start. Design and status:
[repro-mcp.md](repro-mcp.md).

```
duo_start / duo_wait / duo_stop     claims and frees build/coop-test/DUO.md itself; refused while it is held
scene_reset                         the baseline, staged things gone, the world thawed
world_freeze / step(n)              Main::freezeTime in both games; step runs exactly n world updates in each
stage / drop / name_handle          a scene in one call; handles name things per game (host_player, guest_player)
act / press / mouse / console       checked actions, real input, console lines
observe / wait_rest / screenshot    both games side by side (apart: how far the two games have it)
expect                              the correct behaviour; a failing expect is the bug
spec_step                           one shared-spec step (a session of these exports as a spec)
record_start / record_export        the session as scenarios/recorded/<name>.py (and Code/tests/specs/<name>.json)
walk_to / steer_to                  navmesh walks (AI-driven, PathToReference, in legs) / straight by real input
talk_to / pick / menu               conversations by Activate (pick waits for the response); menus
travel / arrives_with_party         a far trip (coc) and the party tether's arrival
needs_save                          a recording on a late save starts by checking the duo was booted from it
counters / network / press_to_motion   readouts (Diagnostics/Counters.h, round trips and clock syncs, press timing)
vanilla_start / quest_walk / job_wait  the vanilla stand-in; a quest walk on a duo booted from a late save
harness                             any raw harness command
```

An exported scenario is an ordinary flow (`coop_test.py scenario <name>`): the runner resets the baseline first, and
its body unstages and thaws in a `finally`. It is step 1 of the bug-fix flow; the GameTests flow still follows.

## Profiling

`coop_test.py profile` shows where a running duo's time goes, per game, over one window (default 20 s). It writes
`build/coop-test/profiles/<time>/summary.md` and prints it:

```bash
python3 scripts/coop_test.py profile                          # idle
python3 scripts/coop_test.py profile --load wolves=15         # a fight: god mode, 15 wolves at the host
python3 scripts/coop_test.py profile --scenario NAME... --seconds 60
python3 scripts/coop_test.py profile --load wolves=15 --stacks  # plus whole-process call stacks
```

| Section | From |
|---|---|
| fps, frame-time and our update's percentiles | the plugin's `perf` command (`Diagnostics::PerfStatus`, `Diagnostics/Perf.h`) |
| CPU and memory | the game's process (psutil / `/proc`) |
| messages by name and way, KB/s | the message trail (`Diagnostics/MessageTrail.h`) |
| each hook's calls per second | the breadcrumb counters (`Breadcrumbs::Totals`) |
| our zones: total, self, mean, max | a Tracy capture (`<game>.tracy`, `tracy-csvexport`) |
| call stacks, engine and ours | `--stacks`: the plugin's sampler (`TestHarness/Sampler.cpp`) |

**Tracy** (v0.11.1, BSD-3) is built in with the xmake option `profiler`:
- On in `scripts/xmake-msvc.sh f`; off in player packages unless `--profiler=y`.
- It is idle until a viewer connects, listens on localhost only (`TRACY_PORT`: the host 8086, the guest 8087), and is
  started and stopped by the plugin (`Diagnostics/Profile.h`), so hot reload works.
- Zones go only in our update (one `PROFILE_ZONE` per service update, `HostAuthority::Update`), never in hooks.
- Plots: each hook's calls per frame, memory and the guest's line.
- It has no call stacks: resolving them would hold our PDB open in the game and block relinking.
- To dig in, open a `.tracy` in Tracy's viewer (`build/third_party/tracy-0.11.1/tracy-profiler.exe` on Windows), or
  connect the viewer to a running game's port.

**The stack sampler** (`{"cmd": "sample"}`, test builds):
- Every millisecond it suspends the main thread and the seven busiest others, one at a time, and unwinds each with
  the crash handler's unwinder. It works the same under Wine as on Windows, with no admin.
- Python names the frames:
  - our DLL: `llvm-symbolizer` and its PDB (it warns when the build is newer than the games' copy)
  - SkyrimSE.exe: the nearest Address Library ID, named from `docs/re/ENGINE_REFERENCE.md`
  - system DLLs: their exports
- Each thread's report ranks the samples where it ran. Its wait calls count as waiting; other system calls
  (`VirtualQuery`, say) count as work.
- A flame graph per game: `<game>-stacks.speedscope.json` (open at speedscope.app).

**Player builds** keep the cheap part:
- A bug report's `snapshot.json` has `perf`: the last minute's frame and update percentiles, messages by name,
  memory and CPU time.
- The debug overlay has a Performance window.

**Open:** `profile` on the Linux duo (Proton, netns `coop2`): the capture runs in the namespace through
`as_instance_user`, and the Tracy tools are built from source there. Not yet run.

The first finding is in [breadcrumbs-cost](../re/systems/breadcrumbs-cost.md).

## Python ownership

`coop_test.py` is the CLI entry point. `Session` owns two explicit `Game` handles, fixture and failure state, and
the registry. Each `Game` owns its prefix, control directory, display and persistent RPC connections. No function
changes a module-global current instance. Low-level runtime functions receive their game explicitly; domain actions
resolve host/guest names through the session. Diagnostic scripts use `Game` handles too.

| Package area | Responsibility |
|---|---|
| `game.py`, `runtime.py` | Paths, connections, processes, deployment, display and status |
| `session.py`, `fixtures.py`, `objects.py` | Session ownership, baselines, cleanup, trace objects |
| `actions/`, `observations.py` | Input, console, actor lookup, sampling and state-based waits |
| `comparisons.py` | Pure comparisons of supplied actor/world snapshots |
| `registry.py`, `registrations.py`, `runner.py`, `reporting.py`, `commands.py` | Selection, repetitions, phases, evidence and CLI commands |
| `scenarios/` | Gameplay flows and their domain fixtures/helpers |

Pass the `Session` into flows. Drive player actions through the existing input dispatch and wait for state, including
animation completion. A console setup command is not a substitute for the player action under test. When adding
checks, assert an expected result and synchronized state: two games doing nothing must not count as success.
The harness must not widen production bridge interfaces or introduce unverified engine facts.

Narrow harness checks run in the gate: `python3 -m unittest discover -s scripts -p test_coop_harness.py`. Existing
world, late-save baseline and ragdoll comparison checks remain separate and retain their original assertions.

## In-process game flows

`Party.h` exposes game lifecycle, stepping and waiting; implementations live in `Party.cpp`. `Comparisons.cpp`
compares world references and owned actors with copies, retaining equipment, carried-item, drawn-state, placement,
death and copy-flag checks. `Fixtures` shares cell setup and player-copy lookup while specialized fixtures keep
their own cell IDs and initial state. Fake-engine implementation is split into existing script, object, inventory,
progress and physics domains; this does not change its state or timing model.

```bash
scripts/xmake-linux.sh build -j8 GameTests
build/linux/x86_64/debug/GameTests '[items],[equipment]'
build/linux/x86_64/debug/GameTests 'spec:npc-disabled-copy-follows'
```

Use sequential checkpoints when actions belong to one lifecycle. Catch2 `SECTION`/`WHEN`/`THEN` rebuild the fixture
for each branch and belong only where fresh initial state is required. Use `Party::Until` / `Agree` and explicit
expected results after each meaningful action. `Step(game)` is for deliberate message ordering; retain those
regressions separately. `COOP_NET` still selects the existing delay/jitter/loss transport profiles. Run the network
stress script only when specifically warranted, never routinely as an audit.

## Shared specs and soak

Each JSON file in `Code/tests/specs` runs on both the fake engine and Skyrim. Its `steps` perform actions or state
expectations, and `cleanup` runs even after assertion failures. Both runners attempt every cleanup step; the Python
runner also restores every tracked actor. Specialized native flows remain code when internal assertions or message
ordering would become more complex as data.

Every spec is a Catch2 case named `spec:<name>`, with `[spec]` and its JSON tags. Runtime discovery occurs before
Catch2 selection, so listing, filtering, timing and sharding work per spec. JSON edits and added specs require no
rebuild. The old aggregate test and section-based selection are removed. Skyrim uses the same canonical names:

```bash
python3 scripts/coop_specs.py npc-disabled-copy-follows
python3 scripts/coop_test.py scenario spec:npc-disabled-copy-follows
python3 scripts/coop_test.py soak --rounds 2 --tag inventory --seed 42 --dry-run
```

A real soak launches a fresh duo each round, shuffles the selected flows and preserves its reports under
`build/coop-test/soak/`. Full release acceptance requires an explicit run and a duo reservation.

Before/after measurements, retained failures, regression controls and exact coverage destinations are in
[the refactor report](../reports/2026-10-30-game-test-flows.md). The previous guide and its historical run narratives
are preserved in [testing history](../reports/testing-history/2026-10-30-testing-guide-before-refactor.md).
