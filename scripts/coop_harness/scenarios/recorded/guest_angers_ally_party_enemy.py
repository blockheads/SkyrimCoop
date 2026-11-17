"""A guest punches an allied NPC (Lydia, staged) six times; she turns on the guest's copy, and the host 800 units away is her enemy too: a fight with one player is the party's (docs/re/systems/combat-targets.md, A fight with one player is the party's).

Recorded with the repro MCP (docs/design/repro-mcp.md)."""
# recorded: 20261117-081523-guest-angers-ally.jsonl, 11 calls (scripts/check_scenarios.py)
NAME = 'guest-angers-ally-party-enemy'
TAGS = ['combat', 'party-aggro']
DOC = "A guest punches an allied NPC (Lydia, staged) six times; she turns on the guest's copy, and the host 800 units away is her enemy too: a fight with one player is the party's (docs/re/systems/combat-targets.md, A fight with one player is the party's)."


def run(ctx):
    try:
        ctx.name_players()
        ctx.stage({'npcs': [{'name': 'lydia', 'base': 'A2C8E', 'frozen': False}], 'players': {'guest': {'near': 'lydia', 'gap': 100}}})
        ctx.stage({'players': {'host': {'near': 'lydia', 'gap': 800}}})
        ctx.equip_weapon('client', 0)
        ctx.console_on('client', 'tgm')
        ctx.fight('client', 'lydia', 6, False)
        ctx.expect_enemy(npc='lydia', of='host_player', seconds=10)
    finally:
        ctx.unstage()
