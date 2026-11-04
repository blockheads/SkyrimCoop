# Repro MCP (2026-11-02: phases 1-3 built; 2026-11-03: the play flow)

## The play flow (2026-11-03)

An agent works alone from a `docs/bugs.md` entry; the user can steer through the same tools. This is how step 1 of the
bug-fix flow is written by default, not a step before it: the session exports as the scenario. The server gives its
clients this order (`INSTRUCTIONS`, `coop_mcp.py`):

```
1 scene    a place with the world held still; NPCs named
2 play  <-----------------+
3 watch    what changed?  |  not reproduced: adjust
4 expect   oracle --------+
5 export   scenario (fails on the duo) + spec draft + leftovers
   duo_stop, then GameTests
```

- **scene** (`actions/repro.py`): `scene(save, at, pin)` goes to `at`, pins, names the NPCs both games show (`faendal`,
  `whiterun_guard_2`), and returns `disagree`. `scene_reset` is the blank QASmoke start. `find(name)` names one more.
  - Pin: the host's `timescale` 0 and `fw` weather. The guest's clock follows. It holds schedules, not a package already
    running: Faendal walked 1,000 units on a stopped clock. `world_freeze` stays the frame-level tool.
  - A real world never starts in agreement (Riverwood, 2026-11-03: `head_track`, a guard's level, the player's hands),
    so differences at the start are shown and kept (`SCENE_KNOWN`), and only new ones fail, as the runner already
    treats a baseline's world and lock differences. They are read each run, not a list of known bugs.
  - A flow tagged `scene` (fixture `scene`, `registry.py`) has no baseline: run by name, one per boot, no restore.
  - `stage` spots (`scene_spot`): the home stage while the party is near it, else ahead of the host on a floor it can
    see by rays, refused otherwise.
- **play** (`actions/play_verbs.py`): `go_to`, `talk`, `use`, `take`, `fight`. Each returns or fails with one outcome:
  `{done, how, why, seconds, trace}`. `why`: `no_path`, `stalled`, `moved_off`, `timeout`, `menu_blocked`, `no_line`,
  `refused`, `player_down`. `go_to` is AI-driven: for getting there, not for testing movement (`by_input` for that).
  `talk` goes on with an open conversation. `walk_to`, `steer_to`, `talk_to` and `pick` are their parts, no longer tools.
- **watch**: `look(game)`; `observe(names)` shows only what changed since the last look, `apart` and `differs`; `why()` is
  the last verb's outcome with its trace summary (speeds, stalls, last events; the raw timeline in a file).
  - A handle follows its copy by session id (`current_copies`): a copy's form id changes when its cell reloads, and the
    guest reused one for a wolf. Copies' `ai.*` read 0 by design: read an NPC's AI on the host.
- **expect** kinds: `stage`, `same_place`, `at`, `at_rest_of`, `items` (a container, by `container_items`: `item_stacks`
  sees only stacks with extra data), `actor` (`field`, `value`, `seconds`), `dialogue`, `agree`.
- **export**: the scenario always; a spec when every call was a `spec_step`; else a draft in `build/coop-test/mcp/` and
  `leftovers`, the calls a flow on the fake says by hand. Walking and talking mean nothing on the fake, so no full export.

### First proofs (2026-11-03, agent from the bug entries)

- **A container the host creates reaches a guest empty: reproduced.** `stage` the chest, `use` as the guest, `expect
  items`: `F: 50 (host) vs 0 (guest); 63B45: 1 (host) vs 0 (guest)`. Exported as `created-chest-reaches-guest-empty`; it
  fails the same way alone, in 6 s. Step 2 is open: the fake's created references are loose items only, and a failing flow
  can't sit in `check.sh`, so the flow lands with the fix.
- **A guest's follower can stop following: not reproduced.** Recruited with `talk`, party away and back by `coc`, a wolf on
  the guest (god mode): on the host she kept `follow_target` and `package_target`. The entry needed a Load-menu round.
  She stood 1,388 units from the host for a minute, target set: beside her anchor, the guest (2026-11-04, `bugs.md`,
  with the Load-menu round: not reproduced either).
