"""The guest's player picks up a bucket (Activate held), carries it into a pile of twelve items (cheese wheel, bottles, food, pots) and lets go in it (collision_trial carry_into_pile). In each game the bucket and the pile move as in a game with no session (the vanilla baseline's ranges: how many move, how far, how long, how fast, no jump in a frame, no shaking), and both games rest each at one place.

Recorded with the repro MCP (docs/design/repro-mcp.md)."""
# recorded: 20261115-111022-guest-carries-into-pile-like-vanilla.jsonl, 2 calls (scripts/check_scenarios.py)
NAME = 'guest-carries-into-pile-like-vanilla'
TAGS = ['physics']
DOC = "The guest's player picks up a bucket (Activate held), carries it into a pile of twelve items (cheese wheel, bottles, food, pots) and lets go in it (collision_trial carry_into_pile). In each game the bucket and the pile move as in a game with no session (the vanilla baseline's ranges: how many move, how far, how long, how fast, no jump in a frame, no shaking), and both games rest each at one place."


def run(ctx):
    try:
        ctx.expect_collision_like_vanilla(walker='guest', kind='carry_into_pile')
    finally:
        ctx.unstage()
