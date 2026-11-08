#!/usr/bin/env python3
"""A Claude Code hook (.claude/settings.json, PostToolUse): a session that holds the shared duo while others wait is
told to free it when it reads code, uses IDA or builds (docs/design/testing.md, The duo lock). Reminds, never blocks;
reads duo.json without its mutex (written whole by os.replace), so a slow takeover never delays the agent."""
import json
import os
import re
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from coop_harness.duo_lock import CAP_MINUTES, alive, cap_left, home_of  # noqa: E402

EVERY_SECONDS = 120  # one reminder per session this often
DRIVES_GAMES = re.compile(r"\b(coop_test\.py|duo\.py)\b")  # a Bash command that is using the duo, not working offline


def warning(root, home, event, now=None):
    """The reminder for this tool call, or None: this checkout holds the duo, someone waits, and the call is offline."""
    tool = event.get("tool_name", "")
    if tool.startswith("mcp__skyrim-coop__") or \
            (tool == "Bash" and DRIVES_GAMES.search(str((event.get("tool_input") or {}).get("command", "")))):
        return None
    try:
        state = json.loads((Path(home) / "duo.json").read_text())
    except (OSError, ValueError):
        return None
    holder = state.get("holder")
    if not holder or Path(holder.get("worktree", "")).resolve() != Path(root).resolve():
        return None
    if not any(alive(token) for token in [holder.get("token"), *(join.get("token") for join in holder.get("joined", []))]):
        return None
    queue = [entry for entry in state.get("queue", []) if alive(entry.get("token"))]
    if not queue:
        return None
    now = now or time.time()
    held = (now - holder["since"]) / 60
    longest = (now - queue[0]["since"]) / 60
    left = cap_left(holder, queue, now)
    cap = (f" In {left:.0f} min the first in line stops your games and takes it (the {CAP_MINUTES:.0f}-minute cap)."
           if left is not None else "")
    return (f"You hold the shared duo ({held:.0f} min, {holder.get('what', '')}) and {len(queue)} waiting, the "
            f"longest {longest:.0f} min. Free it before reading code, using IDA or building: the MCP's duo_stop (or "
            f"coop_test.py stop), then duo_start again with a written list of what to measure.{cap}")


def due(root, session, now=None):
    """Whether this session's last reminder is EVERY_SECONDS old (build/duo-hook/<session>), and marks this one."""
    folder = Path(root) / "build"
    if not folder.is_dir():
        return True
    path = folder / "duo-hook" / f"{session or 'none'}"
    now = now or time.time()
    try:
        if now - float(path.read_text()) < EVERY_SECONDS:
            return False
    except (OSError, ValueError):
        pass
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(str(now))
    return True


def main():
    if os.name == "nt":  # no shared duo on Windows (duo_lock: no fcntl)
        return 0
    try:
        event = json.loads(sys.stdin.read() or "{}")
        root = Path(os.environ.get("CLAUDE_PROJECT_DIR") or event.get("cwd") or os.getcwd())
        message = warning(root, home_of(root, root / "build/coop-test"), event)
        if message and due(root, event.get("session_id")):
            print(json.dumps({"hookSpecificOutput": {"hookEventName": "PostToolUse", "additionalContext": message}}))
    except Exception as error:  # noqa: BLE001  never in the agent's way
        print(f"duo_hook: {error}", file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())
