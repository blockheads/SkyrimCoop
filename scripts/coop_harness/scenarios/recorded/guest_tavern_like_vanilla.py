"""The guest's player walks through a tavern table's worth of items set on the floor (collision_trial tavern: cheese wheels, bottles, food, a bucket, pots; 16 objects). In each game they move as in a game with no session (the vanilla baseline's ranges for the pile: how many move, how far, how long, how fast, no jump in a frame, no shaking), and both games rest each at one place.

Recorded with the repro MCP (docs/design/repro-mcp.md)."""
# recorded: 20261115-105909-guest-tavern-like-vanilla.jsonl, 2 calls (scripts/check_scenarios.py)
NAME = 'guest-tavern-like-vanilla'
TAGS = ['physics']
DOC = "The guest's player walks through a tavern table's worth of items set on the floor (collision_trial tavern: cheese wheels, bottles, food, a bucket, pots; 16 objects). In each game they move as in a game with no session (the vanilla baseline's ranges for the pile: how many move, how far, how long, how fast, no jump in a frame, no shaking), and both games rest each at one place."


def run(ctx):
    try:
        ctx.expect_collision_like_vanilla(walker='guest', kind='tavern')
    finally:
        ctx.unstage()
