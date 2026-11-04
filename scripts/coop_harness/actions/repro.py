"""The repro loop's verbs (docs/design/repro-mcp.md): the duo's lock, named handles for what a repro stages,
and the recorder that turns a session's calls into a scenario. The MCP server (scripts/coop_mcp.py) calls these;
scenarios can too."""
import contextlib
import json
import math
import os
import re
import subprocess
import sys
import time
from pathlib import Path

from ..common import BOOT_FILE, GAME, ROOT, TEST_HOME, WINDOWS
from ..duo_lock import IDLE_MINUTES
from ..errors import TestFailure

RECORDINGS = TEST_HOME / "mcp"
# Measured on the duo (docs/design/repro-mcp.md, Phase 3), units/s: a player running by input (sprint ~500); a walk
# to an NPC in Riverwood, runs and walked legs together, is planned at TRAVEL_SPEED.
RUN_SPEED, TRAVEL_SPEED = 350.0, 200.0
SPRINT_AFTER = 50.0  # units moved before Sprint is pressed: the engine ignores it from standing
STALL_UNITS, STALL_SECONDS = 40.0, 1.5  # a run that moves less in this long has met something
PATH_LEG_SECONDS = 4.0  # a walked leg round what stopped a run
CAPS_LOCK, LEFT_ALT = 0x3A, 0x38  # DirectInput scan codes: Toggle Always Run, Sprint


def travel_seconds(distance, speed=RUN_SPEED):
    """A walk's time limit: twice its straight-line time at `speed` (paths bend) and 10 s to set off, so a stuck walk
    fails in seconds."""
    return 2.0 * distance / speed + 10.0


def stalled(track, now, units=STALL_UNITS, seconds=STALL_SECONDS):
    """Whether a run's `track` [(time, position)] moved under `units` in its last `seconds` (once that long under way)."""
    if now - track[0][0] < seconds:
        return False
    past = [p for t, p in track if now - t >= seconds]
    return math.dist(past[-1][:2], track[-1][1][:2]) < units


@contextlib.contextmanager
def always_running(ctx, instance):
    """Always Run on for a run by input (Caps Lock, as a player toggles it), then as it was."""
    if ctx.local_player(instance).get("always_run") is not False:  # on, or a build without the readout
        yield
        return
    ctx.toggle_always_run(instance)
    try:
        ctx.wait_for(f"[{instance or 'host'}] Always Run is on",
                     lambda: ctx.local_player(instance).get("always_run") is True, 3)
        yield
    finally:
        ctx.toggle_always_run(instance)


def duo_home():
    """The main checkout's build/coop-test, where every worktree's duo lock lives (COOP_DUO_HOME overrides)."""
    if home := os.environ.get("COOP_DUO_HOME"):
        return Path(home)
    common = subprocess.run(["git", "-C", str(ROOT), "rev-parse", "--path-format=absolute", "--git-common-dir"],
                            capture_output=True, text=True).stdout.strip()
    return (Path(common).parent / "build/coop-test").resolve() if common else TEST_HOME


