"""A guest keeps its own favorites (spells, items and their keys), timed effects and camera view over a party reload and a rejoin, and the host keeps its own (bug 17).

Recorded with the repro MCP (docs/design/repro-mcp.md)."""
# recorded: 20261123-204109-guest-keeps-own-state-on-reload.jsonl, 42 calls (scripts/check_scenarios.py)
# Trimmed by hand: the session's searches for a long timed effect and its bare readouts; the spell key check, the
# post-review checks (damage, Always Run, the key pressed) and the cleanup added.
NAME = 'guest_keeps_own_state_on_reload'
TAGS = ['join', 'load', 'records']
DOC = 'A guest keeps its own favorites (spells, items and their keys), timed effects and camera view over a party reload and a rejoin, and the host keeps its own (bug 17).'


def run(ctx):
    try:
        ctx.name_players()
        ctx.hold_key('client', 'f', 0.1)
        ctx.hold_key('client', 'Caps_Lock', 0.1)
        ctx.console_on('client', 'player.cast FB99A player left')
        ctx.console_on('client', 'player.setav healrate 0')
        ctx.console_on('client', 'player.damageav health 40')
        ctx.console_on('client', 'player.additem 12EB7 1')
        ctx.menu('client', 'MagicMenu', True)
        ctx.player_command('client', {'cmd': 'favorite', 'form': 77773, 'hotkey': 0})
        ctx.menu('client', 'MagicMenu', False)
        ctx.menu('client', 'InventoryMenu', True)
        ctx.player_command('client', {'cmd': 'favorite', 'form': 77495, 'hotkey': 2})
        ctx.menu('client', 'InventoryMenu', False)
        ctx.menu('', 'MagicMenu', True)
        ctx.player_command('', {'cmd': 'favorite', 'form': 77772, 'hotkey': 1})
        ctx.menu('', 'MagicMenu', False)
        ctx.expect('guest record', 'kept', within=5.0, value=[{'hotkey': 2, 'item': '0:12EB7'}], field='own.favorite_items', seconds=10.0)
        ctx.host_loads_party(quick=True)
        ctx.expect('guest favorites', 'player', within=5.0, game='guest', value=['0:12FCD'], field='favorite_spells', seconds=5.0)
        ctx.expect('guest spell key', 'player', within=5.0, game='guest', value=['0:12FCD'], field='spell_hotkeys', seconds=0.0)
        ctx.expect('guest item favorites', 'player', within=5.0, game='guest', value=[{'hotkey': 2, 'item': '0:12EB7'}], field='favorite_items', seconds=0.0)
        ctx.expect('guest blessing', 'player', within=5.0, game='guest', value=['0:FB99A'], field='effect_spells', seconds=0.0)
        ctx.expect('guest third person', 'player', within=5.0, game='guest', value=False, field='first_person', seconds=0.0)
        ctx.expect('host favorites', 'player', within=5.0, game='host', value=['0:12FCC'], field='favorite_spells', seconds=0.0)
        # Hand-written after review: its damage (regenerating again: the heal rate is the save's), Always Run, and the
        # restored key as the player uses it.
        ctx.expect('guest damage', 'player', within=20.0, game='guest', value=[-40.0, 0.0, 0.0], field='damage', seconds=0.0)
        ctx.expect('guest always run', 'player', game='guest', value=False, field='always_run', seconds=0.0)
        ctx.hold_key('client', '1', 0.1)
        ctx.expect('guest_player', 'actor', game='guest', value=[0x12FCD, 0x13790], field='state.hands', seconds=5.0)
        ctx.disconnect_client()
        ctx.joins_with_world(what='with its own favorites and effects')
        ctx.expect('guest favorites after rejoin', 'player', within=5.0, game='guest', value=['0:12FCD'], field='favorite_spells', seconds=5.0)
        ctx.expect('guest item favorites after rejoin', 'player', within=5.0, game='guest', value=[{'hotkey': 2, 'item': '0:12EB7'}], field='favorite_items', seconds=0.0)
        ctx.expect('guest blessing after rejoin', 'player', within=5.0, game='guest', value=['0:FB99A'], field='effect_spells', seconds=0.0)
    finally:
        # The guest's view and Always Run back to the baseline's (first person, running).
        state = ctx.player_command('client', {'cmd': 'player_state'})
        if not state.get('first_person', True):
            ctx.hold_key('client', 'f', 0.1)
        if not state.get('always_run', True):
            ctx.hold_key('client', 'Caps_Lock', 0.1)
        ctx.unstage()
