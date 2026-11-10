"""A guest steals the host's created dagger owned by Orgnar, seen by its owner beside the far host: the party's bounty shows in both games, and the guest's HUD shows the host's "N bounty added to <hold>" line (docs/re/systems/crime.md, "Every player hears the bounty").

Recorded with the repro MCP (docs/design/repro-mcp.md)."""
# recorded: 20261110-163646-guest-theft-bystander.jsonl, 30 calls; a doubled name_players, the chest that is not in QASmoke, failed placements and repeated staging taken out; the HUD mark before the theft added after review (scripts/check_scenarios.py)
NAME = 'guest-theft-bounty-message'
TAGS = ['crime']
DOC = 'A guest steals the host\'s created dagger owned by Orgnar, seen by its owner beside the far host: the party\'s bounty shows in both games, and the guest\'s HUD shows the host\'s "N bounty added to <hold>" line (docs/re/systems/crime.md, "Every player hears the bounty").'


def run(ctx):
    try:
        ctx.name_players()
        ctx.report_crimes()
        ctx.use_up_warnings()
        ctx.stage({'npcs': [{'name': 'owner', 'base': '13479', 'frozen': False}, {'name': 'bystander', 'base': '13475', 'frozen': False}]})
        ctx.drop_item(game='host', item='1397E', name='dagger')
        ctx.own(target='dagger', owner='13479')
        ctx.stage({'players': {'guest': {'near': 'dagger', 'gap': 100}}})
        ctx.stand_witness(npc='bystander', near='guest_player')
        ctx.stage({'players': {'host': {'near': 'guest_player', 'gap': 800}}})
        ctx.stand_witness(npc='owner', near='host_player', gap=150)
        ctx.witness_faces(npc='bystander', toward='guest_player')
        before = ctx.bounty_total()
        mark = ctx.hud_mark()
        ctx.use('client', 'dagger')
        ctx.expect_bounty(at_least=before + 1, seconds=10)
        ctx.expect_bounty_message(mark=mark, seconds=5)
    finally:
        ctx.unstage()
