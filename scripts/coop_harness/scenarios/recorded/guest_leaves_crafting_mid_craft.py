"""The guest Tabs out of QASmoke's cooking spit right after the craft box's Yes: its player and the host's copy both end standing (re/systems/actor-movement.md, "Getting up").

Recorded with the repro MCP (docs/design/repro-mcp.md)."""
# recorded: 20261118-122952-guest-leaves-crafting-mid-box.jsonl, trimmed to the path played twice (scripts/check_scenarios.py)
NAME = 'guest-leaves-crafting-mid-craft'
TAGS = ['furniture', 'recorded', 'story']
DOC = 'The guest Tabs out of QASmoke\'s cooking spit right after the craft box\'s Yes: its player and the host\'s copy both end standing (re/systems/actor-movement.md, "Getting up").'


def run(ctx):
    try:
        ctx.name_players()
        ctx.stage({'objects': [{'name': 'spit', 'ref': '0x101973'}]})
        ctx.console_on('client', 'player.additem 34CDF 2')
        ctx.console_on('client', 'player.additem 65C99 2')
        ctx.stage({'players': {'guest': {'near': 'spit', 'gap': 120}, 'host': {'near': 'spit', 'gap': 300, 'bearing': 90}}})
        for _ in range(2):
            ctx.open_crafting_station(instance='client', station=1055091, what='cooking spit')
            ctx.expect('guest_player', 'actor', within=5.0, value=3, field='state.sit_sleep', seconds=10.0)
            ctx.select_makeable_crafting_entry(instance='client')
            ctx.hold_key('client', 'E', 0.1)
            ctx.presses_box_button(instance='client', index=0)
            ctx.hold_key('client', 'Tab', 0.1)
            ctx.expect('guest_player', 'actor', within=5.0, value=0, field='state.sit_sleep', seconds=10.0)
    finally:
        ctx.unstage()
