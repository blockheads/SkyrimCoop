"""The guest's player picks up a cheese wheel, carries it over a pile of six wheels and lets go: it lands and the pile settles as in a game with no session (20 rounds), and both games rest every wheel at one place. The pile ended 3 to 8 units apart and crept for 15 s (docs/design/physics-sync.md, "A drop onto a pile").

Recorded with the repro MCP (docs/design/repro-mcp.md)."""
# recorded: 20261122-151101-guest_drops_wheel_on_wheels_like_vanilla.jsonl, 2 calls (scripts/check_scenarios.py)
NAME = 'guest_drops_wheel_on_wheels_like_vanilla'
TAGS = ['mover', 'physics']
DOC = 'The guest\'s player picks up a cheese wheel, carries it over a pile of six wheels and lets go: it lands and the pile settles as in a game with no session (20 rounds), and both games rest every wheel at one place. The pile ended 3 to 8 units apart and crept for 15 s (docs/design/physics-sync.md, "A drop onto a pile").'


def run(ctx):
    try:
        ctx.expect_collision_like_vanilla(walker='client', kind='drop_wheel_on_wheels')
    finally:
        ctx.unstage()
