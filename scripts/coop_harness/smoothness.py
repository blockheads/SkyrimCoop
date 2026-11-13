"""How smoothly a player's copy follows it (docs/design/copy-clock.md, "Measuring smoothness"): a fixed course by real
input (walk, run, strafe, a running curve), each actor's speed per engine frame, and the copy's roughness against its
owner's. Pure measures below; run_course drives the duo."""
import math
import time

MOVING = 40.0  # units/s: slower is standing, not measured
WINDOW = 4     # frames either side for a frame's typical speed


def series(entries, game, who):
    """[(frame, wall s, engine frame s, (x, y, z), yaw)] of that actor's position entries, one per frame, in order."""
    found = {}
    for e in entries:
        if e.get("kind") == "pos" and e.get("game") == game and e.get("who") == who and "f" in e:
            found[e["f"]] = (e["f"], e["time"], (e.get("dt") or 0.0) / 1000.0, tuple(e["pos"]), e.get("yaw", 0.0))
    return [found[f] for f in sorted(found)]


def frame_speeds(path, lag=1):
    """[(wall s, speed, yaw rate)] between consecutive frames: the move over the engine frame that made it. A position
    read in our update shows the previous frame's physics step (actor-movement.md, "Where a copy's step falls"), so
    `lag` 1 divides by the earlier frame's length, 0 by the later's."""
    out = []
    for a, b in zip(path, path[1:]):
        if b[0] != a[0] + 1:
            continue
        dt = a[2] if lag else b[2]
        if dt <= 0.0:
            continue
        move = math.hypot(b[3][0] - a[3][0], b[3][1] - a[3][1])
        turn = math.remainder(b[4] - a[4], math.tau)
        out.append((b[1], move / dt, turn / dt))
    return out