class ReproActions:
    # --- The duo's lock (coop_harness/duo_lock.py): none on Windows, one machine per duo there ---

    def duo_lock(ctx, name=None):
        if WINDOWS:
            return None
        if getattr(ctx, "DUO_LOCK", None) is None:
            from ..duo_lock import DuoLock
            ctx.DUO_LOCK = DuoLock(duo_home(), ROOT, name or os.environ.get("COOP_DUO_NAME") or ROOT.name,
                                   stop_command=[sys.executable, str(ROOT / "scripts/coop_test.py"), "stop"])
        return ctx.DUO_LOCK

    def duo_games_running(ctx):
        return any(ctx.prefix_processes(_game=game) for game in ctx.games)

    def duo_claim(ctx, owner, what, wait_seconds=0, keep_place=False):
        """Holds the shared duo (or joins this checkout's hold), waiting up to wait_seconds in the queue."""
        from ..duo_lock import DuoBusy
        if not (lock := ctx.duo_lock(owner)):
            return "no lock on Windows"
        try:
            return lock.acquire(what, timeout=wait_seconds, keep_place=keep_place)
        except DuoBusy as error:
            raise ctx.failure(str(error)) from None

    def duo_free(ctx, owner, summary):
        """Stops the duo, checks none of its Skyrims is left and frees the hold with a log line. Refused unless this
        process holds it: never stop a duo someone else started."""
        lock = ctx.duo_lock(owner)
        if lock and lock.role != "holder":
            raise ctx.failure(f"not ours to stop: this session holds no duo ({lock.role or 'none'})")
        ctx.stop_duo()
        if left := ctx.our_skyrims():
            raise ctx.failure(f"{len(left)} of our Skyrim processes still running after stop: {left}")
        if lock:
            lock.release(f"{summary}. Stopped, 0 Skyrim processes, freed")
        return "freed"

    def duo_finish(ctx):
        """A command's end: a hold whose games still run goes to a keeper (scripts/duo.py keep), else it is freed."""
        lock = getattr(ctx, "DUO_LOCK", None)
        if not lock or not lock.role:
            return
        if lock.role == "holder" and ctx.duo_games_running():
            if lock.park([sys.executable, str(ROOT / "scripts/duo.py"), "keep"]):
                print("duo: games left running; a keeper holds the duo until `coop_test.py stop` "
                      f"(or {IDLE_MINUTES:.0f} idle minutes)", file=sys.stderr)
                return
        lock.release("Command ended")

    def our_skyrims(ctx):
        """The Skyrim processes running from this checkout's test game folder (another worktree's are theirs)."""
        folder = str(GAME).replace("/", "\\")
        listing = subprocess.run(["ps", "-eo", "pid,args"], capture_output=True, text=True).stdout.splitlines()
        return [line.split(None, 1)[0] for line in listing if "SkyrimSE.exe" in line and folder.lower() in line.lower()]

    # --- Handles: a repro's names for what it staged, per game ---

    def handles(ctx):
        if not hasattr(ctx, "REPRO_HANDLES"):
            ctx.REPRO_HANDLES = {}
        return ctx.REPRO_HANDLES

    def name_handle(ctx, name, ids):
        """Names a staged thing: ids is {"host": form id, "guest": form id} (either may be missing)."""
        ctx.handles()[name] = {game: ids[game] for game in ("host", "guest") if ids.get(game)}
        return ctx.handles()[name]

    def resolve(ctx, value, game):
        """A form id in that game for a handle name, "player", or a number (or hex string) as given."""
        game = "guest" if game in ("client", "guest") else "host"
        if isinstance(value, int):
            return value
        if value == "player":
            return 0x14
        if value in ctx.handles():
            ids = ctx.handles()[value]
            if game not in ids:
                raise ctx.failure(f"{value!r} has no form id in the {game}'s game")
            return ids[game]
        try:
            return int(value, 16)
        except (TypeError, ValueError):
            raise ctx.failure(f"unknown handle {value!r} (known: {', '.join(ctx.handles()) or 'none'})") from None

    # --- The recorder ---

    def record_start(ctx, name):
        RECORDINGS.mkdir(parents=True, exist_ok=True)
        ctx.REPRO_RECORDING = RECORDINGS / f"{time.strftime('%Y%m%d-%H%M%S')}-{name}.jsonl"
        ctx.REPRO_RECORDING.write_text("")
        if save := ctx.booted_save():  # a late save's flow: the test needs the duo booted from it
            ctx.record_call("needs_save", {"save": save}, True)
        for spec in getattr(ctx, "REPRO_SCENE", []):  # the scene as staged so far: the test starts from it too
            ctx.record_call("stage", {"spec": spec}, True)
        return str(ctx.REPRO_RECORDING)

    def booted_save(ctx):
        """The save the running duo was booted from (its boot file), or None."""
        with contextlib.suppress(OSError, ValueError):
            return json.loads((ctx.game("").control / BOOT_FILE).read_text()).get("save")
        return None

    def needs_save(ctx, save):
        """The duo was booted from `save`: a recorded late-save flow starts where it was recorded."""
        if ctx.booted_save() != save:
            raise ctx.failure(f"this flow needs the duo booted from {save}: `coop_test.py duo --save {save} --keep-running`")

    def record_call(ctx, tool, arguments, ok, result=None):
        """One tool call, as the recorder keeps it (the JSON lines a scenario is exported from)."""
        path = getattr(ctx, "REPRO_RECORDING", None)
        if not path:
            return
        entry = {"time": time.time(), "tool": tool, "args": arguments, "ok": ok}
        if result is not None:
            entry["result"] = result
        with path.open("a") as file:
            file.write(json.dumps(entry, default=str) + "\n")

    # --- The world frozen and stepped (TestHarness world_freeze / step, FrameStepper) ---

    def world_clock(ctx, instance):
        reply = ctx.player_command(instance, {"cmd": "world_clock"})
        if not reply or not reply.get("ok"):
            raise ctx.failure(f"[{instance or 'host'}] no world clock: {reply}")
        return reply

    def freeze_world(ctx, frozen=True):
        """Stops (or restarts) the world's update in both games: time, AI, the player, projectiles."""
        for instance in ("", "client"):
            reply = ctx.player_command(instance, {"cmd": "world_freeze", "frozen": frozen})
            if not reply or not reply.get("ok"):
                raise ctx.failure(f"[{instance or 'host'}] world_freeze refused: {reply}")
        return {"host": ctx.world_clock("")["world_frames"], "guest": ctx.world_clock("client")["world_frames"]}

    def step_world(ctx, frames=1, seconds=10.0):
        """In a frozen world, both games run `frames` world updates and freeze again."""
        for instance in ("", "client"):
            reply = ctx.player_command(instance, {"cmd": "step", "frames": frames})
            if not reply or not reply.get("ok"):
                raise ctx.failure(f"[{instance or 'host'}] step refused: {reply}")
        clocks = ctx.wait_for(f"both games ran {frames} frames", lambda: (c := {i: ctx.world_clock(i) for i in ("", "client")})
                              and all(not x["steps_left"] for x in c.values()) and c, seconds)
        return {"host": clocks[""]["world_frames"], "guest": clocks["client"]["world_frames"]}

    # --- stage: one call sets up a scene ---

    def name_players(ctx):
        """The players' handles: host_player and guest_player, in both games."""
        ctx.name_handle("host_player", {"host": 0x14, "guest": ctx.player_copy("client")})
        ctx.name_handle("guest_player", {"guest": 0x14, "host": ctx.player_copy("")})

    def staged(ctx):
        if not hasattr(ctx, "REPRO_STAGED"):
            ctx.REPRO_STAGED = []
        return ctx.REPRO_STAGED

    def stage(ctx, spec):
        """Sets up a scene: {players: {host|guest: {near, gap, bearing}}, npcs: [{name, base | ref, frozen}],
        objects: [{name, base | ref, by}], items: [{who, item, count, equip}]}. Returns the handles."""
        ctx.name_players()
        if not hasattr(ctx, "REPRO_SCENE"):
            ctx.REPRO_SCENE = []
        ctx.REPRO_SCENE.append(spec)
        for npc in spec.get("npcs", []):
            if "ref" in npc:
                host = int(str(npc["ref"]), 16) if isinstance(npc["ref"], str) else npc["ref"]
                copy = ctx.stage_host_npc(host)
            else:
                base = npc["base"] if isinstance(npc["base"], str) else f"{npc['base']:X}"
                host, _, copy = ctx.spawn_host_npc(base, restrained=npc.get("frozen", True))
                ctx.staged().append(("npc", host))
            if npc.get("frozen", True):
                ctx.freeze("", host)
            ctx.name_handle(npc["name"], {"host": host, "guest": copy})
        for item in spec.get("items", []):
            game = "client" if item.get("who") in ("guest", "client") else ""
            code = item["item"] if isinstance(item["item"], str) else f"{item['item']:X}"
            ctx.console_on(game, f"player.additem {code} {item.get('count', 1)}")
            if item.get("equip"):
                ctx.console_on(game, f"player.equipitem {code}")
        for game, place in spec.get("players", {}).items():
            instance = "client" if game in ("guest", "client") else ""
            ctx.place_before(instance, ctx.resolve(place["near"], game), place.get("gap", 120), place.get("bearing"))
        for thing in spec.get("objects", []):
            if "ref" in thing:  # a plugin's reference: the same id in both games
                ref = int(str(thing["ref"]), 16) if isinstance(thing["ref"], str) else thing["ref"]
                ctx.name_handle(thing["name"], {"host": ref, "guest": ref})
                continue
            ctx.name_handle(thing["name"], ctx.place_object(thing["base"]))
        return ctx.handles()

    def twin_in(ctx, instance, before, host_row, seconds=10):
        """The reference new in that game since `before` with the same base, nearest the other game's: its twin."""
        def find():
            fresh = {ref: row for ref, row in ctx.world_state(instance).items() if ref not in before}
            near = sorted((math.dist(row["position"][:2], host_row["position"][:2]), ref) for ref, row in fresh.items()
                          if row.get("base") == host_row.get("base") and math.dist(row["position"][:2], host_row["position"][:2]) < 300)
            return near[0][1] if near else None
        return ctx.wait_for(f"[{instance or 'host'}] a twin of {host_row.get('id', 0):X}", find, seconds)

    def place_object(ctx, base):
        """The host places a new object at its player's feet; returns its id and its twin's in the guest's game."""
        code = base if isinstance(base, str) else f"{base:X}"
        before = {game: set(ctx.world_state(game)) for game in ("", "client")}
        ctx.console_on("", f"player.placeatme {code} 1")
        made = ctx.wait_for("[host] the new object", lambda: set(ctx.world_state("")) - before[""], 5)
        host = next(iter(made))
        ctx.staged().append(("object", host))
        guest = ctx.twin_in("client", before["client"], ctx.world_state("", [host])[host])
        ctx.staged().append(("guest-object", guest))
        return {"host": host, "guest": guest}

    def drop_item(ctx, game, item, name):
        """That game's player gets one `item` and drops it (the harness's plain drop); names it and its twin."""
        instance = "client" if game in ("guest", "client") else ""
        other = "" if instance else "client"
        item = item if isinstance(item, int) else int(item, 16)
        ctx.console_on(instance, f"player.additem {item:X} 1")
        ctx.wait_for(f"[{instance or 'host'}] the player carries {item:X}", lambda: (ctx.item_count(instance, 0x14, item) or 0) > 0, 5)
        before = set(ctx.world_state(other))
        dropped = ctx.player_command(instance, {"cmd": "drop", "item": item, "plain": True})["object"]
        ctx.staged().append(("object" if not instance else "guest-object", dropped))
        row = ctx.wait_for(f"[{instance or 'host'}] the dropped {item:X}", lambda: ctx.world_state(instance, [dropped]).get(dropped), 5)
        twin = ctx.twin_in(other, before, row)
        ctx.staged().append(("object" if other == "" else "guest-object", twin))
        mine, theirs = ("guest", "host") if instance else ("host", "guest")
        return ctx.name_handle(name, {mine: dropped, theirs: twin})

    def wait_rest(ctx, name, game="host", seconds=15):
        """Waits until that game's body of the handle sleeps (at rest)."""
        instance = "client" if game in ("guest", "client") else ""
        ref = ctx.resolve(name, game)
        ctx.wait_for(f"[{instance or 'host'}] {name} rests", lambda: (b := ctx.body_of(instance, ref)) and not b["awake"], seconds)
        position = ctx.body_of(instance, ref)["position"]
        if not hasattr(ctx, "REPRO_RESTS"):
            ctx.REPRO_RESTS = {}
        ctx.REPRO_RESTS[(name, "guest" if instance else "host")] = position
        return {"position": position}

    def unstage(ctx):
        """Removes what stage made (objects, spawned NPCs) in both games and thaws the world."""
        with contextlib.suppress(TestFailure):
            ctx.freeze_world(False)
        for kind, ref in reversed(ctx.staged()):
            game = "client" if kind == "guest-object" else ""
            ctx.console_on(game, f"{ref:X}.disable")
            ctx.console_on(game, f"{ref:X}.markfordelete")
        ctx.staged().clear()
        ctx.handles().clear()
        ctx.REPRO_SCENE = []

    def spec_step(ctx, step):
        """One shared-spec step (Code/tests/specs, scenarios/specs.run_step): a session of these exports as a spec."""
        from ..scenarios import specs
        return specs.run_step(ctx, {"name": "repro", "steps": [step]}, 0, step)

    def record_export(ctx, name, doc, tags=("recorded",)):
        """The recording as scenarios/recorded/<name>.py, and as Code/tests/specs/<name>.json when it is all spec steps."""
        entries = [json.loads(line) for line in ctx.REPRO_RECORDING.read_text().splitlines() if line]
        module = name.replace("-", "_")
        path = ROOT / "scripts/coop_harness/scenarios/recorded" / f"{module}.py"
        path.write_text(export_scenario(entries, name, doc, tags))
        written = {"scenario": str(path)}
        if spec := export_spec(entries, name, doc, tags):
            spec_path = ROOT / "Code/tests/specs" / f"{name}.json"
            spec_path.write_text(json.dumps(spec, indent=2) + "\n")
            written["spec"] = str(spec_path)
        return written

    # --- observe and expect ---

    def observe_one(ctx, name):
        """What each game shows of a handle: an actor's status entry, or a reference's world-state row."""
        shown = {}
        for game, instance in (("host", ""), ("guest", "client")):
            ref = ctx.handles().get(name, {}).get(game)
            if not ref:
                continue
            actor = ctx.actors_on(instance).get(str(ref))
            if actor is None and ref == 0x14:
                actor = ctx.local_player(instance)
            if actor is None:  # this game's own actors (the host's NPCs)
                actor = (ctx.local_player(instance).get("local_actors") or {}).get(str(ref))
            shown[game] = actor if actor is not None else ctx.world_state(instance, [ref]).get(ref)
        return shown

    def observe_handles(ctx, names):
        """{name: {host, guest, apart}}: apart is the distance between the two games' places of it."""
        seen = {}
        for name in names:
            shown = ctx.observe_one(name)
            places = [where(row) for row in shown.values() if row]
            entry = {game: row for game, row in shown.items()}
            if len(places) == 2 and all(places):
                entry["apart"] = round(math.dist(places[0], places[1]), 2)
            seen[name] = entry
        return seen

    def expect(ctx, name, check, within=5.0, game=None, value=None, of=None):
        """The correct behaviour, checked now: "same_place" (the two games within `within` units), "at" (that game's
        place within `within` of `value`), or "at_rest_of" (that game's place within `within` of where the `of`
        game's body rested, as wait_rest saw it), or "stage" (quest `name` at stage value[0] in both games)."""
        if check == "stage":  # `name` a quest's editor id, `value` [stage]: both games at it
            stages = {}
            for label, instance in (("host", ""), ("guest", "client")):
                printed = " ".join(ctx.console_on(instance, f"getstage {name}") or [])
                found = re.search(r">>\s*(-?[\d.]+)", printed)
                stages[label] = float(found.group(1)) if found else None
            if any(stage != float(value[0]) for stage in stages.values()):
                raise ctx.failure(f"{name}: not at stage {value[0]:g} in both games: {stages}")
            return {"stages": stages}
        shown = ctx.observe_one(name)
        if check == "at_rest_of":
            game = "guest" if game in ("guest", "client") else "host"
            other = "guest" if of in ("guest", "client") else "host"
            rest = getattr(ctx, "REPRO_RESTS", {}).get((name, other))
            if rest is None:
                raise ctx.failure(f"{name}: no rest seen in the {other}'s game (wait_rest first)")
            body = ctx.body_of("client" if game == "guest" else "", ctx.resolve(name, game))
            place = body and body["position"]
            if place is None or math.dist(place, rest) > within:
                off = f"{math.dist(place, rest):.1f}" if place else "?"
                raise ctx.failure(f"{name}: the {game}'s is not where the {other}'s rested at {[round(v) for v in rest]}: "
                                  f"at {[round(v) for v in place] if place else None}, {off} off (at most {within})")
            return {"off": round(math.dist(place, rest), 2)}
        if check == "same_place":
            places = {g: where(row) for g, row in shown.items() if row}
            if len(places) < 2 or None in places.values():
                raise ctx.failure(f"{name}: not shown in both games ({sorted(places)})")
            apart = math.dist(places["host"], places["guest"])
            if apart > within:
                raise ctx.failure(f"{name}: the guest's is {apart:.1f} units from the host's (at most {within}): "
                                  f"host {[round(v) for v in places['host']]}, guest {[round(v) for v in places['guest']]}")
            return {"apart": round(apart, 2)}
        if check == "at":
            place = where(shown.get(game or "host") or {})
            if place is None or math.dist(place, value) > within:
                raise ctx.failure(f"{name}: in the {game or 'host'}'s game at {place}, not within {within} of {value}")
            return {"at": place}
        raise ctx.failure(f"unknown check {check!r}")


