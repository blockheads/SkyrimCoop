"""Scenarios movement for the co-op harness."""
import json
import math
import time
from ..common import LYDIA, ON_GROUND
from ..errors import TestFailure
from ..comparisons import distance
from ..utilities import log
from ..objects import AnimTrace


def scenario_steady_state(ctx):
    """Desync checker: every actor's full state matches between owner and copy."""
    if problems := ctx.check_desync():
        raise ctx.failure("desync: " + " | ".join(problems))

def scenario_move_host_npc(ctx):
    """Host moves its NPC to a player; the client's copy must end up where the host's NPC is."""
    npc, _, copy = ctx.spawn_host_npc(LYDIA, restrained=True)
    ctx.show("client", lambda actors: copy)
    host_actors = ctx.actors_on("")
    players = {"player": ctx.local_player("")["position"]}
    if client_on_host := ctx.other_player(host_actors):
        players[f"{client_on_host:08X}"] = host_actors[str(client_on_host)]["actual"]
    start = ctx.host_npc_state(npc)["position"]
    target = max(players, key=lambda ref: distance(players[ref], start))  # the farther player
    goal = players[target]
    ctx.place("", f"{npc:08X}", goal, start)
    ctx.wait_for("[host] NPC moved and landed", lambda: distance(ctx.host_npc_state(npc)["position"], start) > 100
             and abs(ctx.host_npc_state(npc)["position"][2] - goal[2]) < 60, 10)
    last = {}
    def copy_matches(actors):
        last.update(copy=actors.get(str(copy), {}).get("actual"), owned=ctx.host_npc_state(npc)["position"])
        return last["copy"] and distance(last["copy"], last["owned"]) < 30
    try:
        ctx.wait_on("client", f"NPC copy {copy:08X} at the host NPC's position", copy_matches, 10)
    except TestFailure:
        raise ctx.failure(f"copy at {[round(v) for v in last['copy'] or []]}, host's NPC at {[round(v) for v in last['owned']]}")
    ctx.hold()

def scenario_copy_jumps_with_owner(ctx):
    """The host puts its NPC 900 units away at once (the console's setpos, as a script's MoveTo does): the client's
    copy jumps there too, never sliding across in between (Movement::Teleported)."""
    npc, _, copy = ctx.spawn_host_npc(LYDIA, restrained=True)
    ctx.show("client", lambda actors: copy)
    start = ctx.host_npc_state(npc)["position"]
    def flat(a, b):
        return math.hypot(a[0] - b[0], a[1] - b[1])
    ctx.wait_on("client", f"copy {copy:08X} at the host's NPC", lambda actors: (p := actors.get(str(copy), {}).get("actual")) and flat(p, start) < 30, 10)
    goal = [start[0] + 900, start[1], start[2]]
    ctx.console_on("", f"{npc:08X}.setpos x {goal[0]:.0f}")
    seen = []
    def arrived(actors):
        if position := actors.get(str(copy), {}).get("actual"):
            seen.append(position)
        return position and flat(position, goal) < 30
    ctx.wait_on("client", f"copy {copy:08X} at the NPC's new place", arrived, 5)
    if between := [[round(v) for v in p] for p in seen if flat(p, start) > 30 and flat(p, goal) > 30]:
        raise ctx.failure(f"[client] the copy slid across: {len(between)} of {len(seen)} reads between, {between[:3]}")
    ctx.hold()


def scenario_furniture_host_player(ctx):
    """Host uses the cooking spit and leaves it; the client's copy must follow both ways."""
    ctx.furniture_roundtrip("", "client", "host's character")

def scenario_furniture_client_player(ctx):
    """Client uses the cooking spit and leaves it; the host's copy must follow both ways."""
    ctx.furniture_roundtrip("client", "", "client's character")

def scenario_sneak_host_player(ctx):
    """Host sneaks and stands up; the client's copy of the host must follow."""
    ctx.sneak_roundtrip("", "client", "host's character")

def scenario_sneak_client_player(ctx):
    """Client sneaks and stands up; the host's copy of the client must follow."""
    ctx.sneak_roundtrip("client", "", "client's character")


