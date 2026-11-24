"""A player's character shows no crosshair prompt to the other players: no Talk, and no Pickpocket while sneaking (TESNPC::GetActivateText 24716).

Recorded with the repro MCP (docs/design/repro-mcp.md)."""
# recorded: 20261123-221334-no-prompt-over-players.jsonl, 8 calls (scripts/check_scenarios.py)
NAME = 'no-prompt-over-players'
TAGS = ['copies', 'dialogue']
DOC = "A player's character shows no crosshair prompt to the other players: no Talk, and no Pickpocket while sneaking (TESNPC::GetActivateText 24716)."


def run(ctx):
    try:
        ctx.name_players()
        ctx.expect_prompt_lacks(instance='', target='guest_player')
        ctx.expect_prompt_lacks(instance='client', target='host_player')
        ctx.place_before(instance='client', target='host_player', gap=100)
        ctx.expect('host_player', 'crosshair', within=5.0, game='guest', seconds=0.0)
        ctx.sneak(instance='client', down=True)
        ctx.expect_prompt_lacks(instance='client', target='host_player', word='Pickpocket')
        ctx.sneak(instance='client', down=False)
    finally:
        ctx.unstage()
