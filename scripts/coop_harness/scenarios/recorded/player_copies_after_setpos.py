"""Both players jump 4 cells from Riverwood by setpos (no party travel). Each game's copy of the other player follows by position; the host's copy of the guest used to keep the Riverwood cell, detached there, so it got no UpdateAI, dropped below high process and was not seen (owned by client, but host has no copy). RunPresence moves a copy whose cell is not attached into the cell it stands in: both players' copies agree.

Recorded with the repro MCP (docs/design/repro-mcp.md)."""
# recorded: 20261122-235109-player-copies-after-setpos.jsonl, 13 calls (scripts/check_scenarios.py); the jump's height lowered by hand
# (from 3000: the players were still falling at the check)
NAME = 'player-copies-after-setpos'
TAGS = ['recorded', 'scene', 'own-checks']
DOC = "Both players jump 4 cells from Riverwood by setpos (no party travel). Each game's copy of the other player follows by position; the host's copy of the guest used to keep the Riverwood cell, detached there, so it got no UpdateAI, dropped below high process and was not seen (owned by client, but host has no copy). RunPresence moves a copy whose cell is not attached into the cell it stands in: both players' copies agree."


def run(ctx):
    try:
        ctx.name_players()
        ctx.scene(save=None, at='coc Riverwood', pin=True)
        ctx.god_mode(instance='', on=True)
        ctx.god_mode(instance='client', on=True)
        ctx.console_on('', 'player.setpos z 900')
        ctx.console_on('', 'player.setpos x 26500')
        ctx.console_on('', 'player.setpos y -33500')
        ctx.console_on('client', 'player.setpos z 950')
        ctx.console_on('client', 'player.setpos x 26300')
        ctx.console_on('client', 'player.setpos y -33700')
        ctx.expect('guest_player', 'agree', within=5.0, seconds=10.0)
        ctx.expect('host_player', 'agree', within=5.0, seconds=10.0)
    finally:
        ctx.god_mode(instance='', on=False)  # by hand: the next scenario starts mortal
        ctx.god_mode(instance='client', on=False)  # by hand: the next scenario starts mortal
        ctx.unstage()
