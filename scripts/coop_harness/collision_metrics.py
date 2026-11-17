"""How a physics object moved, as numbers a vanilla game sets the range for (docs/design/physics-sync.md, "The vanilla
baseline"). Pure: a body trace's samples in, metrics out; no games.

A path is [(seconds, position, speed)] of one reference in one game, in order: its 3D each frame and its body's speed.
"""
import math

MOVE = 0.05       # units a frame: smaller steps are rest
STEP = 2 / 60     # Havok steps 1/60 s, sometimes twice in a frame: a frame's move is at most its speed over this
TURN = -0.5       # cosine between two frames' moves: below it the second turns back on the first
EVERY_ROUND = ("jump", "turns")   # wrong whenever it happens: a teleport, a shake


def path_of(entries, ref):
    """The path of `ref` from a body trace's entries (harness body_trace)."""
    path = []
    for entry in entries:
        if entry.get("id") != ref or entry.get("rendered"):
            continue
        body = (entry.get("bodies") or [{}])[0]
        path.append((entry["t"] / 1e6, entry["position_3d"], math.hypot(*body.get("velocity", [0.0, 0.0, 0.0]))))
    return path


def measure(path):
    """One object's motion:
      moved      start to rest, units
      path       the way it went, units
      seconds    first move to last
      peak       its highest speed over five frames, units/s, by its 3D (its body's own peak is one step's impulse,
                 caught or not by where the frame fell; a keyframed body reads none)
      jump       the furthest one frame took it beyond what its speed allows (a teleport; flying, about 0)
      turns      frames whose move turned back on the one before (shaking)"""
    if len(path) < 2:
        return None
    steps = [(t1, math.dist(p0, p1), [b - a for a, b in zip(p0, p1)], max(t1 - t0, STEP) * max(s0, s1))
             for (t0, p0, s0), (t1, p1, s1) in zip(path, path[1:])]
    # A keyframed body (held in another game) reads no speed when sampled though it moves evenly: a step is also
    # allowed twice what the steps around it took.
    def around(n):
        near = sorted(step for _, step, _, _ in steps[max(0, n - 2):n] + steps[n + 1:n + 3])
        return 2 * near[len(near) // 2] if len(near) >= 2 else 0.0
    steps = [(t, step, move, max(allowed, around(n))) for n, (t, step, move, allowed) in enumerate(steps)]
    covered = max((math.dist(path[n][1], path[n + 5][1]) / (path[n + 5][0] - path[n][0])
                   for n in range(len(path) - 5) if path[n + 5][0] - path[n][0] >= 4 * STEP / 2), default=0.0)
    moving = [t for t, step, _, _ in steps if step > MOVE]
    turns = 0
    for (_, a, da, _), (_, b, db, _) in zip(steps, steps[1:]):
        if a > MOVE and b > MOVE and sum(x * y for x, y in zip(da, db)) / (a * b) < TURN:
            turns += 1
    return {
        "moved": math.dist(path[0][1], path[-1][1]),
        "path": sum(step for _, step, _, _ in steps if step > MOVE),
        "seconds": moving[-1] - moving[0] if moving else 0.0,
        "peak": covered,
        "jump": max((step - allowed for _, step, _, allowed in steps), default=0.0),
        "turns": turns,
        "rest": list(path[-1][1]),
    }


def measure_group(each):
    """Many objects as one (a pile, whose objects trade places run to run): the worst jump, shake and speed, the
    longest any moved, the ways added up, and how many moved at all (`moved`)."""
    each = [m for m in each if m]
    if not each:
        return None
    return {
        "moved": sum(1 for m in each if m["moved"] > 5.0),
        "path": sum(m["path"] for m in each),
        "seconds": max(m["seconds"] for m in each),
        "peak": max(m["peak"] for m in each),
        "jump": max(m["jump"] for m in each),
        "turns": max(m["turns"] for m in each),
    }


def envelope(runs, widen=0.25, floors=None, margin=0.10):
    """The range of each metric over a vanilla game's runs, widened each way by a quarter of its spread, at least the
    metric's floor, and (how far, how long, how fast) at least a tenth of the bound: Havok is not repeatable, so a
    co-op game is held to the range, not to a number. The tenth: two samples of 20 rounds of one game with no session
    differ from each other by more than that (3 to 13 of 20 rounds of one outside the other's ranges)."""
    floors = {"moved": 5.0, "path": 5.0, "seconds": 0.3, "peak": 15.0, "jump": 1.0, "turns": 2, "fell": 5.0, **(floors or {})}
    ranges = {}
    for metric, floor in floors.items():
        values = [run[metric] for run in runs if run and metric in run]
        if not values:
            continue
        low, high = min(values), max(values)
        slack = max((high - low) * widen, floor)
        relative = 0.0 if metric in EVERY_ROUND else margin
        ranges[metric] = [max(0.0, low - max(slack, low * relative)), high + max(slack, high * relative)]
    return ranges


def outside(metrics, ranges, only=None):
    """The metrics outside the vanilla range, as text; none: []."""
    off = []
    for metric, (low, high) in ranges.items():
        if only and metric not in only:
            continue
        value = metrics[metric]
        if not low <= value <= high:
            off.append(f"{metric} {value:.1f} (vanilla {low:.1f} to {high:.1f})")
    return off



def limits(ranges, baseline=None):
    """A trial's highest turns over its objects, and the highest jump of any trial in `baseline` (a frame's excess is
    the engine's, whatever the objects): what vanilla does to any of them, not to one."""
    limit = {metric: max(r[metric][1] for r in ranges.values() if metric in r) for metric in EVERY_ROUND}
    for trial in (baseline or {}).values():
        limit["jump"] = max([limit["jump"]] + [r["jump"][1] for r in trial["ranges"].values()])
    return limit


def compare(rounds, ranges, limit):
    """Several rounds of one object against vanilla, as text; none: []. Every round's jump and turns are inside the
    trial's limits; of how far, how long and how fast, the middle round is inside vanilla's range: one odd roll is
    Havok (a single vanilla round is outside another twenty's ranges one time in five), a habit is ours."""
    rounds = [r for r in rounds if r]
    if not rounds:
        return ["never moved"]
    off = []
    for metric in EVERY_ROUND:
        worst = max(r[metric] for r in rounds)
        if worst > limit[metric]:
            off.append(f"{metric} {worst:.1f} (vanilla up to {limit[metric]:.1f})")
    for metric, (low, high) in ranges.items():
        if metric in EVERY_ROUND:
            continue
        values = sorted(r[metric] for r in rounds)
        middle = values[len(values) // 2] if len(values) % 2 else (values[len(values) // 2 - 1] + values[len(values) // 2]) / 2
        if not low <= middle <= high:
            off.append(f"{metric} {middle:.1f} in the middle round of {len(values)} (vanilla {low:.1f} to {high:.1f})")
    return off


def slept_moving(entries, ref, speed=20.0):
    """How often a reference's first body went from awake to asleep while it moved faster than `speed` units/s: our
    island sleeps once took a carried or falling wheel along (physics.md, "When an island sleeps"); Havok's own never
    does."""
    count, before = 0, None
    for entry in entries:
        if entry.get("id") != ref or not entry.get("bodies") or not entry["bodies"][0]:
            continue
        body = entry["bodies"][0]
        if before and before["awake"] and not body["awake"] and math.hypot(*before["velocity"]) > speed:
            count += 1
        before = body
    return count
