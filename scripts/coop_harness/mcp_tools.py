"""The repro MCP's tools (docs/design/repro-mcp.md): scripts/coop_mcp.py registers them, and mcp_reload imports this
module again, so a tool or verb added mid-task is usable at once."""
import contextlib
import functools
import inspect
import json
import os
import re
import subprocess
import sys
import tempfile
import threading
import time
import traceback
from pathlib import Path
from typing import Any

import anyio

from .common import LOG_LINE, ROOT, TEST_HOME, log_time
from .errors import TestFailure
from .objects import AnimTrace, trace_lines

OWNER = f"repro-mcp ({ROOT.name})"
TOOLS = {}
# Set by register(): the server's session and the state that outlives a reload (the duo's boot, jobs, trace, log offsets).
session = None
duo = jobs = shared = None
_depth = threading.local()  # per thread: together's lanes are calls inside a call


def register(server, state):
    """Adds every tool to the server; `state` is the server's, kept across reloads."""
    global session, duo, jobs, shared
    shared, session, duo, jobs = state, state["session"], state["duo"], state["jobs"]
    state.setdefault("lock", anyio.Lock())  # a server started before the lock existed
    state.setdefault("logs", {})
    for name, run in TOOLS.items():
        server.add_tool(_off_loop(run), name=name)
    return list(TOOLS)


def _off_loop(run):
    """A tool run in a worker thread, one at a time (state["lock"]): a long call (duo_start in the queue) leaves the
    server answering, and mcp_reload waits for it instead of swapping code under it."""
    @functools.wraps(run)
    async def call(*args, **kwargs):
        async with shared["lock"]:
            return await anyio.to_thread.run_sync(functools.partial(run, *args, **kwargs))
    return call


def _brief(error):
    """An unexpected error as the agent needs it: the type, message and the last frames of ours."""
    frames = [f"{os.path.basename(f.filename)}:{f.lineno} {f.name}" for f in traceback.extract_tb(error.__traceback__)][-4:]
    return {"ok": False, "error": f"{type(error).__name__}: {error}", "where": frames}


def tool(function):
    """A tool: the harness's prints to stderr, failures as text, every top-level call recorded."""
    @functools.wraps(function)
    def run(*args, **kwargs):
        arguments = inspect.signature(function).bind(*args, **kwargs).arguments
        depth = getattr(_depth, "value", 0)
        top = depth == 0
        _depth.value = depth + 1
        with contextlib.redirect_stdout(sys.stderr):
            try:
                result = function(*args, **kwargs)
                if top:
                    session.record_call(function.__name__, arguments, True, result)
                return result
            except TestFailure as error:
                if top:
                    session.record_call(function.__name__, arguments, False, str(error))
                outcome = getattr(error, "outcome", None)
                facts = {key: value for key, value in (outcome or {}).items() if key != "failure"}
                return {"ok": False, "failure": str(error), **({"outcome": facts} if facts else {})}
            except Exception as error:  # a bug in a verb: say where, keep the server up
                return _brief(error)
            finally:
                _depth.value = depth
    TOOLS[function.__name__] = run
    return run


def _game(game):
    return "" if game in ("host", "", None) else "client"


def forget_old_crashes():
    """Crash logs from before this hold (another run's, between this server's calls) are not this session's crash."""
    session.KNOWN_CRASHES = {game.name: set(session.crash_logs(game.name)) for game in session.games}


