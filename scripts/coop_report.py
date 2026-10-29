#!/usr/bin/env python3
"""Reads what players send after a session (docs/design/bug-reports.md): `/export` zips, or report folders.

  coop_report.py PATH...                      every report in them: its note, what each game did in the last
                                              minute on one timeline, where the copies differ from their owners
  coop_report.py PATH... --id ID --since 120  one report, the last two minutes
  coop_report.py PATH... --messages           the messages on the timeline too (else counted per game)
  coop_report.py PATH... --id ID --boot       the host's save from the report into both test games, then
                                              `coop_test.py duo --save` from it (left running)

Also `coop_test.py report ...`. A report is a folder per game (<id>/host-<name>, <id>/guest-<name>, each with
note.txt, log.txt, messages.txt and snapshot.json; the host's save.txt names its save, kept once in saves/ beside
the reports); a crash bundle is <id>-crash/ with the last run's logs and its CrashLoggerSSE log. Each player's zip
holds its own parts: pass them all.
"""
import argparse
import json
import math
from pathlib import Path
import re
import shutil
import subprocess
import sys
import zipfile

ROOT = Path(__file__).resolve().parents[1]
UNPACKED = ROOT / "build/coop-test/reports"
TEST_HOME = ROOT / "build/coop-test"


def unpack(path):
    """A zip unpacked under build/coop-test/reports/<its name>/, or the folder itself."""
    path = Path(path)
    if path.is_dir():
        return path
    out = UNPACKED / path.stem
    if not out.exists():
        out.mkdir(parents=True)
        with zipfile.ZipFile(path) as archive:
            archive.extractall(out)
    return out


def read_note(folder):
    note = {}
    text = (folder / "note.txt").read_text(errors="replace") if (folder / "note.txt").exists() else ""
    for line in text.splitlines():
        if ": " in line:
            key, value = line.split(": ", 1)
            note.setdefault(key, value)
    return note


def find_parts(roots):
    """{report id: {"parts": [folders], "crash": folder or None}}: every game's part found under the roots."""
    reports = {}
    for root in roots:
        for note in root.rglob("note.txt"):
            folder = note.parent
            info = read_note(folder)
            if "kind" in info:
                reports.setdefault(folder.name, {"parts": [], "crash": None})["crash"] = folder
            else:
                report = reports.setdefault(info.get("id", folder.parent.name), {"parts": [], "crash": None})
                if folder not in report["parts"]:
                    report["parts"].append(folder)
    return reports


LOG_LINE = re.compile(r"^(\d+) (\S+) \[(\w+)\] (.*)$")
MESSAGE_LINE = re.compile(r"^(\d+) t=(\d+) (\w) (\S+) peer=(\w+) (\d+)B$")


def read_lines(path, pattern):
    if not path.exists():
        return []
    return [m for m in (pattern.match(line) for line in path.read_text(errors="replace").splitlines()) if m]


def timeline(parts, since, with_messages):
    """Every game's log lines (and messages) on the host's clock: each game's clock is shifted by when it wrote
    its part (all within a network round trip of the host)."""
    host = next((p for p in parts if p.name.startswith("host-")), parts[0])
    anchor = int(read_note(host).get("unix_ms", 0))
    events, counts = [], {}
    for part in parts:
        offset = int(read_note(part).get("unix_ms", anchor)) - anchor
        game = part.name
        for m in read_lines(part / "log.txt", LOG_LINE):
            t = int(m[1]) - offset - anchor
            if t >= -since * 1000:
                events.append((t, game, f"[{m[3]}] {m[4]}"))
        for m in read_lines(part / "messages.txt", MESSAGE_LINE):
            t = int(m[1]) - offset - anchor
            if t < -since * 1000:
                continue
            if with_messages:
                events.append((t, game, f"{m[3]} {m[4]} peer={m[5]} {m[6]}B"))
            key = (game, m[3], m[4])
            counts[key] = counts.get(key, 0) + 1
    events.sort(key=lambda e: e[0])
    return events, counts


def actors_by_session(snapshot):
    """The snapshot's loaded actors by session id: (form id, its snapshot)."""
    found = {}
    for form, actor in (snapshot.get("world") or {}).items():
        session = ((actor.get("mod") or {}).get("session_id"))
        if session is not None and session >= 0:
            found[session] = (int(form), actor)
    return found


def differences(parts):
    """Where a copy differs from its owner, between the host's part and each guest's."""
    snapshots = {}
    for part in parts:
        # The game's status as the failure was raised (coop_test.py --until-fail), else its snapshot at the report.
        path = part / "at-failure.json"
        if not path.exists():
            path = part / "snapshot.json"
        if path.exists():
            snapshots[part.name] = json.loads(path.read_text(errors="replace"))
    lines = []
    names = list(snapshots)
    for i, a in enumerate(names):
        for b in names[i + 1:]:
            left, right = actors_by_session(snapshots[a]), actors_by_session(snapshots[b])
            for session in sorted(set(left) & set(right)):
                (form_a, actor_a), (form_b, actor_b) = left[session], right[session]
                if actor_a.get("role") == actor_b.get("role"):
                    continue
                owner, copy = (a, actor_a, form_a), (b, actor_b, form_b)
                if actor_a.get("role") == "copy":
                    owner, copy = copy, owner
                name = (owner[1].get("identity") or {}).get("name") or f"{owner[2]:X}"
                found = []
                pos_o = (owner[1].get("placement") or {}).get("position") or [0, 0, 0]
                pos_c = (copy[1].get("placement") or {}).get("position") or [0, 0, 0]
                distance = math.dist(pos_o, pos_c)
                if distance > 64:
                    found.append(f"{distance:.0f} units apart")
                for group, field in (("values", "health"), ("state", "dead"), ("state", "weapon_drawn"), ("state", "sneaking"),
                                     ("state", "knock"), ("ai_state", "combat")):
                    va, vb = (owner[1].get(group) or {}).get(field), (copy[1].get(group) or {}).get(field)
                    if va != vb and not (isinstance(va, float) and isinstance(vb, float) and abs(va - vb) < 1):
                        found.append(f"{field} {va} (owner) vs {vb} (copy)")
                if (owner[1].get("equipment") or []) != (copy[1].get("equipment") or []):
                    found.append("equipment differs")
                if found:
                    lines.append(f"  {name} (session {session}, owner {owner[0]} {owner[2]:X}, copy {copy[0]} {copy[2]:X}): "
                                 + "; ".join(found))
    return lines


