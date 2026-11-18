"""The guest cooks at QASmoke's cooking spit and Tabs out right after the box's Yes (mid-craft): its player and the host's copy of it both end standing (sit state 0). The copy's exit is the owner's IdleChairExitStart replayed (chairExitStart sets 4 in 39004), ended by the exit clip's ChairFurnitureExitHandler (42835). bugs.md's "Leaving a crafting station" (2026-10-27) saw the copy stay on 4; not seen on dev 2026-11-18.

Recorded with the repro MCP (docs/design/repro-mcp.md)."""
# recorded: 20261118-122952-guest-leaves-crafting-mid-box.jsonl, trimmed to the path played twice (scripts/check_scenarios.py)
NAME = 'guest-leaves-crafting-mid-craft'
TAGS = ['furniture', 'recorded', 'story']
DOC = 'The guest cooks at QASmoke\'s cooking spit and Tabs out right after the box\'s Yes (mid-craft): its player and the host\'s copy of it both end standing (sit state 0). The copy\'s exit is the owner\'s IdleChairExitStart replayed (chairExitStart sets 4 in 39004), ended by the exit clip\'s ChairFurnitureExitHandler (42835). bugs.md\'s "Leaving a crafting station" (2026-10-27) saw the copy stay on 4; not seen on dev 2026-11-18.'


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
