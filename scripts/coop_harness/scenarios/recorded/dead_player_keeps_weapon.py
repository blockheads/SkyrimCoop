"""A player dies with a sword drawn, the guest then the host: nothing falls on either game (DropHeldItemsOnDeath 37320 is skipped for a player in a session), and once respawned each holds the sword again.

Recorded with the repro MCP (docs/design/repro-mcp.md)."""
# recorded: 20261106-005743-dead-player-keeps-weapon.jsonl, 12 calls (scripts/check_scenarios.py)
NAME = 'dead-player-keeps-weapon'
TAGS = ['death', 'items', 'scene']
DOC = 'A player dies with a sword drawn, the guest then the host: nothing falls on either game (DropHeldItemsOnDeath 37320 is skipped for a player in a session), and once respawned each holds the sword again.'


def run(ctx):
    try:
        ctx.scene(save=None, at='coc WhiterunBanneredMare', pin=False)
        ctx.give_sword(instance='client')
        ctx.draw(instance='client')
        ctx.expect_death_weapon_kept(instance='client', other='')
        ctx.give_sword(instance='')
        ctx.draw(instance='')
        ctx.expect_death_weapon_kept(instance='', other='client')
    finally:
        ctx.unstage()
