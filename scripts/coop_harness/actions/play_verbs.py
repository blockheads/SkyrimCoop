"""A player's verbs for playing real flows (docs/design/repro-mcp.md): walking, talking, using, fighting. Goal verbs
(`goal`) report one outcome, kept for `why`, and fail naming the reason; the MCP server and scenarios call the same code."""
import contextlib
import functools
import json
import math
import time

from ..errors import TestFailure
from ..objects import AnimTrace
from .. import terrain as terrain_views
from ..common import TEST_HOME
from ..utilities import log

# Measured on the duo (docs/design/repro-mcp.md, Phase 3), units/s: a player running by input (sprint ~500); a walk
# to an NPC in Riverwood, runs and walked legs together, is planned at TRAVEL_SPEED.
RUN_SPEED, TRAVEL_SPEED, WALK_SPEED = 350.0, 200.0, 80.0  # WALK_SPEED: PathToReference's fastest
SPRINT_AFTER = 50.0  # units moved before Sprint is pressed: the engine ignores it from standing
STALL_UNITS, STALL_SECONDS = 40.0, 1.5  # a run that moves less in this long has met something
PATH_LEG_SECONDS = 4.0  # a walked leg round what stopped a run
CROSSING_SPACING, CROSSING_REACH = 30.0, 60.0  # a crossing's waypoints: kept apart, reached within
MAX_CROSSINGS = 2  # cuts crossed by input in one walk
PATH_START = 200.0  # units of a refused walk's path run by input before pathing again
OFF_NAVMESH = 40.0  # units from the start's navmesh point past which a walk first steps onto it
NAVMESH_REACH = 30.0  # units, a waypoint of that step (a running player overshoots closer ones)
TALK_REACH = 150.0  # units, 3D, within the activation pick (fActivatePickLength 180 from the eyes)
TALK_TRIES = 3  # presses at an NPC walking on, as a player follows it
SET_OFF_SECONDS = 5.0  # KeepOffsetFromActor takes a package evaluation to set off: 2 s and more on a busy machine
CAPS_LOCK, LEFT_ALT, SPACE = 0x3A, 0x38, 0x39  # DirectInput scan codes: Toggle Always Run, Sprint, Jump


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


def set_off(start, here, seconds):
    """Whether a run is under way, so standing still means something: it has moved, or had its time to."""
    return seconds >= SET_OFF_SECONDS or math.dist(start[:2], here[:2]) >= STALL_UNITS


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


REACH = 130.0  # units a player can activate from, with room to spare
MELEE_REACH = 110.0
USE_SECONDS = 5.0  # what an activation changes shows by then


def verb_failure(ctx, why, text, **facts):
    """A goal verb's failure: `why` is one word (docs/design/repro-mcp.md, play), `facts` go in its outcome."""
    error = ctx.failure(text)
    error.outcome = {"why": why, **facts}
    return error


def probe_line(probe):
    """A path_probe in a few words: the search's answer and where each end landed on the navmesh."""
    if not probe:
        return "no path probe"
    def end(name):
        part = probe[name]
        if not part["resolved"]:
            return f"{name} on no navmesh"
        off = math.dist(part["at"], part["point"])
        door = part.get("closed_door") or "0"
        return (f"{name} navmesh {part['navmesh']} tri {part['triangle']} ({off:.0f} off)"
                + (f" under closed door {door}" if door != "0" else ""))
    found = "found" if probe["built"] else "none"
    line = f"engine search {found} ({probe['nodes']} nodes), {end('start')}, {end('goal')}"
    if probe.get("same_navmesh"):
        line += (f"; triangles join in {probe['hops']} ({probe['closed_doors_on_way']} closed doors, "
                 f"{probe.get('ledges_on_way') or 0} ledges on the way)" if probe.get("linked") else
                 f"; no triangles join them (the goal's piece: {probe.get('goal_piece') or 0} triangles, "
                 f"{probe.get('goal_piece_exits') or 0} links out)")
        line += f", {probe.get('obstacles') or 0} obstacles"
        cutting = [o for o in probe.get("obstacle_refs") or [] if o.get("on_navmesh")]
        if cutting:
            line += " (" + ", ".join(f"{o['ref']}{' ' + o['name'] if o.get('name') else ''}{' ' + o['role'] if o.get('role') else ' actor' if o.get('actor') else ''}"
                                     f" {o['distance']:.0f} away" for o in cutting) + ")"
        doors = lambda key: ",".join(probe.get(key) or []) or "none"
        line += f"; doors: start's piece {doors('start_doors')}, goal's {doors('goal_doors')}, closed {doors('closed_doors')}"
        if crossing := probe.get("crossing"):
            line += f"; crossable by input: a {crossing['gap']:.0f}-unit step after {len(crossing['route'])} waypoints"
    return line


