"""The guest takes a bottle off Orgnar's bar while he sees it, warnings unspent: Orgnar warns the guest (StartStealWarning 37556, procedure 40), walks to the guest's copy and takes the bottle back from the guest's game, with no bounty (docs/re/systems/crime.md, The steal warning).

Recorded with the repro MCP (docs/design/repro-mcp.md)."""
# hand-written: from the recording 20261113-121351-guest-theft-warned.jsonl, its probes dropped and the give-back
# (expect_given_back, the bottle base 0x64B36 read there) added; Orgnar's bar scene BCF05 stopped first (stop_scene):
# it replaced the warning's package for the host's player too
NAME = 'guest-theft-warned'
TAGS = ['crime', 'scene']
DOC = "The guest takes a bottle off Orgnar's bar while he sees it, warnings unspent: Orgnar warns the guest (StartStealWarning 37556, procedure 40), walks to the guest's copy and takes the bottle back from the guest's game, with no bounty (docs/re/systems/crime.md, The steal warning)."


def run(ctx):
    try:
        ctx.name_players()
        ctx.scene(save=None, at='coc RiverwoodSleepingGiantInn', pin=True)
        ctx.report_crimes()
        ctx.set_warnings(warnings=0)
        ctx.name_handle(name='bottle', ids={'host': 209544, 'guest': 209544})
        ctx.stage({'players': {'guest': {'near': 'bottle', 'gap': 90}, 'host': {'near': 'bottle', 'gap': 700}}})
        ctx.stop_scene(scene='BCF05')
        ctx.turn_to(instance='client', face=209544)
        ctx.expect('bottle', 'crosshair', within=5.0, game='guest', seconds=0.0)
        ctx.wait_seen(npc='orgnar', of='guest_player', seconds=20)
        ctx.hold_key(instance='client', key='e', seconds=0.05)
        ctx.expect_warnings(count=1, seconds=5)
        ctx.expect_given_back(item='64B36', to='orgnar', seconds=30)
    finally:
        ctx.unstage()
