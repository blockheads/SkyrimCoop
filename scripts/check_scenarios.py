#!/usr/bin/env python3
"""Every scenario added since origin/dev says where it came from (docs/design/repro-mcp.md, Robustness): a recorded one
carries record_export's `# recorded:` stamp, a hand-written registration a `# hand-written: <why>` comment on its line.
Run by scripts/check.sh."""
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
REGISTRATIONS = "scripts/coop_harness/registrations.py"
RECORDED = "scripts/coop_harness/scenarios/recorded/"
ADDED = re.compile(r"^\+\s*registry\.add\('([^']+)'")


def unstated(registry_diff, recorded_files):
    """The new flows with no origin: `registry_diff` is registrations.py's diff, `recorded_files` {path: text}."""
    missing = [f"{match.group(1)} ({REGISTRATIONS})" for line in registry_diff.splitlines()
               if (match := ADDED.match(line)) and "# hand-written:" not in line]
    missing += [path for path, text in recorded_files.items()
                if not path.endswith("__init__.py") and not re.search(r"^# (recorded|hand-written):", text, re.M)]
    return missing


def git(*arguments):
    return subprocess.run(["git", "-C", str(ROOT), *arguments], capture_output=True, text=True).stdout


def main():
    base = git("merge-base", "HEAD", "origin/dev").strip()
    if not base:
        print("check_scenarios: no origin/dev to compare with; skipped")
        return 0
    added = set(git("diff", "--name-only", "--diff-filter=A", base, "--", RECORDED).split())
    added |= set(git("ls-files", "--others", "--exclude-standard", "--", RECORDED).split())
    files = {path: (ROOT / path).read_text() for path in sorted(added) if path.endswith(".py") and (ROOT / path).exists()}
    if missing := unstated(git("diff", "-U0", base, "--", REGISTRATIONS), files):
        print("New scenarios that don't say where they came from:")
        for each in missing:
            print(f"  {each}")
        print("Record them through the skyrim-coop MCP (record_export stamps them), or say why not: a "
              "`# hand-written: <why>` comment on the registry.add line (or atop the file).")
        return 1
    print(f"check_scenarios: {len(files)} recorded file(s) and every new registration say their origin")
    return 0


if __name__ == "__main__":
    sys.exit(main())
