"""The guest takes all from a chest Orgnar owns: the taken sweetrolls are stolen on the guest's own game (ExtraOwnership on their stack). 16059 marks a theft only on the item it moves to a target, which was the host's copy of the guest (docs/re/systems/inventory-changes.md, "What 16059's target does").

Recorded with the repro MCP (docs/design/repro-mcp.md)."""
# recorded: 20261119-143120-guest-container-theft-is-stolen.jsonl, 12 calls (scripts/check_scenarios.py)
NAME = 'guest-container-theft-is-stolen'
TAGS = ['crime', 'items']
DOC = 'The guest takes all from a chest Orgnar owns: the taken sweetrolls are stolen on the guest\'s own game (ExtraOwnership on their stack). 16059 marks a theft only on the item it moves to a target, which was the host\'s copy of the guest (docs/re/systems/inventory-changes.md, "What 16059\'s target does").'


def run(ctx):
    try:
        ctx.stage({'objects': [{'name': 'chest', 'ref': 'C2CE8'}]})
        ctx.stage({'players': {'guest': {'near': 'chest', 'gap': 90}}})
        ctx.player_command('', {'cmd': 'crime', 'action': 'set_owner', 'target': 797928, 'owner': 78969})
        ctx.player_command('client', {'cmd': 'crime', 'action': 'set_owner', 'target': 797928, 'owner': 78969})
        ctx.console_on('', 'C2CE8.additem 64B3D 2')
        ctx.expect('chest', 'items', within=5.0, seconds=5.0)  # by hand: waits for the host's additem to arrive
        ctx.take('client', 'chest')
        ctx.expect('64B3D', 'stolen', within=5.0, game='guest', value=[2], seconds=5.0)  # by hand: 2, one additem
        ctx.player_command('client', {'cmd': 'stack_extras', 'object': 20, 'item': 412477})
    finally:
        # By hand: the test-room chest is nobody's again and the guest holds no sweetroll, for the scenarios after
        # (chest-take-all counts the guest's copy's sweetrolls).
        for game in ('', 'client'):
            ctx.player_command(game, {'cmd': 'crime', 'action': 'set_owner', 'target': 797928, 'owner': 0})
        ctx.console_on('client', 'player.removeitem 64B3D 99')
        ctx.unstage()
