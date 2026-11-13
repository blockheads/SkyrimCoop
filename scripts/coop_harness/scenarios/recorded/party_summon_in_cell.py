"""/tpa: a guest's summon brings the host beside it, even when the host moved away inside the same cell (where the party tether says "already there").

Recorded with the repro MCP (docs/design/repro-mcp.md)."""
# recorded: 20261109-093323-party-summon-in-cell.jsonl, 18 calls (scripts/check_scenarios.py)
NAME = 'party-summon-in-cell'
TAGS = ['travel']
DOC = '/tpa: a guest\'s summon brings the host beside it, even when the host moved away inside the same cell (where the party tether says "already there").'


def run(ctx):
    try:
        # The host moves inside QASmoke: the tether leaves the guest there ("already there").
        ctx.console_on('', 'player.setpos x -900')
        ctx.say(instance='client', text='/tpa')
        ctx.arrives_with_party('', 250.0)
    finally:
        ctx.unstage()