@tool
def duo_start(cell: str = "QASmoke", save: str = "", wait: int = 90) -> dict:
    """Holds the shared duo (coop_harness/duo_lock.py) and boots both games in the background; then call duo_wait.
    Waits up to `wait` seconds in the queue; if still busy, fails keeping this session's place: call again within 5
    minutes or the place lapses (duo_stop gives it up). This server holds the duo until duo_stop or until it exits,
    or for 15 minutes once someone waits: the first in line then stops the games and takes it (call duo_start to queue
    again). Hold it only while driving the games, never while reading code or IDA."""
    held = (lock := session.duo_lock(OWNER)) is not None and lock.role in ("holder", "joined")
    session.duo_claim(OWNER, f"repro session in {cell}", wait_seconds=wait, keep_place=True, capped=True)
    if not held:  # a new hold: crash logs from before it are other runs'; a repeated call keeps this run's
        forget_old_crashes()
    if duo["process"] and duo["process"].poll() is None:
        return {"ok": True, "booting": True}
    if all(game.read_status() for game in session.games):  # ours, already running: attach
        return {"ok": True, "ready": True, "attached": True}
    duo["log"] = TEST_HOME / "mcp" / f"duo-{time.strftime('%Y%m%d-%H%M%S')}.log"
    duo["log"].parent.mkdir(parents=True, exist_ok=True)
    command = [sys.executable, str(ROOT / "scripts/coop_test.py"), "duo", "--keep-running", "--cell", cell]
    if save:
        command += ["--save", save]
    duo["process"] = subprocess.Popen(command, cwd=ROOT, stdout=duo["log"].open("w"), stderr=subprocess.STDOUT)
    return {"ok": True, "booting": True, "log": str(duo["log"])}


@tool
def vanilla_start(cell: str = "QASmoke", wait: int = 90, plugin: bool = False) -> dict:
    """Holds the duo (as duo_start) and boots its host game without our plugin (the vanilla stand-in: player_trace, console),
    in the background; then duo_wait. For comparing a repro with vanilla (press_to_motion vanilla=true). plugin=true boots
    one game with our plugin and no session instead: what the plugin costs alone (profile game="host")."""
    held = (lock := session.duo_lock(OWNER)) is not None and lock.role in ("holder", "joined")
    session.duo_claim(OWNER, f"vanilla stand-in in {cell}", wait_seconds=wait, keep_place=True, capped=True)
    if not held:
        forget_old_crashes()
    duo["log"] = TEST_HOME / "mcp" / f"vanilla-{time.strftime('%Y%m%d-%H%M%S')}.log"
    duo["log"].parent.mkdir(parents=True, exist_ok=True)
    command = [sys.executable, str(ROOT / "scripts/coop_test.py"), "launch", *([] if plugin else ["--vanilla"]), "--cell", cell]
    duo["process"] = subprocess.Popen(command, cwd=ROOT, stdout=duo["log"].open("w"), stderr=subprocess.STDOUT)
    return {"ok": True, "booting": True, "log": str(duo["log"])}


@tool
def duo_wait(seconds: int = 50) -> dict:
    """Waits up to `seconds` for the duo's boot: ready, still booting, or failed with the boot log's end. While only
    queued: renews the place and says so (duo_start takes the duo when its turn comes)."""
    lock = session.duo_lock(OWNER)
    if lock and lock.role == "queued":
        return {"ok": True, "ready": False, "queued": lock.poll_place(), "next": "not ours yet: call duo_start"}
    process = duo["process"]
    if not process:
        return {"ok": True, "ready": bool(session.game_state("").get("online") is not None)}
    with contextlib.suppress(subprocess.TimeoutExpired):
        process.wait(timeout=seconds)
    if process.poll() is None:
        return {"ok": True, "ready": False, "booting": True}
    tail = duo["log"].read_text().splitlines()[-8:]
    ready = process.returncode == 0
    duo["process"] = None
    return {"ok": ready, "ready": ready, "log_tail": tail}


@tool
def duo_stop(summary: str = "repro session") -> dict:
    """Stops both games, checks no Skyrim is left and frees the duo with a log line (also freed when this server exits).
    A session only queued leaves the queue instead."""
    return {"ok": True, "result": session.duo_free(OWNER, summary)}


@tool
def status(game: str = "host") -> dict:
    """The game's harness status (session, player, menus, frame)."""
    return {"ok": True, "status": session.game(_game(game)).read_status()}


@tool
def console(game: str, line: str) -> dict:
    """Runs a console command in that game (host or guest) and returns what it printed."""
    return {"ok": True, "printed": session.console_on(_game(game), line)}


@tool
def harness(game: str, request: dict) -> dict:
    """Sends one raw harness command (TestHarness, {"cmd": ...}) to that game: the escape hatch for anything the
    other tools don't cover. Its reply as the plugin gives it."""
    return {"ok": True, "reply": _jsonable(session.player_command(_game(game), request))}


