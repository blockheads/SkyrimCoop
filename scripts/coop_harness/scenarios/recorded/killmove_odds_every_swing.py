"""/killmove 100: a guest's single swing at a healthy wolf (not a killing blow) is a kill move, and the wolf dies.

Recorded with the repro MCP (docs/design/repro-mcp.md)."""
# recorded: 20261122-162413-killmove-odds-every-swing.jsonl, 14 calls (scripts/check_scenarios.py)
# hand-written: the recording's detours dropped (a first kill, three probe swings), its form id resolved, wolves thawed
NAME = 'killmove-odds-every-swing'
TAGS = ['dev-commands', 'kill-move']
DOC = "/killmove 100: a guest's single swing at a healthy wolf (not a killing blow) is a kill move, and the wolf dies."


def run(ctx):
    try:
        ctx.name_players()
        ctx.stage({'players': {'guest': {'near': 'host_player', 'gap': 150}}, 'npcs': [{'name': 'wolf2', 'base': '23ABE'}], 'items': [{'who': 'guest', 'item': '139B9', 'count': 1, 'equip': True}]})
        # Staged frozen, then thawed: the host refuses a kill move on an NPC with no AI (HostGame::CanPair).
        ctx.freeze('', 'wolf2', False)
        ctx.say(instance='client', text='/killmove 100')
        ctx.say(instance='client', text='/god on')
        # No swing of the guest's kills it: a kill move comes only from the odds (vanilla needs a fatal swing).
        ctx.console_on('', f"{ctx.resolve('wolf2', ''):X}.setav health 1000")
        ctx.fight('client', 'wolf2', 1, False)
        ctx.expect('wolf2', 'actor', within=5.0, game='host', value=True, field='state.dead', seconds=8.0)
        # The guest's /killmove reached the host's game: its own player's swing is one too.
        ctx.unstage()
        ctx.stage({'npcs': [{'name': 'wolf3', 'base': '23ABE'}], 'items': [{'who': 'host', 'item': '139B9', 'count': 1, 'equip': True}]})
        ctx.freeze('', 'wolf3', False)
        ctx.console_on('', f"{ctx.resolve('wolf3', ''):X}.setav health 1000")
        ctx.say(instance='', text='/god on')
        ctx.fight('', 'wolf3', 1, False)
        ctx.expect('wolf3', 'actor', within=5.0, game='host', value=True, field='state.dead', seconds=8.0)
    finally:
        ctx.say(instance='client', text='/reset')  # every game's odds back to the game's own
        ctx.unstage()