def scenario_cast_host_player(ctx):
    """Host casts Flames (held) and stops; the client's copy of the host must follow."""
    ctx.cast_roundtrip("", "client", "host's character")

def scenario_cast_client_player(ctx):
    """Client casts Flames (held) and stops; the host's copy of the client must follow."""
    ctx.cast_roundtrip("client", "", "client's character")

def scenario_quick_sheathe_host_player(ctx):
    """Host raises and at once lowers a spell; the client's copy must end with the hands down."""
    ctx.quick_sheathe("", "client", "host's character")

def scenario_quick_sheathe_client_player(ctx):
    """Client raises and at once lowers a spell; the host's copy must end with the hands down."""
    ctx.quick_sheathe("client", "", "client's character")

def scenario_hands_survive_reload_host_player(ctx):
    """Host's hands up/down across hot reloads; the client's respawned copy must match."""
    ctx.hands_survive_reload("", "client", "host's character")

def scenario_hands_survive_reload_client_player(ctx):
    """Client's hands up/down across hot reloads; the host's respawned copy must match."""
    ctx.hands_survive_reload("client", "", "client's character")

def scenario_health_host_player(ctx):
    """Host player loses/regains health; the client's copy of the host must match."""
    ctx.health_roundtrip("", "player", lambda: ctx.local_player("").get("health"), "client", ctx.other_player, "host's character")

def scenario_health_client_player(ctx):
    """Client player loses/regains health; the host's copy of the client must match."""
    ctx.health_roundtrip("client", "player", lambda: ctx.local_player("client").get("health"), "", ctx.other_player,
                     "client's character")



def scenario_sneaking_players_stay_hidden(ctx):
    """Both players sneak facing each other, nobody else about: each game's stealth eye (the HUD's highest detection
    of its player, 41408) stays hidden; the other player's copy is no observer (playtests/2026-10-31-fabien.md S6)."""
    def eye(instance):
        return ctx.player_command(instance, {"cmd": "crime", "action": "stealth_meter"})["level"]
    def observers(instance):
        world = (ctx.game(instance).read_status() or {}).get("world") or {}
        return {f"{int(actor):X} {(state.get('identity') or {}).get('name', '')}":
                ctx.player_command(instance, {"cmd": "crime", "action": "detection", "actor": int(actor), "target": 0x14})["level"]
                for actor, state in world.items() if int(actor) != 0x14}
    try:
        ctx.show("", ctx.other_player)
        ctx.show("client", ctx.other_player)
        ctx.sneak("", True)
        ctx.sneak("client", True)
        frame = ctx.game_frame("")
        ctx.wait_for("both sneak for 120 frames", lambda: ctx.game_frame("") > frame + 120, 10)
        for instance in ("", "client"):
            if (level := eye(instance)) >= 0:
                raise ctx.failure(f"[{instance or 'host'}] the sneaking player's eye shows detected ({level}): {observers(instance)}")
    finally:
        ctx.sneak("", False)
        ctx.sneak("client", False)



def scenario_other_player_looking_leaves_eye_hidden(ctx):
    """One player sneaks, the other stands and turns to face it: in the sneaker's game the other player's copy answers
    its detection of the player unseen (ActorKnowledge 42704 installs a pass, docs/re/systems/stealth.md), and the eye
    (41408) stays hidden. A skipped pass left the copy's request queued, its older level shown at any age."""
    def copy_in(instance):
        return ctx.other_player((ctx.game(instance).read_status() or {}).get("actors") or {})
    for sneaker, looker in (("", "client"), ("client", "")):
        name = sneaker or "host"
        copy, sneaker_copy = copy_in(sneaker), copy_in(looker)
        if not copy or not sneaker_copy:
            raise ctx.failure(f"[{name}] no copy of the other player to watch")
        try:
            ctx.sneak(sneaker, True)
            ctx.sneak(looker, False)
            ctx.face(looker, sneaker_copy)
            known = {}
            def answered():
                known.update(ctx.player_command(sneaker, {"cmd": "crime", "action": "knowledge", "actor": copy, "target": 0x14}))
                return known.get("level") == -1000 and known.get("age", 99) < 2
            frame = ctx.game_frame(sneaker)
            ctx.wait_for(f"[{name}] 120 frames sneaking, watched", lambda: ctx.game_frame(sneaker) > frame + 120, 10)
            ctx.wait_for(f"[{name}] the other player's copy answers its detection of the player unseen, freshly",
                         answered, 5, explain=lambda: f"its knowledge: {known}")
            if (eye := ctx.player_command(sneaker, {"cmd": "crime", "action": "stealth_meter"})["level"]) >= 0:
                raise ctx.failure(f"[{name}] the sneaking player's eye shows detected ({eye}) with the other player looking")
        finally:
            ctx.sneak(sneaker, False)


