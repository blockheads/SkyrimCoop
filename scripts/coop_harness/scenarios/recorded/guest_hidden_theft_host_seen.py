"""A guest steals sneaking behind a closed door, unseen, while the host stands in view at Orgnar's bar: nobody should react, yet the witnesses' pending crime judges the host's player and Delphine attacks the host (playtest 2026-11-22; docs/re/systems/crime.md).

Recorded with the repro MCP (docs/design/repro-mcp.md)."""
# recorded: 20261122-211134-guest-hidden-theft-host-seen.jsonl, 24 calls (scripts/check_scenarios.py)
NAME = 'guest-hidden-theft-host-seen'
TAGS = ['crime', 'scene']
DOC = "A guest steals sneaking behind a closed door, unseen, while the host stands in view at Orgnar's bar: nobody should react, yet the witnesses' pending crime judges the host's player and Delphine attacks the host (playtest 2026-11-22; docs/re/systems/crime.md)."


def run(ctx):
    try:
        ctx.name_players()
        ctx.scene(save=None, at='coc RiverwoodSleepingGiantInn', pin=True)
        ctx.name_handle(name='chest', ids={'host': 342493, 'guest': 342493})
        ctx.report_crimes()
        ctx.own(target='chest', owner='13479')
        ctx.console_on('', '539DD.additem f 100')
        ctx.stage({'players': {'guest': {'near': 'chest', 'gap': 90}, 'host': {'near': 'orgnar', 'gap': 150}}})
        ctx.sneak(instance='client', down=True)
        if ctx.door_state(instance='', door=932474) != 3:
            raise ctx.failure("the guest's room door is closed")
        ctx.wait_seen(npc='orgnar', of='host_player', seconds=5)
        if ctx.witness(npc='orgnar', of='guest_player')['level'] > 0:
            raise ctx.failure("the sneaking guest is unseen behind the door")
        ctx.wait_for("the guest sees the chest's 100 gold",
                     lambda: ctx.container_items('client', ctx.resolve('chest', 'guest')).get(0xF, 0) >= 100, 5)
        stole = lambda: ctx.crime_command('', 'actor_state', actor=ctx.resolve('orgnar', 'host'))['stole_today']
        if stole():
            raise ctx.failure("the party has not stolen today before the theft")
        ctx.take('client', 'chest')
        # A guest's take is the host's to apply: its own chest reads the change when it comes back.
        ctx.wait_for("the guest's take empties the chest in both games",
                     lambda: not ctx.container_items('', ctx.resolve('chest', 'host')).get(0xF)
                     and not ctx.container_items('client', ctx.resolve('chest', 'guest')).get(0xF), 5)
        ctx.wait_for("the host judges the guest's theft (stole-today set)", stole, 5)
        ctx.expect('delphine', 'stays', game='host', value=0, field='ai.combat_target', seconds=8.0)
        ctx.expect('orgnar', 'stays', game='host', value=0, field='ai.combat_target', seconds=8.0)
    finally:
        ctx.unstage()
