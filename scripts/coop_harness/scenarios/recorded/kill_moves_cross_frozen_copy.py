"""Crossed kill moves, the host's swing first: on the guest the host's copy played the victim half of the guest's yielded pair, and its skeleton stays where that pair was while the copy walks on.

Recorded with the repro MCP (docs/design/repro-mcp.md)."""
# recorded: 20261127-234207-kill-moves-cross-frozen-copy.jsonl, 20 calls (scripts/check_scenarios.py)
NAME = 'kill-moves-cross-frozen-copy'
TAGS = ['combat', 'death']
DOC = "Crossed kill moves, the host's swing first: on the guest the host's copy played the victim half of the guest's yielded pair, and its skeleton stays where that pair was while the copy walks on."


from ...comparisons import distance


def run(ctx):
    try:
        ctx.console_on('', 'set KillMoveRandom to 100')
        ctx.console_on('client', 'set KillMoveRandom to 100')
        ctx.equip_weapon('', 77495)
        ctx.equip_weapon('client', 77495)
        if not ctx.kill_move_exchange({g: ctx.other_player(ctx.actors_on(g)) for g in ('', 'client')}, ''):
            raise ctx.failure("the host's swing first: the two pairs did not cross")
        if ctx.death_state('').get('down'):
            raise ctx.failure("[host] the first swing lost")
        start = ctx.player_snapshot('')['placement']['position']
        ctx.name_handle('hostp', {'host': 0x14, 'guest': ctx.other_player(ctx.actors_on('client'))})
        ctx.hold_key('', 's', 2.0)
        ctx.wait_for("[host] the host walked away from the pair", lambda: distance(ctx.player_snapshot('')['placement']['position'], start) > 120, 3)
        ctx.expect('hostp', 'skeleton', within=5.0, seconds=1.0)
    finally:
        ctx.unstage()
