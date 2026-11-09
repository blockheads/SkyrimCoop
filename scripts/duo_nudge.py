#!/usr/bin/env python3
"""Tells the worker holding the duo to free it when its interactive hold has sent the games nothing for a while and
others wait: the agent-pool watcher runs it every 90 s (`scripts/duo_nudge.py <checkout>`) and it `agent send`s the
holder's worker. Reaches an idle session, which duo_hook.py (tool calls) cannot (docs/design/testing.md, The duo lock)."""
import json
import os
import subprocess
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from coop_harness.duo_lock import alive, cap_left, home_of  # noqa: E402

IDLE_MINUTES = float(os.environ.get("COOP_DUO_NUDGE_IDLE_MINUTES", 4))  # no command to the games for this long
EVERY_MINUTES = float(os.environ.get("COOP_DUO_NUDGE_EVERY_MINUTES", 10))  # one nudge per hold this often


def nudge(home, now=None):
    """(worker, message) for an idle interactive hold with a live waiter, at most every EVERY_MINUTES; else None."""
    home, now = Path(home), now or time.time()
    try:
        state = json.loads((home / "duo.json").read_text())
    except (OSError, ValueError):
        return None
    holder = state.get("holder")
    queue = [entry for entry in state.get("queue", []) if alive(entry.get("token"))]
    if not holder or not queue or not (holder.get("capped") or holder.get("keeper")) or not holder.get("worktree"):
        return None  # a batch command's hold ends by itself
    if not any(alive(token) for token in [holder.get("token"), *(join.get("token") for join in holder.get("joined", []))]):
        return None  # stale: the next in line takes it
    idle = (now - holder.get("last_used", holder["since"])) / 60
    if idle < IDLE_MINUTES:
        return None
    sent = home / "nudged.json"
    try:
        seen = json.loads(sent.read_text())
    except (OSError, ValueError):
        seen = {}
    if (now - seen.get(holder["id"], 0)) / 60 < EVERY_MINUTES:
        return None
    sent.write_text(json.dumps({holder["id"]: now}))
    left = cap_left(holder, queue, now)
    cap = f" In {left:.0f} min the first in line takes it anyway (the cap)." if left else ""
    return Path(holder["worktree"]).name, (
        f"You hold the shared duo, idle {idle:.0f} min, and {len(queue)} waiting "
        f"({', '.join(entry['name'] for entry in queue)}). Unless you are driving the games this minute, free it now: "
        f"the MCP's duo_stop (or coop_test.py stop); duo_start again when you need Skyrim.{cap} No reply needed.")


def main():
    if os.name == "nt":  # no shared duo on Windows (duo_lock: no fcntl)
        return 0
    root = Path(sys.argv[1] if len(sys.argv) > 1 else os.getcwd())
    if found := nudge(home_of(root, root / "build/coop-test")):
        environment = dict(os.environ, AGENT_SESSION=os.environ.get("AGENT_SESSION", "agents-SkyrimCoop"))
        subprocess.run(["agent", "send", *found], env=environment, capture_output=True, timeout=60)
    return 0


if __name__ == "__main__":
    sys.exit(main())