def path_start(probe, reach=PATH_START):
    """A refused walk from a spot the search starts badly from (by a door): the first stretch of the search's path, or
    of the triangles' way when the search has none, as a crossing to run by input before pathing again."""
    points = (probe.get("path") or []) if probe.get("built") else (probe.get("way") or []) if probe.get("linked") else []
    stretch, length = [], 0.0
    for before, here in zip(points, points[1:]):
        stretch.append(here)
        length += math.dist(before, here)
        if length >= reach:
            break
    return {"route": stretch[:-1], "to": stretch[-1], "gap": 0.0} if stretch else None


def walkable(probe):
    """A path_probe says the player can walk there: the search's path, or its navmesh's triangles joining them (the
    search fails from some spots, by a door: walk_to steps off them; a wall's cut leaves the triangles apart)."""
    return bool(probe.get("built") or probe.get("linked"))


def turning_points(route):
    """A cell-to-cell way's corners and its end: a run between corners stays in walkable cells."""
    points = []
    for before, here, after in zip(route, route[1:], route[2:]):
        if (here[0] - before[0], here[1] - before[1]) != (after[0] - here[0], after[1] - here[1]):
            points.append(here)
    return points + [route[-1]]


def off_navmesh(end):
    """How far a path_probe end's asked point is from its point on the navmesh, units."""
    end = end or {}
    return math.dist(end["at"], end["point"]) if end.get("resolved") and end.get("at") and end.get("point") else 0.0


def crossing_points(crossing, spacing=CROSSING_SPACING):
    """A crossing's waypoints (one leg each, inside one triangle: thinning more cuts corners into walls), then the step
    across."""
    points = []
    for at in crossing["route"]:
        if not points or math.dist(points[-1][:2], at[:2]) >= spacing or at is crossing["route"][-1]:
            points.append(list(at))
    return points + [list(crossing["to"])]


def navmesh_walk(ctx, instance, target, ref, within, deadline, limit, walk, follows, apart):
    """walk_to's stretch along the navmesh, AI-driven, until there, out of time or refused (walk["refused"])."""
    ctx.player_script_call(instance, "Game", "SetPlayerAIDriven", [True])
    try:
        while (left := apart()) > within and not walk["refused"]:
            walk["closest"] = min(walk["closest"], left)
            if time.monotonic() > deadline:
                raise verb_failure(ctx, walk_why(walk, left), f"[{instance or 'host'}] did not reach {target} in {limit:.0f} s "
                                   f"along the navmesh: closest {walk['closest']:.0f} units (at most {within}), "
                                   f"{walk['runs']} runs and {walk['legs']} walked legs", closest=round(walk["closest"]),
                                   runs=walk["runs"], legs=walk["legs"])
            if follows:
                walk["runs"] += 1
                if ctx.follow_run(instance, ref, within, deadline) != "stalled":
                    continue
                walk["stalls"] += 1
            walk["legs"] += 1
            walk["refused"] = ctx.path_leg(instance, ref, within, deadline) == "refused"
    finally:
        ctx.player_script_call(instance, "Game", "SetPlayerAIDriven", [False])


def walk_why(walk, apart):
    """Why a walk ran out of time: the target left, something kept stopping it, or just the clock."""
    if apart > walk["closest"] + 500:
        return "moved_off"
    return "stalled" if walk.get("stalls") else "timeout"


def trace_summary(entries, stall_speed=20.0, stall_seconds=1.0, window=0.25):
    """A verb's trace in a few numbers: the player's speeds, where it stood still, the last graph events."""
    points = [(e["time"], e["pos"]) for e in entries if e.get("kind") == "pos" and e.get("who") == "me"]
    speeds, last = [], None
    for at, place in points:
        if last is None or at - last[0] >= window:
            if last is not None:
                speeds.append((last[0], math.dist(place[:2], last[1][:2]) / (at - last[0])))
            last = (at, place)
    stalls, began = [], None
    for at, speed in speeds + [(points[-1][0] if points else 0.0, math.inf)]:
        if speed < stall_speed:
            began = at if began is None else began
        elif began is not None:
            if at - began >= stall_seconds:
                stalls.append([round(began - points[0][0], 1), round(at - began, 1)])
            began = None
    moving = [speed for _, speed in speeds]
    events = [e.get("event") for e in entries if e.get("kind") == "graph" and e.get("who") == "me"]
    return {"speed": moving and {"min": round(min(moving)), "mean": round(sum(moving) / len(moving)), "max": round(max(moving))},
            "stalls": stalls, "events": events[-6:]}


def goal(function):
    """A goal verb: its outcome {done, how, why, seconds, trace} is kept for `why`; a failure carries it too."""
    @functools.wraps(function)
    def run(ctx, instance, target="", *args, **kwargs):
        began, outcome, error = time.monotonic(), {}, None
        ctx.refresh_handles()
        with ctx.verb_trace(function.__name__, instance, target) as trace:
            try:
                outcome = function(ctx, instance, target, *args, **kwargs)
            except TestFailure as failed:
                error = failed
        report = {"verb": function.__name__, "game": "guest" if instance else "host", "target": target, "done": not error,
                  **outcome, **getattr(error, "outcome", {}), "seconds": round(time.monotonic() - began, 1),
                  "trace": {**trace_summary(trace.entries), "file": str(trace.dir / "timeline.txt")}}
        ctx.LAST_VERB = report
        if error:
            report.setdefault("why", "timeout")
            report["failure"] = str(error)
            error.outcome = report
            raise error
        return report
    return run


