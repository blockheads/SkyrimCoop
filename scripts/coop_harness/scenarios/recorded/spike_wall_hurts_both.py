"""Playtest 2026-11-22 items 13 and 14: Bleak Falls Barrow's spike wall (54B15, pressure plate 567F2) hurts the guest's player on its own game each time it steps on the plate, with the host far out of sight (the host's script then waited on a clip its game never updated), and hurts the host's player alike.

Recorded with the repro MCP (docs/design/repro-mcp.md)."""
# recorded: 20261123-134158-spike-wall-hurts-guest.jsonl, 5 calls (scripts/check_scenarios.py)
NAME = 'spike_wall_hurts_both'
TAGS = ['combat', 'scene', 'traps']
DOC = "Playtest 2026-11-22 items 13 and 14: Bleak Falls Barrow's spike wall (54B15, pressure plate 567F2) hurts the guest's player on its own game each time it steps on the plate, with the host far out of sight (the host's script then waited on a clip its game never updated), and hurts the host's player alike."


def run(ctx):
    try:
        ctx.scene(save=None, at='coc BleakFallsBarrow01', pin=True)
        ctx.expect_trap_hurts(who='guest', at=[8589, -5338, -2045], away=[8700, -5100, -2035], rounds=4)
        ctx.expect_trap_hurts(who='host', at=[8589, -5338, -2045], away=[8650, -5000, -2035], rounds=4)
    finally:
        ctx.unstage()
