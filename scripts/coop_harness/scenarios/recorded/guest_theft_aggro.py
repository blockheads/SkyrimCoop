"""A guest steals in plain sight once the party's warnings are spent, the host beside the witness: the witness goes for the guest, and the host lives (docs/re/systems/crime.md)."""
# hand-written: staged from the 2026-11-05 MCP session, whose recording held the probes that found the warning count
NAME = 'guest-theft-aggro'
TAGS = ['crime', 'scene']
DOC = "A guest steals in plain sight once the party's warnings are spent, the host beside the witness: the witness goes for the guest, and the host lives (docs/re/systems/crime.md)."


def run(ctx):
    try:
        ctx.name_players()
        ctx.report_crimes()
        ctx.use_up_warnings()
        ctx.name_handle('chest', {'host': 0xC2CE8, 'guest': 0xC2CE8})
        ctx.stage({'players': {'host': {'near': 'chest', 'gap': 450}}})
        ctx.stage({'npcs': [{'name': 'witness', 'base': '13479', 'frozen': False}]})
        ctx.stage({'npcs': [{'name': 'bystander', 'base': '13478', 'frozen': False}]})
        ctx.own(target='chest', owner='13479')
        ctx.console_on('', 'C2CE8.additem f 100')
        ctx.stage({'players': {'guest': {'near': 'chest', 'gap': 100}}})
        ctx.stand_witness(npc='witness', near='guest_player')
        ctx.stand_witness(npc='bystander', near='guest_player', gap=260)
        ctx.stage({'players': {'host': {'near': 'witness', 'gap': 120}}})
        ctx.witness_faces(npc='witness', toward='guest_player')
        ctx.witness_faces(npc='bystander', toward='guest_player')
        before = ctx.bounty_total()
        ctx.take('client', 'chest')
        ctx.expect_bounty(at_least=before + 1, seconds=10)
        ctx.expect_target(npc='witness', player='guest_player', seconds=10)
        ctx.expect_mood(npc='witness', angry=False)
        ctx.expect_mood(npc='bystander', angry=False)
        ctx.expect('host_player', 'stays', field='state.dead', game='host', value=False, seconds=8)
    finally:
        ctx.unstage()
