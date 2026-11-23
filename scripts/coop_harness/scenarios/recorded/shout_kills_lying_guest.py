"""The host's three-word shout hits the guest lying in its ragdoll, at full health and then at 10 health: the guest's game keeps running, and the second shout kills it (playtest 2026-11-22: the guest's game froze).

Recorded with the repro MCP (docs/design/repro-mcp.md)."""
# recorded: 20261122-161605-shout-kills-prone-guest.jsonl, 10 calls (scripts/check_scenarios.py)
NAME = 'shout-kills-lying-guest'
TAGS = ['combat', 'death', 'friendly-fire']
DOC = "The host's three-word shout hits the guest lying in its ragdoll, at full health and then at 10 health: the guest's game keeps running, and the second shout kills it (playtest 2026-11-22: the guest's game froze)."


def run(ctx):
    try:
        ctx.shout_at_lying_player(attacker='', health=0)
        ctx.shout_at_lying_player(attacker='', health=10)
    finally:
        ctx.unstage()
