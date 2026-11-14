"""The guest's player grabs a ladle, carries it and lets go while walking: it flies on with its hand's motion (PlayerCharacter::DestroyMouseSprings 40557 only activates it). The guest's own physics decides where it lands: its ladle never jumps after the let go, and the host's lies where the guest's lies (physics-sync.md, "The mover's until its rest"). Before: the host re-simulated the fall from the let go and its rest was placed on the guest, a 10-unit jump 8 s in.

Recorded with the repro MCP (docs/design/repro-mcp.md)."""
# recorded: 20261114-022251-guest_throw_lands_where_thrown.jsonl, 4 calls (scripts/check_scenarios.py)
NAME = 'guest-throw-lands-where-thrown'
TAGS = ['physics']
DOC = 'The guest\'s player grabs a ladle, carries it and lets go while walking: it flies on with its hand\'s motion (PlayerCharacter::DestroyMouseSprings 40557 only activates it). The guest\'s own physics decides where it lands: its ladle never jumps after the let go, and the host\'s lies where the guest\'s lies (physics-sync.md, "The mover\'s until its rest"). Before: the host re-simulated the fall from the let go and its rest was placed on the guest, a 10-unit jump 8 s in.'


def run(ctx):
    try:
        ctx.moves_and_watches(instance='client', how='throw', name='ladle')
        ctx.expect('ladle', 'steady', within=5.0, game='guest', seconds=0.0)
        ctx.expect('ladle', 'at_rest_of', within=2.0, game='host', of='guest', seconds=0.0)
    finally:
        ctx.unstage()
