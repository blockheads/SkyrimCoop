"""The guest walks into an NPC: its own game makes the player's bump noise (iSoundLevelLoud, PlayerCharacter::Update), and the host's copy of it carries the same noise, so the host's NPCs hear it (stealth.md, "A player's noise").

Recorded with the repro MCP (docs/design/repro-mcp.md)."""
# recorded: 20261121-115448-guest-bump-heard-on-host.jsonl, 9 calls (scripts/check_scenarios.py)
NAME = 'guest-bump-heard-on-host'
TAGS = ['combat', 'stealth']
DOC = 'The guest walks into an NPC: its own game makes the player\'s bump noise (iSoundLevelLoud, PlayerCharacter::Update), and the host\'s copy of it carries the same noise, so the host\'s NPCs hear it (stealth.md, "A player\'s noise").'


def run(ctx):
    try:
        ctx.name_players()
        ctx.stage({'npcs': [{'name': 'andurs', 'base': '00013BA8', 'frozen': True}], 'players': {'guest': {'near': 'andurs', 'gap': 160, 'bearing': 0}, 'host': {'near': 'andurs', 'gap': 300, 'bearing': 90}}})
        ctx.go_to('client', 'andurs', 45.0, False, True)
        ctx.expect('guest_player', 'actor', within=5.0, game='guest', value=100, field='state.noise', seconds=2.0)
        ctx.expect('guest_player', 'actor', within=5.0, value=100, field='state.noise', seconds=2.0)
    finally:
        ctx.unstage()
