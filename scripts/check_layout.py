#!/usr/bin/env python3
"""Reports how far Code/client is from the layout in docs/architecture.md.

scripts/layout.tsv says where each file is going. This reads every file's includes and prints what breaks the rules:
core using a feature, sync using the engine, two features using each other, and files with no place yet.
It only reports; the exit code is 0 unless --strict.
    scripts/check_layout.py            # the summary
    scripts/check_layout.py --list     # every offending include
"""
import collections
import fnmatch
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
CLIENT = ROOT / "Code/client"
INCLUDE = re.compile(r'^\s*#\s*include\s*[<"]([^>"]+)[>"]', re.M)
# Where an include is looked up, as the build's include directories do.
SEARCH = ["", "Games/Skyrim/", "Games/", "Services/"]


def read_rules(path):
    rules = []
    for line in path.read_text().splitlines():
        if line.strip() and not line.startswith("#"):
            target, pattern = line.split("\t")
            rules.append((target, pattern))
    return rules


def place(relative, rules):
    for target, pattern in rules:
        if fnmatch.fnmatchcase(relative, pattern):
            # A bare pattern (no folder) is for the files in Code/client itself.
            if "/" not in pattern and "/" in relative:
                continue
            return target
    return None


def collect(rules):
    files = {}
    for path in sorted(CLIENT.rglob("*")):
        if path.suffix in (".h", ".cpp"):
            relative = path.relative_to(CLIENT).as_posix()
            files[relative] = place(relative, rules)
    return files


def resolve(include, source, files):
    beside = (Path(source).parent / include).as_posix()
    for candidate in [beside] + [prefix + include for prefix in SEARCH]:
        if candidate in files:
            return candidate
    return None


def main(argv):
    rules = read_rules(ROOT / "scripts/layout.tsv")
    files = collect(rules)
    edges = collections.defaultdict(list)  # (from, to) -> [(source, included)]
    for source, origin in files.items():
        if origin is None:
            continue
        text = (CLIENT / source).read_text(errors="ignore")
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
        ("the engine uses sync (ports and events are fine; decisions are not)", count(lambda a, b: a == "engine" and b.startswith("sync/"))),
        ("two features use each other", {pair: uses for pair, uses in edges.items()
                                         if is_feature(pair[0]) and is_feature(pair[1]) and (pair[1], pair[0]) in edges}),
    ]
    unplaced = sorted(name for name, target in files.items() if target is None)
    listing = "--list" in argv
    total = 0
    for title, found in findings:
        includes = sum(len(uses) for uses in found.values())
        total += includes
        print(f"{includes:5}  {title}")
        for (origin, target), uses in sorted(found.items(), key=lambda item: -len(item[1])):
            print(f"         {len(uses):4}  {origin} -> {target}")
            if listing:
                for source, included in uses:
                    print(f"                 {source} includes {included}")
    print(f"{len(unplaced):5}  files with no place in scripts/layout.tsv")
    if listing:
        for name in unplaced:
            print(f"         {name}")
    sizes = collections.Counter(target for target in files.values() if target)
    print("\nfiles per place: " + ", ".join(f"{target} {n}" for target, n in sorted(sizes.items())))
    return 1 if "--strict" in argv and (total or unplaced) else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