def print_report(report_id, report, args):
    parts = sorted(report["parts"])
    if parts:
        note = read_note(parts[0])
        print(f"=== Bug report {report_id} by {note.get('by', '?')}: {note.get('text', '')}")
        for part in parts:
            info = read_note(part)
            save = (part / "save.txt").read_text().strip() if (part / "save.txt").exists() else None
            print(f"  {part.name}: build {info.get('build', '?')}{', save ' + save if save else ''}  ({part})")
        events, counts = timeline(parts, args.since, args.messages)
        print(f"\n--- The last {args.since:.0f} s, on the host's clock (0 = the report)")
        width = max(len(p.name) for p in parts)
        for t, game, text in events:
            print(f"{t / 1000:+8.2f}s {game:<{width}} {text}")
        if counts and not args.messages:
            print("\n--- Messages in that time (per game: way, name, count; S sent, R received, I/O the host's rules)")
            for game in sorted({key[0] for key in counts}):
                rows = sorted(((way, name, n) for (g, way, name), n in counts.items() if g == game), key=lambda r: -r[2])
                print(f"  {game}: " + ", ".join(f"{way} {name} x{n}" for way, name, n in rows))
        diffs = differences(parts)
        print("\n--- Copies that differ from their owners" + ("" if diffs else ": none"))
        for line in diffs:
            print(line)
    if report["crash"]:
        crash = report["crash"]
        info = read_note(crash)
        print(f"=== {crash.name}: {info.get('kind', '?')}  ({crash})")
        crash_log = next(iter(sorted(crash.glob("crash-*.log"))), None)
        plugin_log = next(iter(sorted(crash.glob("tp_client*.log"))), None)
        if crash_log:
            sys.path.insert(0, str(ROOT / "scripts/re"))
            import crash_report
            print(crash_report.report(crash_log, plugin_log))
        elif plugin_log:
            print("  The last lines of its log:")
            for line in plugin_log.read_text(errors="replace").splitlines()[-40:]:
                print("  " + line)
    print()


def boot(report_id, report):
    """The host's save into both test games' save folders as BugReport<id>, then `duo --save` from it."""
    host = next((p for p in report["parts"] if p.name.startswith("host-")), None)
    named = host / "save.txt" if host else None
    saves = [host.parent.parent / named.read_text().strip()] if named and named.exists() else []
    if not saves or not saves[0].exists():
        raise SystemExit(f"report {report_id} has no host save (pass the host's zip)")
    name = "BugReport" + re.sub("[^A-Za-z0-9]", "", report_id)
    for prefix in ("prefix-host", "prefix-client"):
        folder = TEST_HOME / prefix / "pfx/drive_c/users/steamuser/Documents/My Games/Skyrim Special Edition/Saves"
        folder.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(saves[0], folder / f"{name}.ess")
        if saves[0].with_suffix(".skse").exists():
            shutil.copyfile(saves[0].with_suffix(".skse"), folder / f"{name}.skse")
    snapshot = host / "snapshot.json"
    if snapshot.exists():
        player = json.loads(snapshot.read_text()).get("player") or {}
        print(f"At the report the host's player was at {player.get('position')} in cell {player.get('cell')} "
              f"(the save is the newest before it)")
    print("Claim the duo in build/coop-test/DUO.md first.")
    subprocess.run([sys.executable, str(ROOT / "scripts/coop_test.py"), "duo", "--save", name, "--keep-running"], check=False)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("paths", nargs="+", help="zips from /export or report folders")
    parser.add_argument("--id", help="only this report")
    parser.add_argument("--since", type=float, default=60, help="seconds of timeline before the report (default 60)")
    parser.add_argument("--messages", action="store_true", help="every message on the timeline")
    parser.add_argument("--boot", action="store_true", help="boot the duo from the report's host save")
    args = parser.parse_args(argv)
    reports = find_parts([unpack(path) for path in args.paths])
    if args.id:
        reports = {key: value for key, value in reports.items() if key.startswith(args.id)}
    if not reports:
        raise SystemExit("no reports found")
    if args.boot:
        if len(reports) != 1:
            raise SystemExit(f"--boot needs one report (--id): {', '.join(sorted(reports))}")
        return boot(*next(iter(reports.items())))
    for report_id in sorted(reports):
        print_report(report_id, reports[report_id], args)


if __name__ == "__main__":
    main()