def where(row):
    """A shown thing's place: an actor's actual or position, a reference's position."""
    if not row:
        return None
    return row.get("actual") or row.get("position")


# Recorded tool -> its scenario line; None: not part of the test (looking, the duo, the recorder).
_INSTANCE = {"host": "", "guest": "client", "client": "client", "": ""}


# Harness commands that only read: left out of an exported scenario.
_READS = {"status", "dialogue", "camera", "world_clock", "counters", "papyrus_result", "path_request", "screen", "item_count"}


def _line(tool, args):
    game = repr(_INSTANCE.get(args.get("game", "host"), ""))
    match tool:
        case "world_freeze":
            return f"ctx.freeze_world({args.get('frozen', True)!r})"
        case "step":
            return f"ctx.step_world({args.get('frames', 1)!r})"
        case "stage":
            return f"ctx.stage({args['spec']!r})"
        case "name_handle":
            return f"ctx.name_handle({args['name']!r}, {{'host': {args.get('host', 0)!r}, 'guest': {args.get('guest', 0)!r}}})"
        case "needs_save":
            return f"ctx.needs_save({args['save']!r})"
        case "console":
            if re.match(r"(\S+\.)?(get|is)\w*\b", args["line"], re.I):  # a read: looking, not part of the test
                return None
            return f"ctx.console_on({game}, {args['line']!r})"
        case "harness":
            if args["request"].get("cmd") in _READS:
                return None
            return f"ctx.player_command({game}, {args['request']!r})"
        case "act":
            return f"ctx.act({game}, {args['spec']!r})"
        case "press":
            return f"ctx.hold_key({game}, {args['key']!r}, {args.get('seconds', 0.1)!r})"
        case "mouse":
            return f"ctx.mouse_on({game}, {args.get('action', 'click')!r}, {args.get('button', 1)!r})"
        case "expect":
            extra = "".join(f", {key}={args[key]!r}" for key in ("within", "game", "value", "of") if args.get(key) not in (None, ""))
            return f"ctx.expect({args['name']!r}, {args['check']!r}{extra})"
        case "drop":
            return f"ctx.drop_item({args['game']!r}, {args['item']!r}, {args['name']!r})"
        case "wait_rest":
            return f"ctx.wait_rest({args['name']!r}, {args.get('game', 'host')!r})"
        case "walk_to":
            return f"ctx.walk_to({game}, {args['target']!r}, {args.get('within', 100.0)!r})"
        case "steer_to":
            return f"ctx.steer_to({game}, {args['target']!r}, {args.get('within', 100.0)!r})"
        case "arrives_with_party":
            return f"ctx.arrives_with_party({game}, {args.get('within', 3000.0)!r})"
        case "travel":
            return f"ctx.go_far({game}, {args['command']!r})"
        case "talk_to":
            return f"ctx.talk_to({game}, {args['target']!r})"
        case "pick":
            line = args["line"]
            return f"ctx.pick({game}, {int(line) if str(line).isdigit() else line!r})"
        case "leave_dialogue":
            return "ctx.close_dialogues()"
        case "menu":
            return f"ctx.menu({game}, {args['name']!r}, {args.get('show', True)!r})"
        case "spec_step":
            return f"ctx.spec_step({args['step']!r})"
    return None


