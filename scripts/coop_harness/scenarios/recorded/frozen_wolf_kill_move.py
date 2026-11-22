"""A guest's kill move on a frozen NPC: the host's replay must not leave the NPC held by a kill move its graph never plays.

Recorded with the repro MCP (docs/design/repro-mcp.md)."""
# recorded: 20261118-121025-frozen-wolf-kill-move.jsonl, 13 calls (scripts/check_scenarios.py)
# hand-written: the recording kept only its passing detours; the kill move is forced and traced as client-kill-move does
from ...common import KILL_MOVE_RANDOM
from ...objects import AnimTrace

NAME = 'frozen-wolf-kill-move'
TAGS = ['copies', 'kill-move']
DOC = "A guest's kill move on a frozen NPC: the host's replay must not leave the NPC held by a kill move its graph never plays."


def run(ctx):
    try:
        ctx.name_players()
        ctx.stage({'players': {'guest': {'near': 'host_player', 'gap': 150}}, 'npcs': [{'name': 'wolf1', 'base': '23ABE'}], 'items': [{'who': 'guest', 'item': '139B9', 'count': 1, 'equip': True}]})
        copy = ctx.resolve('wolf1', 'guest')
        ctx.console_on("client", "set KillMoveRandom to 100")
        with AnimTrace(ctx, "frozen-wolf-kill-move", {"client": {0x14: "player", copy: "wolf"}}, video=False) as trace:
            # The guest's copy of the wolf (processed, as copies are) takes the victim's half: the pair starts there.
            ctx.fight('client', 'wolf1', 10, True)
            trace.wait_state("client", "player", "kill_move", True, 3)
        ctx.expect('wolf1', 'actor', within=5.0, game='host', value=True, field='state.dead', seconds=10.0)
        ctx.expect('wolf1', 'actor', within=5.0, game='host', value=False, field='state.deferred_kill', seconds=5.0)
    finally:
        ctx.console_on("client", f"set KillMoveRandom to {KILL_MOVE_RANDOM}")
        ctx.unstage()
