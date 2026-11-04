#!/usr/bin/env python3
"""The repro MCP server (docs/design/repro-mcp.md): one harness Session kept open between calls, so Claude can stage
a bug in the duo step by step, look at both games, and export the session as a scenario."""
import contextlib
import functools
import inspect
import json
import os
import subprocess
import sys
import time
from pathlib import Path

# MCP speaks on stdout: keep its own handle, and point fd 1 (prints, child processes) at stderr.
_mcp_out = os.fdopen(os.dup(1), "w", buffering=1)
os.dup2(2, 1)
sys.stdout = _mcp_out

sys.path.insert(0, str(Path(__file__).resolve().parent))
from mcp.server.fastmcp import FastMCP  # noqa: E402
from coop_harness import Session  # noqa: E402
from coop_harness.common import ROOT, TEST_HOME  # noqa: E402
from coop_harness.errors import TestFailure  # noqa: E402

OWNER = "repro-mcp"
server = FastMCP("skyrim-coop")
session = Session()
duo = {"process": None, "log": None}


def tool(function):
    """A tool: the harness's prints to stderr, failures as text, every call recorded."""
    @functools.wraps(function)
    def run(*args, **kwargs):
        arguments = inspect.signature(function).bind(*args, **kwargs).arguments
        with contextlib.redirect_stdout(sys.stderr):
            try:
                result = function(*args, **kwargs)
                session.record_call(function.__name__, arguments, True, result)
                return result
            except TestFailure as error:
                session.record_call(function.__name__, arguments, False, str(error))
                return {"ok": False, "failure": str(error)}
    return server.tool()(run)


def _game(game):
    return "" if game in ("host", "", None) else "client"


@tool
def duo_start(cell: str = "QASmoke", save: str = "") -> dict:
    """Claims the shared duo (build/coop-test/DUO.md) and boots both games in the background; then call duo_wait.
    Refused while another agent holds the duo."""
    session.duo_claim(OWNER, f"repro session in {cell}")
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
def vanilla_start(cell: str = "QASmoke") -> dict:
    """Claims the duo's host game and boots it without our plugin (the vanilla stand-in: player_trace, console),
    in the background; then duo_wait. For comparing a repro with vanilla (press_to_motion vanilla=true)."""
    session.duo_claim(OWNER, f"vanilla stand-in in {cell}")
    duo["log"] = TEST_HOME / "mcp" / f"vanilla-{time.strftime('%Y%m%d-%H%M%S')}.log"
    duo["log"].parent.mkdir(parents=True, exist_ok=True)
    command = [sys.executable, str(ROOT / "scripts/coop_test.py"), "launch", "--vanilla", "--cell", cell]
    duo["process"] = subprocess.Popen(command, cwd=ROOT, stdout=duo["log"].open("w"), stderr=subprocess.STDOUT)
    return {"ok": True, "booting": True, "log": str(duo["log"])}


@tool
def duo_wait(seconds: int = 50) -> dict:
    """Waits up to `seconds` for the duo's boot: ready, still booting, or failed with the boot log's end."""
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
    """Stops both games, checks no Skyrim is left and frees the duo with a log line."""
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
    return {"ok": True, "reply": session.player_command(_game(game), request)}


@tool
def scene_reset() -> dict:
    """The blank slate: both players home in the baseline cell, staged things gone, the world thawed."""
    session.unstage()
    session.reset_baseline()
    if problems := session.check_baseline():
        raise session.failure("the baseline did not come back: " + "; ".join(problems))
    session.name_players()
    return {"ok": True, "handles": session.handles()}


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
    """Sets up a scene in one call: {players: {host|guest: {near, gap, bearing}}, npcs: [{name, base | ref, frozen}],
    objects: [{name, base | ref}], items: [{who, item, count, equip}]}. Names are handles for later calls;
    host_player and guest_player always exist. Returns every handle's form id per game."""
    return {"ok": True, "handles": session.stage(spec)}


@tool
def unstage() -> dict:
    """Removes what stage made in both games and thaws the world."""
    session.unstage()
    return {"ok": True}


@tool
def observe(names: list[str]) -> dict:
    """What each game shows of each handle (an actor's status entry, a reference's world-state row), and how far
    apart the two games have it."""
    return {"ok": True, "seen": session.observe_handles(names)}


@tool
def expect(name: str, check: str = "same_place", within: float = 5.0, game: str = "", value: list[float] | None = None,
           of: str = "") -> dict:
    """The correct behaviour, checked now and recorded: "same_place" (both games within `within` units), "at" (that
    game's place within `within` of `value`), or "at_rest_of" (that game's place within `within` of where the `of`
    game's body rested, from wait_rest: replays without literal coordinates). A failing expect is the bug: the
    exported scenario keeps it."""
    return {"ok": True, **session.expect(name, check, within, game or None, value, of or None)}


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
def counters(game: str = "host", reset: bool = False) -> dict:
    """That game's named counters (Diagnostics/Counters.h): count, sum, max each; reset zeroes them after reading."""
    return {"ok": True, "counters": (session.player_command(_game(game), {"cmd": "counters", "reset": reset}) or {}).get("counters")}


@tool
def network(game: str = "host") -> dict:
    """That game's line: the session time, round trips (the host's per connection), a guest's clock syncs."""
    return {"ok": True, "network": (session.game(_game(game)).read_status() or {}).get("network")}


@tool
def walk_to(game: str, target: str, within: float = 100.0) -> dict:
    """That game's player travels to a handle along the navmesh (round walls, through rooms) as the engine moves an
    actor (AI-driven): running after an actor, walking round what stops it; the controls are the player's again after."""
    return {"ok": True, **session.walk_to(_game(game), target, within)}


@tool
def steer_to(game: str, target: str, within: float = 100.0) -> dict:
    """That game's player runs to a handle by real input (turning toward it, forward held, Always Run, sprinting), in a
    straight line: a wall stops it and the failure says how close it got."""
    return {"ok": True, **session.steer_to(_game(game), target, within)}


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
def talk_to(game: str, target: str) -> dict:
    """That game's player faces a handle and presses Activate, as a player starts a conversation; returns the lines."""
    options = session.talk_to(_game(game), target)
    return {"ok": True, "options": [option.get("text") for option in options]}


@tool
def pick(game: str, line: str) -> dict:
    """Picks a line in that game's open conversation: a number is its index, text picks the first line containing it."""
    return {"ok": True, **session.pick(_game(game), int(line) if line.isdigit() else line)}


@tool
def leave_dialogue() -> dict:
    """Closes every open conversation in both games."""
    session.close_dialogues()
    return {"ok": True}


@tool
def menu(game: str, name: str, show: bool = True) -> dict:
    """Opens or closes a menu (Console, InventoryMenu, MapMenu, ...) in that game, as the game does."""
    return {"ok": True, "reply": session.menu(_game(game), name, show)}


jobs = {}


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
    <name>), and as Code/tests/specs/<name>.json when every replayed call was a spec_step."""
    return {"ok": True, "written": session.record_export(name, doc, tags or ["recorded"])}


@tool
def record_start(name: str) -> dict:
    """Starts recording this session's calls under a name (build/coop-test/mcp/<time>-<name>.jsonl)."""
    return {"ok": True, "recording": session.record_start(name)}


@tool
def name_handle(name: str, host: int = 0, guest: int = 0) -> dict:
    """Names a form id (per game) so later calls can say `name` instead."""
    return {"ok": True, "handle": session.name_handle(name, {"host": host, "guest": guest})}


if __name__ == "__main__":
    server.run()
