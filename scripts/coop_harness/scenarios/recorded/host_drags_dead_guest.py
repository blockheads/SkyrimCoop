"""The host's player kills the guest, holds E on its body and walks back with it: the guest's own body moves with the drag and the host's copy lies as it (before the fix the host never took the copy's corpse, and its bones were pinned in the played group).

Recorded with the repro MCP (docs/design/repro-mcp.md)."""
# recorded: 20261127-171214-host-drags-dead-guest.jsonl, 5 calls (scripts/check_scenarios.py)
NAME = 'host-drags-dead-guest'
TAGS = ['death', 'physics']
DOC = "The host's player kills the guest, holds E on its body and walks back with it: the guest's own body moves with the drag and the host's copy lies as it (before the fix the host never took the copy's corpse, and its bones were pinned in the played group)."


def run(ctx):
    try:
        ctx.hold_respawn(standing='')
        ctx.kill_player(attacker='')
        ctx.drags_dead_player(dragger='')
        ctx.unstage()
    finally:
        ctx.unstage()
