"""A guest dies with a sword drawn in an inn. One sword lies on each game with the same owner, and the host taking it is no theft (a reference with no owner takes the cell's: StealAlarm 37422).

Recorded with the repro MCP (docs/design/repro-mcp.md)."""
# recorded: 20261105-165252-dead-guest-sword-no-theft.jsonl, 7 calls (scripts/check_scenarios.py)
NAME = 'dead-guest-sword-no-theft'
TAGS = ['crime', 'death', 'items', 'scene']
DOC = "A guest dies with a sword drawn in an inn. One sword lies on each game with the same owner, and the host taking it is no theft (a reference with no owner takes the cell's: StealAlarm 37422)."


def run(ctx):
    try:
        ctx.scene(save=None, at='coc WhiterunBanneredMare', pin=False)
        ctx.give_sword(instance='client')
        ctx.draw(instance='client')
        ctx.console_on('client', 'player.kill')
        ctx.expect_dropped_sword_taken(instance='', other='client')
    finally:
        ctx.unstage()
