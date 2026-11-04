#!/usr/bin/env python3
"""The repro MCP server (docs/design/repro-mcp.md): one harness Session kept open between calls, so Claude can stage
a bug in the duo step by step, look at both games, and export the session as a scenario. The tools are in
coop_harness/mcp_tools.py; mcp_reload loads them and the verbs again."""
import contextlib
import importlib
import os
import sys
import traceback
from pathlib import Path

import anyio

# MCP speaks on stdout: keep its own handle, and point fd 1 (prints, child processes) at stderr.
_mcp_out = os.fdopen(os.dup(1), "w", buffering=1)
os.dup2(2, 1)
sys.stdout = _mcp_out

sys.path.insert(0, str(Path(__file__).resolve().parent))
from mcp.server.fastmcp import Context, FastMCP  # noqa: E402
from mcp.server.lowlevel.server import NotificationOptions  # noqa: E402
from mcp.server.stdio import stdio_server  # noqa: E402
from coop_harness import Session  # noqa: E402

INSTRUCTIONS = """Plays the shared duo (two Skyrims) to recreate a bug: step 1 of the bug-fix flow in CLAUDE.md. Guide:
docs/design/repro-mcp.md, "The play flow".

Order: duo_start, duo_wait; scene (a place; scene_reset for the blank QASmoke) or stage; record_start; go_to, talk, use,
take, fight; look, observe, why to see what happened; expect the correct behaviour; record_export; duo_stop.

- A failing expect is the bug: export it, and the scenario keeps it. Then duo_stop: the next steps need no Skyrim.
- State the property the bug breaks. Check an expect can fail: 0 items in both games agrees too.
- Read an NPC's AI on the host: a copy's ai.* is 0 by design, and copies run no AI.
- A verb's `why` says why it stopped, and its trace how it moved: read them before writing a script.
- scene's `disagree` lists real desyncs already there; only new ones fail after it.
- duo_start waits in the shared queue. Hold the duo only while you use it.
- Missing or awkward? Extend it, never script around it: add a verb (coop_harness/actions) or tool
  (coop_harness/mcp_tools.py), mcp_reload, then call it (a new verb by verb(name, args) at once).
- rounds runs a call N times, one line each; trace_start/trace_read show animations; log greps the plugin logs."""
server = FastMCP("skyrim-coop", instructions=INSTRUCTIONS)
# What outlives a reload: the session (its handles, recording and duo hold), the duo's boot, jobs, the trace, log offsets.
state = {"session": Session(), "duo": {"process": None, "log": None}, "jobs": {}, "trace": None, "logs": {},
         "lock": anyio.Lock()}  # one harness call at a time, reloads included
# Kept on reload: the live lock and the failure type every verb raises.
KEEP = ("coop_harness.errors", "coop_harness.duo_lock")
tools = []


def load_tools():
    from coop_harness import mcp_tools
    tools[:] = mcp_tools.register(server, state)


def reload_harness():
    """Imports coop_harness again (the new code of every verb and tool) and moves the live session onto it; on an
    import error the old code stays. Returns (tools added, removed) or raises."""
    old = {name: module for name, module in sys.modules.items() if name.split(".")[0] == "coop_harness"}
    for name in old:
        if name not in KEEP:
            del sys.modules[name]
    try:
        harness = importlib.import_module("coop_harness")
        game = importlib.import_module("coop_harness.game")
        importlib.import_module("coop_harness.mcp_tools")
    except BaseException:
        for name in [name for name in sys.modules if name.split(".")[0] == "coop_harness"]:
            del sys.modules[name]
        sys.modules.update(old)
        raise
    session = state["session"]
    session.__class__ = harness.Session  # same instance: handles, recording and the hold stay
    for each in session.games:
        each.__class__ = game.Game
    session.registry = harness.session.Registry()
    harness.session.register_flows(session.registry)
    before = list(tools)
    for name in before:
        server.remove_tool(name)
    load_tools()
    return sorted(set(tools) - set(before)), sorted(set(before) - set(tools))


@server.tool()
async def mcp_reload(ctx: Context) -> dict:
    """Loads the harness's code again (coop_harness: verbs, scenarios, these tools) without a restart, keeping the
    session, its handles, recording and duo. After writing or fixing a verb or tool: call this, then use it."""
    try:
        async with state["lock"]:
            added, removed = reload_harness()
    except BaseException as error:
        return {"ok": False, "error": f"{type(error).__name__}: {error}", "kept": "the old code",
                "where": traceback.format_exc().splitlines()[-6:]}
    with contextlib.suppress(Exception):
        await ctx.session.send_tool_list_changed()
    return {"ok": True, "tools": len(tools), "added": added, "removed": removed}


async def serve():
    """FastMCP's stdio run, announcing that the tool list changes (mcp_reload)."""
    async with stdio_server() as (read, write):
        await server._mcp_server.run(read, write, server._mcp_server.create_initialization_options(
            NotificationOptions(tools_changed=True)))


load_tools()

if __name__ == "__main__":
    anyio.run(serve)
