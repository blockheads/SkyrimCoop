"""The dev verb spawn_horse places a party-owned horse beside the guest's player (as a stable sale makes it): the guest gets on with real input, both games show it ridden, and the guest gets off.

Recorded with the repro MCP (docs/design/repro-mcp.md)."""
# recorded: 20261124-010946-spawn-horse-for-guest.jsonl, 6 calls (scripts/check_scenarios.py)
NAME = 'guest-rides-spawned-horse'
TAGS = ['movement', 'ownership', 'scene', 'spawn']
DOC = "The dev verb spawn_horse places a party-owned horse beside the guest's player (as a stable sale makes it): the guest gets on with real input, both games show it ridden, and the guest gets off."


def run(ctx):
    try:
        ctx.scene(save=None, at='coc Riverwood', pin=False)
        ctx.spawn_horse(instance='client', owned=True)
        ctx.use('client', 'horse')
        ctx.expect('horse', 'actor', within=5.0, value=True, field='placement.ridden', seconds=10.0)
        ctx.wait_seated(instance='client')
        ctx.get_off(instance='client')
        ctx.horse_owned_by(horse='horse', owner='host')  # handed back before unstage deletes it
    finally:
        ctx.unstage()
