"""The host's player holds a spell (Healing, late save); after the party travels, the guest's copy of that player holds it too. The copy's move to the new cell drops its selected spells; the equipment snapshot must put them back.

Recorded with the repro MCP (docs/design/repro-mcp.md)."""
NAME = 'copy-keeps-spell-after-party-travel'
TAGS = ['equipment', 'hands', 'late-save', 'magic', 'scene']
DOC = "The host's player holds a spell (Healing, late save); after the party travels, the guest's copy of that player holds it too. The copy's move to the new cell drops its selected spells; the equipment snapshot must put them back."


def run(ctx):
    try:
        ctx.needs_save('LateAfterDragonslayerBackinSkyrim')
        ctx.scene(save=None, at='coc Riverwood', pin=True)
        ctx.go_far('', 'coc RiverwoodSleepingGiantInn')
        ctx.arrives_with_party('client', 3000.0)
        ctx.expect('host_player', 'actor', within=5.0, field='state.hands', seconds=8.0)
    finally:
        ctx.unstage()
