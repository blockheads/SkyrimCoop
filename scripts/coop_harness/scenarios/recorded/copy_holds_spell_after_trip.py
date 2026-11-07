"""Each player's copy holds the owner's Healing after the party's trip into Whiterun (docs/re/systems/copy-hand-spells.md).

Recorded with the repro MCP (docs/design/repro-mcp.md), trimmed by hand."""
# recorded: 20261107-144447-copy-holds-spell-after-door.jsonl, 30 calls (scripts/check_scenarios.py)
NAME = 'copy-holds-spell-after-trip'
TAGS = ['equipment', 'hands', 'magic', 'scene', 'travel']
DOC = "Each player holds Healing in the left hand; after the party's trip into Whiterun each player's copy holds it too (a copy's hand emptied while its spell stayed selected)."


def run(ctx):
    try:
        ctx.name_players()
        ctx.scene(save=None, at='coc Riverwood', pin=True)
        ctx.console_on('', 'player.equipspell 12FCC left')
        ctx.console_on('client', 'player.equipspell 12FCC left')
        ctx.expect('host_player', 'actor', within=5.0, field='state.hands', seconds=6.0)
        ctx.expect('guest_player', 'actor', within=5.0, field='state.hands', seconds=1.0)
        ctx.scene(save=None, at='coc WhiterunOrigin', pin=True)
        ctx.expect('host_player', 'actor', within=5.0, field='state.hands', seconds=10.0)
        ctx.expect('guest_player', 'actor', within=5.0, field='state.hands', seconds=1.0)
    finally:
        ctx.unstage()
