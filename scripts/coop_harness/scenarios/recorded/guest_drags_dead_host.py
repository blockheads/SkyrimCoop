"""The guest's player kills the host, holds E on its body and walks back with it: the host's own body moves with the drag and the guest's copy lies as it.

Recorded with the repro MCP (docs/design/repro-mcp.md)."""
# recorded: 20261127-171830-guest-drags-dead-host.jsonl, 5 calls (scripts/check_scenarios.py)
NAME = 'guest-drags-dead-host'
TAGS = ['death', 'physics']
DOC = "The guest's player kills the host, holds E on its body and walks back with it: the host's own body moves with the drag and the guest's copy lies as it."


def run(ctx):
    try:
        ctx.hold_respawn(standing='client')
        ctx.kill_player(attacker='client')
        ctx.drags_dead_player(dragger='client')
        ctx.unstage()
    finally:
        ctx.unstage()