@tool
def scene_reset() -> dict:
    """The blank slate: both players home in the baseline cell, staged things gone, the world thawed."""
    session.unstage()
    session.forget_scene()
    session.reset_baseline()
    if problems := session.check_baseline():
        raise session.failure("the baseline did not come back: " + "; ".join(problems))
    session.name_players()
    return {"ok": True, "handles": session.handles()}


@tool
def scene(save: str = "", at: str = "", pin: bool = True) -> dict:
    """Starts a repro where it happens: on the duo booted from `save` (duo_start save=...), the party goes to `at` (a
    console line: coc Riverwood) and time and weather are pinned. Returns the NPCs around by handle (faendal,
    whiterun_guard_2) and `disagree`: what already differs here, real desyncs to read; from now on only new ones fail.
    scene_reset is the same for the blank QASmoke baseline."""
    return {"ok": True, **session.scene(save or None, at or None, pin)}


@tool
def find(name: str, handle: str = "") -> dict:
    """Names the nearest NPC both games show whose name holds `name` (replays by name: ids differ between runs)."""
    return {"ok": True, **session.find(name, handle or None)}


@tool
def world_freeze(frozen: bool = True) -> dict:
    """Stops (or restarts) the world's update in both games: time, AI, the player, projectiles (Main::freezeTime).
    Our update and the network keep running."""
    return {"ok": True, "world_frames": session.freeze_world(frozen)}


@tool
def step(frames: int = 1) -> dict:
    """In a frozen world, runs both games `frames` world updates, then waits until both froze again."""
    return {"ok": True, "world_frames": session.step_world(frames)}


@tool
def stage(spec: dict) -> dict:
    """Sets up a scene in one call: {players: {host|guest: {near, gap, bearing}}, npcs: [{name, base | ref, frozen
    (default true; false for anyone talked to: a frozen NPC never finishes a line)}],
    objects: [{name, base | ref}], items: [{who, item, count, equip}]}. Names are handles for later calls;
    host_player and guest_player always exist. Returns every handle's form id per game."""
    return {"ok": True, "handles": session.stage(spec)}


@tool
def unstage() -> dict:
    """Removes what stage made in both games and thaws the world."""
    session.unstage()
    return {"ok": True}


@tool
def look(game: str = "host") -> dict:
    """That game around its player: cell, menus, health, and the actors near it with distance and package."""
    return {"ok": True, **session.look(_game(game))}


@tool
def terrain(game: str = "host", radius: float = 1000.0, cell: float = 50.0, toward: str = "", at: list[float] | None = None) -> dict:
    """The ground around that game's player (or `at`) as a walking player meets it: a top-down view (one character a
    cell, legend included), a side profile toward `toward` (a handle or id), what it can walk to and what stops it."""
    return {"ok": True, **session.terrain(_game(game), radius, cell, toward or None, at)}


@tool
def observe(names: list[str], full: bool = False) -> dict:
    """Each handle in both games: everything the first time, then only what changed ({field: [was, is]}), plus how far
    apart the games have it (apart) and what they hold differently (differs). full=true reads every field."""
    return {"ok": True, "seen": session.watch(names, full)}


@tool
def why() -> dict:
    """The last goal verb's outcome: done, how, why it stopped, seconds, its trace (speeds, stalls, last events, file)."""
    return {"ok": True, "last": session.last_verb()}


@tool
def expect(name: str, check: str = "same_place", within: float = 5.0, game: str = "", value: Any = None, of: str = "",
           field: str = "", seconds: float = 0.0) -> dict:
    """The correct behaviour, checked now and recorded; a failing expect is the bug, and the exported scenario keeps it.
    "stage" (quest `name` at value [n] in both games), "same_place" (both games within `within` units), "at" (within
    `within` of value [x, y, z]), "at_rest_of" (where the `of` game's body rested, from wait_rest), "items" (container
    `name` holds the same in both games), "crosshair" (`game`'s player, or each, faces `name` and the engine's crosshair
    takes it, as for its name and Activate prompt), "bounty" (crime faction `name`, hex: at least value [n] on the host
    and the same on the guest), "actor" (`field` such as ai.follow_target equals `value` in `game` or both, within
    `seconds`; "set": not nothing; no value: equal in both), "stays" (as actor, but all through `seconds`: nobody dies), "dialogue" (the menus agreed), "agree" (no desync of `name`; "all": anywhere), "graph" (owner's and copy's graphs read live for `seconds`, never apart for `within` s or more nor unread as long, the owner entering a state named like `field` at least `value` times (no `field`: its graph changing that often)), "stream" (the graph input stream's live check for `seconds`: every streamed copy's graph equal to its owner's at each update; with `name`, its copy compared on at least `value` updates), "guest_npcs" (the guest shows no NPC of its own, all through `seconds`)."""
    return {"ok": True, **session.expect(name, check, within, game or None, value, of or None, field or None, seconds)}


