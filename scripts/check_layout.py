#!/usr/bin/env python3
"""Checks the layers in docs/architecture.md: plugin -> engine -> sync, and sync's feature folders.

A file's folder is its place: Code/plugin, Code/engine, Code/harness, Code/sync/<feature>. This reads every file's
includes and prints what breaks the rules: core using a feature, sync using the engine, two features using each other.
--strict fails on the rules marked "must be 0" and on a sync file outside a feature folder (scripts/check.sh runs it).
    scripts/check_layout.py            # the summary
    scripts/check_layout.py --list     # every offending include
"""
import collections
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
CODE = ROOT / "Code"
LAYERS = ("sync", "engine", "plugin", "harness")  # also where an include is looked up, as the build does
INCLUDE = re.compile(r'^\s*#\s*include\s*[<"]([^>"]+)[>"]', re.M)


def place(relative):
    parts = relative.split("/")
    if parts[0] != "sync":
        return parts[0]
    if len(parts) == 2:
        return "sync/core" if parts[1] == "SyncPch.h" else None
    # Code/sync/ports: the ports every feature shares, and the last ones that still name engine types.
    return "ports" if parts[1] == "ports" else f"sync/{parts[1]}"


def collect():
    files = {}
    for layer in LAYERS:
        for path in sorted((CODE / layer).rglob("*")):
            if path.suffix in (".h", ".cpp"):
                relative = path.relative_to(CODE).as_posix()
                files[relative] = place(relative)
    return files


def resolve(include, source, files):
    beside = (Path(source).parent / include).as_posix()
    for candidate in [beside] + [f"{layer}/{include}" for layer in LAYERS]:
        if candidate in files:
            return candidate
    return None


def main(argv):
    files = collect()
    edges = collections.defaultdict(list)  # (from, to) -> [(source, included)]
    for source, origin in files.items():
        if origin is None:
            continue
        text = (CODE / source).read_text(errors="ignore")
        for include in INCLUDE.findall(text):
            included = resolve(include, source, files)
            target = files.get(included) if included else None
            if target and target != origin:
                edges[(origin, target)].append((source, included))

    def count(test):
        return {pair: uses for pair, uses in edges.items() if test(*pair)}

    def is_feature(target):
        return target.startswith("sync/") and target not in ("sync/core", "sync/game")

    findings = [
        ("core uses a feature (must be 0)", count(lambda a, b: a == "sync/core" and (is_feature(b) or b == "sync/game"))),
        ("a feature uses game, the wiring (must be 0)", count(lambda a, b: is_feature(a) and b == "sync/game")),
        ("sync uses the engine (must be 0)", count(lambda a, b: a.startswith("sync/") and b == "engine")),
        ("sync uses the plugin or the harness (must be 0)", count(lambda a, b: a.startswith("sync/") and b in ("plugin", "harness"))),
        ("the engine uses the plugin", count(lambda a, b: a == "engine" and b == "plugin")),
        ("the engine uses sync (ports and events are fine; decisions are not)", count(lambda a, b: a == "engine" and b.startswith("sync/"))),
        ("two features use each other", {pair: uses for pair, uses in edges.items()
                                         if is_feature(pair[0]) and is_feature(pair[1]) and (pair[1], pair[0]) in edges}),
    ]
    unplaced = sorted(name for name, target in files.items() if target is None)
    listing = "--list" in argv
    total = 0
    for title, found in findings:
        includes = sum(len(uses) for uses in found.values())
        if "must be 0" in title:
            total += includes
        print(f"{includes:5}  {title}")
        for (origin, target), uses in sorted(found.items(), key=lambda item: -len(item[1])):
            print(f"         {len(uses):4}  {origin} -> {target}")
            if listing:
                for source, included in uses:
                    print(f"                 {source} includes {included}")
    print(f"{len(unplaced):5}  files in Code/sync outside a feature folder (must be 0)")
    for name in unplaced:
        print(f"         {name}")
    sizes = collections.Counter(target for target in files.values() if target)
    print("\nfiles per place: " + ", ".join(f"{target} {n}" for target, n in sorted(sizes.items())))
    return 1 if "--strict" in argv and (total or unplaced) else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
