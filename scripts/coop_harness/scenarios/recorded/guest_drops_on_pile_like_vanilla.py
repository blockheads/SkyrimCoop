"""The guest's player picks up a bucket, carries it over a pile of nine tavern items (food, bottles, pots) and lets go: it lands and the pile settles as in a game with no session (20 rounds), and both games rest every item at one place (docs/design/physics-sync.md, "A drop onto a pile").

Recorded with the repro MCP (docs/design/repro-mcp.md)."""
# recorded: 20261122-151149-guest_drops_on_pile_like_vanilla.jsonl, 2 calls (scripts/check_scenarios.py)
NAME = 'guest_drops_on_pile_like_vanilla'
TAGS = ['mover', 'physics']
DOC = 'The guest\'s player picks up a bucket, carries it over a pile of nine tavern items (food, bottles, pots) and lets go: it lands and the pile settles as in a game with no session (20 rounds), and both games rest every item at one place (docs/design/physics-sync.md, "A drop onto a pile").'


def run(ctx):
    try:
        ctx.expect_collision_like_vanilla(walker='client', kind='drop_on_pile')
    finally:
        ctx.unstage()