- **`faendal-letter`:** through `coop_test.py scenario`, all its steps pass (stage 10 in both games, 129 s), then the end
  comparison fails on new differences: `head_track` (deferred, `bugs.md`) and a guest copy with no owner (a rabbit).
- What the proofs found in the tools: a run cancelled before it set off (`SET_OFF_SECONDS`), `dialogue_pick` not landing a
  guest's recruit (`pick` uses real keys now), `expect items` passing on 0 of 0, `use` reporting before the menu, a
  stale handle on a reused form id.

Not built: a fast retry on a save (reload a checkpoint, the guest rejoining; loads froze guests in the 2026-10-31
playtest), `equip`, `cast`, `together`. Open: these runs called the tool functions from a scratch process, not through
a connected MCP client, so the client's call limit is untested; `talk` took about 90 s in the Faendal run.

## Robustness (2026-11-04): grow it, don't leave it

The kill-move work (PR #13) left the MCP for scratch scripts: rounds of a setup and a swing (`km.py`), a pull of both
games' `anim_trace` and a filter over it (`take.py`, `show.py`). The cause: the server loaded its code once, so a verb
written mid-task needed a restart an agent can't do, and raw `anim_trace` through `harness` was unreadable. Now:

```
agent ── MCP ──> coop_mcp.py      server, the live state, mcp_reload
                   │ mcp_reload: drop coop_harness.* from sys.modules (errors, duo_lock kept), import again,
                   │             session.__class__ = the new Session (handles, recording, hold stay),
                   │             tools checked on a scratch server, then re-registered, tools/list_changed sent;
                   │             an import or registration error keeps the old code and tools
                   v
            coop_harness/mcp_tools.py   every tool; verb(name, args) calls any Session method
                   v
            Session ── Game.rpc ──> games   (a driving command refused without the duo)
```

- **Grow:** a verb in `coop_harness/actions/` (or a tool in `mcp_tools.py`), then `mcp_reload`, then use it.
  `verb(name, args)` reaches a new method at once, even if the client doesn't refresh its tool list; `verbs(match)`
  lists them. A verb call records and exports as `ctx.<name>(...)`; an `instance` argument takes host or guest.
  A reload doesn't run `Session.__init__`: new state on the session is read with `getattr(ctx, NAME, default)`.
- **rounds**(call, args, count, until, reset): a tool or verb N times, one line each (`3 FAIL {...}`), stopping at the
  first line matching `until`; full results in `build/coop-test/mcp/rounds-*.jsonl`. Exported as a `for` loop of the
  rounds it ran. A failing `expect*` called through `verb` stays in the export, as a failing `expect` does.
- **trace_start**(names) / **trace_read**(origin, who, kinds, grep) / **trace_mark** / **trace_stop**: `AnimTrace` of
  handles in both games; `trace_read` is `objects.trace_lines` (show.py's filter): seconds from the first event
  matching `origin` (`^pa_` for a kill move), no positions, steps, sounds or first-person graph, a state only when
  it changed.
- **log**(game, pattern, since): the plugin log since the last read, the last N seconds, or all.
- **Unexpected errors** come back as `{error, where}` (the last frames) instead of killing the call.
- **Bypass refused:** `Game.rpc` refuses a driving command (`READ_COMMANDS` are free) from a process that neither
  holds the duo (nor descends from its holder or a joined process) nor came in through `coop_test.py`
  (testing.md, The duo lock), checked on every such command so a released hold stops at once; `script_var` with a
  value and `trainings` with `set` count as driving. `COOP_HARNESS_FREE=1` is the deliberate way past.
- **Proof:** `record_export` stamps the scenario `# recorded: <session file>, <n> calls`; `scripts/check_scenarios.py`
  (in `check.sh`) fails a new `registry.add` line without `# hand-written: <why>` and a new recorded file without
  either line.
- **The rule:** CLAUDE.md's bug-fix flow and the worker prompts (`.claude/skills/task`, `session`): extend the MCP,
  never script around it. The PR template asks which MCP tools a change added.
- **Python units:** `scripts/test_coop_mcp.py` (in `check.sh`).
- **One call at a time, off the loop:** FastMCP runs a sync tool on its event loop, so `duo_start` waiting in the
  queue blocked every call (mcp_reload too) past the client's 120 s. Tools now run in a worker thread under one lock
  (`state["lock"]`), and `duo_start`/`vanilla_start` wait 90 s by default, then fail keeping their place.
- **Checked live (2026-11-04, through the connected server):** with no games, a verb added to `actions/repro.py`
  worked after `mcp_reload` through `verb` and `rounds`; a new tool showed in Claude Code's tool list after the reload
  (`tools/list_changed` honoured); a syntax error was refused with its line and the old code kept serving. On the duo:
  `trace_start` on both players, `verb` (`give_sword`, `draw`), a swing and `rounds` of three jumps, `trace_read`
  showing the draw and the jump in both games (the guest's copy 100-190 ms behind), `log` on the guest, `mcp_reload`
  with the games running (the trace and handles kept), a bare `Session` refused `input` while `coop_test.py console`
  passed, and `record_export` (stamped; `rounds` as a loop of the three it ran) replayed by `coop_test.py scenario`:
  PASS. `rounds` of presses fires them back to back: two of the three jumps fell in the air.
- **Fresh worktrees didn't boot (fixed):** `prepare_prefix` made `My Games/Skyrim Special Edition` as this user after
  the prefix went to the game's user (`coop2`), so SKSE couldn't write its logs and CrashLogger failed to load (a
  modal dialog before our heartbeat). It now gives the game's user an ACL there, as for `Data/SKSE`
  (`test_the_games_user_can_write_my_games_this_user_creates`).

## Context

Today a repro means writing a Python scenario and rerunning it from the start for every change: 30–90 s per try, with
no way to stop and look in between. Red flags 1–5 showed the cost: each measurement needed temporary code, and
`play` is for a human at the keyboard.

The goal (the user, 2026-11-02): the user describes a bug and Claude sets it up at once. Start from a frozen blank
slate, add exactly the actors, objects and state the bug needs, step it, look at both games, and once it
reproduces, save the session as a replayable test. Later the same tools grow into playing real Skyrim flows as a
player would.

Most of the parts exist: about 140 harness commands, checked actions (`ctx.act`), checked placement, the QASmoke
baseline, traces, and shared specs that run on both the fake and Skyrim. What's missing is a live session over
them, a world freeze, a recorder, and a few readouts.

## As built (2026-11-02)

Phases 1-3 are built and accepted on the duo; what is still open is listed at the end of this section.

- **Server:** `scripts/coop_mcp.py` (FastMCP, `mcp` 1.30 installed for the user; `.mcp.json` registers it as
  `skyrim-coop`). The harness's prints go to stderr: MCP speaks on stdout. Tools: `duo_start` (attaches when our duo
  already runs), `duo_wait`, `duo_stop`, `scene_reset`, `world_freeze`, `step`, `stage`, `drop`, `wait_rest`,
  `observe`, `expect`, `act`, `press`, `mouse`, `console`, `spec_step`, `screenshot`, `harness`, `name_handle`,
  `record_start`, `record_export`, `status`, `unstage`, the play flow's (above), and since 2026-11-04 `mcp_reload`,
  `verb`, `verbs`, `rounds`, `trace_*` and `log` (Robustness). The tools live in `coop_harness/mcp_tools.py`.
- **Verbs as Session methods** (`actions/repro.py`), so exported scenarios call the same code.
- **Freeze:** `world_freeze` / `step` / `world_clock` harness commands, `Games/Skyrim/Main.h`, `TestHarness/FrameStepper.h`.
  Engine fact: `docs/re/systems/main-loop-and-papyrus-vm.md`, "Freezing the world".
- **Acceptance (duo):**
  1. Frozen, both games' world-frame counts stop; `step(10)` moves both by exactly 10; game time stays; the guest
     stays connected.
  2. `guest-drops-item-stays` recreated from a blank slate (`drop`, `wait_rest` on both, `expect at_rest_of`):
     the host's sword 143.8 units from the guest's rest. Exported, the scenario failed the same way, 126 off,
     without the MCP. Deleted afterwards (it duplicates the existing scenario).
  3. The chest spec's steps run as `spec_step` calls exported as a spec: it passed on the duo
     (`spec:mcp-chest-repro`) and in GameTests. Deleted afterwards.
- **Lessons:**
  - An `expect` has to state the property the bug breaks. "Same place now" passed while the bug was there (the
    host's rest had already moved the guest's copy), so `expect` gained `at_rest_of`.
  - Verbs make their own handles (`drop`, `stage`), because created form ids differ from run to run and a
    recorded raw id would not replay.
- **Havok while frozen:** stopped. A sword dropped into the frozen world stays awake at the same height (7096.9)
  for a second; `step(10)` moves it (to 7109.9). So a frozen world holds physics too, a frame at a time.

### Phase 2, as built

- **Counters** (`Diagnostics/Counters.h`): `COUNT(name, value)` and `COUNT_TIME(name)`, atomic, fine in hooks. They
  show in `status` (`counters`), the harness `counters` command (with `reset`), `coop_test.py profile` (reset at the
  window's start, a table per game) and the MCP `counters` tool. ClientTests `[counters]`. First uses:
  `graph state save us` and `graph state bytes` (`Animation.cpp`), `movement messages` and
  `movement stamp ahead ms` (`HostCharacters::OnMovement`).
- **Network** (`status.network`, MCP `network`, a line in `profile`): the session time, the host's round trip per
  connection (`Server::GetConnections`), a guest's round trip and its last 8 clock syncs (`SynchronizedClock::Syncs`:
  the server tick, the ping used, the correction asked).
- **Vanilla:** `vanilla_start` boots the stand-in, and `press_to_motion(vanilla=true)` measures with its
  `player_trace`. Our games measure through the animation trace (`PressTiming`).
- **Measured on the duo:** saves about 100 us, 7.9-15.4 KB. 99 of 557 guest movement messages arrived ahead of the
  host's time (3 ms on average, 9 at most). The round trip was 23 ms between two games on one machine, and the
  syncs corrected +10 then +1 ms: the ping is mostly frame waits, so `ping/2` overestimates the trip and the
  guest's clock runs a little ahead (red-flags.md, 2). Press to motion: host 2-3 frames, guest 2, vanilla 2 (6 on
  the first press after its load).

### Phase 3, as built

- `walk_to(game, target, within)`: along the navmesh as the engine moves an actor, the player AI-driven
  (`Game.SetPlayerAIDriven`). After an actor it runs: `Actor.KeepOffsetFromActor` with `within` as the catch-up
  radius (an actor catches up running). When a run stalls (under 40 units in 1.5 s: a fence, a door) it walks a
  4 s leg of `Actor.PathToReference` round it and runs again; an object gets legs only. When the engine refuses the
  path (the call ends false at once) it runs straight by real input (`steer_to`) and says `pathed: false`. The time
  limit is twice the distance at 200 units/s plus 10 s, so a stuck walk fails in seconds. `steer_to` is the
  straight run on its own (a checked action: turn toward it, forward held, Always Run switched on with Caps Lock and
  back after, Sprint pressed once moving).
- **Speeds** (2026-11-03, units/s, the duo at ~55 fps):
  - A player by real input: walk 81, run 350-360, sprint 490-500, the same in QASmoke, Riverwood and the plains.
    Sprint only starts once moving and from a run (pressed while standing, or while walking, it does nothing).
  - `PathToReference`'s float is `MovementParameters::walkRunPercent` (built in `Actor` 37893, which refuses values
    outside 0-1). For the AI-driven player it scales a walk: 1.0 walks (80), 0.5 is 40 (the old default, the
    "41 units/s" and outdoor crawl below), 0 stands. Always Run and Sprint don't change it. So a path never runs.
  - `KeepOffsetFromActor` catching up runs at 300-450 whatever Always Run says; inside the radius it walks.
  - Always Run is the status readout `player.always_run` (`PlayerControlsData::running`).
  - The Faendal recorded flow (late save, Riverwood), host's walk / guest's walk / the whole flow: 71 / 83 / 252 s
    before, 19 / 6.5 / 113 s after (the rest is dialogue). The runner's baseline refused this save (Odahviing's
    copy differs), so both were timed through a Session.
