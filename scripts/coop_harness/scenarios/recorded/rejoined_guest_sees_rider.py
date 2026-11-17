"""The host rides a horse while the guest leaves and joins again: the rejoined guest's new copy of the host's player sits on its horse (the host sends who rides what after the spawns), and gets off with its owner.

Recorded with the repro MCP (docs/design/repro-mcp.md)."""
# recorded: 20261117-084551-rejoined-guest-sees-rider.jsonl, 13 calls (scripts/check_scenarios.py)
NAME = 'rejoined-guest-sees-rider'
TAGS = ['movement', 'restart', 'spawn']
DOC = "The host rides a horse while the guest leaves and joins again: the rejoined guest's new copy of the host's player sits on its horse (the host sends who rides what after the spawns), and gets off with its owner."


def run(ctx):
    try:
        ctx.scene(save=None, at='coc Riverwood', pin=False)  # unpinned: with the clock stopped, Riverwood's mill and fish drift apart
        ctx.console_on('', 'player.placeatme 109E3D 1')
        ctx.find('Horse', 'horse')
        ctx.use('', 'horse')
        ctx.wait_seated(instance='')
        ctx.disconnect_client()
        ctx.rejoin_client()
        ctx.find('Horse', 'horse')  # the rejoined guest's copy is a new actor (the MCP refreshed handles itself)
        ctx.expect('horse', 'actor', within=5.0, value=True, field='placement.ridden', seconds=10.0)
        ctx.get_off(instance='')
        ctx.expect('horse', 'actor', within=5.0, value=False, field='placement.ridden', seconds=10.0)
        ctx.expect('horse', 'agree', within=5.0, seconds=0.0)
    finally:
        ctx.remove('horse')
        ctx.unstage()
