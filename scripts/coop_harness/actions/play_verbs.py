"""A player's verbs for playing real flows (docs/design/repro-mcp.md): walking, talking, using, fighting. Goal verbs
(`goal`) report one outcome, kept for `why`, and fail naming the reason; the MCP server and scenarios call the same code."""
import contextlib
import functools
import math
import time

from ..errors import TestFailure
from ..objects import AnimTrace

# Measured on the duo (docs/design/repro-mcp.md, Phase 3), units/s: a player running by input (sprint ~500); a walk
# to an NPC in Riverwood, runs and walked legs together, is planned at TRAVEL_SPEED.
RUN_SPEED, TRAVEL_SPEED, WALK_SPEED = 350.0, 200.0, 80.0  # WALK_SPEED: PathToReference's fastest
SPRINT_AFTER = 50.0  # units moved before Sprint is pressed: the engine ignores it from standing
STALL_UNITS, STALL_SECONDS = 40.0, 1.5  # a run that moves less in this long has met something
PATH_LEG_SECONDS = 4.0  # a walked leg round what stopped a run
SET_OFF_SECONDS = 5.0  # KeepOffsetFromActor takes a package evaluation to set off: 2 s and more on a busy machine
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
    def run(ctx, instance, target, *args, **kwargs):
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
        limit = seconds or travel_seconds(math.dist(start[:2], ctx.place_of(instance, ref)[:2]), TRAVEL_SPEED if follows else WALK_SPEED)
        deadline = time.monotonic() + limit
        walk = {"legs": 0, "runs": 0, "stalls": 0, "closest": math.inf, "refused": False}
        apart = lambda: math.dist(ctx.my_position(instance)[:2], ctx.place_of(instance, ref)[:2])
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
        walked = lambda: round(math.dist(start[:2], ctx.my_position(instance)[:2]), 1)
        if walk["refused"] and apart() > within:  # no navmesh path to it (a guest's NPC copy, docs/design/repro-mcp.md)
            if not straight:
                raise verb_failure(ctx, "no_path", f"[{instance or 'host'}] the engine has no path to {target}: stopped "
                                   f"{apart():.0f} units off (at most {within}); straight=True runs there in a line",
                                   closest=round(apart()), runs=walk["runs"], legs=walk["legs"])
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
            track = []
            while True:
                state = ctx.player_command(instance, {"cmd": "act_state"})
                if state.get("id") != action or state.get("phase") == "ended":
                    return (state.get("outcome") or {}).get("status") == "done"
                if state.get("phase") != "running":  # its ready conditions can hold it for seconds
                    time.sleep(0.1)
                    continue
                now, at = time.monotonic(), ctx.my_position(instance)
                track = track or [(now, at)]
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

    def activate_ref(ctx, instance, ref, target):
        """The player, in first person, faces `ref` and presses Activate (real input)."""
        if ctx.player_command(instance, {"cmd": "camera"}).get("state") in (8, 9):  # third person: the view key, as a player
            ctx.hold_key(instance, "f", 0.1)
            ctx.wait_for(f"[{instance or 'host'}] first person", lambda: ctx.player_command(instance, {"cmd": "camera"}).get("state") == 0, 5)
        ctx.act(instance, {"name": f"aims at {target}", "ready": ["takes_input"], "turn": {"face": ref, "rate": 180},
                           "until": ["aimed"], "seconds": 3})
        ctx.act(instance, {"name": "presses Activate", "ready": ["takes_input"], "buttons": ["activate"],
                           "until": [{"for": 0.15}], "seconds": 1, "allow_menus": True})

    def talk_to(ctx, instance, target):
        """The player faces `target` and presses Activate, as a player starts a conversation; returns its options."""
        game = "guest" if instance else "host"
        ref = ctx.resolve(target, game)
        if math.dist(ctx.my_position(instance)[:2], ctx.place_of(instance, ref)[:2]) > 150:  # out of reach (it moved off)
            ctx.walk_to(instance, target, 100.0)
        ctx.activate_ref(instance, ref, target)
        ctx.wait_for(f"[{instance or 'host'}] the conversation with {target} opens", lambda: ctx.talking_to(instance, ref), 10)
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
    def use(ctx, instance, target):
        """The player walks up and activates `target` (a door, a chest, furniture); done when that changed something."""
        ctx.can_play(instance)
        ctx.reach(instance, target)
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
        swings = 0
        while swings < hits and not dead(ref):
            if dead(0x14):
                raise verb_failure(ctx, "player_down", f"[{instance or 'host'}] died fighting {target} after {swings} swings", swings=swings)
            with contextlib.suppress(TestFailure):  # it keeps moving: swing from as near as the walk got
                ctx.reach(instance, target, MELEE_REACH, straight=True)
            ctx.act(instance, {"name": f"faces {target}", "ready": ["on_ground"], "turn": {"face": ref, "rate": 360},
                               "until": ["aimed"], "seconds": 1}, expect=None)
            ctx.attack(instance)
            swings += 1
        if not dead(ref):
            raise verb_failure(ctx, "timeout", f"[{instance or 'host'}] {target} still stands after {swings} swings", swings=swings)
        return {"how": "input", "swings": swings}

    def last_verb(ctx):
        """The last goal verb's outcome (`why`)."""
        return getattr(ctx, "LAST_VERB", None)
