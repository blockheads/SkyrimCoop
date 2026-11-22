"""A script's rename that raises no event reaches the other game: the host's SKSE ObjectReference.SetDisplayName on its NPC shows on the guest's copy, and the guest's Form.SetName on its own player's base shows on the host's copy of it and in the host's party lists (docs/re/systems/names.md; bugs.md "A mod's rename at run time").

Recorded with the repro MCP (docs/design/repro-mcp.md); the name's restore moved into finally by hand."""
# recorded: 20261119-083807-script-renames-reach-party.jsonl, 10 calls (scripts/check_scenarios.py)
NAME = 'script-renames-reach-party'
TAGS = ['names', 'scripts', 'spawn']
DOC = 'A script\'s rename that raises no event reaches the other game: the host\'s SKSE ObjectReference.SetDisplayName on its NPC shows on the guest\'s copy, and the guest\'s Form.SetName on its own player\'s base shows on the host\'s copy of it and in the host\'s party lists (docs/re/systems/names.md; bugs.md "A mod\'s rename at run time").'


def run(ctx):
    try:
        ctx.name_players()
        ctx.stage({'players': {'host': {'near': 'wolf', 'gap': 200, 'bearing': 0}, 'guest': {'near': 'wolf', 'gap': 200, 'bearing': 60}}, 'npcs': [{'name': 'wolf', 'base': '00023ABE', 'frozen': True}]})
        ctx.expect('wolf', 'actor', within=5.0, value='Wolf', field='identity.name', seconds=5.0)
        ctx.script_call(instance='', klass='ObjectReference', function='SetDisplayName', args=['Old Greyjaw', True], on='wolf')
        ctx.expect('wolf', 'actor', within=5.0, value='Old Greyjaw', field='identity.name', seconds=5.0)
        ctx.script_call(instance='client', klass='Form', function='SetName', args=['Rinara'], on=7)
        ctx.expect('guest_player', 'actor', within=5.0, value='Rinara', field='identity.name', seconds=5.0)
        ctx.expect_names_agree(seconds=10)
    finally:  # the duo's character's name again
        ctx.script_call(instance='client', klass='Form', function='SetName', args=['Prisoner'], on=7)
        ctx.expect('guest_player', 'actor', within=5.0, value='Prisoner', field='identity.name', seconds=5.0)
        ctx.unstage()