@tool
def act(game: str, spec: dict) -> dict:
    """A checked action in that game (actions/play.py, docs/design/checked-actions.md): ready, do (turn, buttons),
    until, keep, watch. Returns its outcome."""
    return {"ok": True, "outcome": session.act(_game(game), spec, expect=None)}


@tool
def press(game: str, key: str, seconds: float = 0.1) -> dict:
    """Holds a key in that game's window for `seconds` (real input, as a player presses it)."""
    session.hold_key(_game(game), key, seconds)
    return {"ok": True}


@tool
def mouse(game: str, action: str = "click", button: int = 1) -> dict:
    """A mouse action in that game's window (xdotool click, mousedown, mouseup): button 1 attacks or casts right."""
    session.mouse_on(_game(game), action, button)
    return {"ok": True}


@tool
def drop(game: str, item: str, name: str) -> dict:
    """That game's player gets one `item` (hex form id) and drops it; `name` is a handle for it and its twin."""
    return {"ok": True, "handle": session.drop_item(game, item, name)}


@tool
def wait_rest(name: str, game: str = "host") -> dict:
    """Waits until that game's body of the handle is at rest; returns where."""
    return {"ok": True, **session.wait_rest(name, game)}


@tool
def press_to_motion(game: str = "host", key: str = "w", presses: int = 5, vanilla: bool = False) -> dict:
    """Real key presses in that game: for each, frames and ms until the player moves. vanilla=true for the vanilla
    stand-in (vanilla_start), so the same number is had with and without our plugin."""
    return {"ok": True, "presses": session.press_to_motion(_game(game), key, presses, vanilla=vanilla)}


@tool
def profile(seconds: float = 60, route: str = "", budget: str = "", stacks: bool = False, game: str = "both",
            no_tracy: bool = False) -> dict:
    """coop_test.py profile on the held games (the duo, or the vanilla stand-in alone): frame-time percentiles, stalls,
    CPU; route="whiterun" walks the same streaming route either way; budget="p95=33,p99=70,stalls_per_min=6" fails
    over it. Returns the summary and its folder."""
    import argparse
    args = argparse.Namespace(seconds=seconds, instance=None if game == "both" else _game(game) or "host", load=None,
                              scenario=None, stacks=stacks, no_tracy=no_tracy, route=route or None, budget=budget or None)
    session.profile_run = None
    try:
        session.command_profile(args)
        over = None
    except TestFailure as error:  # over the budget, or failed before a window: the folder only if this call made one
        if session.profile_run is None:
            raise
        over = str(error)
    run = session.profile_run
    summary = run / "summary.md"
    return {"ok": over is None, "error": over, "folder": str(run), "summary": summary.read_text() if summary.exists() else None}


@tool
def counters(game: str = "host", reset: bool = False) -> dict:
    """That game's named counters (Diagnostics/Counters.h): count, sum, max each; reset zeroes them after reading."""
    return {"ok": True, "counters": (session.player_command(_game(game), {"cmd": "counters", "reset": reset}) or {}).get("counters")}


