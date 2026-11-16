"""The host's player walks into a ladle that runs into a second one (collision_trial push_line). In each game both ladles move as in a game with no session (the vanilla baseline's ranges: distance, time, speed, no jump in a frame, no shaking), and both games rest them at one place.

Recorded with the repro MCP (docs/design/repro-mcp.md)."""
# recorded: 20261115-104628-host-push-like-vanilla.jsonl, 2 calls (scripts/check_scenarios.py)
NAME = 'host-push-like-vanilla'
TAGS = ['physics']
DOC = "The host's player walks into a ladle that runs into a second one (collision_trial push_line). In each game both ladles move as in a game with no session (the vanilla baseline's ranges: distance, time, speed, no jump in a frame, no shaking), and both games rest them at one place."


def run(ctx):
    try:
        ctx.expect_collision_like_vanilla(walker='host', kind='push_line')
    finally:
        ctx.unstage()
