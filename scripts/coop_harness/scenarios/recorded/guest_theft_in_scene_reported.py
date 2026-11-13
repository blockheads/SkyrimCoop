"""The guest takes a bottle Orgnar owns off his bar while his bar scene (BCF05) plays and he sees it, warnings unspent: as in vanilla, his steal warning starts, the scene's package action ends it at its next update, which reports the theft (bounty 5 in both games), the guest keeps the bottle (docs/re/systems/crime.md, The steal warning in a scene).

Recorded with the repro MCP (docs/design/repro-mcp.md)."""
# hand-written: from the recording 20261113-143954-guest-theft-in-scene-reported.jsonl (38 calls), its probes and a
# mis-aimed second try dropped; the bottle made Orgnar's (own): the bar's bottles are faction-owned, and StealAlarm
# asks the first faction member in high process (41335), Delphine or Orgnar by run
NAME = 'guest-theft-in-scene-reported'
TAGS = ['crime', 'scene']
DOC = "The guest takes a bottle Orgnar owns off his bar while his bar scene (BCF05) plays and he sees it, warnings unspent: as in vanilla, his steal warning starts, the scene's package action ends it at its next update, which reports the theft (bounty 5 in both games), the guest keeps the bottle (docs/re/systems/crime.md, The steal warning in a scene)."


def run(ctx):
    try:
        ctx.name_players()
        ctx.scene(save=None, at='coc RiverwoodSleepingGiantInn', pin=True)
        ctx.report_crimes()
        ctx.set_warnings(warnings=0)
        ctx.name_handle(name='bottle', ids={'host': 209544, 'guest': 209544})
        ctx.own('bottle', '13479')  # Orgnar's base
        ctx.stage({'players': {'guest': {'near': 'bottle', 'gap': 90}, 'host': {'near': 'bottle', 'gap': 700}}})
        ctx.turn_to(instance='client', face=209544)
        ctx.expect('bottle', 'crosshair', within=5.0, game='guest', seconds=0.0)
        ctx.wait_seen(npc='orgnar', of='guest_player', seconds=20)
        ctx.expect_scene(scene='BCF05', playing=True)
        ctx.hold_key(instance='client', key='e', seconds=0.05)
        ctx.expect_warnings(count=1, seconds=5)
        ctx.expect_bounty(at_least=5, seconds=15)
        ctx.expect_kept(item='64B36', player='guest_player', seconds=5)
    finally:
        ctx.unstage()
