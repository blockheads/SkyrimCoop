"""Trace and fixture objects owned by an explicit Session."""
import bisect
import json
import math
import re
import subprocess
import time
from .common import (
    log_time,
    DYNAMIC_MOTIONS,
    LOG_LINE,
    TRACES,
    TRACE_NOISE,
)
from .utilities import log

class GodMode:
    """Both players invulnerable for the scenario (toggled back afterwards)."""
    def __enter__(self):
        ctx = self.session
        _game = ctx.host
        for instance in ("", "client"):
            ctx.console_on(instance, "tgm")
    def __exit__(self, *exc):
        ctx = self.session
        _game = ctx.host
        for instance in ("", "client"):
            ctx.console_on(instance, "tgm")

    def __init__(self, ctx):
        self.session = ctx


class AnimTrace:
    """Records what actors' animations do in both games (the harness's anim_trace: every behavior graph
    event, action taken, life state, kill move, health and position, each frame), both screens, and the
    plugin logs' lines naming them, onto one wall-clock timeline:

        with AnimTrace("kill-move", {"": {npc: "wolf", 0x14: "host"}, "client": {copy: "wolf"}}) as trace:
            ...
            trace.wait("client", "wolf", "KillActor", 3)

    Leaves build/coop-test/traces/<time>-<name>/: timeline.txt (host left, client right), trace.json,
    host.mp4 / client.mp4, and strip-*.png (both screens side by side) at moments asked for with mark()."""

    def __init__(self, ctx, name, actors, bools=("bIsSynced",), video=True):
        _game = ctx.host
        self.session = ctx
        self.name, self.actors, self.bools, self.video = name, actors, list(bools), video
        self.entries, self.marks, self.captures = [], [], {}

    def __enter__(self):
        ctx = self.session
        _game = ctx.host
        self.dir = TRACES / f"{time.strftime('%Y%m%d-%H%M%S')}-{self.name}"
        self.dir.mkdir(parents=True, exist_ok=True)
        if self.video:
            for game in self.actors:
                _game = ctx.game(game)
                path = self.dir / f"{game or 'host'}.mp4"
                capture = subprocess.Popen(
                    ["ffmpeg", "-y", "-hide_banner", "-nostats", "-f", "x11grab", "-framerate", "30", "-video_size", "1280x720",
                     "-i", f"{_game.display}+4,30", "-c:v", "libx264", "-preset", "ultrafast", "-pix_fmt", "yuv420p", str(path)],
                    stdin=subprocess.PIPE, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE, text=True)
                self.captures[game] = [capture, path, None]
        self.start = time.time()
        for game, ids in self.actors.items():
            _game = ctx.game(game)
            if not (_game.rpc({"cmd": "anim_trace", "actors": list(ids), "bools": self.bools}) or {}).get("ok"):
                self.__exit__(None, None, None)  # the captures and any game already recording
                raise ctx.failure(f"[{game or 'host'}] the animation trace did not start")
        return self

    def who(self, game, form_id):
        ctx = self.session
        _game = ctx.host
        return "game" if form_id == 0 else self.actors.get(game, {}).get(form_id, f"{form_id:X}")

    def _add(self, game, entries):
        ctx = self.session
        _game = ctx.host
        for entry in entries:
            entry.update(game=game, who=self.who(game, entry.get("actor", 0)), time=entry["t"] / 1e6)
            self.entries.append(entry)

    def take(self):
        """Pulls what both games recorded so far."""
        ctx = self.session
        _game = ctx.host
        for game in self.actors:
            _game = ctx.game(game)
            self._add(game, (_game.rpc({"cmd": "anim_trace"}, timeout=10) or {}).get("entries", []))
        self.entries.sort(key=lambda entry: entry["time"])

    def find(self, game, who, event=None, kind="graph", after=0.0):
        """The first entry of that game's actor (by its name here) of that kind (graph event name, or action
        event), after a wall-clock time."""
        ctx = self.session
        _game = ctx.host
        return next((e for e in self.entries if e["game"] == game and e["who"] == who and e["kind"] == kind
                     and e["time"] >= after and (event is None or e.get("event") == event)), None)

    def action_before(self, entry):
        """The last action that entry's actor took up to it (what put it in that state)."""
        ctx = self.session
        _game = ctx.host
        return next((e for e in reversed(self.entries) if e["game"] == entry["game"] and e["who"] == entry["who"]
                     and e["kind"] == "action" and e["time"] <= entry["time"]), None)

    def wait(self, game, who, event, timeout, kind="graph", after=0.0):
        """Waits for that entry (see find); returns it."""
        ctx = self.session
        _game = ctx.host
        def found():
            self.take()
            return self.find(game, who, event, kind, after)
        return ctx.wait_for(f"[{game or 'host'}] {who}'s graph sends {event}" if kind == "graph" else
                        f"[{game or 'host'}] {who} {kind} {event}", found, timeout, _game=_game)

    def wait_state(self, game, who, key, value, timeout, after=0.0):
        """Waits for that actor's state (the harness's sample: life, weapon, kill_move, health, graph
        booleans) to read value; returns the entry."""
        ctx = self.session
        _game = ctx.host
        def found():
            self.take()
            return next((e for e in self.entries if e["game"] == game and e["who"] == who and e["kind"] == "state"
                         and e["time"] >= after and e["state"].get(key) == value), None)
        return ctx.wait_for(f"[{game or 'host'}] {who}'s {key} is {value}", found, timeout, _game=_game)

    def mark(self, label, host=None, client=None):
        """Both screens side by side into strip-<n>.png: each game's at its wall-clock time (now by default;
        the client's at the host's unless given, e.g. at the same moment of a replayed animation)."""
        ctx = self.session
        _game = ctx.host
        host = host if host is not None else time.time()
        self.marks.append((label, {"": host, "client": client if client is not None else host}))

    def layout(self, game, who, other, at):
        """Where the other actor is from this one at a wall-clock time: (distance, bearing from its heading,
        heading difference), degrees."""
        ctx = self.session
        _game = ctx.host
        def nearest(name):
            return min((e for e in self.entries if e["kind"] == "pos" and e["game"] == game and e["who"] == name),
                       key=lambda e: abs(e["time"] - at))
        mine, theirs = nearest(who), nearest(other)
        dx, dy = theirs["pos"][0] - mine["pos"][0], theirs["pos"][1] - mine["pos"][1]
        turn = lambda degrees: (degrees + 180) % 360 - 180
        return (math.hypot(dx, dy), turn(math.degrees(math.atan2(dx, dy) - mine["yaw"])),
                turn(math.degrees(theirs["yaw"] - mine["yaw"])))

    def __exit__(self, *exc):
        ctx = self.session
        _game = ctx.host
        for game in self.actors:
            _game = ctx.game(game)
            self._add(game, (_game.rpc({"cmd": "anim_trace", "stop": True}, timeout=10) or {}).get("entries", []))
        self.entries.sort(key=lambda entry: entry["time"])
        self.end = time.time()
        for game, capture in self.captures.items():
            process = capture[0]
            try:
                _, errors = process.communicate("q", timeout=15)
            except subprocess.TimeoutExpired:
                process.kill()
                _, errors = process.communicate()
            # x11grab stamps frames with the wall clock: the first frame's time lines the video up.
            started = re.search(r"start: (\d+\.\d+)", errors or "")
            capture[2] = float(started.group(1)) if started else None
        self._logs()
        (self.dir / "trace.json").write_text(json.dumps({"start": self.start, "actors": {g or "host": {f"{i:X}": n for i, n in a.items()}
                                                                                          for g, a in self.actors.items()},
                                                         "videos": {g or "host": c[2] for g, c in self.captures.items()},
                                                         "entries": self.entries}))
        (self.dir / "timeline.txt").write_text(self.timeline())
        for index, (label, times) in enumerate(self.marks):
            self.strip(self.dir / f"strip-{index}-{re.sub(r'[^a-z0-9]+', '-', label.lower())}.png", times, label)
        log(f"Animation trace: {self.dir}")

    def _logs(self):
        """The plugin logs' lines in the trace's window naming a traced actor (by its hex form id; the
        player's 14 is too common a number to match)."""
        ctx = self.session
        _game = ctx.host
        for game, ids in self.actors.items():
            names = {f"{i:X}": n for i, n in ids.items() if i != 0x14}
            if not names:
                continue
            token = re.compile(r"\b(" + "|".join(names) + r")\b")
            try:
                lines = ctx.plugin_log(game).read_text(errors="replace").splitlines()[-20000:]
            except OSError:
                continue
            for line in lines:
                match = LOG_LINE.match(line)
                if not match or not (named := token.search(match.group(3))):
                    continue
                at = log_time(match.group(1))
                if self.start - 0.1 <= at <= self.end + 0.1:
                    self.entries.append({"game": game, "who": names[named.group(1)], "time": at, "kind": "log",
                                         "level": match.group(2), "text": match.group(3), "f": None})
        self.entries.sort(key=lambda entry: entry["time"])

    def timeline(self, everything=False):
        """One line per entry, host's on the left and the client's on the right, at ms from the start.
        Positions are left out; noise (steps, sounds, the first-person graph) unless everything."""
        ctx = self.session
        _game = ctx.host
        width = 70
        lines = [f"{'ms':>8}  {'host':<{width}}  client"]
        for entry in self.entries:
            kind = entry["kind"]
            if kind == "pos" or (not everything and kind == "graph" and (entry.get("project") == "FirstPerson"
                                                                           or TRACE_NOISE.match(entry.get("event", "")))):
                continue
            if kind == "graph":
                text = f"{entry['event']}" + (f" ({entry['payload']})" if entry.get("payload") else "")
            elif kind == "action":
                text = f"ACTION {entry['event']} {entry['action']:X}" + (f" idle {entry['idle']:X}" if entry.get("idle") else "") \
                       + (f" -> {self.who(entry['game'], entry['target'])}" if entry.get("target") else "")
            elif kind in ("state", "game"):
                text = kind.upper() + " " + " ".join(f"{k}={v}" for k, v in entry["state"].items())
            elif kind == "log":
                text = f"LOG {entry['text']}"
            else:
                text = kind.upper() + " " + " ".join(f"{k}={v}" for k, v in entry.items()
                                                     if k not in ("t", "f", "kind", "game", "who", "time", "actor"))
            frame = f"f{entry['f']}" if entry.get("f") is not None else ""
            cell = f"{frame:>6} {entry['who']}: {text}"
            ms = (entry["time"] - self.start) * 1000
            lines.append(f"{ms:8.1f}  {cell:<{width}}" if entry["game"] == "" else f"{ms:8.1f}  {'':<{width}}  {cell}")
        return "\n".join(lines) + "\n"

    def gap(self, first, second):
        """Milliseconds from one entry to another."""
        ctx = self.session
        _game = ctx.host
        return (second["time"] - first["time"]) * 1000

    def strip(self, path, times, label=""):
        """Both games' screens side by side, each at its wall-clock time ({game: time})."""
        ctx = self.session
        _game = ctx.host
        frames = []
        for game, (_, video, started) in self.captures.items():
            if started is None:
                continue
            frame = self.dir / f".frame-{game or 'host'}.png"
            subprocess.run(["ffmpeg", "-y", "-loglevel", "error", "-ss", f"{max(0.0, times[game] - started):.3f}", "-i", str(video),
                            "-frames:v", "1", "-vf", "scale=640:-1", str(frame)], check=False)
            if frame.exists():
                frames.append(frame)
        if frames:
            subprocess.run(["convert", *map(str, frames), "+append", "-gravity", "north", "-background", "black",
                            "-fill", "white", "-pointsize", "18", "-splice", "0x26",
                            "-annotate", "+0+2", f"{label}  (host +{(times[''] - self.start) * 1000:.0f}ms | "
                                                 f"client +{(times['client'] - self.start) * 1000:.0f}ms)", str(path)], check=False)
            for frame in frames:
                frame.unlink(missing_ok=True)