- `talk_to(game, target)`: walks up if it is out of reach (NPCs wander), switches a third-person camera to first with
  the view key, faces it, presses Activate (real input), waits for the conversation; returns its lines.
  `pick(game, line)` by index or text waits for the speaker's response (new lines, or its line over) and returns
  what is offered next; `leave_dialogue`.
- `travel(game, command)`: that game's player goes far (`coc Riverwood`) and its fade ends; `arrives_with_party(game)`
  waits for the other player to arrive by the party tether. A recording made on a duo booted from a save starts
  with `needs_save(save)` (from the duo's boot file), so the export says what to boot.
- `expect(name, "stage", value=[n])`: quest `name` (editor id) at stage n in both games.
- `menu(game, name, show)`: opens or closes a menu as the game does (InventoryMenu opened and closed).
- `quest_walk(quest, arguments)` + `job_wait`: `coop_test.py quest-walk` in the background, on our duo booted from
  a late save (`duo_start save=...`).
  - The late-save boot never connected (open since 2026-10-27, `progress.md`). The guest loaded the save itself,
    and a save of its own is its world, which it hosts (`SteamService::HostWorld`). `duo --save` now boots the
    guest to the main menu, and it joins with the host's world (`joins_with_world`), as a friend joining a late
    game does.
  - A late save has no character of the guest's, so the join opens the creator: `joins_with_world(make_character=True)`
    finishes the race menu and names "Courier".
  - Run through the MCP: `MQ202` on `LateAfterDiplomaticImmunity`, every stage to 300 matched on the guest. The
    walk's last step, both games writing a save for the closing diff, timed out after 60 s (not looked into: maybe
    the guest's save path, now a dedicated multiplayer save). `BQ01` on `LateAfterDragonslayerBackinSkyrim` never
    got running after `startquest` (already noted in progress.md for that save).
- All verbs record and export (`_line`); Python units cover the export of each.
- **Safety:** `duo_free` refuses unless this server holds the duo's lock (testing.md, The duo lock), and counts leftovers only in this checkout's
  game folder. An early `duo_stop` ran its stop while another agent held the duo, before checking the claim; their
  games ran from their own worktree's folder, which this checkout's stop doesn't touch.
- **A quest by the verbs** (2026-11-03, through the MCP server's process, `LateAfterDragonslayerBackinSkyrim`): the
  host travels to Riverwood, the guest arrives with the party, both walk to Faendal along the navmesh (2,580 and
  2,715 units), the host talks to him and picks three lines, and "A Lovely Letter" (FreeformRiverwood01) is at
  stage 10 in both games. Exported as `scenarios/recorded/faendal_letter.py` (tags `late-save`, `quest`); its
  `run(ctx)` on a plain harness `Session`, on a fresh boot of the save, passed (4 min). `coop_test.py scenario` can't
  run it there yet: its baseline check before the first flow stops on the summit's dragons (Paarthurnax, Odahviing,
  two more), which neither game registered with the session (both list them as their own, session id -1). Why
  (2026-11-03, the announcement's exits now log their reason): they stand in exterior cell `FF000A15`, one the engine
  created at runtime (no plugin has a record for it), and `CharacterService::BuildAnnouncement` drops an actor whose
  parent cell has no session id. Fixed by the bug-fix flow (`progress.md`, 2026-11-03); the baseline check on that
  save then stops on Odahviing's copy's level and its flight lag (open, `progress.md`).
- **Open:** the guest's path to NPC copies it spawned (Closing, below).

## The shape

```
Claude ── MCP (stdio) ──> scripts/coop_mcp.py          one Session, kept open between calls
                             │  tools: duo / scene / stage / act / observe / step / expect / record
                             │  handles: "lydia" -> {host: FF000812, guest: FF000833}
                             │  every call -> build/coop-test/mcp/<session>.jsonl
                             v
                    coop_harness.Session (existing mixins: actions, fixtures, observations)
                             │  ctx.player_command / Game.rpc (command socket)
                             v
          host Skyrim + guest Skyrim (TestHarness: commands, Actions, new: world freeze + frame step)

record_export ──> scenarios/recorded/<bug>.py   (ctx.* calls, one per recorded tool call)
             └──> Code/tests/specs/<bug>.json  (when every step is a spec verb: runs in GameTests AND the duo)
```

## Phase 1: the repro loop (the main deliverable)

### 1. The MCP server: a Session that stays open

```
coop_mcp.py (Python MCP SDK, FastMCP, stdio; registered in the project's .mcp.json)
  session = Session()                       # the same object coop_test.py builds
  duo_start(cell="QASmoke", save=None)      # hold the duo lock (queue up to `wait` s), boot
                                            # duo --keep-running, or attach to our own running duo
  duo_stop()                                # coop_test.py stop, check 0 SkyrimSE.exe, release + log line
  every tool: try ctx call -> {ok, result} | {ok: false, failure text (ctx.failure's step + reason)}
```

- Reuses: `Session` (`scripts/coop_harness/session.py`), `Runtime.launch`/`restart_duo`, the `stop` command.
- Handles: a dict in the server. Values are per-game form ids, resolved through `ctx.player_copy` and
  `actors_on`, as names already are inside `ctx.act`.
- Needs `pip install mcp`, which isn't installed (pydantic and anyio already are).

### 2. Blank slate: reset and freeze

```
scene_reset()        ctx.reset_baseline() + check_baseline() must be empty   (fixtures.py)
world_freeze(on)     both games: game time stopped, weather locked (fw, as reset_world does), AI off for
                     every actor not in the scene (FreezeActor), combat AI off (tcai); scene actors opt in
step(frames=N)       new harness command: Main::freezeTime on, then freezeNextFrame N times
                     (CommonLibSSE Main.h 0x16/0x17; PhotoMode, reference/plugins, does the same)
                     -> returns each game's frame counter, so both are known to have advanced
```

- First a spike, in IDA and on the duo: does `freezeTime` stop Havok and AI but keep our update and the network
  running? Do both games need to step together (yes, or the copy clock drifts)? The answer goes in
  `docs/re/systems/` before building.
- New: `TestHarness/CommandsWorld.cpp` gets `world_freeze` and `step`. Engine access goes through a typed `Main`
  struct in `Games/` with `static_assert`ed offsets.

### 3. stage: one call sets up a scene

```
stage({
  players: {host: {near: "table", gap: 120}, guest: {near: "host", bearing: 90}},
  npcs:    [{name: "lydia", base: 0xA2C8E, at: {near: "host", gap: 200}, live: false, package: null}],
  objects: [{name: "bucket", base: 0x..., on: "table"}, ...],
  items:   [{who: "guest", item: 0x..., count: 1, equip: true}],
})
  -> handles, plus where each one actually ended up (checked placement: floor, room, line of sight)
```

- Built from existing pieces: `spawn_host_npc`, `stage_host_npc`, `place_before`, the `place`/`ray`/`surroundings`
  commands, `actor_place`, `give_item`, `freeze`, `put_package`.
- After staging, it waits until both games show every staged thing (`wait_until_settled`, `ctx.show`).

### 4. observe: both games side by side

```
observe(["lydia", "bucket", "guest"], fields=["pose", "graph", "ai", "items", "body"])
  -> {name: {host: {...}, guest: {...}, differs: ["pose: 3.1 units", ...]}}
screenshot(game) -> a PNG path (Claude reads images)
trace_start(kind=anim|body|dialogue, names) / trace_take() -> the timeline (AnimTrace, BodyTrace)
```

- Reuses: `actors_on`, `world_state`, `body_of`, `status`, `comparisons.py`, the `screenshot` command,
  `objects.AnimTrace`.

### 5. act: the player's verbs

```
act(game, verb, ...)   the ctx.act presets: turn_to, grab, carry, hold_buttons, wait (actions/play.py)
press(game, key, seconds) / mouse(game, ...)   real input (input.py hold_key, mouse_on), like a player
console(game, line) / script(game, call)        escape hatches, recorded like the rest
```

### 6. expect and record: the session becomes the test

```
expect(name, field, value | same_on_both, within)   the correct behaviour: checked now, recorded
record_export(bug, kind="scenario"|"spec")
  scenario: scenarios/recorded/<bug>.py + a registry line; each recorded call -> its ctx.* line
  spec:     Code/tests/specs/<bug>.json if every step maps to a spec verb (specs.py: add_items, kill,
            stage_npc, ...); otherwise it says which steps don't map
```

- Recording on this frozen, staged base, the exported scenario starts from the baseline and runs on its own, with
  the same steps the bug-fix flow's step 1 asks for. Step 2 (the GameTests flow) gets a head start when the export
  is a spec.

## Phase 2: readouts the repros need (found in red flags 1–5)

- **Counters:** `Diagnostics/Counters.h`, a named count, sum and max that hooks and services can bump cheaply. It
  shows in `status` and `coop_test.py profile` and as an MCP `counters()` call, so there's no temporary measuring
  code.
- **Network:** each game's ping (enet RTT) and clock-sync history (server tick, ping used, resulting offset) in
  `status`. This is flag 2's open question.
- **Vanilla:** checked actions in the vanilla stand-in (`Code/vanilla_harness`), or at least `press` plus
  `player_trace`, so every repro can be run against vanilla through the same tool.

## Phase 3: toward playing Skyrim (later, one verb at a time)

- `walk_to(target)` along the navmesh: the player AI-driven (`SetPlayerAIDriven`, condition 443) with a travel
  package, or steering by checked action. Search GitHub and Nexus first (the project rule).
- `talk_to(npc)` / `pick(line)`, `use(menu, ...)` over the existing dialogue and menu commands.
- Quest flows: `quest-walk` from late saves (`fetch_late_saves.py`) driven through the same verbs.

## Files

- New: `scripts/coop_mcp.py` (the server), `scripts/coop_harness/actions/repro.py` (stage, observe, expect,
  recorder as Session methods, so scenarios can call them too), `scripts/coop_harness/scenarios/recorded/`,
  `.mcp.json`.
- C++: `Code/client/TestHarness/CommandsWorld.cpp` (world_freeze, step), a typed `Main` in
  `Code/client/Games/Skyrim/`, with its address in `docs/re/ENGINE_REFERENCE.md`
  (`scripts/re/build_re_data.py`).
- Docs: `docs/design/repro-mcp.md` (this plan, kept current), `docs/design/testing.md` (a "Repro with the MCP"
  section), `docs/progress.md`; it replaces step 7 of `checked-actions.md`.

## Verification

- **Python units** (`scripts/test_coop_harness.py`): the recorder turns a recorded call list into scenario code
  and spec JSON; handle resolution; refusal when the duo is held by someone else.
- **ClientTests:** the pure half of the frame step (counting requested against advanced frames), if any logic
  ends up outside the engine call.
- **Duo acceptance, end to end through the MCP:**
  1. `duo_start`, `scene_reset`, `world_freeze`: both games' frame counters stop; `step(10)` moves both by 10.
  2. Recreate a known bug from scratch: `guest-drops-item-stays` (fails on committed code today). Stage the
     sword, drop it as the guest, `observe`, `expect` the host's sword within 5 units of the guest's.
  3. `record_export` -> the scenario fails the same way when run with `coop_test.py scenario`, without the MCP.
  4. A spec-shaped repro (a chest's items) exports as JSON and passes or fails identically in GameTests
     (`spec:<name>`) and on the duo.
  5. `duo_stop` frees the duo and leaves 0 Skyrim processes.
- `scripts/check.sh` passes throughout. No `net_check.sh`.

## Order

1. Phase 1 pieces 1, 2 (spike first), 3, 4, 5, 6, then the acceptance run.
2. Phase 2.
3. Phase 3, verb by verb, when a real flow needs it.

Each piece lands with its tests, and commits only when the user asks.

### Closing the open items (2026-11-02, late)

- Navmesh `walk_to` (AI-driven player + `Actor.PathToReference`, through `papyrus_call` as the player's action):
  in QASmoke the player walked toward Lydia at about 41 units/s from 2 s after the call (the latent call still
  running). On the Dragonslayer late save it barely moved toward a nearby NPC in 15 s (not looked into; the time
  limit is now distance/30 + 10 s). `steer_to` keeps the straight-line walk by real input.
- Rechecked on the duo (2026-11-02, 23:40-00:10), all through the MCP server's own process:
  - QASmoke: the host's player paths to its own staged Lydia (304 units). The guest's engine refuses a path
    (`PathToReference` ends false at once) to the copy it spawned for her, frozen or not, while it paths to the
    host player's copy. `PathToReference` (ID 54731) builds the path's end from the target's position, parent cell
    and worldspace (`Actor` 37893, 37820); her copy's cell is QASmoke and it lands at floor height within 3 s of the
    spawn, so neither explains it, nor does movement (still refused after the host moved her and after a `setpos` on
    the copy). What fails is narrow: a copy the guest spawned for a host-spawned NPC (staged, or the baseline's);
    its copies of world NPCs and of the host's player take paths. The request is built (the harness's
    `path_request`, calling 37893 directly, returns an id for both), so the refusal is the search's; ghosting the
    copy (no collision) and making it persistent change nothing. Why the search refuses is open; `walk_to` sees the refusal and walks straight by real input instead
    (`steer_to`, `pathed: false`).
  - The Dragonslayer late save, both players in Riverwood (`coc Riverwood`): both paths along the navmesh to a
    persistent NPC 2,500 units off (`pathed: true`, the guest's to its copy of a world NPC). The first try at the
    Throat of the World failed for the setup: the nearest NPC (on the summit) had no reachable path, and the guest
    loads 4,100 units from the host.
  - A recording started after `stage` now starts with that stage (the recorder keeps the scene since the last
    `scene_reset`); the exported walk replayed with `coop_test.py scenario` and passed. A recorded spec exported as
    JSON passed in GameTests. `vanilla_start` + `press_to_motion(vanilla=True)`: 2 frames a press once warm.
  - `observe` also shows a game's own actors (the host's NPCs), which it returned as null before.
- The quest walk's closing save: a guest saves nothing in a session (`SaveService`), so the walk's guest now leaves
  first (`save_both(guest_leaves=True)`); MQ202 then ran to its report.
- BQ01: on 1.7.104 the save loads with it already run and unfilled, so `startquest` can't start it;
  `--start-by "coc Riverwood"` (a story-manager start) works, and every step matched.
- The quest flow by the verbs (2026-11-03) found and fixed, each with a unit test where it is Python:
  - `takes_input` (Python and the plugin's checked actions) counted `LoadWaitSpinner` as a menu holding the input.
    After a `coc` it can stay open with nothing on screen while a real W press moves the player 375 units; it is
    ignored now.
  - A long outdoor walk ended short with the latent call still running: the time limit assumed QASmoke's 41
    units/s (outdoors about 20); now distance/15 + 20 s, and a leg the engine ends short starts the next one.
    (Later: both came from `PathToReference` at 0.5, half a walk; `walk_to` runs now, see Phase 3's speeds.)
  - `pick` returned before the response was said, so the next pick saw the old lines.
  - `talk_to` failed in third person (the late save's camera) and when the NPC had wandered off.
  - The export: a numeric pick replays as an index; reads (`get*` console lines, read-only harness commands) are
    left out; the save goes first (`needs_save`).
  - Two new methods shadowed existing mixin methods (`travel`, `follows`); every repro method name is now unique.
  - The tether after the host's `coc` on this save: once `travel_on_host` timed out waiting 90 s for the guest
    within 1,000 units; in the three later runs the guest arrived 1-5 units from the host. Not looked into further.
