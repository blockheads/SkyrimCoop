"""The guest's player walks into a cheese wheel, which runs into a cabbage and a bucket (collision_trial push_mixed). In each game each object moves as in a game with no session (the vanilla baseline's ranges: distance, time, speed, no jump in a frame, no shaking), and both games rest them at one place.

Recorded with the repro MCP (docs/design/repro-mcp.md)."""
# recorded: 20261115-110013-guest-push-mixed-like-vanilla.jsonl, 2 calls (scripts/check_scenarios.py)
NAME = 'guest-push-mixed-like-vanilla'
TAGS = ['physics']
DOC = "The guest's player walks into a cheese wheel, which runs into a cabbage and a bucket (collision_trial push_mixed). In each game each object moves as in a game with no session (the vanilla baseline's ranges: distance, time, speed, no jump in a frame, no shaking), and both games rest them at one place."


def run(ctx):
    try:
        ctx.expect_collision_like_vanilla(walker='guest', kind='push_mixed')
    finally:
        ctx.unstage()
