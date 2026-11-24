"""The test cell WarehouseTraps: a fire plate's hazard (62865, both players: the guest's burn is one at a time, not stacked), falling rocks on two tripwires (7998B the guest, 7998C the host: physics traps hit the guest through its own game's contact), the swinging mace on its tripwire (247A9, the guest) and the log lean-to the host pulls (78EE0, the guest under it), each hurting the player in it on its own game. Rocks, mace and logs fire once per load: run on a fresh duo.

Recorded with the repro MCP (docs/design/repro-mcp.md)."""
# hand-written: from the recording below, its two failed tries dropped (the host's away spot was the guest's;
# the puller had no path from afar: now put beside the lever first); the gas steps added by hand after it
# recorded: 20261123-185606-warehouse-traps-hurt-both.jsonl, 10 calls (scripts/check_scenarios.py)
NAME = 'warehouse_traps_hurt_both'
TAGS = ['magic', 'physics', 'scene', 'traps']
DOC = "The test cell WarehouseTraps: a fire plate's hazard (62865, both players: the guest's burn is one at a time, not stacked), falling rocks on two tripwires (7998B the guest, 7998C the host: physics traps hit the guest through its own game's contact), the swinging mace on its tripwire (247A9, the guest) and the log lean-to the host pulls (78EE0, the guest under it), each hurting the player in it on its own game. Rocks, mace and logs fire once per load: run on a fresh duo."


def run(ctx):
    try:
        ctx.scene(save=None, at='coc WarehouseTraps', pin=True)
        ctx.expect_trap_hurts(who='guest', at=[3328, -480, -128], away=[3100, -480, -128], rounds=1, seconds=5, health=100)
        ctx.expect_trap_hurts(who='guest', at=[5120, -2491, -121], away=[5110, -2280, -121], rounds=1, seconds=6, health=100)
        ctx.expect_trap_hurts(who='host', at=[5115, -3048, -121], away=[5110, -2800, -121], rounds=1, seconds=6, health=100)
        ctx.expect_trap_hurts(who='guest', at=[1536, -1472, -128], away=[1536, -1250, -128], rounds=1, seconds=6, health=100)
        ctx.expect_trap_hurts(who='host', at=[3328, -480, -128], away=[3100, -380, -128], rounds=1, seconds=5, health=100)
        ctx.expect_trap_hurts(who='guest', at=[5500, -650, -128], away=[5300, -700, -128], rounds=1, seconds=6, health=100, pull='78EE0')
        # The poison gas (plate FDA6B): each walks onto it; its explosion went off under a guest's copy's feet (2026-11-23).
        ctx.expect_trap_hurts(who='guest', at=[-1121, -2566, 266], away=[-1121, -2420, 266], rounds=2, seconds=5, health=300, walk_on='FDA6B')
        ctx.expect_trap_hurts(who='host', at=[-1121, -2566, 266], away=[-1121, -2420, 266], rounds=2, seconds=5, health=300, walk_on='FDA6B')
    finally:
        ctx.unstage()
