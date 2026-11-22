"""The guest dies (a plain kill), respawns and travels: its own body stands on its feet in the new cell, its ragdoll out of the physics world. The revive's ragdoll removal used to come between the graph's listing for the physics step and the step, which put every body back, dynamic: after a trip the skeleton stood about 2,100 units above its feet.

Recorded with the repro MCP (docs/design/repro-mcp.md)."""
# recorded: 20261122-093236-revived-guest-stands-after-travel.jsonl, 6 calls (scripts/check_scenarios.py)
NAME = 'revived-guest-stands-after-travel'
TAGS = ['death', 'physics']
DOC = "The guest dies (a plain kill), respawns and travels: its own body stands on its feet in the new cell, its ragdoll out of the physics world. The revive's ragdoll removal used to come between the graph's listing for the physics step and the step, which put every body back, dynamic: after a trip the skeleton stood about 2,100 units above its feet."


def run(ctx):
    try:
        ctx.name_players()
        ctx.console_on('client', 'player.kill')
        ctx.expect('guest_player', 'actor', within=5.0, game='guest', value=True, field='state.dead', seconds=5.0)
        ctx.expect('guest_player', 'actor', within=5.0, game='guest', value=False, field='state.dead', seconds=40.0)
        ctx.go_far('client', 'coc Riverwood')
        ctx.expect('guest_player', 'stands', within=5.0, game='guest', seconds=3.0)
        ctx.go_far('client', 'coc QASmoke')  # the end's comparison is of the session, not of Riverwood with one player
    finally:
        ctx.unstage()