def export_scenario(entries, name, doc, tags):
    """A recorded session as a scenario module: its calls in order (failed ones left out, except expects: a failing
    expect is the bug), in a try that unstages and thaws the world."""
    lines = [line for entry in entries if entry["ok"] or entry["tool"] == "expect"
             if (line := _line(entry["tool"], entry["args"]))]
    if not lines:
        raise ValueError("nothing recorded to replay")
    body = "\n".join(f"        {line}" for line in lines)
    if any(entry["tool"] == "needs_save" for entry in entries):  # the runner's saved-story fixture
        tags = [*tags, "late-save"]
    return (f'"""{doc}\n\nRecorded with the repro MCP (docs/design/repro-mcp.md)."""\n'
            f"NAME = {name!r}\nTAGS = {sorted(set(tags))!r}\nDOC = {doc!r}\n\n\n"
            f"def run(ctx):\n    try:\n{body}\n    finally:\n        ctx.unstage()\n")


def export_spec(entries, name, doc, tags):
    """A recorded session made only of spec steps, as a shared spec (Code/tests/specs): None when a call isn't one."""
    replayed = [entry for entry in entries if _line(entry["tool"], entry["args"])]
    if not replayed or any(entry["tool"] != "spec_step" for entry in replayed):
        return None
    return {"name": name, "doc": doc, "tags": sorted(set(tags)), "steps": [entry["args"]["step"] for entry in replayed]}


