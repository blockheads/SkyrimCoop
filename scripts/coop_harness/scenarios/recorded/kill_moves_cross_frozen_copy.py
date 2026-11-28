"""Crossed kill moves: on the loser's game the winner's copy played the victim half of the loser's yielded pair; its skeleton walks with it, not left where that pair was.

Recorded with the repro MCP (docs/design/repro-mcp.md)."""
# recorded: 20261127-234207-kill-moves-cross-frozen-copy.jsonl, 20 calls (scripts/check_scenarios.py)
NAME = 'kill-moves-cross-frozen-copy'
TAGS = ['combat', 'death']
DOC = "Crossed kill moves: on the loser's game the winner's copy played the victim half of the loser's yielded pair; its skeleton walks with it, not left where that pair was."


import contextlib

from ...comparisons import distance
from ...errors import TestFailure


def run(ctx):
    games = ('', 'client')
    try:
        ctx.console_on('', 'set KillMoveRandom to 100')
        ctx.console_on('client', 'set KillMoveRandom to 100')
        ctx.equip_weapon('', 77495)
        ctx.equip_weapon('client', 77495)
        copies = {g: ctx.other_player(ctx.actors_on(g)) for g in games}
        if not ctx.kill_move_exchange(copies):
            raise ctx.failure("the two swings did not start a pair on each game: nothing crossed")
        # The winner walks away; on the loser's game its copy played the loser's cut-short victim half.
        winner = next(g for g in games if not ctx.death_state(g).get('down'))
        loser = next(g for g in games if g != winner)
        ctx.name_handle('winner', {'host': 0x14 if winner == '' else copies[''], 'guest': 0x14 if winner else copies['client']})
        start = ctx.player_snapshot(winner)['placement']['position']
        ctx.hold_key(winner, 's', 2.0)
        ctx.wait_for(f"[{winner or 'host'}] the winner walked away from the pair",
                     lambda: distance(ctx.player_snapshot(winner)['placement']['position'], start) > 120, 3)
        ctx.expect('winner', 'skeleton', game=loser or 'host', within=5.0, seconds=1.0)
    finally:
        with contextlib.suppress(TestFailure):
            ctx.wait_for("the loser respawns", lambda: not any(ctx.death_state(g).get('down') for g in games), 30)
        ctx.unstage()
