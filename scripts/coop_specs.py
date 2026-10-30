#!/usr/bin/env python3
"""Run shared specs in Skyrim, or print their steps with --dry (no games required)."""
import sys
from coop_harness import Session
from coop_harness.scenarios import specs


def main():
    session = Session()
    try:
        arguments = sys.argv[1:]
        if "--dry" in arguments:
            specs.dry(session, [a.removeprefix("spec:") for a in arguments if a != "--dry"])
            return
        names = [a for a in arguments if not a.startswith("-") and not a.isdigit()]
        options = [a for a in arguments if a not in names]
        selected = ["spec:" + spec["name"] for spec in specs.load(session,
            [n.removeprefix("spec:") for n in names] or None)]
        sys.argv = [sys.argv[0], "scenario", *selected, *options]
        session.cli()
    finally:
        session.close()


if __name__ == "__main__":
    main()