class PressTiming:
    """Frames and ms from a real key press to the player's own motion: our games through the animation trace, the
    vanilla stand-in through its player_trace (red flag 3's measurement, kept as a verb)."""

    def press_to_motion(ctx, instance, key="w", presses=5, hold=0.5, vanilla=False):
        env, window = ctx.game_window(instance)
        pressed_at = []
        def press_all(between):
            for _ in range(presses):
                between()
                subprocess.run(["xdotool", "windowfocus", window], env=env, check=True, stderr=subprocess.DEVNULL)
                subprocess.run(["xdotool", "keydown", key], env=env, check=True, stderr=subprocess.DEVNULL)
                pressed_at.append(time.time())
                time.sleep(hold)
                subprocess.run(["xdotool", "keyup", key], env=env, check=True, stderr=subprocess.DEVNULL)
        if vanilla:
            game = ctx.game(instance)
            switch, trace = game.control / "player_trace.on", game.control / "player_trace.txt"
            trace.unlink(missing_ok=True)
            switch.write_text("")
            time.sleep(0.6)  # the stand-in checks the switch every 250 ms
            press_all(lambda: time.sleep(1.0))
            time.sleep(1.0)
            switch.unlink(missing_ok=True)
            points = [(int(us) / 1e6, (float(x), float(y))) for us, x, y, _, _ in
                      (line.split() for line in trace.read_text().splitlines())]
        else:
            from ..objects import AnimTrace
            with AnimTrace(ctx, f"press-{instance or 'host'}", {instance: {0x14: "me"}}, video=False) as recorded:
                press_all(lambda: (f := ctx.game_frame(instance)) and ctx.wait_for(
                    "standing still", lambda: ctx.game_frame(instance) > f + 50, 5))
                frame = ctx.game_frame(instance)
                ctx.wait_for("a last second", lambda: ctx.game_frame(instance) > frame + 60, 5)
            points = [(e["time"], tuple(e["pos"][:2])) for e in recorded.entries if e["kind"] == "pos" and e["who"] == "me"]
        rows = []
        for pressed in pressed_at:
            before = [p for p in points if p[0] <= pressed]
            after = [p for p in points if p[0] > pressed]
            if not before or not after:
                continue
            moved = next((i for i, p in enumerate(after) if math.dist(p[1], before[-1][1]) > 0.5), None)
            rows.append({"frames": None if moved is None else moved + 1,
                         "ms": None if moved is None else round((after[moved][0] - pressed) * 1000)})
        return rows


