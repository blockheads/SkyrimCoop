"""A horse passes from rider to rider (host, guest, host): each ride makes it the rider's game's, and each ride's end gives it back to the host, the games agreeing on it.

Recorded with the repro MCP (docs/design/repro-mcp.md)."""
# recorded: 20261117-084238-horse-passes-between-riders.jsonl, 18 calls (scripts/check_scenarios.py)
NAME = 'horse-passes-between-riders'
TAGS = ['movement', 'ownership', 'spawn']
DOC = "A horse passes from rider to rider (host, guest, host): each ride makes it the rider's game's, and each ride's end gives it back to the host, the games agreeing on it."


def run(ctx):
    try:
        ctx.scene(save=None, at='coc Riverwood', pin=False)  # unpinned: with the clock stopped, Riverwood's mill and fish drift apart
        ctx.console_on('', 'player.placeatme 109E3D 1')
        ctx.find('Horse', 'horse')
        ctx.use('', 'horse')
        ctx.wait_seated(instance='')
        ctx.get_off(instance='')
        ctx.wait_horse_free(instance='client', horse='horse')  # its copy of the host's player off too
        ctx.use('client', 'horse')
        ctx.wait_seated(instance='client')
        ctx.horse_owned_by(horse='horse', owner='guest')
        ctx.get_off(instance='client')
        ctx.horse_owned_by(horse='horse', owner='host')
        ctx.place_before(instance='client', target='horse', gap=400)  # out of the host's way (was go_to Lydia, a staged NPC)
        ctx.wait_horse_free(instance='', horse='horse')
        ctx.use('', 'horse')
        ctx.wait_seated(instance='')
        ctx.expect('horse', 'actor', within=5.0, value=True, field='placement.ridden', seconds=5.0)
        ctx.get_off(instance='')
        ctx.expect('horse', 'agree', within=5.0, seconds=0.0)
    finally:
        ctx.remove('horse')
        ctx.unstage()