class StandInHost:
    """A host on the network that refuses every game asking to join (coop_peer host); its port."""
    def __init__(self, ctx, refuse, *options):
        _game = ctx.host
        self.session = ctx
        self.args = ("host", "--refuse", refuse, "--version", "v0.0.0-stand-in", "--game", "1.6.640", "--skse", "2.2.3", *options)

    def __enter__(self):
        ctx = self.session
        _game = ctx.host
        port = ctx.free_port()
        self.process = subprocess.Popen(ctx.run_peer(*self.args, "--port", port), stdout=subprocess.PIPE, text=True)
        if self.process.stdout.readline().strip() != f"listening {port}":
            self.process.kill()
            raise ctx.failure(f"the stand-in host did not listen on {port}")
        return port

    def __exit__(self, *exc):
        ctx = self.session
        _game = ctx.host
        self.process.kill()
        self.process.wait()


class Watch:
    """A condition with the value a press changes (for press_until)."""
    def __init__(self, ctx, value, test):
        _game = ctx.host
        self.session = ctx
        self.watch, self.test = value, test

    def __call__(self):
        ctx = self.session
        _game = ctx.host
        return self.test(self.watch())


class FlightTrace:
    """Records the host's and the client's physics of some references each frame (the harness's body_trace), and
    reports how the client's playback follows the host's flight: each client frame's 3D against the host's 3D
    BodyService::kDelayMs earlier (both on one machine's wall clock), the worst and typical gap, its biggest
    frame-to-frame jump beyond the host's, and the snap at its rest. Leaves the samples in
    build/coop-test/traces/<time>-<name>/bodies.json.

        with FlightTrace("fireball", refs) as trace:
            ...
        log(trace.report(target))"""

    DELAY = 0.1  # BodyService::kDelayMs

    def __init__(self, ctx, name, refs):
        _game = ctx.host
        self.session = ctx
        self.name, self.refs = name, sorted(refs)
        self.samples = {"": [], "client": []}

    def __enter__(self):
        ctx = self.session
        _game = ctx.host
        for game in self.samples:
            _game = ctx.game(game)
            if not (_game.rpc({"cmd": "body_trace", "refs": self.refs}) or {}).get("ok"):
                raise ctx.failure(f"[{game or 'host'}] the body trace did not start")
        return self

    def take(self):
        ctx = self.session
        _game = ctx.host
        for game, samples in self.samples.items():
            _game = ctx.game(game)
            samples.extend((_game.rpc({"cmd": "body_trace"}, timeout=10) or {}).get("entries", []))

    def __exit__(self, *exc):
        ctx = self.session
        _game = ctx.host
        for game, samples in self.samples.items():
            _game = ctx.game(game)
            samples.extend((_game.rpc({"cmd": "body_trace", "stop": True}, timeout=10) or {}).get("entries", []))
        folder = TRACES / f"{time.strftime('%Y%m%d-%H%M%S')}-{self.name}"
        folder.mkdir(parents=True, exist_ok=True)
        (folder / "bodies.json").write_text(json.dumps(self.samples))
        self.folder = folder

    def path(self, game, ref):
        """[(seconds, 3D position, first body's motion, awake)] of that game's reference, in order."""
        ctx = self.session
        _game = ctx.host
        return [(e["t"] / 1e6, e["position_3d"], (e["bodies"] or [{}])[0].get("motion"), (e["bodies"] or [{}])[0].get("awake"))
                for e in self.samples[game] if e["id"] == ref and not e.get("rendered")]

    @staticmethod
    def at(path, when):
        """The path's position at a time, between its samples."""
        for (t0, p0, *_), (t1, p1, *_) in zip(path, path[1:]):
            if t0 <= when <= t1:
                f = (when - t0) / (t1 - t0) if t1 > t0 else 0.0
                return [a + (b - a) * f for a, b in zip(p0, p1)]
        return None

    def measure(self, ref):
        """How the client's 3D followed the host's flight of that reference (see the class), or None without one."""
        ctx = self.session
        _game = ctx.host
        host, guest = self.path("", ref), self.path("client", ref)
        moving = [t for (t, p, *_), (_, q, *_) in zip(host, host[1:]) if math.dist(p, q) > 0.05]
        if not moving or not guest:
            return None
        start, end = moving[0], moving[-1]
        gaps, jumps, previous = [], [], None
        for t, p, *_ in guest:
            shown = self.at(host, t - self.DELAY) if start + self.DELAY <= t <= end + self.DELAY else None
            if shown is None:
                continue
            gaps.append((math.dist(p, shown), t - start))
            if previous:
                jumps.append((abs(math.dist(p, previous[0]) - math.dist(shown, previous[1])), t - start))
            previous = (p, shown)
        if not gaps:
            return None
        # A flicker: one frame's 3D away from both its neighbours, which agree (it went somewhere and came back).
        flickers = [(t1 - start, math.dist(p0, p1)) for (_, p0, *_), (t1, p1, *_), (_, p2, *_) in zip(guest, guest[1:], guest[2:])
                    if math.dist(p0, p1) > 5 and math.dist(p1, p2) > 5 and math.dist(p0, p2) < 0.5 * math.dist(p0, p1)]
        before = [p for t, p, *_ in guest if t <= end + self.DELAY]
        return {"seconds": end - start, "distance": math.dist(host[0][1], host[-1][1]), "frames": len(gaps),
                "typical": sorted(g for g, _ in gaps)[len(gaps) // 2], "worst": max(gaps), "jump": max(jumps, default=(0.0, 0.0)),
                "snap": math.dist(before[-1], host[-1][1]) if before else float("nan"), "flickers": flickers}

    def report(self, ref):
        ctx = self.session
        _game = ctx.host
        m = self.measure(ref)
        if not m:
            return f"{ref:X}: no flight traced"
        flickers = f" {[f'{a:.2f} s: {d:.0f}' for a, d in m['flickers'][:5]]}" if m["flickers"] else ""
        return (f"{ref:X} flew {m['seconds']:.2f} s ({m['distance']:.0f} units): the client's 3D is {m['typical']:.1f} units off "
                f"the host's {self.DELAY * 1000:.0f} ms earlier typically, {m['worst'][0]:.1f} at worst ({m['worst'][1]:.2f} s in), "
                f"its biggest jump {m['jump'][0]:.1f} beyond the host's ({m['jump'][1]:.2f} s in), {m['snap']:.1f} from the "
                f"host's rest just before its own; {len(m['flickers'])} flickers{flickers} ({m['frames']} frames; {self.folder.name})")

    # The client's playback never jumps further than this in a frame beyond the host's flight (ours did 10-12 units,
    # keyframing over a frame's time: docs/re/systems/physics.md), and never flickers.
    MAX_JUMP = 6.0

    def check(self, ref):
        ctx = self.session
        _game = ctx.host
        m = self.measure(ref)
        if m and (m["flickers"] or m["jump"][0] > self.MAX_JUMP):
            raise ctx.failure(f"[client] its playback of {self.report(ref)}")


class RagdollTrace:
    """Records actors' ragdolls frame by frame on their games (the harness's body_trace: each ragdoll body, and the
    skeleton's pelvis and head nodes, what a person sees) and says how each moved without a video: per series (a body,
    or a node) how often it turned back on itself (back and forth), its flickers (a frame away from both neighbours,
    which agree), when everything came to rest, and how far the skeleton strayed from its ragdoll. Leaves the samples
    in build/coop-test/traces/<time>-<name>/ragdoll.json.

        with RagdollTrace("kill-move", {"client": {0x14: "the guest"}, "": {copy: "the host's copy of the guest"}}) as trace:
            ...
        log(trace.report()); trace.check()"""

    MOVE = 0.5           # units a frame: smaller moves are rest, not motion
    TURN = -0.5          # cosine between two frames' moves: below it, the second turns back on the first
    MAX_SHAKE = 6        # turn-backs within any second of one series before it is jitter (a fall bounces once or twice)
    MAX_STRAY = 25.0     # units between a simulated ragdoll body and the bone it moves (the skeleton left behind)

    def __init__(self, ctx, name, actors):
        _game = ctx.host
        self.session = ctx
        self.name, self.actors = name, actors
        self.samples = {game: [] for game in actors}

    def __enter__(self):
        ctx = self.session
        _game = ctx.host
        for game, actors in self.actors.items():
            _game = ctx.game(game)
            if not (_game.rpc({"cmd": "body_trace", "refs": sorted(actors)}) or {}).get("ok"):
                raise ctx.failure(f"[{game or 'host'}] the body trace did not start")
        return self

    def take(self):
        """Fetches what was recorded so far (keeps the games' buffers small on long traces)."""
        ctx = self.session
        _game = ctx.host
        for game, samples in self.samples.items():
            _game = ctx.game(game)
            samples.extend((_game.rpc({"cmd": "body_trace"}, timeout=10) or {}).get("entries", []))

    def __exit__(self, *exc):
        ctx = self.session
        _game = ctx.host
        for game, samples in self.samples.items():
            _game = ctx.game(game)
            samples.extend((_game.rpc({"cmd": "body_trace", "stop": True}, timeout=10) or {}).get("entries", []))
        self.folder = TRACES / f"{time.strftime('%Y%m%d-%H%M%S')}-{self.name}"
        self.folder.mkdir(parents=True, exist_ok=True)
        (self.folder / "ragdoll.json").write_text(json.dumps(self.samples))

    def frames(self, game, actor):
        ctx = self.session
        _game = ctx.host
        return [e for e in self.samples[game] if e["id"] == actor and not e.get("rendered")]

    def series(self, frames):
        """{name: [(seconds, position or None)]}: each ragdoll body, then the pelvis and head nodes."""
        ctx = self.session
        _game = ctx.host
        out = {}
        count = max((len(f["bodies"]) for f in frames), default=0)
        for index in range(count):
            out[f"body {index}"] = [(f["t"] / 1e6, (f["bodies"][index] or {}).get("position") if index < len(f["bodies"]) else None)
                                   for f in frames]
        for index in range(count):
            out[f"bone {index}"] = [(f["t"] / 1e6, (f["bodies"][index] or {}).get("bone") if index < len(f["bodies"]) else None)
                                   for f in frames]
        for node in ("pelvis", "head"):
            out[f"{node} node"] = [(f["t"] / 1e6, f.get(node)) for f in frames]
        return out

    @classmethod
    def shake(cls, path):
        """Turn-backs of one series: [(seconds, size of the move that turned back)], and its flickers [(seconds, units)]."""
        points = [(t, p) for t, p in path if p]
        turns, flickers = [], []
        for (_, a), (t, b), (_, c) in zip(points, points[1:], points[2:]):
            first, second = [y - x for x, y in zip(a, b)], [y - x for x, y in zip(b, c)]
            n1, n2 = math.hypot(*first), math.hypot(*second)
            if n1 > cls.MOVE and n2 > cls.MOVE and sum(x * y for x, y in zip(first, second)) / (n1 * n2) < cls.TURN:
                turns.append((t, min(n1, n2)))
            if n1 > 5 and n2 > 5 and math.dist(a, c) < 0.5 * n1:
                flickers.append((t, n1))
        return turns, flickers

    @staticmethod
    def worst_second(turns):
        """The most turn-backs within any one second: (count, starting at)."""
        best, start = (0, None), 0
        for end in range(len(turns)):
            while turns[end][0] - turns[start][0] > 1.0:
                start += 1
            if end - start + 1 > best[0]:
                best = (end - start + 1, turns[start][0])
        return best

    def measure(self, game, actor):
        ctx = self.session
        _game = ctx.host
        frames = self.frames(game, actor)
        if len(frames) < 3:
            return None
        t0 = frames[0]["t"] / 1e6
        per = {}
        for name, path in self.series(frames).items():
            turns, flickers = self.shake(path)
            count, at = self.worst_second(turns)
            per[name] = {"turns": len(turns), "worst": count, "at": (at - t0) if at else None,
                         "size": sum(s for _, s in turns) / len(turns) if turns else 0.0, "flickers": [(t - t0, d) for t, d in flickers]}
        awake = [any((b or {}).get("awake") for b in f["bodies"]) for f in frames]
        rest = next((f["t"] / 1e6 - t0 for i, f in enumerate(frames) if not any(awake[i:])), None)
        stray = 0.0
        for f in frames:
            for b in f["bodies"]:
                if b and b.get("bone") and b.get("motion") in DYNAMIC_MOTIONS:
                    stray = max(stray, math.dist(b["bone"], b["position"]))
        return {"seconds": frames[-1]["t"] / 1e6 - t0, "frames": len(frames), "rest": rest, "stray": stray, "series": per}

    def describe(self, game, actor, label):
        ctx = self.session
        _game = ctx.host
        m = self.measure(game, actor)
        if not m:
            return f"[{game or 'host'}] {label}: not traced"
        worst = max(m["series"].items(), key=lambda kv: kv[1]["worst"])
        flick = sum(len(v["flickers"]) for v in m["series"].values())
        rest = f"came to rest {m['rest']:.1f} s in" if m["rest"] is not None else "NEVER came to rest"
        name, w = worst
        shake = (f"{name} turned back {w['worst']} times within a second ({w['at']:.1f} s in, moves of {w['size']:.1f} units), "
                 f"{w['turns']} in all" if w["worst"] else "nothing turned back on itself")
        return (f"[{game or 'host'}] {label}: {rest}; {shake}; {flick} flickers; a bone up to {m['stray']:.0f} units off its "
                f"body ({m['frames']} frames over {m['seconds']:.1f} s)")

    SPIN = 25.0          # degrees between the copy's facing (pelvis to head, seen from above) and its owner's
    LEVEL = 20.0         # units of pelvis to head seen from above below which a body stands too upright to face
    GETUP_APART = 0.25   # seconds between the owner's getting up and its copy's, its playback delay taken out

    def compare(self, owner_game, owner, copy_game, copy):
        """The copy against its owner as a person sees them, with the copy's playback delay taken out (the delay at
        which their pelvises lie closest): the stretches where the copy's skeleton faces away from the owner's (a spin,
        then a snap back), and when each began to get up (knock state 5). None without both traced."""
        ctx = self.session
        _game = ctx.host
        own = [f for f in self.frames(owner_game, owner) if f.get("pelvis") and f.get("head")]
        cop = [f for f in self.frames(copy_game, copy) if f.get("pelvis") and f.get("head")]
        if len(own) < 3 or len(cop) < 3:
            return None
        times = [f["t"] for f in own]
        def owner_at(t):
            i = min(max(bisect.bisect_left(times, t), 1), len(times) - 1)
            return own[i] if abs(own[i]["t"] - t) < abs(own[i - 1]["t"] - t) else own[i - 1]
        def gap(delay):
            gaps = sorted(math.dist(f["pelvis"], owner_at(f["t"] - delay)["pelvis"]) for f in cop[::5])
            return gaps[len(gaps) // 2]
        delay = min(range(0, 800_000, 10_000), key=gap)
        def facing(f):
            x, y = f["head"][0] - f["pelvis"][0], f["head"][1] - f["pelvis"][1]
            return math.degrees(math.atan2(x, y)), math.hypot(x, y)
        t0, spins = cop[0]["t"], []
        for f in cop:
            o = owner_at(f["t"] - delay)
            (cf, cl), (of, ol) = facing(f), facing(o)
            turn = abs((cf - of + 180) % 360 - 180)
            if turn > self.SPIN and cl > self.LEVEL and ol > self.LEVEL:
                t = (f["t"] - t0) / 1e6
                if spins and t - spins[-1]["end"] < 0.1:
                    spins[-1].update(end=t, worst=max(spins[-1]["worst"], turn))
                else:
                    spins.append({"start": t, "end": t, "worst": turn, "knock": (f["knock"], o["knock"])})
        def getups(frames, shift):
            return [(f["t"] - shift - t0) / 1e6 for a, f in zip(frames, frames[1:]) if f["knock"] == 5 and a["knock"] != 5]
        owner_ups, copy_ups = getups(own, -delay), getups(cop, 0)
        pairs = [(o, min(copy_ups, key=lambda c: abs(c - o)) if copy_ups else None) for o in owner_ups]
        return {"delay": delay / 1000, "spins": spins, "getups": pairs, "copy_getups": copy_ups}

    SNAP = 12.0          # units a bone jumps in one frame, to come back within SNAP_FRAMES (half way at least)
    SNAP_FRAMES = 4

    def snaps(self, game, actor):
        """Its skeleton's snaps: a frame where its bones jump and come back within a few frames (what a person sees as
        a pop or a quick spin), each with what else changed in that frame: [{t, frames, jump, ref, heading, bodies,
        knock}], t in seconds from its first frame."""
        ctx = self.session
        _game = ctx.host
        frames = [f for f in self.frames(game, actor) if f["bodies"] and any(f["bodies"]) and f.get("pelvis")]
        def apart(a, b, key):
            gaps = [math.dist(x[key], y[key]) for x, y in zip(a["bodies"], b["bodies"]) if x and y and x.get(key) and y.get(key)]
            return max(gaps, default=0.0)
        found = []
        for i in range(1, len(frames) - 1):
            before, now = frames[i - 1], frames[i]
            jump = apart(now, before, "bone")
            if jump <= self.SNAP:
                continue
            back = next((k for k in range(i + 1, min(i + 1 + self.SNAP_FRAMES, len(frames)))
                         if apart(frames[k], before, "bone") < jump * 0.5), None)
            if back is None:
                continue
            found.append({"t": (now["t"] - frames[0]["t"]) / 1e6, "wall": now["t"], "frames": back - i, "jump": jump,
                          "ref": math.dist(now["position"], before["position"]),
                          "heading": abs((math.degrees(now.get("angle", 0) - before.get("angle", 0)) + 180) % 360 - 180),
                          "bodies": apart(now, before, "position"), "knock": (before["knock"], now["knock"])})
        return found

    def copy_snaps(self, owner_game, owner, copy_game, copy):
        """The copy's snaps its owner does not have (Skyrim's own show on both, its playback delay apart)."""
        ctx = self.session
        _game = ctx.host
        c = self.compare(owner_game, owner, copy_game, copy)
        delay = (c["delay"] if c else 300) * 1000
        owners = [s["wall"] for s in self.snaps(owner_game, owner)]
        return [s for s in self.snaps(copy_game, copy) if not any(abs(s["wall"] - delay - w) < 60_000 for w in owners)]

    def snaps_report(self, owner_game, owner, copy_game, copy):
        ctx = self.session
        _game = ctx.host
        snaps = self.copy_snaps(owner_game, owner, copy_game, copy)
        return f"{len(snaps)} snaps the owner does not have" + "".join(
            f"\n  {s['t']:6.2f} s: bones jump {s['jump']:.0f} units for {s['frames']} frame(s); that frame the copy moved "
            f"{s['ref']:.1f}, turned {s['heading']:.1f} deg, its bodies moved {s['bodies']:.1f}, knock {s['knock'][0]}->{s['knock'][1]}"
            for s in snaps)

    def compare_report(self, owner_game, owner, copy_game, copy):
        ctx = self.session
        _game = ctx.host
        c = self.compare(owner_game, owner, copy_game, copy)
        if not c:
            return "not traced on both games"
        spins = "; ".join(f"{s['start']:.2f} s for {(s['end'] - s['start']) * 1000 + 17:.0f} ms ({s['worst']:.0f} deg, knock "
                          f"copy {s['knock'][0]} owner {s['knock'][1]})" for s in c["spins"])
        ups = "; ".join(f"owner {o:.2f} s, copy {'never' if u is None else f'{u:.2f} s ({u - o:+.2f})'}" for o, u in c["getups"])
        return (f"the copy {c['delay']:.0f} ms behind its owner; it turned away from the owner's facing "
                f"{len(c['spins'])} times{': ' + spins if spins else ''}; got up: {ups or 'never, nor the owner'}")

    def check_copy(self, owner_game, owner, copy_game, copy):
        """Fails when the copy spins away from its owner or gets up apart from it."""
        ctx = self.session
        _game = ctx.host
        c = self.compare(owner_game, owner, copy_game, copy)
        if not c:
            return
        apart = [(o, u) for o, u in c["getups"] if u is None or abs(u - o) > self.GETUP_APART]
        if c["spins"] or apart:
            raise ctx.failure("the copy does not move as its owner: " + self.compare_report(owner_game, owner, copy_game, copy) +
                              f" (samples: {self.folder.name})")

    def report(self):
        ctx = self.session
        _game = ctx.host
        return "\n".join(self.describe(game, actor, label) for game, actors in self.actors.items() for actor, label in actors.items()) + \
            f"\n(samples: {self.folder.name})"

    def check(self, games=None):
        """Fails on the traced games' jitter (`games`: only those, say a copy's; the owner's own is Skyrim's)."""
        ctx = self.session
        _game = ctx.host
        problems = []
        for game, actors in self.actors.items():
            if games is not None and game not in games:
                continue
            for actor, label in actors.items():
                m = self.measure(game, actor)
                if not m:
                    continue
                shaking = [n for n, v in m["series"].items() if v["worst"] > self.MAX_SHAKE]
                flickering = [n for n, v in m["series"].items() if v["flickers"]]
                if shaking or flickering or m["stray"] > self.MAX_STRAY:
                    problems.append(self.describe(game, actor, label))
        if problems:
            raise ctx.failure("bodies did not move as bodies do: " + " | ".join(problems) + f" (samples: {self.folder.name})")

    def check_copy_skeleton(self, owner_game, owner, copy_game, copy):
        """Compare visible bones; getting-up animation can depart from rigid bodies on the owner too."""
        ctx = self.session
        _game = ctx.host
        own = self.frames(owner_game, owner)
        cop = self.frames(copy_game, copy)
        times = [f["t"] for f in own]
        worst, compared = 0.0, 0
        for frame in cop:
            if not 1 <= frame["knock"] <= 6:
                continue
            target = frame["t"] - 300_000
            candidates = own[bisect.bisect_left(times, target - 60_000):bisect.bisect_right(times, target + 60_000)]
            gaps = []
            for source in candidates:
                if not 1 <= source["knock"] <= 6:
                    continue
                bones = [math.dist(a["bone"], b["bone"]) for a, b in zip(frame["bodies"], source["bodies"])
                         if a and b and a.get("bone") and b.get("bone")]
                if bones:
                    gaps.append(max(bones))
            if gaps:
                worst = max(worst, min(gaps))
                compared += 1
        measured = self.measure(copy_game, copy)
        jitter = measured and any(v["worst"] > self.MAX_SHAKE or v["flickers"] for v in measured["series"].values())
        if not compared or worst > self.MAX_STRAY or jitter:
            raise ctx.failure(f"the copy's visible skeleton differs from its owner by up to {worst:.1f} units; "
                              f"{compared} frames compared; jitter={bool(jitter)} (samples: {self.folder.name})")
