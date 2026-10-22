#!/usr/bin/env python3
"""Owner vs copy behaviour graph variables, by name, split by who writes them: the graph itself (harness
`graph_writers`: output bindings, state machine sync variables, expression assignments) or not (inputs:
the engine, scripts, defaults). Measures what a name-keyed variable sync would carry and what it must leave
out (docs/design/graph-replication.md).

    scripts/re/graph_compare.py [--seconds 20] [--delay 0.3] [--descriptor Master_Behavior]

Samples every owned actor in both games with its copy in the other, the copy `--delay` after the owner
(the playback delay). Run it while scenarios drive the games for the interesting part.
"""
import argparse
import collections
import math
from pathlib import Path
import re
import struct
import sys
import time

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
sys.path.insert(0, str(Path(__file__).resolve().parent))
import coop_test  # noqa: E402
import graph_snapshot  # noqa: E402

ROOT = Path(__file__).resolve().parents[2]
OTHER = {"": "client", "client": ""}


def descriptor_variables(name):
    """The variables a hand-written descriptor syncs, by name (its enum names without the k)."""
    source = (ROOT / f"Code/encoding/Structs/Skyrim/AnimationGraphDescriptor_{name}.cpp").read_text()
    body = re.sub(r"//.*", "", source[source.index("AnimationGraphDescriptor("):].split("}));")[0])
    return {n[1:].lower() for n in re.findall(r"\bk\w+", body)}


def rpc(instance, request):
    coop_test.select_instance(instance)
    return coop_test.rpc(request, timeout=10.0) or {}


def writers(instance, actor):
    """{project: {variable: [writing nodes]}}."""
    out = collections.defaultdict(lambda: collections.defaultdict(list))
    for w in rpc(instance, {"cmd": "graph_writers", "actor": actor}).get("writers", []):
        out[w["project"]][w["variable"].lower()].append(w["node"])
    return out


def variables(instance, actor):
    """{project: {name: value}} from the actor's graph state (only those off their defaults)."""
    response = rpc(instance, {"cmd": "graph_state", "actor": actor})
    if not response.get("ok"):
        return None
    _, _, graphs = graph_snapshot.parse_actor(bytes.fromhex(response["state"]))
    # By lower-case name: BSFixedString is case-insensitive, each game keeps the spelling it met first
    # ("Speed" in one, "speed" in the other).
    return {project: {name.lower(): value for name, _, value in graph["variables"]} for project, graph in graphs.items()}


def gap(a, b):
    """The difference of two words read as floats, 0 if either is missing or not a word."""
    if not a or not b or len(a) != 8 or len(b) != 8:
        return 0.0
    x, y = (struct.unpack("<f", bytes.fromhex(v))[0] for v in (a, b))
    return abs(x - y) if math.isfinite(x - y) else 0.0


def same(a, b):
    """Equal, or both floats within 1e-3 (a word's bits read as a float; ints and bools compare exactly)."""
    if a == b:
        return True
    if a is None or b is None or len(a) != 8:
        return False
    x, y = (struct.unpack("<f", bytes.fromhex(v))[0] for v in (a, b))
    return abs(x - y) <= 1e-3 * max(1.0, abs(x), abs(y))


def pairs():
    """(owner game, owner form id, copy game, copy form id, name) for every owned actor with a copy."""
    worlds = {}
    for game in ("", "client"):
        coop_test.select_instance(game)
        worlds[game] = (coop_test.read_status() or {}).get("world") or {}
    found = []
    for game, world in worlds.items():
        copies = {s["mod"]["session_id"]: int(fid) for fid, s in worlds[OTHER[game]].items() if s.get("role") == "copy"}
        for fid, s in world.items():
            sid = s.get("mod", {}).get("session_id", -1)
            if s.get("role") == "own" and sid in copies:
                who = (s.get("identity") or {}).get("name") or f"{int(fid):X}"
                found.append((game, int(fid), OTHER[game], copies[sid], f"{who} (owned by {game or 'host'})"))
    return found


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--seconds", type=float, default=20)
    parser.add_argument("--delay", type=float, default=0.3)
    parser.add_argument("--descriptor", default="Master_Behavior")
    args = parser.parse_args()
    descriptor = descriptor_variables(args.descriptor)
    actors = pairs()
    written = {(game, fid): writers(game, fid) for game, fid, *_ in actors}
    # per (actor, class): Counter of mismatching names; samples per actor
    mismatch = collections.defaultdict(collections.Counter)
    samples = collections.Counter()
    largest = collections.defaultdict(float)
    end = time.monotonic() + args.seconds
    while time.monotonic() < end:
        for game, fid, copy_game, copy_fid, name in actors:
            owner = variables(game, fid)
            time.sleep(args.delay)
            copy = variables(copy_game, copy_fid)
            if not owner or not copy:
                continue
            project = next((p for p in copy if p in owner), None)
            if not project:
                continue
            samples[name] += 1
            outputs = written[(game, fid)].get(project, {})
            a, b = owner[project], copy[project]
            for var in set(a) | set(b):
                if var == "TimeDelta" or same(a.get(var), b.get(var)):
                    continue
                kind = "written" if var in outputs else ("descriptor" if var in descriptor else "input")
                mismatch[(name, kind)][var] += 1
                largest[var] = max(largest[var], gap(a.get(var), b.get(var)))
    for name, n in samples.items():
        print(f"== {name}: {n} samples")
        for kind in ("written", "descriptor", "input"):
            counts = mismatch.get((name, kind), {})
            top = ", ".join(f"{v} {c}" + (f" (<={largest[v]:.3g})" if largest[v] else "")
                            for v, c in sorted(counts.items(), key=lambda t: -t[1])[:25])
            print(f"  {kind:10} {len(counts):3} differ: {top}")


if __name__ == "__main__":
    main()