@tool
def jitter(game: str = "client", seconds: float = 8.0, top: int = 12) -> dict:
    """What shakes in that game over `seconds`, frame by frame (TestHarness/JitterWatch): each actor's place, drawn root,
    pelvis against its root and heading, by how often a step undid the last; and what all of a screen hangs on (hour
    going back, clock speed changes, the player). The actors that turned back most first."""
    game = _game(game)
    session.player_command(game, {"cmd": "jitter", "start": True})
    time.sleep(seconds)
    reading = session.player_command(game, {"cmd": "jitter", "stop": True}) or {}
    names = {int(a["id"], 16): a.get("name") for a in session.look(game).get("nearby", [])}
    rows = []
    for form_id, actor in (reading.pop("actors", None) or {}).items():
        shakes = max(actor[part]["reversals"] for part in ("place", "root", "pelvis", "heading"))
        rows.append({"id": f"{int(form_id):X}", "name": names.get(int(form_id)), "shakes": shakes, **actor})
    rows.sort(key=lambda row: -row["shakes"])
    return {"ok": True, **reading, "actors_seen": len(rows), "actors": rows[:top]}


@tool
def network(game: str = "host") -> dict:
    """That game's line: the session time, round trips (the host's per connection), a guest's clock syncs."""
    return {"ok": True, "network": (session.game(_game(game)).read_status() or {}).get("network")}


@tool
def go_to(game: str, target: str, within: float = 100.0, straight: bool = False, by_input: bool = False) -> dict:
    """That game's player gets within `within` units of a handle: AI-driven along the navmesh, so for getting there, not
    for testing movement. Fails with why: no_path (straight=true runs there in a line), stalled, moved_off, timeout,
    menu_blocked. by_input=true runs in a line by real input, for a bug in the movement itself."""
    return {"ok": True, **session.go_to(_game(game), target, within, straight, by_input)}


@tool
def travel(game: str, command: str) -> dict:
    """That game's player travels far by a console command (`coc Riverwood`); waits until it arrives there. The
    other player stays (the party tether is the game's to show, not this verb's)."""
    return {"ok": True, **session.go_far(_game(game), command)}


@tool
def arrives_with_party(game: str = "guest", within: float = 3000.0) -> dict:
    """Waits until that game's player arrives near the other player: the party tether after the other's trip."""
    return {"ok": True, **session.arrives_with_party(_game(game), within)}


@tool
def talk(game: str, target: str, say: list[str] | None = None) -> dict:
    """That game's player starts a conversation with a handle (or goes on with the open one) and picks each line of
    `say`: text it contains, or an index. Returns what was said and the lines on offer; why no_line lists them."""
    return {"ok": True, **session.talk(_game(game), target, [int(l) if l.isdigit() else l for l in say or []])}


@tool
def use(game: str, target: str) -> dict:
    """That game's player walks up and activates a handle (door, chest, furniture) with real input; says what changed,
    or fails with why refused."""
    return {"ok": True, **session.use(_game(game), target)}


@tool
def take(game: str, target: str) -> dict:
    """That game's player opens a container and takes everything; returns how many items left it."""
    return {"ok": True, **session.take(_game(game), target)}


@tool
def pickpocket(game: str, target: str, item: str, caught: bool = False) -> dict:
    """That game's player sneaks up to an NPC and takes `item` (its name in the list) from its pockets, the chance
    pinned: caught (the alarm closes the menu) or not."""
    return {"ok": True, **session.pickpocket(_game(game), target, item, caught)}


@tool
def fight(game: str, target: str, hits: int = 30) -> dict:
    """That game's player draws what it holds and hits a handle until it dies; why player_down if the player falls."""
    return {"ok": True, **session.fight(_game(game), target, hits)}


@tool
def equip(game: str, item: str) -> dict:
    """That game's player gets `item` (hex form id) and holds it; "0" is bare hands."""
    session.equip_weapon(_game(game), int(item, 16))
    return {"ok": True}


@tool
def kill_move_check(game: str, target: str) -> dict:
    """What decides a kill move now for that game's player on a handle: should_attack_kill, killable, attack_target."""
    return session.player_command(_game(game), {"cmd": "kill_move_check", "victim": session.resolve(target, _game(game))})


@tool
def kill_move_exchange() -> dict:
    """Both players, one hit from death and moving, swing at each other at once. crossed: each game started its own pair."""
    copies = {game: session.other_player(session.actors_on(game)) for game in ("", "client")}
    return {"ok": True, "crossed": session.kill_move_exchange(copies)}


@tool
def leave_dialogue() -> dict:
    """Closes every open conversation in both games."""
    session.close_dialogues()
    return {"ok": True}


