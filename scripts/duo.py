#!/usr/bin/env python3
"""The shared duo's lock from the shell (docs/design/testing.md, The duo lock).

  scripts/duo.py status [--json]       who holds it, the queue, their ages
  scripts/duo.py run -- <command...>   wait for the duo, hold it while the command runs, stop its games, free it
  scripts/duo.py leave <name or id>    drop a stuck queue place by hand
  scripts/duo.py keep <hold id>        (internal) the keeper of a hold whose games were left running"""
import argparse
import json
import os
import subprocess
import sys
import time

from coop_harness import Session
from coop_harness.duo_lock import DuoBusy, DuoLock, age, polled


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    commands = parser.add_subparsers(dest="command", required=True)
    status = commands.add_parser("status")
    status.add_argument("--json", action="store_true")
    run = commands.add_parser("run")
    run.add_argument("--timeout", type=float, default=float(os.environ.get("COOP_DUO_WAIT", 7200)))
    run.add_argument("argv", nargs=argparse.REMAINDER)
    keep = commands.add_parser("keep")
    keep.add_argument("hold")
    leave = commands.add_parser("leave")
    leave.add_argument("who", help="the queue place's name or id (scripts/duo.py status)")
    args = parser.parse_args()

    session = Session()
    lock = session.duo_lock()
    if not lock:
        sys.exit("no duo lock on Windows")
    if args.command == "status":
        state = lock.status()
        if args.json:
            print(json.dumps(state, indent=1))
            return
        holder = state["holder"]
        print("Now: " + (f"{DuoLock.describe(holder)}{'' if holder['alive'] else ' (STALE: process ended)'}"
                         if holder else "free"))
        for index, entry in enumerate(state["queue"], 1):
            print(f"  {index}. {DuoLock.describe(entry)} (waiting {age(entry['since'])}, "
                  f"last poll {age(polled(entry))} ago, id {entry['id']})")
        return
    if args.command == "leave":
        matches = lock.drop_queued(args.who)
        if len(matches) != 1:
            sys.exit(f"{len(matches)} queue places match {args.who}; none dropped"
                     + "".join(f"\n  id {entry['id']}: {DuoLock.describe(entry)}" for entry in matches))
        print(f"dropped {DuoLock.describe(matches[0])}")
        return
    if args.command == "keep":
        sys.stdout.reconfigure(line_buffering=True)

        def ready():
            print("ready")
            sys.stdout.close()
            print(f"{time.strftime('%H:%M:%S')} keeping {args.hold} for {lock.worktree}", file=sys.stderr)
        sys.exit(0 if lock.keep(args.hold, session.duo_games_running, ready=ready) else 1)

    argv = args.argv[1:] if args.argv[:1] == ["--"] else args.argv
    if not argv:
        parser.error("run needs a command after --")
    try:
        lock.acquire(" ".join(argv), timeout=args.timeout)
    except DuoBusy as error:
        sys.exit(f"duo busy after {args.timeout:.0f} s: {error}")
    code = 1
    try:
        code = subprocess.run(argv).returncode
    finally:
        if session.duo_games_running():
            session.stop_duo()
        lock.release(f"duo.py run exited {code}")
    sys.exit(code)


if __name__ == "__main__":
    main()
