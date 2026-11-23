"""The host picks a locked door: the lockpicking menu opens with its lock and pick built (51950 builds them only on the main thread; a live menu's kUpdate came from Job_UI), the pick opens it, and the guest reads it open.

Recorded with the repro MCP (docs/design/repro-mcp.md)."""
# recorded: 20261122-212226-host-lockpick-menu-builds-lock.jsonl, 5 calls (scripts/check_scenarios.py)
NAME = 'host-lockpick-menu-builds-lock'
TAGS = ['menus', 'world']
DOC = "The host picks a locked door: the lockpicking menu opens with its lock and pick built (51950 builds them only on the main thread; a live menu's kUpdate came from Job_UI), the pick opens it, and the guest reads it open."


def run(ctx):
    try:
        ctx.inner_door()
        ctx.name_handle('door', {'host': 33606316, 'guest': 33606316})
        ctx.lock_on(instance='', door=33606316, locked=True, level=25)
        ctx.pick_lock('', 'door')
    finally:
        ctx.unstage()