@tool
def menu(game: str, name: str, show: bool = True) -> dict:
    """Opens or closes a menu (Console, InventoryMenu, MapMenu, ...) in that game, as the game does."""
    return {"ok": True, "reply": session.menu(_game(game), name, show)}



@tool
def quest_walk(quest: str, arguments: list[str]) -> dict:
    """Starts coop_test.py quest-walk <quest> <arguments> (a real quest driven through its stages; the research doc
    docs/research/world-state-drift.md, "Quest walks") in the background, on our duo booted from the late save
    (duo_start save=...); then job_wait("quest-walk")."""
    log = TEST_HOME / "mcp" / f"quest-walk-{time.strftime('%Y%m%d-%H%M%S')}.log"
    jobs["quest-walk"] = (subprocess.Popen([sys.executable, str(ROOT / "scripts/coop_test.py"), "quest-walk", quest, *arguments],
                                           cwd=ROOT, stdout=log.open("w"), stderr=subprocess.STDOUT), log)
    return {"ok": True, "log": str(log)}


@tool
def job_wait(name: str, seconds: int = 50) -> dict:
    """Waits up to `seconds` for a background job (quest-walk); done or not, with its log's end."""
    process, log = jobs[name]
    with contextlib.suppress(subprocess.TimeoutExpired):
        process.wait(timeout=seconds)
    done = process.poll() is not None
    if done:
        jobs.pop(name)
    return {"ok": not done or process.returncode == 0, "done": done, "log_tail": log.read_text().splitlines()[-10:]}


@tool
def spec_step(step: dict) -> dict:
    """One shared-spec step (Code/tests/specs: do add_items, kill, stage_npc, script...; expect items, actor...).
    A session of these exports as a spec that runs in GameTests and on the duo."""
    session.spec_step(step)
    return {"ok": True}


@tool
def screenshot(game: str = "host") -> dict:
    """A PNG of that game's screen; returns its path (read it to see the image)."""
    path = TEST_HOME / "mcp" / f"{game}-{time.strftime('%H%M%S')}.png"
    subprocess.run([sys.executable, str(ROOT / "scripts/coop_test.py"), "screenshot", "--instance",
                    "client" if _game(game) else "host", str(path)], cwd=ROOT, check=True, stdout=sys.stderr)
    return {"ok": True, "path": str(path)}


@tool
def record_export(name: str, doc: str, tags: list[str] | None = None) -> dict:
    """Writes the recording as scripts/coop_harness/scenarios/recorded/<name>.py (run with coop_test.py scenario
    <name>), and as Code/tests/specs/<name>.json when every replayed call was a spec_step; otherwise a spec draft and
    the leftovers: the calls a flow on the fake has to say by hand."""
    return {"ok": True, "written": session.record_export(name, doc, tags or ["recorded"])}


@tool
def record_start(name: str) -> dict:
    """Starts recording this session's calls under a name (build/coop-test/mcp/<time>-<name>.jsonl)."""
    return {"ok": True, "recording": session.record_start(name)}


@tool
def name_handle(name: str, host: int = 0, guest: int = 0) -> dict:
    """Names a form id (per game) so later calls can say `name` instead."""
    return {"ok": True, "handle": session.name_handle(name, {"host": host, "guest": guest})}




# --- Growing the server: any Session verb by name (mcp_reload, in coop_mcp.py, picks up new code) ---

BIG_RESULT = 3000  # characters: a larger result goes to a file, the reply keeps its outline


def _jsonable(value):
    """A result as JSON; a big one (a whole character record) is written to a file and outlined: keys and sizes."""
    text = json.dumps(value, default=str)
    if len(text) <= BIG_RESULT:
        return json.loads(text)
    (TEST_HOME / "mcp").mkdir(parents=True, exist_ok=True)
    handle, name = tempfile.mkstemp(prefix=f"result-{time.strftime('%Y%m%d-%H%M%S')}-", suffix=".json", dir=TEST_HOME / "mcp")
    with os.fdopen(handle, "w") as file:
        file.write(json.dumps(value, default=str, indent=1))
    path = Path(name)
    outline = ({key: (item if len(json.dumps(item, default=str)) <= 80 else f"<{type(item).__name__}, {len(item)}>")
                for key, item in value.items()} if isinstance(value, dict) else f"<{type(value).__name__}, {len(value)}>")
    return {"outline": outline, "file": str(path)}