# The locomotion graph's floats: the synced ones (Master_Behavior's) and what the graph damps from them.
LOCOMOTION = ("Direction", "DirectionDamped", "Speed", "SpeedSampled", "SpeedDamped", "TurnDelta")


def strafe_path(entries, game, who, yaw=None):
    """[(time, lateral)]: that actor's position along the strafer's right axis (its yaw at the start), per frame."""
    points = [e for e in entries if e["kind"] == "pos" and e["game"] == game and e["who"] == who]
    if not points:
        return [], yaw
    yaw = points[0]["yaw"] if yaw is None else yaw
    right = (math.cos(yaw), -math.sin(yaw))
    return [(e["time"], e["pos"][0] * right[0] + e["pos"][1] * right[1]) for e in points], yaw


def lateral_at(path, t):
    """The path's lateral position at time t, linear between frames."""
    for (t0, x0), (t1, x1) in zip(path, path[1:]):
        if t0 <= t <= t1:
            return x0 + (x1 - x0) * (t - t0) / (t1 - t0) if t1 > t0 else x0
    return None


def compare_strafe(owner, copy):
    """How the copy's path follows the owner's: its delay (s), and after it the mean and worst error (units)."""
    def errors(delay):
        found = [(t, x, lateral_at(owner, t - delay)) for t, x in copy]
        return [(t, x, o) for t, x, o in found if o is not None]
    best = min((d / 100 for d in range(0, 80)), key=lambda d: (lambda e: sum(abs(x - o) for _, x, o in e) / max(len(e), 1))(errors(d)))
    found = errors(best)
    if not found:
        return None
    mean = sum(abs(x - o) for _, x, o in found) / len(found)
    worst = max(found, key=lambda f: abs(f[1] - f[2]))
    return {"delay": best, "mean": round(mean, 1), "worst": round(abs(worst[1] - worst[2]), 1), "frames": len(found)}


def frame_speeds(path):
    """[(time, speed)] between a path's frames: each frame's move at 60 frames a second (what a frame shows; the
    harness's own stamps jitter by up to a frame)."""
    return [((t0 + t1) / 2, (x1 - x0) * 60) for (t0, x0), (t1, x1) in zip(path, path[1:]) if t1 > t0]


