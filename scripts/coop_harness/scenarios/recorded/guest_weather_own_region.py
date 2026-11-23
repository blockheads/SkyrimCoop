"""The host in Winterhold shows a snowstorm (sw, then a cell crossed: its region read again, the snow kept, no longer a set weather); the guest walks off alone to Riverwood: its game holds and shows the weather the host's engine holds for Riverwood's region (TESRegion's own, weather.md), one Riverwood's list allows. It walks back to the host: it fades from Riverwood's weather into the host's snowstorm (no cut) and shows it. The host's console storm (fw, a set weather) reaches it; then at timescale 2000 (past every climate's interval) its held weather never gives way to its own game's roll.

Recorded with the repro MCP (docs/design/repro-mcp.md)."""
# recorded: 20261122-225443-guest-weather-own-region.jsonl, 14 calls (scripts/check_scenarios.py); weather_fading's value (Riverwood's rolled weather, 12F89 that run) dropped by hand
NAME = 'guest-weather-own-region'
TAGS = ['cells', 'world']
DOC = "The host in Winterhold shows a snowstorm (sw, then a cell crossed: its region read again, the snow kept, no longer a set weather); the guest walks off alone to Riverwood: its game holds and shows the weather the host's engine holds for Riverwood's region (TESRegion's own, weather.md), one Riverwood's list allows. It walks back to the host: it fades from Riverwood's weather into the host's snowstorm (no cut) and shows it. The host's console storm (fw, a set weather) reaches it; then at timescale 2000 (past every climate's interval) its held weather never gives way to its own game's roll."


def run(ctx):
    try:
        ctx.go_far('', 'cow Tamriel 27 24')
        ctx.console_on('', 'sw C8221')
        ctx.solo_move('', 111048.0, 106000.0)
        ctx.solo_move('client', 17226.0, -47203.0)
        ctx.expect('guest_weather', 'weather', within=5.0, value=True, seconds=240.0)
        ctx.solo_move('client', 111048.0, 105600.0)
        ctx.expect('guest_weather', 'weather_fading', within=5.0, seconds=0.0)
        ctx.expect('guest_weather', 'weather', within=5.0, value=True, seconds=240.0)
        ctx.console_on('', 'fw 10A241')
        ctx.expect('guest_weather', 'weather', within=5.0, seconds=10.0)
        ctx.console_on('', 'set timescale to 2000')
        ctx.expect('guest_weather', 'weather_fade', within=5.0, game='guest', value=[0], seconds=60.0)
        ctx.console_on('', 'set timescale to 20')
    finally:
        ctx.console_on('', 'set timescale to 20')
        ctx.unstage()
