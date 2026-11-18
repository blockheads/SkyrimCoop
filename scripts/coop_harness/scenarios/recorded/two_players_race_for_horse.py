"""Both players press Activate on one horse in the same frame: the host's ride reaches the host first and keeps it, the guest's is refused and its player gets off and stands; the games agree on the horse.

Recorded with the repro MCP (docs/design/repro-mcp.md)."""
# recorded: 20261117-084441-two-players-race-for-horse.jsonl, 16 calls (scripts/check_scenarios.py)
NAME = 'two-players-race-for-horse'
TAGS = ['movement', 'ownership', 'spawn']
DOC = "Both players press Activate on one horse in the same frame: the host's ride reaches the host first and keeps it, the guest's is refused and its player gets off and stands; the games agree on the horse."


def run(ctx):
    try:
        ctx.scene(save=None, at='coc Riverwood', pin=False)  # unpinned: with the clock stopped, Riverwood's mill and fish drift apart
        ctx.console_on('', 'player.placeatme 109E3D 1')
        ctx.find('Horse', 'horse')
        ctx.place_before(instance='', target='horse', gap=130, bearing=90)
        ctx.place_before(instance='client', target='horse', gap=130, bearing=270)
        ctx.together([
            lambda: ctx.hold_key('', 'e', 0.1),
            lambda: ctx.hold_key('client', 'e', 0.1),
        ], sync='frame')
        ctx.wait_off(instance='client')
        ctx.wait_seated(instance='')
        ctx.horse_owned_by(horse='horse', owner='host')
        ctx.get_off(instance='')
        ctx.expect('horse', 'agree', within=5.0, seconds=0.0)
    finally:
        ctx.remove('horse')
        ctx.unstage()