def roughness(speeds, start=None, end=None):
    """How unevenly it moves: each moving frame's speed against its neighbours' (WINDOW either side, less the fastest
    and slowest), as a fraction. {frames, median, p95, over20 (share of frames off by more than 20 %), speed (median)}."""
    values = [s for _, s, _ in speeds]
    errors, typical = [], []
    for i, (t, s, _) in enumerate(speeds):
        if (start is not None and t < start) or (end is not None and t > end):
            continue
        # Its neighbours, not itself, less their extremes: a median would hide a frame-by-frame alternation.
        near = sorted(values[max(0, i - WINDOW):i] + values[i + 1:i + WINDOW + 1])[1:-1]
        if len(near) < WINDOW:
            continue
        mid = sum(near) / len(near)
        if mid < MOVING:
            continue
        errors.append(abs(s - mid) / mid)
        typical.append(mid)
    if not errors:
        return None
    errors.sort()
    return {"frames": len(errors), "median": round(errors[len(errors) // 2], 3),
            "p95": round(errors[min(len(errors) - 1, int(len(errors) * 0.95))], 3),
            "over20": round(sum(e > 0.2 for e in errors) / len(errors), 3), "speed": round(sorted(typical)[len(typical) // 2])}


def reversals(path, start=None, end=None, move=0.5):
    """Frames whose move turns back on the previous one (cosine < -0.5) while both move more than `move` units."""
    found = 0
    for a, b, c in zip(path, path[1:], path[2:]):
        if (start is not None and b[1] < start) or (end is not None and b[1] > end):
            continue
        u = (b[3][0] - a[3][0], b[3][1] - a[3][1])
        v = (c[3][0] - b[3][0], c[3][1] - b[3][1])
        lu, lv = math.hypot(*u), math.hypot(*v)
        if lu > move and lv > move and (u[0] * v[0] + u[1] * v[1]) / (lu * lv) < -0.5:
            found += 1
    return found


def at(path, when):
    """The path's (x, y) at a wall time, linear between frames."""
    for a, b in zip(path, path[1:]):
        if a[1] <= when <= b[1]:
            f = (when - a[1]) / (b[1] - a[1]) if b[1] > a[1] else 0.0
            return (a[3][0] + (b[3][0] - a[3][0]) * f, a[3][1] + (b[3][1] - a[3][1]) * f)
    return None


def tracking(owner, copy, start=None, end=None):
    """The copy against its owner's path: the delay fitted (s) and the mean and worst distance (units) after it."""
    def errors(delay):
        out = []
        for c in copy:
            if (start is not None and c[1] < start) or (end is not None and c[1] > end):
                continue
            o = at(owner, c[1] - delay)
            if o is not None:
                out.append(math.hypot(c[3][0] - o[0], c[3][1] - o[1]))
        return out
    best = min((d / 200 for d in range(0, 160)), key=lambda d: (lambda e: sum(e) / len(e) if e else 1e9)(errors(d)))
    found = errors(best)
    if not found:
        return None
    return {"delay_ms": round(best * 1000), "mean": round(sum(found) / len(found), 1), "worst": round(max(found), 1)}


def measure(entries, owner_game, copy_game, segments):
    """Per segment (name, start, end): the owner's and the copy's roughness, reversals and the copy's tracking."""
    owner, copy = series(entries, owner_game, "owner"), series(entries, copy_game, "copy")
    # The alignment that makes the owner's own (vanilla) motion smoothest is the engine's; the copy gets the same.
    lag = min((0, 1), key=lambda l: (roughness(frame_speeds(owner, l)) or {"median": 9})["median"])
    owner_speeds, copy_speeds = frame_speeds(owner, lag), frame_speeds(copy, lag)
    out = {"lag": lag}
    for name, start, end in segments:
        delay = (tracking(owner, copy, start, end) or {}).get("delay_ms", 0) / 1000
        out[name] = {"owner": roughness(owner_speeds, start, end), "copy": roughness(copy_speeds, start + delay, end + delay),
                     "owner_reversals": reversals(owner, start, end), "copy_reversals": reversals(copy, start + delay, end + delay),
                     "tracking": tracking(owner, copy, start, end)}
    return out


def message_bytes(perf, names=("ReferencesMoveRequest", "NotifyReferencesMove", "NotifyBodyFrames", "GraphOpsRequest", "NotifyGraphOps",
                                "GraphSyncPointRequest", "NotifyGraphSyncPoint")):
    """Bytes/s and messages/s per message name from a perf readout's top rows (both ways summed)."""
    messages = (perf or {}).get("messages") or {}
    window = messages.get("window_s") or 1.0
    out = {}
    for row in messages.get("top", []):
        if row.get("name") in names:
            entry = out.setdefault(f"{row['name']} {row.get('way', '')}", {"bytes_s": 0.0, "count_s": 0.0})
            entry["bytes_s"] += row.get("bytes", 0) / window
            entry["count_s"] += row.get("count", 0) / window
    for entry in out.values():
        entry["bytes_each"] = round(entry["bytes_s"] / entry["count_s"], 1) if entry["count_s"] else 0.0
        entry["bytes_s"], entry["count_s"] = round(entry["bytes_s"]), round(entry["count_s"], 1)
    return out


def wire_sample(ctx, game):
    """(seconds, ENet's sent and received bytes after compression, and the payload bytes before it) for that game."""
    line = (ctx.game(game).read_status() or {}).get("line") or {}
    wire = line.get("wire_totals") or {}
    return (time.monotonic(), wire.get("sent_bytes", 0), wire.get("received_bytes", 0), line.get("sent_uncompressed", 0),
            line.get("recv_uncompressed", 0))


def wire_rate(before, after):
    """Bytes/s on the wire (ENet datagrams, compression and protocol included; its 32-bit counters wrap)."""
    seconds = max(after[0] - before[0], 1e-3)
    return {"sent_s": round(((after[1] - before[1]) % 2**32) / seconds), "received_s": round(((after[2] - before[2]) % 2**32) / seconds)}


def run_fight(ctx, wolves, seconds):
    """Wolves fight both players (god mode) at the host's for `seconds`: each game's wire and message bytes/s, and the
    graph state saved per action (the counter), to weigh sending it only when a copy needs it (docs/ideas.md)."""
    for game in ("", "client"):
        ctx.run_console("tgm", _game=ctx.game(game))
        ctx.game(game).rpc({"cmd": "counters", "reset": True}, timeout=10)
    ctx.run_console(f"player.placeatme 00023ABE {wolves}", _game=ctx.host)
    ctx.wait_for("the wolves to fight", lambda: any(a.get("combat_target") not in (None, "0") for a in ctx.look("").get("nearby", [])), 15)
    before = {game: wire_sample(ctx, game) for game in ("", "client")}
    time.sleep(seconds)
    after = {game: wire_sample(ctx, game) for game in ("", "client")}
    out = {}
    for game in ("", "client"):
        perf = (ctx.game(game).rpc({"cmd": "perf", "window_ms": int(seconds * 1000)}, timeout=10) or {}).get("perf")
        counters = (ctx.game(game).rpc({"cmd": "counters"}, timeout=10) or {}).get("counters") or {}
        out[game or "host"] = {"wire": wire_rate(before[game], after[game]), "messages": message_bytes(perf),
                               "graph_state": counters.get("graph state bytes")}
    return out


def run_course(ctx, mover, trace_type):
    """Drives the mover's player through the course by real input while both games trace; returns the measures."""
    watcher = "client" if mover == "" else ""
    copy = ctx.player_copy(watcher)
    if copy is None:
        raise ctx.failure(f"[{watcher or 'host'}] no copy of the other player")
    segments = []

    def segment(name, run):
        start = time.time()
        run()
        segments.append((name, start, time.time()))

    def curve():
        yaw = math.degrees(ctx.local_player(mover)["rotation"][2])
        ctx.act(mover, {"name": "runs in a curve", "ready": ["takes_input"], "buttons": ["forward"],
                        "turn": {"yaw": (yaw + 170) % 360, "rate": 90}, "seconds": 3}, expect=None)

    with trace_type(ctx, f"smoothness-{mover or 'host'}", {mover: {0x14: "owner"}, watcher: {copy: "copy"}}, video=False) as trace:
        begun = time.time()
        wire_before = {game: wire_sample(ctx, game) for game in ("", "client")}
        segment("walk", lambda: ctx.hold_key(mover, "shift+w", 2.5))
        segment("run", lambda: ctx.hold_key(mover, "w", 2.5))
        segment("strafe", lambda: [ctx.hold_key(mover, key, 0.6) for key in ("d", "a", "d", "a")])
        segment("curve", curve)
        frame = ctx.game_frame(watcher)
        ctx.wait_for("the copy has a second to finish", lambda: ctx.game_frame(watcher) > frame + 30, 5)
        window = int((time.time() - begun) * 1000)
        wire_after = {game: wire_sample(ctx, game) for game in ("", "client")}
    perf = {game or "host": {**message_bytes((ctx.game(game).rpc({"cmd": "perf", "window_ms": window}, timeout=10) or {}).get("perf")),
                             "on the wire": wire_rate(wire_before[game], wire_after[game])} for game in ("", "client")}
    return {"trace": str(trace.dir), "measures": measure(trace.entries, mover, watcher, segments), "wire": perf}