def _instance_args(args):
    """A verb's `instance` argument as the harness takes it: host -> "", guest -> "client"."""
    return {**args, "instance": _game(args["instance"])} if "instance" in args else args


def _session_verb(name):
    if name.startswith("_") or not inspect.isfunction(getattr(type(session), name, None)):
        raise session.failure(f"no verb {name!r}: verbs(match) lists them")
    return getattr(session, name)


@tool
def verb(name: str, args: dict | None = None) -> dict:
    """Calls any Session method (coop_harness/actions/*.py and the rest) by name with keyword `args`, recorded: the
    exported scenario calls ctx.<name>(**args). A verb written mid-task: mcp_reload, then call it here. An `instance`
    argument takes host or guest."""
    return {"ok": True, "result": _jsonable(_session_verb(name)(**_instance_args(args or {})))}


@tool
def verbs(match: str = "", limit: int = 60) -> dict:
    """The Session's verbs whose name or first doc line matches `match` (regex): "name(arguments): doc"."""
    found = []
    for name in sorted(dir(type(session))):
        method = getattr(type(session), name)
        if name.startswith("_") or not inspect.isfunction(method):
            continue
        doc = (inspect.getdoc(method) or "").split("\n")[0]
        if not match or re.search(match, f"{name} {doc}", re.I):
            found.append(f"{name}({', '.join(list(inspect.signature(method).parameters)[1:])}): {doc}")
    return {"ok": True, "verbs": found[:limit], "more": max(0, len(found) - limit)}


def _call(name, args):
    """One call by name, a tool or else a Session verb, unrecorded; a failure comes back as its result."""
    return TOOLS[name](**args) if name in TOOLS else TOOLS["verb"](name=name, args=args)


def _summary(result, width=160):
    text = json.dumps({k: v for k, v in result.items() if k != "ok"} if isinstance(result, dict) else result, default=str)
    return text if len(text) <= width else text[:width - 3] + "..."


@tool
def rounds(call: str, args: dict | None = None, count: int = 10, until: str = "", reset: str = "",
           reset_args: dict | None = None) -> dict:
    """Runs a tool or verb `count` times, one line each ("3 ok {...}"): for a bug that shows once in N tries. `reset` (a
    tool or verb) runs before each round; stops at the first line matching `until` (a regex: "crossed.: true", "FAIL").
    Every full result goes to the file."""
    path = TEST_HOME / "mcp" / f"rounds-{time.strftime('%Y%m%d-%H%M%S')}-{call}.jsonl"
    path.parent.mkdir(parents=True, exist_ok=True)
    lines, hit, ran = [], None, 0
    with path.open("w") as file:
        for index in range(count):
            if reset and not (done := _call(reset, reset_args or {})).get("ok", True):
                lines.append(f"{index} reset FAIL {_summary(done)}")
                break
            result, ran = _call(call, args or {}), ran + 1
            file.write(json.dumps({"round": index, "result": result}, default=str) + "\n")
            lines.append(f"{index} {'ok' if result.get('ok', True) else 'FAIL'} {_summary(result)}")
            if until and re.search(until, lines[-1]):
                hit = index
                break
    return {"ok": True, "lines": lines, "hit": hit, "ran": ran, "file": str(path)}


@tool
def together(lanes: list[dict], sync: str = "frame") -> dict:
    """Both players at once: each lane {call, args} (a tool or verb: dialogue_press, dialogue_move, dialogue_highlight, dialogue_hover,
    talk, go_to...) runs in its own thread from one shared moment. sync "frame": each lane's first key or mouse input
    lands in its game's first frame at that moment (by the game's clock), so presses in two games share a frame;
    "start": the lanes only start together. Returns each lane's result and the frame its input landed in."""
    def lane(call, args):
        def run():
            _depth.value = 1  # a lane is part of this call: not recorded on its own
            return _call(call, args)
        return run
    outcomes = session.together([lane(l["call"], l.get("args") or {}) for l in lanes], sync)
    for spec, outcome in zip(lanes, outcomes):
        outcome["call"] = spec["call"]
    apart = session.landed_apart(outcomes)
    failed = [o for o in outcomes if isinstance(o.get("result"), dict) and o["result"].get("ok") is False]
    return {"ok": not failed, "ms_apart": apart, "lanes": outcomes}