class PlayerVerbs:
    """A player's verbs for playing real flows (docs/design/repro-mcp.md, phase 3): walking, talking, menus. Real input
    through the engine's dispatch (checked actions), so a flow goes as a player's would."""

    def walk_to(ctx, instance, target, within=100.0, seconds=None):
        """The player travels to `target` along the navmesh as the engine moves an actor (AI-driven, as vanilla's
        scenes drive the player): running after an actor (Actor.KeepOffsetFromActor, catching up), and where that
        stalls (or for an object) a stretch of Actor.PathToReference, which only walks. Until within `within` units;
        then the player's controls are its own again. When the engine finds no way, it runs straight (`steer_to`)."""
        game = "guest" if instance else "host"
        ref = ctx.resolve(target, game)
        start = ctx.my_position(instance)
        limit = seconds or travel_seconds(math.dist(start[:2], ctx.place_of(instance, ref)[:2]), TRAVEL_SPEED)
        deadline = time.monotonic() + limit
        follows = ctx.is_actor(instance, ref)
        walk = {"legs": 0, "runs": 0, "closest": math.inf, "refused": False}
        apart = lambda: math.dist(ctx.my_position(instance)[:2], ctx.place_of(instance, ref)[:2])
        ctx.player_script_call(instance, "Game", "SetPlayerAIDriven", [True])
        try:
            while (left := apart()) > within and not walk["refused"]:
                walk["closest"] = min(walk["closest"], left)
                if time.monotonic() > deadline:
                    raise ctx.failure(f"[{instance or 'host'}] did not reach {target} in {limit:.0f} s along the navmesh: "
                                      f"closest {walk['closest']:.0f} units (at most {within}), {walk['runs']} runs and "
                                      f"{walk['legs']} walked legs")
                if follows:
                    walk["runs"] += 1
                    if ctx.follow_run(instance, ref, within, deadline) != "stalled":
                        continue
                walk["legs"] += 1
                walk["refused"] = ctx.path_leg(instance, ref, within, deadline) == "refused"
        finally:
            ctx.player_script_call(instance, "Game", "SetPlayerAIDriven", [False])
        walked = lambda: round(math.dist(start[:2], ctx.my_position(instance)[:2]), 1)
        if walk["refused"] and apart() > within:  # no navmesh path to it (a guest's NPC copy, docs/design/repro-mcp.md)
            ctx.steer_to(instance, target, within)
            return {"walked": walked(), "pathed": False, "runs": walk["runs"], "legs": walk["legs"]}
        return {"walked": walked(), "pathed": True, "runs": walk["runs"], "legs": walk["legs"]}

    def is_actor(ctx, instance, ref):
        """Whether that game has `ref` as an actor (a player, a copy, its own NPC), not an object."""
        return (ref == 0x14 or str(ref) in ctx.actors_on(instance)
                or str(ref) in (ctx.local_player(instance).get("local_actors") or {}))

    def follow_run(ctx, instance, ref, within, deadline):
        """The AI-driven player runs after actor `ref` (KeepOffsetFromActor: catching up beyond `within`, which
        runs): "arrived", "stalled" (under STALL_UNITS in STALL_SECONDS: something in the way) or "late"."""
        ctx.player_command(instance, {"cmd": "papyrus_call", "class": "Actor", "function": "KeepOffsetFromActor",
                                      "args": [{"form": ref}, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, within, within / 2],
                                      "player_action": True, "self": 0x14})
        try:
            track = [(time.monotonic(), ctx.my_position(instance))]
            while time.monotonic() < deadline:
                here = ctx.my_position(instance)
                if math.dist(here[:2], ctx.place_of(instance, ref)[:2]) <= within:
                    return "arrived"
                now = time.monotonic()
                track.append((now, here))
                if stalled(track, now):
                    return "stalled"
                time.sleep(0.1)
            return "late"
        finally:
            ctx.player_command(instance, {"cmd": "papyrus_call", "class": "Actor", "function": "ClearKeepOffsetFromActor",
                                          "args": [], "player_action": True, "self": 0x14})

    def path_leg(ctx, instance, ref, within, deadline, seconds=PATH_LEG_SECONDS):
        """A stretch along the navmesh at the engine's walk (PathToReference, its fastest: 1.0), round what stopped a
        run: until "arrived", the engine ends it ("refused" at once: no path; "ended"), or `seconds` pass ("done")."""
        call = ctx.player_command(instance, {"cmd": "papyrus_call", "class": "Actor", "function": "PathToReference",
                                             "args": [{"form": ref}, 1.0], "player_action": True, "self": 0x14})["id"]
        began = time.monotonic()
        while time.monotonic() < min(deadline, began + seconds):
            if math.dist(ctx.my_position(instance)[:2], ctx.place_of(instance, ref)[:2]) <= within:
                return "arrived"
            answer = ctx.player_command(instance, {"cmd": "papyrus_result", "id": call})
            if answer.get("done"):
                return "ended" if answer.get("value") or time.monotonic() - began > 1.0 else "refused"
            time.sleep(0.1)
        return "done"  # the next run (or leg) replaces this path

    def run_straight(ctx, instance, ref, within, sprint=True):
        """The player turns toward `ref` and runs there by real input (forward held, Always Run on, Sprint once
        moving). True when within `within`; False when it stalls (a wall, a fence) or time is up."""
        here = ctx.my_position(instance)
        spec = {"name": f"runs to {ref:X}", "ready": ["takes_input", "on_ground"],
                "turn": {"face": ref, "rate": 180}, "buttons": ["forward"],
                "until": [{"near_xy": ["player", ref, within]}],
                "seconds": travel_seconds(math.dist(here[:2], ctx.place_of(instance, ref)[:2]))}
        action, sprinting = ctx.act_start(instance, spec), False
        try:
            track = [(time.monotonic(), here)]
            while True:
                state = ctx.player_command(instance, {"cmd": "act_state"})
                if state.get("id") != action or state.get("phase") == "ended":
                    return (state.get("outcome") or {}).get("status") == "done"
                now, at = time.monotonic(), ctx.my_position(instance)
                if sprint and not sprinting and math.dist(at[:2], here[:2]) > SPRINT_AFTER:  # sprint starts only once moving
                    sprinting = ctx.sprint_key(instance, True)
                track.append((now, at))
                if now - track[0][0] > 1.0 and stalled(track[1:], now):  # a second to turn and set off
                    ctx.player_command(instance, {"cmd": "act_cancel"})
                    return False
                time.sleep(0.1)
        finally:
            if sprinting:
                ctx.sprint_key(instance, False)

    def steer_to(ctx, instance, target, within=100.0, sprint=True):
        """The player turns toward `target` and runs (real input, Always Run, sprinting once moving) until within
        `within` units of it, in a straight line: no pathing, so a wall stops it and it fails naming how close it got."""
        game = "guest" if instance else "host"
        ref = ctx.resolve(target, game)
        with always_running(ctx, instance):
            if not ctx.run_straight(instance, ref, within, sprint):
                closest = math.dist(ctx.my_position(instance)[:2], ctx.place_of(instance, ref)[:2])
                raise ctx.failure(f"[{instance or 'host'}] did not reach {target} in a straight line: stopped "
                                  f"{closest:.0f} units off (at most {within})")
        return {"apart": round(math.dist(ctx.my_position(instance)[:2], ctx.place_of(instance, ref)[:2]))}

    def toggle_always_run(ctx, instance):
        """Caps Lock pressed and released through the game's input dispatch (Toggle Always Run)."""
        ctx.player_command(instance, {"cmd": "key", "scan": CAPS_LOCK, "down": True})
        frame = ctx.game_frame(instance)
        ctx.wait_for(f"[{instance or 'host'}] a frame for the key", lambda: ctx.game_frame(instance) > frame, 3)
        ctx.player_command(instance, {"cmd": "key", "scan": CAPS_LOCK, "down": False})

    def sprint_key(ctx, instance, down):
        """Left Alt (Sprint) down or up through the game's input dispatch; True once sent."""
        return bool(ctx.player_command(instance, {"cmd": "key", "scan": LEFT_ALT, "down": down}).get("ok"))

    def go_far(ctx, instance, command):
        """That game's player goes far by a console command (coc): until it is 2000+ units off."""
        before = ctx.my_position(instance)
        ctx.console_on(instance, command)
        ctx.wait_for(f"[{instance or 'host'}] arrives after '{command}'",
                     lambda: (here := ctx.local_player(instance).get("position")) and math.dist(here, before) > 2000, 90)
        ctx.wait_for(f"[{instance or 'host'}] the arrival's fade is over", lambda: ctx.takes_input(instance), 15)
        return {"at": [round(v) for v in ctx.my_position(instance)]}

    def arrives_with_party(ctx, instance, within=3000.0):
        """That game's player arrives near the other player (the party tether after the other's trip), its fade over."""
        other = "" if instance else "client"
        ctx.wait_for(f"[{instance or 'host'}] arrives with the other player (the party tether)",
                     lambda: math.dist(ctx.my_position(instance)[:2], ctx.my_position(other)[:2]) <= within, 90)
        ctx.wait_for(f"[{instance or 'host'}] the arrival's fade is over", lambda: ctx.takes_input(instance), 15)
        return {"apart": round(math.dist(ctx.my_position(instance)[:2], ctx.my_position(other)[:2]))}

    def place_of(ctx, instance, ref):
        """Where that game has a reference or actor (its status entry, or its world-state row)."""
        if ref == 0x14:
            return ctx.my_position(instance)
        actor = ctx.actors_on(instance).get(str(ref))
        if actor and actor.get("actual"):
            return actor["actual"]
        local = (ctx.local_player(instance).get("local_actors") or {}).get(str(ref))  # this game's own actors
        if local and local.get("position"):
            return local["position"]
        row = ctx.world_state(instance, [ref]).get(ref)
        if not row:
            raise ctx.failure(f"[{instance or 'host'}] {ref:X} is not in this game")
        return row["position"]

    def talk_to(ctx, instance, target):
        """The player faces `target` and presses Activate, as a player starts a conversation; returns its options."""
        game = "guest" if instance else "host"
        ref = ctx.resolve(target, game)
        if math.dist(ctx.my_position(instance)[:2], ctx.place_of(instance, ref)[:2]) > 150:  # out of reach (it moved off)
            ctx.walk_to(instance, target, 100.0)
        if ctx.player_command(instance, {"cmd": "camera"}).get("state") in (8, 9):  # third person: the view key, as a player
            ctx.hold_key(instance, "f", 0.1)
            ctx.wait_for(f"[{instance or 'host'}] first person", lambda: ctx.player_command(instance, {"cmd": "camera"}).get("state") == 0, 5)
        ctx.act(instance, {"name": f"talks to {target}", "ready": ["takes_input"], "turn": {"face": ref, "rate": 180},
                           "until": ["aimed"], "seconds": 3})
        ctx.act(instance, {"name": "presses Activate", "ready": ["takes_input"], "buttons": ["activate"],
                           "until": [{"for": 0.15}], "seconds": 1, "allow_menus": True})
        ctx.wait_for(f"[{instance or 'host'}] the conversation with {target} opens", lambda: ctx.talking_to(instance, ref), 10)
        return ctx.wait_for(f"[{instance or 'host'}] the menu lists options", lambda: ctx.dialogue(instance).get("options"), 10)

    def pick(ctx, instance, line):
        """Picks a line in that game's open conversation: its index, or (a string) the first whose text contains it."""
        options = ctx.dialogue(instance).get("options") or []
        if isinstance(line, str):
            index = next((i for i, option in enumerate(options) if line.lower() in option.get("text", "").lower()), None)
            if index is None:
                raise ctx.failure(f"[{instance or 'host'}] no line like {line!r}: {[o.get('text') for o in options]}")
        else:
            index = line
        reply = ctx.player_command(instance, {"cmd": "dialogue_pick", "index": index})
        if not reply or not reply.get("ok"):
            raise ctx.failure(f"[{instance or 'host'}] the pick was refused: {reply}")
        before = [option.get("text") for option in options]
        speaking = {"seen": False}
        def answered():  # the speaker has said the response: new options, its line over, or the conversation over
            shown = ctx.dialogue(instance)
            if not shown.get("open"):
                return {"closed": True}
            speaking["seen"] = speaking["seen"] or bool(shown.get("current_info"))
            after = [option.get("text") for option in shown.get("options") or []]
            done = after and (after != before or (speaking["seen"] and not shown.get("current_info")))
            return done and {"options": after}
        said = ctx.wait_for(f"[{instance or 'host'}] the response to {before[index] if index < len(before) else index!r}",
                            answered, 60)
        return {"picked": before[index] if index < len(before) else index, **said}

    def menu(ctx, instance, name, show=True):
        """Opens or closes a menu in that game, as the game does."""
        reply = ctx.player_command(instance, {"cmd": "menu", "name": name, "show": show})
        if not reply or not reply.get("ok"):
            raise ctx.failure(f"[{instance or 'host'}] menu {name} refused: {reply}")
        return reply
