"""The guest takes a bottle off Orgnar's bar while he sees it, warnings unspent: the party's bounty rises in both games. The steal warning (37556) would go to the host's player, so a guest's seen theft is not warned (docs/re/systems/crime.md).

Recorded with the repro MCP (docs/design/repro-mcp.md)."""
# hand-written: from the recording 20261113-092352-guest-theft-at-bar-bounty.jsonl, its probes and the too-slow tap (a grab) dropped
NAME = 'guest-theft-at-bar-bounty'
TAGS = ['crime', 'scene']
DOC = "The guest takes a bottle off Orgnar's bar while he sees it, warnings unspent: the party's bounty rises in both games. The steal warning (37556) would go to the host's player, so a guest's seen theft is not warned (docs/re/systems/crime.md)."


def run(ctx):
    try:
        ctx.name_players()
        ctx.scene(save=None, at='coc RiverwoodSleepingGiantInn', pin=True)
        ctx.report_crimes()
        ctx.name_handle('bottle', {'host': 0x33288, 'guest': 0x33288})
        ctx.stage({'players': {'guest': {'near': 'bottle', 'gap': 90}, 'host': {'near': 'bottle', 'gap': 700}}})
        ctx.turn_to(instance='client', face=ctx.resolve('bottle', 'guest'))
        ctx.expect('bottle', 'crosshair', game='guest')
        ctx.wait_seen(npc='orgnar', of='guest_player', seconds=20)
        before = ctx.bounty_total()
        ctx.hold_key('client', 'e', 0.05)  # a tap takes it; a hold past fZKeyDelay would grab
        ctx.expect_bounty(at_least=before + 1, seconds=10)
    finally:
        ctx.unstage()