class PlayerVerbs:
    """A player's verbs for playing real flows (docs/design/repro-mcp.md, phase 3): walking, talking, menus. Real input
    through the engine's dispatch (checked actions), so a flow goes as a player's would."""

    def walk_to(ctx, instance, target, within=100.0, seconds=None, straight=True):
        """The AI-driven player travels to `target` along the navmesh, within `within` units (repro-mcp.md, Phase 3);
        with no way there it runs straight (`steer_to`), or with `straight` off fails saying so."""
        game = "guest" if instance else "host"
        ref = ctx.resolve(target, game)
        start = ctx.my_position(instance)
        follows = ctx.is_actor(instance, ref)  # an object takes walked legs only
        apart = lambda: math.dist(ctx.my_position(instance)[:2], ctx.place_of(instance, ref)[:2])
        probe = (ctx.path_probe(instance, ref) or {}) if apart() > within else {}
        if probe and not probe.get("built") and off_navmesh(probe.get("start")) > OFF_NAVMESH and ctx.step_onto_navmesh(instance):
            probe = ctx.path_probe(instance, ref) or {}  # the search again, from the navmesh
        way = max(math.dist(start[:2], ctx.place_of(instance, ref)[:2]), probe.get("path_length") or 0)  # round a river
        limit = seconds or travel_seconds(way, TRAVEL_SPEED if follows else WALK_SPEED)
        deadline = time.monotonic() + limit
        walk = {"legs": 0, "runs": 0, "stalls": 0, "closest": math.inf, "refused": False, "crossings": 0}
        while apart() > within:
            # the navmesh's pieces don't join (an obstacle cuts it): across by input first, then on by path
            if probe is None:
                probe = (ctx.path_probe(instance, ref) or {}) if walk["crossings"] < MAX_CROSSINGS else {}
            crossing = None if probe.get("built") else probe.get("crossing")  # the engine paths there: no cut to cross
            if not crossing:
                navmesh_walk(ctx, instance, target, ref, within, deadline, limit, walk, follows, apart)
                if not walk["refused"] or apart() <= within or walk["crossings"] >= MAX_CROSSINGS:
                    break
                probe = ctx.path_probe(instance, ref) or {}
                crossing = probe.get("crossing") or path_start(probe)
                if not crossing:
                    break
            ctx.cross(instance, crossing)  # an obstacle cuts the navmesh: across it by input, then on by path
            walk["crossings"] += 1
            probe = None  # asked again from across
            walk["refused"] = False
            deadline = time.monotonic() + limit  # the crossing took its own time
        walked = lambda: round(math.dist(start[:2], ctx.my_position(instance)[:2]), 1)
        if walk["refused"] and apart() > within:  # no navmesh path to it (a guest's NPC copy, docs/design/repro-mcp.md)
            probe = ctx.path_probe(instance, ref)
            if not straight:
                raise verb_failure(ctx, "no_path", f"[{instance or 'host'}] the engine has no path to {target}: stopped "
                                   f"{apart():.0f} units off (at most {within}); {probe_line(probe)}; straight=True runs "
                                   "there in a line", closest=round(apart()), runs=walk["runs"], legs=walk["legs"], probe=probe)
            walk["probe"] = probe
            log(f"[{instance or 'host'}] the engine refused the walk to {target} after {walk['legs']} legs and {walk['runs']} "
                f"runs: {probe_line(probe)}; running straight")
            ctx.steer_to(instance, target, within)
            return {"walked": walked(), "pathed": False, "runs": walk["runs"], "legs": walk["legs"],
                    "crossings": walk["crossings"], "probe": probe_line(probe)}
        return {"walked": walked(), "pathed": True, "runs": walk["runs"], "legs": walk["legs"], "crossings": walk["crossings"]}

    def cross(ctx, instance, crossing):
        """The player runs by input along a path_probe's crossing: its piece's way to the cut (waypoints), then the step
        across onto the other piece, where the navmesh joins the target again (docs/re/systems/pathing.md)."""
        points = crossing_points(crossing)
        with always_running(ctx, instance):
            for at in points:
                reached = ctx.run_straight(instance, at, CROSSING_REACH, sprint=False, swim=True, jump=True)  # water, low walls
                if at is points[-1] or not reached:
                    log(f"[{instance or 'host'}] crossing: waypoint {points.index(at) + 1} of {len(points)} "
                        f"{'reached' if reached else 'stalled'} at {[round(v) for v in ctx.my_position(instance)]} (aim {[round(v) for v in at]})")
                if not reached:
                    here = ctx.my_position(instance)
                    raise ctx.failure(f"[{instance or 'host'}] stalled crossing the navmesh's cut by input: "
                                      f"{math.dist(here[:2], at[:2]):.0f} units from waypoint {points.index(at) + 1} "
                                      f"of {len(points)} (gap {crossing['gap']:.0f}); {ctx.terrain_note(instance, at=None)}")
        return {"waypoints": len(points), "gap": round(crossing["gap"])}

    def path_probe(ctx, instance, ref, start=0x14):
        """The engine's view of the refused path (path_probe: both ends' navmesh triangles, the search run at once)."""
        answer = ctx.game(instance).rpc({"cmd": "path_probe", "from": start, "target": ref}) or {}
        keys = ("built", "incomplete", "virtual_nodes", "nodes", "path_length", "path", "start", "goal", "same_navmesh", "linked", "hops",
                "way_length", "way", "closed_doors_on_way", "ledges_on_way", "portals_on_way", "goal_piece", "goal_piece_exits", "obstacles",
                "start_doors", "goal_doors", "closed_doors", "obstacle_refs", "crossing")
        if not answer.get("ok"):
            return {}
        world = (ctx.game(instance).read_status() or {}).get("world") or {}
        for obstacle in answer.get("obstacle_refs") or []:  # who each obstacle is: the player, a copy, an NPC
            form = int(obstacle["ref"], 16)
            obstacle["role"] = "player" if form == 0x14 else (world.get(str(form)) or {}).get("role")
        return {key: answer.get(key) for key in keys}

    def terrain(ctx, instance, radius=1000.0, cell=50.0, toward=None, at=None, up=600.0, down=1500.0, seconds=60):
        """The ground around the player (or `at`) as a walking player meets it (coop_harness/terrain.py): what it can
        walk to, the walls, drops and low ceilings at its edges, the navmesh's pieces; top-down and a side profile
        toward `toward` (a handle or id). The scan's JSON is saved; its path is in the answer."""
        game = "guest" if instance else "host"
        scan = ctx.terrain_scan(instance, radius, cell, toward, at, up, down, seconds)
        path = TEST_HOME / "terrain" / f"{time.strftime('%Y%m%d-%H%M%S')}-{game}.json"
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(json.dumps(scan))
        top, side, said = terrain_views.top_view(scan), terrain_views.side_view(scan), terrain_views.summary(scan)
        path.with_suffix(".txt").write_text("\n".join([said, "", *top, "", *side, "",
                                                        terrain_views.LEGEND]) + "\n")
        return {"summary": said, "top": top, "side": side, "legend": terrain_views.LEGEND,
                "capsule": scan.get("capsule"), "frames": scan.get("frames"), "file": str(path)}

    def terrain_scan(ctx, instance, radius=1000.0, cell=50.0, toward=None, at=None, up=600.0, down=1500.0, seconds=60):
        """The `terrain` command's scan as JSON (it runs over frames: a ticket, then its result)."""
        game = "guest" if instance else "host"
        request = {"cmd": "terrain", "radius": radius, "cell": cell, "up": up, "down": down}
        if toward is not None:
            request["toward"] = ctx.resolve(toward, game)
        if at is not None:
            request["at"] = list(at)
        ticket = ctx.game(instance).rpc(request)["ticket"]
        scan = {}
        def done():
            scan.update(ctx.game(instance).rpc({"cmd": "terrain_result", "ticket": ticket}) or {})
            return scan.get("done")
        ctx.wait_for(f"[{game}] the terrain scan", done, seconds)
        return scan

    def step_onto_navmesh(ctx, instance):
        """The player, off the navmesh (a nook a trip left it in), runs by input along the terrain's walkable way to the
        nearest navmesh floor, where the engine's search can start. False when the terrain shows no such way."""
        route = terrain_views.route_to_navmesh(ctx.terrain_scan(instance, radius=300.0, cell=20.0))
        if not route:
            return False
        points = turning_points(route)
        log(f"[{instance or 'host'}] off the navmesh: steps onto it by input, {len(points)} waypoints to "
            f"{[round(v) for v in route[-1]]}")
        with always_running(ctx, instance):
            for at in points:
                if not ctx.run_straight(instance, at, NAVMESH_REACH, sprint=False, jump=True):
                    return False
        return True

    def terrain_note(ctx, instance, toward=None, at=None):
        """A failure's picture of where it stopped: the terrain summary and the file with both views."""
        try:
            seen = ctx.terrain(instance, toward=toward, at=at)
        except TestFailure as error:
            return f"no terrain scan ({error})"
        return f"terrain: {seen['summary']} ({seen['file'].replace('.json', '.txt')})"

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
            start = ctx.my_position(instance)
            track, began = [(time.monotonic(), start)], time.monotonic()
            while time.monotonic() < deadline:
                here = ctx.my_position(instance)
                if math.dist(here[:2], ctx.place_of(instance, ref)[:2]) <= within:
                    return "arrived"
                now = time.monotonic()
                track.append((now, here))
                if set_off(start, here, now - began) and stalled(track, now):
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

    def run_straight(ctx, instance, ref, within, sprint=True, swim=False, jump=False):
        """The player turns toward `ref` and runs there by real input (forward held, Always Run on, Sprint once
        moving). True when within `within`; False when it stalls (a wall, a fence) or time is up."""
        here = ctx.my_position(instance)
        label = f"{ref:X}" if isinstance(ref, int) else "a point"
        there = ctx.place_of(instance, ref) if isinstance(ref, int) else ref
        ctx.act(instance, {"name": f"faces {label}", "ready": ["takes_input"], "turn": {"face": ref, "rate": 180},
                           "until": ["aimed"], "seconds": 3}, expect=None)  # turned first: running while turning goes wide
        spec = {"name": f"runs to {label}", "ready": ["takes_input"] if swim else ["takes_input", "on_ground"],
                "turn": {"face": ref, "rate": 180}, "buttons": ["forward"],
                "until": [{"near_xy": ["player", ref, within]}],
                "seconds": travel_seconds(math.dist(here[:2], there[:2]))}
        action, sprinting = ctx.act_start(instance, spec), False
        try:
            track = []
            while True:
                state = ctx.player_command(instance, {"cmd": "act_state"})
                if state.get("id") != action or state.get("phase") == "ended":
                    outcome = state.get("outcome") or {}
                    if outcome.get("status") != "done":
                        log(f"[{instance or 'host'}] run to {label} ended: {outcome}")
                    return outcome.get("status") == "done"
                if state.get("phase") != "running":  # its ready conditions can hold it for seconds
                    time.sleep(0.1)
                    continue
                now, at = time.monotonic(), ctx.my_position(instance)
                track = track or [(now, at)]
                if sprint and not sprinting and math.dist(at[:2], here[:2]) > SPRINT_AFTER:  # sprint starts only once moving
                    sprinting = ctx.sprint_key(instance, True)
                track.append((now, at))
                if now - track[0][0] > 1.0 and stalled(track[1:], now):  # a second to turn and set off
                    if jump:  # a low wall: over it, as a player hops a ledge (Space, forward still held)
                        jump = False
                        log(f"[{instance or 'host'}] run to {label} stalled at {[round(v) for v in at]}: jumps")
                        ctx.press_jump(instance)
                        track = []
                        continue
                    log(f"[{instance or 'host'}] run to {label} stalled at {[round(v) for v in at]}")
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

    def press_jump(ctx, instance):
        """Space pressed and released through the game's input dispatch (Jump)."""
        ctx.player_command(instance, {"cmd": "key", "scan": SPACE, "down": True})
        frame = ctx.game_frame(instance)
        ctx.wait_for(f"[{instance or 'host'}] a frame for the jump", lambda: ctx.game_frame(instance) > frame + 1, 3)
        ctx.player_command(instance, {"cmd": "key", "scan": SPACE, "down": False})

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

    def activate_ref(ctx, instance, ref, target):
        """The player, in first person, faces `ref` and presses Activate (real input)."""
        if ctx.player_command(instance, {"cmd": "camera"}).get("state") in (8, 9):  # third person: the view key, as a player
            ctx.hold_key(instance, "f", 0.1)
            ctx.wait_for(f"[{instance or 'host'}] first person", lambda: ctx.player_command(instance, {"cmd": "camera"}).get("state") == 0, 5)
        ctx.act(instance, {"name": f"aims at {target}", "ready": ["takes_input"], "turn": {"face": ref, "rate": 180},
                           "until": ["aimed"], "seconds": 3})
        # Activate takes what the crosshair is on: another player or an NPC in the way, or nothing when out of reach.
        on = lambda: ctx.player_command(instance, {"cmd": "camera"}).get("crosshair") or 0
        with contextlib.suppress(TestFailure):
            ctx.wait_for(f"[{instance or 'host'}] the crosshair on {target}", lambda: on() == ref, 2)
        if on() != ref and (ctx.actor_row(instance, ref).get("state") or {}).get("dead"):  # a corpse lies below an actor's chest
            body = (ctx.body_of(instance, ref) or {}).get("position") or ctx.place_of(instance, ref)
            ctx.act(instance, {"name": f"aims at {target}'s body", "ready": ["takes_input"], "turn": {"face": list(body), "rate": 180},
                               "until": ["aimed"], "seconds": 3})
            with contextlib.suppress(TestFailure):
                ctx.wait_for(f"[{instance or 'host'}] the crosshair on {target}'s body", lambda: on() == ref, 2)
        if (crosshair := on()) != ref:
            if crosshair:
                raise verb_failure(ctx, "blocked", f"[{instance or 'host'}] {crosshair:X} is between the player and {target}",
                                   blocker=crosshair)
            raise verb_failure(ctx, "out_of_reach", f"[{instance or 'host'}] the crosshair is on nothing, not {target}: "
                               f"{math.dist(ctx.my_position(instance)[:2], ctx.place_of(instance, ref)[:2]):.0f} units off")
        ctx.act(instance, {"name": "presses Activate", "ready": ["takes_input"], "buttons": ["activate"],
                           "until": [{"for": 0.15}], "seconds": 1, "allow_menus": True})

    def talk_to(ctx, instance, target):
        """The player faces `target` and presses Activate, as a player starts a conversation; returns its options."""
        game = "guest" if instance else "host"
        ref = ctx.resolve(target, game)
        apart = lambda: math.dist(ctx.my_position(instance), ctx.place_of(instance, ref))  # 3D: a bank below counts
        opens = f"[{instance or 'host'}] the conversation with {target} opens"
        talking = lambda: ctx.talking_to(instance, ref)
        for attempt in range(TALK_TRIES):
            last = attempt == TALK_TRIES - 1
            if apart() > TALK_REACH:  # out of reach (it walks on)
                ctx.walk_to(instance, target, 80.0)
            try:
                ctx.activate_ref(instance, ref, target)
            except TestFailure as error:  # it walked on while the player turned
                if (getattr(error, "outcome", None) or {}).get("why") != "out_of_reach" or last:
                    raise
                log(f"[{instance or 'host'}] {target} walked out of reach while the player aimed; closer again")
                continue
            with contextlib.suppress(TestFailure):
                ctx.wait_for(opens, talking, 2)
            if talking():
                break
            if last or apart() <= TALK_REACH:  # in reach and still no conversation: not the walk
                ctx.wait_for(opens, talking, 8)
                break
            log(f"[{instance or 'host'}] Activate found {target} {apart():.0f} units off (it walks on); closer again")
        return ctx.wait_for(f"[{instance or 'host'}] the menu lists options", lambda: ctx.dialogue(instance).get("options"), 10)

    def pick(ctx, instance, line):
        """Picks a line in that game's open conversation: its index, or (a string) the first whose text contains it."""
        options = ctx.dialogue(instance).get("options") or []
        if isinstance(line, str):
            index = next((i for i, option in enumerate(options) if line.lower() in option.get("text", "").lower()), None)
            if index is None:
                raise verb_failure(ctx, "no_line", f"[{instance or 'host'}] no line like {line!r}: {[o.get('text') for o in options]}",
                                   options=[o.get("text") for o in options])
        else:
            index = line
        ctx.wait_for(f"[{instance or 'host'}] the menu takes a pick", lambda: ctx.menu_takes_pick(instance), 15)
        ctx.pick_line_with_keys(instance, index)
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

    # --- Goal verbs: what a repro asks for; each checks itself and says why it stopped ---

    def verb_trace(ctx, verb, instance, target):
        """A verb's recording: the player and its target, each frame (AnimTrace, no video)."""
        ids = {0x14: "me"}
        with contextlib.suppress(TestFailure):
            ids.setdefault(ctx.resolve(target, instance), "target")
        return AnimTrace(ctx, f"{verb}-{instance or 'host'}", {instance: ids}, video=False)

    def actor_row(ctx, instance, ref):
        """That game's snapshot of an actor (status world), {} when it has none."""
        return ((ctx.game(instance).read_status() or {}).get("world") or {}).get(str(ref)) or {}

    def can_play(ctx, instance):
        if not ctx.takes_input(instance):
            menus = sorted(ctx.menus(instance) - {"HUD Menu"})
            raise verb_failure(ctx, "menu_blocked", f"[{instance or 'host'}] does not take input: menus {menus}", menus=menus)

    def reach(ctx, instance, target, within=REACH, straight=False):
        """Within `within` of `target`, walking only if it is further: the walk's numbers, or {} when already there."""
        ref = ctx.resolve(target, instance)
        if math.dist(ctx.my_position(instance)[:2], ctx.place_of(instance, ref)[:2]) <= within:
            return {}
        return ctx.walk_to(instance, target, within, straight=straight)

    @goal
    def go_to(ctx, instance, target, within=100.0, straight=False, by_input=False):
        """The player gets within `within` units of `target`: AI-driven along the navmesh, so for getting somewhere, not
        for testing movement. With no path it fails with why no_path, unless `straight` lets it run in a line.
        `by_input` runs there in a line by real input instead, for a bug in the movement itself."""
        ctx.can_play(instance)
        if by_input:
            return {"how": "input", **ctx.steer_to(instance, target, within)}
        walked = ctx.reach(instance, target, within, straight)
        how = "here" if not walked else "straight" if not walked["pathed"] else "path_leg" if walked["legs"] else "run"
        return {"how": how, **walked}

    @goal
    def talk(ctx, instance, target, say=()):
        """The player starts a conversation with `target` (or goes on with the open one) and picks each line of `say`
        (text or index), waiting for each response; returns what was said and what is offered next."""
        if ctx.talking_to(instance, ctx.resolve(target, instance)):
            options = [option.get("text") for option in ctx.dialogue(instance).get("options") or []]
        else:
            ctx.can_play(instance)
            ctx.reach(instance, target, within=150.0)
            options = [option.get("text") for option in ctx.talk_to(instance, target)]
        said = []
        for line in say:
            picked = ctx.pick(instance, line)
            said.append(picked["picked"])
            options = picked.get("options", [])
        return {"how": "input", "said": said, "options": options}

    def use_state(ctx, instance, ref):
        """What an activation can change: the menus, the player's sit state and cell, the object's row."""
        status = ctx.game(instance).read_status() or {}
        player = (status.get("world") or {}).get("20") or {}
        return {"menus": set(status.get("menus") or []) - {"HUD Menu", "LoadWaitSpinner"},
                "sit": (player.get("state") or {}).get("sit_sleep"), "cell": ctx.player_cell(instance),
                "object": ctx.world_state(instance, [ref]).get(ref)}

    @goal
    def use(ctx, instance, target, straight=False):
        """The player walks up and activates `target` (a door, a chest, furniture); done when that changed something.
        `straight`: with no path there (a door at the navmesh's edge), run the rest in a line."""
        ctx.can_play(instance)
        ctx.reach(instance, target, straight=straight)
        ref = ctx.resolve(target, instance)
        before = ctx.use_state(instance, ref)
        ctx.activate_ref(instance, ref, target)
        container = "items" in (before["object"] or {})  # its lid moves first: done is its menu
        ended, changed = time.monotonic() + USE_SECONDS, []
        while time.monotonic() < ended:
            after = ctx.use_state(instance, ref)
            changed = sorted(key for key in after if after[key] != before[key])
            if set(changed) & {"menus", "sit", "cell"}:
                return {"how": "input", "changed": changed, "menus": sorted(after["menus"])}
            time.sleep(0.1)
        if changed and not container:  # a door swung, a lever moved: only the object shows it
            return {"how": "input", "changed": changed, "menus": []}
        raise verb_failure(ctx, "refused", f"[{instance or 'host'}] activating {target} did nothing in {USE_SECONDS:g} s: {changed or 'no change'}")

    @goal
    def take(ctx, instance, target):
        """The player opens container `target` and takes everything in it (R); returns how many items left it."""
        ctx.can_play(instance)
        ctx.reach(instance, target)
        ref = ctx.resolve(target, instance)
        held = ctx.container_items(instance, ref)
        ctx.activate_ref(instance, ref, target)
        ctx.take_from_open_container(instance)
        return {"how": "input", "took": sum(held.values()) - sum(ctx.container_items(instance, ref).values())}

    @goal
    def fight(ctx, instance, target, hits=30):
        """The player draws what it holds and hits `target` until it dies, chasing it when it moves off."""
        ctx.can_play(instance)
        ref = ctx.resolve(target, instance)
        dead = lambda who: (ctx.actor_row(instance, who).get("state") or {}).get("dead")
        ctx.draw(instance)
        swings = misses = unreached = 0
        last_miss = "none"  # the last reach's failure
        log(f"[{instance or 'host'}] path to {target}: {probe_line(ctx.path_probe(instance, ref))}")  # passing runs too
        log(f"[{instance or 'host'}] {target}'s path back: {probe_line(ctx.path_probe(instance, 0x14, start=ref))}")  # an NPC's request
        while swings < hits and not dead(ref):
            if dead(0x14):
                raise verb_failure(ctx, "player_down", f"[{instance or 'host'}] died fighting {target} after {swings} swings", swings=swings)
            try:  # it keeps moving: swing from as near as the walk got
                ctx.reach(instance, target, MELEE_REACH, straight=True)
            except TestFailure as error:
                last_miss = str(error)
                log(f"[{instance or 'host'}] reach {target}: {error}")
            apart = math.dist(ctx.my_position(instance)[:2], ctx.place_of(instance, ref)[:2])
            if apart <= 3 * MELEE_REACH:
                unreached = 0  # three tries in a row, not over the whole fight
            else:  # never got there: no swing at the air
                unreached += 1
                if unreached >= 3:
                    probe = ctx.path_probe(instance, ref)
                    raise verb_failure(ctx, "no_path", f"[{instance or 'host'}] could not get to {target} in three tries: "
                                       f"{apart:.0f} units off after {swings} swings (last: {last_miss}); {probe_line(probe)}; "
                                       f"{ctx.terrain_note(instance, toward=ref)}", swings=swings,
                                       closest=round(apart), probe=probe)
                continue
            ctx.act(instance, {"name": f"faces {target}", "ready": ["on_ground"], "turn": {"face": ref, "rate": 360},
                               "until": ["aimed"], "seconds": 1}, expect=None)
            try:
                ctx.attack(instance)
                misses = 0
            except TestFailure:  # staggered or recoiling from a hit: the click starts nothing, a player clicks again
                misses += 1
                if misses >= 5:
                    raise verb_failure(ctx, "refused", f"[{instance or 'host'}] five swings at {target} in a row never started")
            swings += 1
        if not dead(ref):
            raise verb_failure(ctx, "timeout", f"[{instance or 'host'}] {target} still stands after {swings} swings", swings=swings)
        return {"how": "input", "swings": swings}

    @goal
    def door(ctx, instance, target):
        """The player walks to load door `target` and goes through it (Activate, real input); the other player follows by
        the party tether. Done when both are in the new cell, taking input. When the other player stands in the doorway,
        it goes first."""
        cell, other, led = ctx.player_cell(instance), "" if instance else "client", instance
        try:
            ctx.use(instance, target, straight=True)
        except TestFailure as error:  # the other player stands in the doorway: it goes first, the tether brings this one
            if (getattr(error, "outcome", None) or {}).get("blocker") != ctx.player_copy(instance):
                raise
            led = other
            ctx.use(other, target, straight=True)
        for game in (instance, other):
            ctx.wait_for(f"[{game or 'host'}] through {target}", lambda game=game: ctx.player_cell(game) != cell, 30)
            ctx.wait_for(f"[{game or 'host'}] the load's fade is over", lambda game=game: ctx.takes_input(game), 30)
        return {"how": "input", "from": cell, "to": ctx.player_cell(instance), "led": "guest" if led else "host"}

    def hostiles(ctx, radius, instance=""):
        """The living NPCs within `radius` of that game's player hostile to the party (Actor::IsHostileToActor, 37537, read on
        the host), nearest first: [(host id, distance)]."""
        here, found = ctx.my_position(instance), []
        for form, row in ((ctx.game("").read_status() or {}).get("world") or {}).items():
            place = (row.get("placement") or {}).get("position")
            if row.get("player") or (row.get("state") or {}).get("dead") or not place:
                continue
            distance = math.dist(place[:2], here[:2])
            if distance <= radius and ctx.player_command("", {"cmd": "crime", "action": "reaction", "actor": int(form),
                                                              "target": 0x14}).get("hostile"):
                found.append((int(form), round(distance)))
        return sorted(found, key=lambda pair: pair[1])

    def reachable_hostiles(ctx, instance, radius, given_up=()):
        """The hostiles within `radius` split by whether that player can walk to them (the engine's search from it,
        path_probe): (reachable nearest along the way first, [(match, probe line)] behind a cut or off the navmesh)."""
        shared = ctx.nearby_shared()
        reachable, unreachable = [], []
        for host_id, flat in ctx.hostiles(radius, instance):
            match = next((a for a in shared if a["host"] == host_id), None)
            if instance and not match:  # the guest probes its copy; far ones have none yet
                unreachable.append(({"host": host_id, "name": f"{host_id:X}"}, "no copy in the guest's game yet"))
                continue
            match = match or {"host": host_id, "name": f"{host_id:X}"}  # far off: a copy comes once the party is near
            if host_id in given_up:
                unreachable.append((match, "its fight could not reach it"))
                continue
            probe = ctx.path_probe(instance, match["guest"] if instance else host_id)
            if walkable(probe):
                reachable.append((probe.get("path_length") or probe.get("way_length") or flat, match))
            else:
                unreachable.append((match, probe_line(probe)))
        return [match for _, match in sorted(reachable, key=lambda pair: pair[0])], unreachable

    @goal
    def clear(ctx, instance, target="", radius=1500.0, most=8):
        """The player fights every NPC hostile to the party within `radius` that it can walk to, nearest along the
        way first (`target` unused: goal verbs take one); those the navmesh cuts off (an invisible wall, a cliff) are
        skipped and listed. Each fought is named as a handle (enemy_1, ...) and fought with `fight`."""
        killed, cut_off, given_up = [], [], set()
        while True:
            enemies, cut_off = ctx.reachable_hostiles(instance, radius, given_up)
            if not enemies:
                break
            if len(killed) >= most:
                raise verb_failure(ctx, "timeout", f"[{instance or 'host'}] still hostiles in reach after {len(killed)} kills")
            match = enemies[0]
            log(f"[{instance or 'host'}] fights {match['name']} {match['host']:X} first: {len(enemies)} in reach, "
                f"{len(cut_off)} cut off")
            try:
                if instance and "guest" not in match:  # the guest's copy appears as its player nears: walk there first
                    ctx.walk_to(instance, match["host"], 1500.0)
                    match = next((a for a in ctx.nearby_shared() if a["host"] == match["host"]), None)
                    if not match:
                        raise verb_failure(ctx, "refused", f"hostile {enemies[0]['host']:X} has no copy in the guest's game")
                handle = f"enemy_{len(killed) + 1}"
                ctx.name_handle(handle, match)
                ctx.fight(instance, handle)
            except TestFailure as error:  # it moved behind a cut since the probe (a patrol past an invisible wall)?
                match = match or enemies[0]
                ref = match.get("guest") if instance else match["host"]
                probe = (ctx.path_probe(instance, ref) or {}) if ref else {}
                if walkable(probe) or (ref and (ctx.actor_row(instance, ref).get("state") or {}).get("dead")):
                    raise
                log(f"[{instance or 'host'}] gives up on {match['name']} {match['host']:X} for now, the way there is cut: "
                    f"{probe_line(probe)} ({error})")
                given_up.add(match["host"])
                continue
            killed.append(match["name"])
        if not killed and cut_off:
            raise verb_failure(ctx, "no_path", f"[{instance or 'host'}] no hostile within {radius:.0f} units it can walk to: "
                               + "; ".join(f"{m['name']} ({line})" for m, line in cut_off))
        for match, line in cut_off:
            log(f"[{instance or 'host'}] skipped {match['name']} {match['host']:X}: not walkable from here ({line})")
        return {"how": "input", "killed": killed, "skipped": [m["name"] for m, _ in cut_off]}

    def last_verb(ctx):
        """The last goal verb's outcome (`why`)."""
        return getattr(ctx, "LAST_VERB", None)
