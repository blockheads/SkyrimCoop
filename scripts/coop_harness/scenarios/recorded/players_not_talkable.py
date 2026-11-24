"""A player's character offers no Talk to the other players: neither game's crosshair prompt over the other player's copy says Talk (Actor::CanTalkToPlayer 37469).

Recorded with the repro MCP (docs/design/repro-mcp.md)."""
# recorded: 20261123-204715-players-not-talkable.jsonl, 11 calls (scripts/check_scenarios.py)
NAME = 'players-not-talkable'
TAGS = ['copies', 'dialogue']
DOC = "A player's character offers no Talk to the other players: neither game's crosshair prompt over the other player's copy says Talk (Actor::CanTalkToPlayer 37469)."


def run(ctx):
    try:
        ctx.name_players()
        # hand-written: the recording's prompt_lacks calls (both failed: 'Talk\nPrisoner'), renamed expect_prompt_lacks
        # so the exporter keeps a failing one; the crosshair's failed expect at 186 units left out (out of reach)
        ctx.expect_prompt_lacks(instance='', target='guest_player')
        ctx.expect_prompt_lacks(instance='client', target='host_player')
        ctx.place_before(instance='client', target='host_player', gap=100)
        ctx.expect('host_player', 'crosshair', within=5.0, game='guest', seconds=0.0)
        ctx.expect_prompt_lacks(instance='client', target='host_player')  # hand-written: the crosshair on it, still none
    finally:
        ctx.unstage()
