"""A guest's kill move on a frozen NPC: the host's replay must not leave the NPC held by a kill move its graph never plays.

Recorded with the repro MCP (docs/design/repro-mcp.md)."""
# recorded: 20261118-121025-frozen-wolf-kill-move.jsonl, 13 calls (scripts/check_scenarios.py)
# hand-written: trimmed to the frozen wolf's fight; the recording kept only the detours that passed
NAME = 'frozen-wolf-kill-move'
TAGS = ['copies', 'kill-move']
DOC = "A guest's kill move on a frozen NPC: the host's replay must not leave the NPC held by a kill move its graph never plays."


def run(ctx):
    try:
        ctx.name_players()
        ctx.stage({'players': {'guest': {'near': 'host_player', 'gap': 150}}, 'npcs': [{'name': 'wolf1', 'base': '23ABE'}], 'items': [{'who': 'guest', 'item': '139B9', 'count': 1, 'equip': True}]})
        # The guest's fatal swing picks a kill move on its (unfrozen) copy of the frozen wolf: the wolf dies on the host.
        ctx.fight('client', 'wolf1', 10, True)
        ctx.expect('wolf1', 'actor', within=5.0, game='host', value=False, field='state.deferred_kill', seconds=5.0)
    finally:
        ctx.unstage()