def strafe_stutters(owner, copy, delay, tolerance=0.35, settle=0.1):
    """The copy's frames that move more than `tolerance` faster or slower than its owner did then, where the owner
    moved steadily for `settle` seconds either side (a turn blends over a snapshot interval): (time, owner's speed,
    copy's speed)."""
    theirs, found = frame_speeds(owner), []
    for t, speed in frame_speeds(copy):
        at = t - delay
        near = [v for tt, v in theirs if abs(tt - at) <= settle]
        if len(near) < 5:
            continue
        typical = sorted(near)[len(near) // 2]
        if abs(typical) < 200 or max(near) - min(near) > 0.15 * abs(typical):
            continue
        if abs(speed - typical) > tolerance * abs(typical):
            found.append((round(t, 3), round(typical), round(speed)))
    return found


def animation_lags(entries, game, who, yaw):
    """For each strafe turn of that actor: ms from its movement turning to its graph's Direction flipping."""
    right = (math.cos(yaw), -math.sin(yaw))
    lateral = lambda e: e["pos"][0] * right[0] + e["pos"][1] * right[1]
    frames = [e for e in entries if e["kind"] == "pos" and e["game"] == game and e["who"] == who and "floats" in e]
    lags = []
    for before, after in zip(frames, frames[1:]):
        if {before["floats"]["Direction"], after["floats"]["Direction"]} != {0.25, 0.75}:
            continue
        near = [e for e in frames if abs(e["time"] - after["time"]) < 0.15]
        moves = [(b["time"], lateral(b) - lateral(a)) for a, b in zip(near, near[1:])]
        settled = [m for t, m in moves if t > after["time"] + 0.08]
        if not settled:
            continue
        way = 1 if sum(settled) > 0 else -1
        turned = next((t for t, m in moves if m * way > 1.0), None)
        if turned is not None:
            lags.append(round((after["time"] - turned) * 1000))
    return lags


def scenario_copy_strafes_with_owner(ctx, strafer=""):
    """S10 (playtests/2026-10-31-fabien.md, "lagging out when moving side to side"): the strafer's player strafes
    left and right with real keys, slow then quick; the other game's copy follows its path side to side."""
    watcher = "client" if strafer == "" else ""
    copy = ctx.player_copy(watcher)
    if copy is None:
        raise ctx.failure(f"[{watcher or 'host'}] no copy of the other player")
    with AnimTrace(ctx, f"strafe-{strafer or 'host'}", {strafer: {0x14: "owner"}, watcher: {copy: "copy"}}, video=False,
                   floats=LOCOMOTION) as trace:
        for seconds, times in ((0.8, 3), (0.3, 6)):
            for _ in range(times):
                ctx.hold_key(strafer, "d", seconds)
                ctx.hold_key(strafer, "a", seconds)
        frame = ctx.game_frame(watcher)
        ctx.wait_for("the copy has a second to finish", lambda: ctx.game_frame(watcher) > frame + 60, 5)
    owner, yaw = strafe_path(trace.entries, strafer, "owner")
    theirs, _ = strafe_path(trace.entries, watcher, "copy", yaw)
    result = compare_strafe(owner, theirs)
    log(f"[{watcher or 'host'}] the copy against its owner: {result}; trace {trace.dir}")
    (trace.dir / "strafe.json").write_text(json.dumps({"owner": owner, "copy": theirs, "result": result,
                                                       "entries": [e for e in trace.entries if e["kind"] == "pos"]}))
    if result is None:
        raise ctx.failure("no overlapping frames between the owner's and the copy's paths")
    lags = animation_lags(trace.entries, watcher, "copy", yaw)
    log(f"[{watcher or 'host'}] the copy's strafe animation flips after its movement turns by (ms): {sorted(lags)}")
    # The owner turns in two steps (a frame stopped, then the new way): its copy blends one frame between them.
    if lags and (sum(lags) / len(lags) > 16 or max(lags) > 34):
        raise ctx.failure(f"[{watcher or 'host'}] the copy slides the new way in its old strafe: its animation flips "
                          f"{sorted(lags)} ms after its movement turns (a frame on average, two at most)")
    if stutters := strafe_stutters(owner, theirs, result["delay"]):
        raise ctx.failure(f"[{watcher or 'host'}] the copy stutters: {len(stutters)} frames off its owner's steady speed by "
                          f"over a third, e.g. (owner, copy) {[(o, c) for _, o, c in stutters[:5]]}")


def height_path(entries, game, who):
    """[(time, z)]: that actor's height per frame."""
    return [(e["time"], e["pos"][2]) for e in entries if e["kind"] == "pos" and e["game"] == game and e["who"] == who]


def jump_reversals(path, rest, lift=8.0, still=0.3):
    """The frames an airborne path (over `rest` + `lift`) turns back the way it came: a vanilla jump rises, then falls,
    turning once; a stutter turns more. (time, z)."""
    turns, way = [], 0
    for (t0, z0), (t1, z1) in zip(path, path[1:]):
        if min(z0, z1) < rest + lift:
            way = 0  # landed: the next jump starts afresh
            continue
        if abs(z1 - z0) < still:
            continue
        now = 1 if z1 > z0 else -1
        if way and now != way:
            turns.append((round(t1, 3), round(z1, 1)))
        way = now
    return turns


def scenario_copy_jumps_smoothly(ctx, jumper=""):
    """The jumper's player jumps in place three times (real space presses): the other game's copy rises and falls
    with it as a vanilla jump does, never stuttering mid-air (turning back and forth, or lagging off its owner)."""
    watcher = "client" if jumper == "" else ""
    copy = ctx.player_copy(watcher)
    if copy is None:
        raise ctx.failure(f"[{watcher or 'host'}] no copy of the other player")
    ctx.show(watcher, lambda actors: copy)
    rest = ctx.local_player(jumper)["position"][2]
    with AnimTrace(ctx, f"jump-{jumper or 'host'}", {jumper: {0x14: "owner"}, watcher: {copy: "copy"}}, video=False,
                   bools=("bInJumpState",)) as trace:
        def jumped(game, who, after):
            """That actor's trace rose 20 units over the ground after `after`, then came back down."""
            trace.take()
            heights = [e["pos"][2] for e in trace.entries if e["kind"] == "pos" and e["game"] == game
                       and e["who"] == who and e["time"] >= after]
            up = next((i for i, z in enumerate(heights) if z > rest + 20), None)
            return up is not None and any(z < rest + 3 for z in heights[up:])
        for jump in range(3):
            pressed = time.time()
            ctx.hold_key(jumper, "space", 0.1)
            ctx.wait_for(f"[{jumper or 'host'}] jump {jump + 1} rises and lands", lambda: jumped(jumper, "owner", pressed)
                         and ctx.local_player(jumper).get("character_state") == ON_GROUND
                         and not ctx.graph_bools(jumper, "bInJumpState")["bInJumpState"], 5)
            ctx.wait_for(f"[{watcher or 'host'}] the copy rises and lands with jump {jump + 1}",
                         lambda: jumped(watcher, "copy", pressed), 5)
    owner = height_path(trace.entries, jumper, "owner")
    theirs = height_path(trace.entries, watcher, "copy")
    result = compare_strafe(owner, theirs)
    peak = max((z for _, z in owner), default=rest) - rest
    copy_peak = max((z for _, z in theirs), default=rest) - rest
    log(f"[{watcher or 'host'}] the copy's height against its owner's: {result}; peaks {peak:.0f} / {copy_peak:.0f}; "
        f"trace {trace.dir}")
    (trace.dir / "jump.json").write_text(json.dumps({"owner": owner, "copy": theirs, "result": result}))
    if result is None or peak < 20:
        raise ctx.failure(f"[{jumper or 'host'}] the player did not jump (peak {peak:.0f} units)")
    step = lambda path: max((abs(z1 - z0) for (_, z0), (_, z1) in zip(path, path[1:])), default=0)
    if step(theirs) > 2 * step(owner) + 2:  # a warp up (the owner's frames: 6-7 units at most)
        raise ctx.failure(f"[{watcher or 'host'}] the copy hops: {step(theirs):.0f} units in a frame, its owner "
                          f"{step(owner):.0f} at most")
    owner_turns, copy_turns = jump_reversals(owner, rest), jump_reversals(theirs, rest)
    log(f"[{watcher or 'host'}] turns in the air: owner {owner_turns}, copy {copy_turns}")
    if len(owner_turns) != 3:
        raise ctx.failure(f"[{jumper or 'host'}] the trace holds {len(owner_turns)} jumps, not 3: {owner_turns}")
    if len(copy_turns) > len(owner_turns):
        raise ctx.failure(f"[{watcher or 'host'}] the copy stutters in the air: it turns {len(copy_turns)} times, its owner "
                          f"{len(owner_turns)}, e.g. {copy_turns[:6]}")
    # One delay fitted to the whole trace: a steered clock leaves up to ~20 units in a fall (the stutter left 73-77).
    if copy_peak < peak - 10 or result["worst"] > 35:
        raise ctx.failure(f"[{watcher or 'host'}] the copy's jump is off its owner's: peaks {peak:.0f} / {copy_peak:.0f}, "
                          f"{result}")
