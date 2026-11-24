"""Playtest 2026-11-22 item 14: Bleak Falls Barrow's swinging blades (3AC33-3AC37, trigger box 86018) hurt the player standing in them on its own game, whoever holds the trigger: the host in the box and the guest in the blades, then the other way round.

Recorded with the repro MCP (docs/design/repro-mcp.md)."""
# hand-written: from the recording below, the probing calls (darts' aim, a first try) dropped
# recorded: 20261123-145857-bleak-falls-traps.jsonl, 16 calls (scripts/check_scenarios.py)
NAME = 'scythes_hurt_both'
TAGS = ['combat', 'scene', 'traps']
DOC = "Playtest 2026-11-22 item 14: Bleak Falls Barrow's swinging blades (3AC33-3AC37, trigger box 86018) hurt the player standing in them on its own game, whoever holds the trigger: the host in the box and the guest in the blades, then the other way round."


def run(ctx):
    try:
        ctx.scene(save=None, at='coc BleakFallsBarrow01', pin=True)
        ctx.expect_trap_hurts(who='host', at=[6336, -7168, -2300], away=[7400, -7168, -2300], rounds=2, holder_at=[8212, -6622, -2112], seconds=6)
        ctx.expect_trap_hurts(who='guest', at=[6336, -7168, -2300], away=[7400, -7168, -2300], rounds=2, holder_at=[8212, -6622, -2112], seconds=6)
    finally:
        ctx.unstage()
