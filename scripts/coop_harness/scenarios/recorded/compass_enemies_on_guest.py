"""A host NPC fighting the party is a red enemy marker on the guest's compass too, as on the host's (compass.md).

Recorded with the repro MCP (docs/design/repro-mcp.md)."""
# recorded: 20261124-230836-compass-enemies-on-guest.jsonl, 15 calls (scripts/check_scenarios.py); trimmed to the
# calls that held: the host's hit starts the wolf's fight with it
NAME = 'compass-enemies-on-guest'
TAGS = ['combat', 'compass', 'ui']
DOC = "A host NPC fighting the party is a red enemy marker on the guest's compass too, as on the host's (compass.md)."


def run(ctx):
    try:
        ctx.name_players()
        ctx.stage({'players': {'guest': {'near': 'host_player', 'gap': 150}}, 'npcs': [{'name': 'wolf', 'base': '23ABE', 'frozen': False}]})
        ctx.fight('', 'wolf', 1, False)
        ctx.expect_compass_enemies(instance='', count=1)
        ctx.expect_compass_enemies(instance='client', count=1)
    finally:
        ctx.unstage()