@tool
def checkpoint(name: str = "mcp-checkpoint") -> dict:
    """The host saves the party here; retry comes back to it in seconds instead of a new duo (staged things included)."""
    return {"ok": True, **session.checkpoint(name)}


@tool
def retry(name: str = "") -> dict:
    """Back to the checkpoint: the host loads it, the guest loads with it, both show each other again; handles follow."""
    return {"ok": True, **session.retry(name or None)}


# --- Seeing what happened: the animation trace and the plugin logs ---

def _trace():
    if not shared.get("trace"):
        raise session.failure("no trace running: trace_start first")
    return shared["trace"]


@tool
def trace_start(names: list[str], bools: list[str] | None = None, floats: list[str] | None = None,
                ints: list[str] | None = None, video: bool = False) -> dict:
    """Starts the animation trace of handles in both games (objects.AnimTrace: graph events, events sent into the graph
    with their engine callers (input), actions, states such as kill_move and life, positions; `ints`: graph integers
    recorded with the state as they change); trace_read shows it.
    One at a time: a new one stops the last."""
    if shared.get("trace"):
        trace_stop()
    session.refresh_handles()
    actors = {instance: {session.resolve(name, label): name for name in names} for label, instance in (("host", ""), ("guest", "client"))}
    trace = AnimTrace(session, "mcp-" + "-".join(names)[:40], actors, bools=["bIsSynced", *(bools or [])], video=video,
                      floats=floats or (), ints=ints or ())
    shared["trace"] = trace.__enter__()
    return {"ok": True, "dir": str(trace.dir)}


@tool
def trace_read(origin: str = "", who: list[str] | None = None, kinds: list[str] | None = None, grep: str = "",
               limit: int = 200, everything: bool = False) -> dict:
    """The trace so far as short lines, in seconds from the first action or graph event matching `origin` (a regex:
    "^pa_" for a kill move); positions, noise and unchanged states left out. `who` handles, `kinds` (graph, input, action,
    state, game), `grep` a regex on the line; everything=true keeps the noise."""
    trace = _trace()
    trace.take()
    return {"ok": True, **trace_lines(trace.entries, who or (), kinds or (), grep, origin, limit=limit, everything=everything)}


@tool
def trace_mark(label: str) -> dict:
    """Marks now in the trace: with video=true, both screens side by side are saved there when it stops."""
    _trace().mark(label)
    return {"ok": True}


@tool
def trace_stop() -> dict:
    """Stops the trace: its folder gets timeline.txt (with the plugin logs' lines naming the actors) and trace.json."""
    trace, shared["trace"] = _trace(), None
    trace.__exit__(None, None, None)
    return {"ok": True, "dir": str(trace.dir), "entries": len(trace.entries)}


@tool
def log(game: str = "host", pattern: str = "", since: str = "last", limit: int = 60) -> dict:
    """That game's plugin log (tp_client*.log): lines matching `pattern` (a regex) since this tool last read it ("last";
    the first time, its end), in the last N seconds ("30"), or all of it ("all"). The newest `limit` matches."""
    path = session.plugin_log(_game(game))
    try:
        size = path.stat().st_size
    except OSError as error:
        raise session.failure(f"no plugin log: {error}") from None
    start = 0
    if since == "last":
        start = shared["logs"].get(str(path), max(0, size - 20000))
        start = 0 if start > size else start  # rewritten by a new boot
    with path.open("rb") as file:
        file.seek(max(0, start - 1))
        text = file.read().decode(errors="replace")
    lines = text.splitlines()[1 if start else 0:]  # from the byte before start: a line's tail, or "" after a newline
    if since not in ("last", "all"):
        cutoff = time.time() - float(since)
        lines = [line for line in lines if (stamp := LOG_LINE.match(line)) and log_time(stamp.group(1)) >= cutoff]
    shared["logs"][str(path)] = size
    matched = [line for line in lines if not pattern or re.search(pattern, line)]
    return {"ok": True, "lines": matched[-limit:], "matched": len(matched), "file": str(path)}
