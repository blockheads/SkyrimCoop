"""A guest picks up an item the host's game made, owned by the NPC watching, the party's warnings spent: the host judges it and the party gets a bounty (docs/re/systems/crime.md, "A created item's theft names its owner").

Recorded with the repro MCP (docs/design/repro-mcp.md)."""
# recorded: 20261107-175807-guest-steals-created-item.jsonl, 17 calls, a doubled name_players and a no-op expect taken out, the bounty counted from before the pickup (scripts/check_scenarios.py)
NAME = 'guest-steals-created-item'
TAGS = ['crime', 'scene']
DOC = 'A guest picks up an item the host\'s game made, owned by the NPC watching, the party\'s warnings spent: the host judges it and the party gets a bounty (docs/re/systems/crime.md, "A created item\'s theft names its owner").'


def run(ctx):
    try:
        ctx.name_players()
        ctx.report_crimes()
        ctx.use_up_warnings()
        ctx.stage({'npcs': [{'name': 'witness', 'base': '13479', 'frozen': False}]})
        ctx.drop_item('host', '1397E', 'dagger')
        ctx.own(target='dagger', owner='13479')
        ctx.stage({'players': {'guest': {'near': 'dagger', 'gap': 100}}})
        ctx.stand_witness(npc='witness', near='guest_player')
        ctx.witness_faces(npc='witness', toward='guest_player')
        before = ctx.bounty_total()
        ctx.use('client', 'dagger')
        ctx.expect_bounty(at_least=before + 1, seconds=10)
    finally:
        ctx.unstage()
