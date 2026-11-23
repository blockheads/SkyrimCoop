"""The soft leash (party-travel.md): the guest goes four cells east of the host at Riverwood, out of the host's grid; the host counts 10 s down on the guest's HUD, then sends it to the host's player by party travel. Expect: the guest stands at the host.

Recorded with the repro MCP (docs/design/repro-mcp.md)."""
# recorded: 20261122-223411-guest-leash.jsonl, 15 calls (scripts/check_scenarios.py); a failed probe expect and a repeated scene
# dropped by hand, the 'at' given the countdown's 10 s and the trip
NAME = 'guest-leash'
TAGS = ['recorded', 'scene', 'own-checks']
DOC = "The soft leash (party-travel.md): the guest goes four cells east of the host at Riverwood, out of the host's grid; the host counts 10 s down on the guest's HUD, then sends it to the host's player by party travel. Expect: the guest stands at the host."


def run(ctx):
    try:
        ctx.name_players()
        ctx.scene(save=None, at='coc Riverwood', pin=True)
        ctx.god_mode(instance='client', on=True)
        ctx.console_on('client', 'player.setpos z 3000')
        ctx.console_on('client', 'player.setpos x 34000')
        ctx.expect('guest_player', 'at', within=300.0, game='guest', value=[17226, -47202, -128], seconds=25.0)
        ctx.player_command('client', {'cmd': 'hud_messages'})
    finally:
        ctx.unstage()
