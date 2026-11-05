"""Every name stays right on both games through a whole session: at join, after party travel, on an NPC a quest alias names on the host (and for a guest rejoining after), after a guest renames its character in the race menu, after the host's load, after a guest's death. A copy shows what its owner's game shows (docs/re/systems/names.md).

Recorded with the repro MCP (docs/design/repro-mcp.md); tidied by hand after recording."""
# recorded: 20261105-145828-names-in-every-case.jsonl, 35 calls (scripts/check_scenarios.py)
NAME = 'names-in-every-case'
TAGS = ['death', 'join', 'names', 'session']
DOC = "Every name stays right on both games through a whole session: at join, after party travel, on an NPC a quest alias names on the host (and for a guest rejoining after), after a guest renames its character in the race menu, after the host's load, after a guest's death. A copy shows what its owner's game shows (docs/re/systems/names.md)."


def run(ctx):
    load_order = ctx.load_order_file(_game=ctx.game('client')).read_bytes()
    try:
        ctx.expect_names_agree(seconds=5)
        ctx.go_far(instance='', command='coc WhiterunBanneredMare')
        ctx.arrives_with_party('client', 3000.0)
        ctx.expect_names_agree(seconds=10)
        # Hulda (1A66E) takes the name of dunBlindCliffQST's alias Petra on the host's game only.
        ctx.player_command('', {'cmd': 'quest_force_alias', 'quest': 236347, 'alias': 2, 'ref': 108142, 'parent': 0})
        ctx.wait_for('the alias names Hulda on both games', lambda: ctx.party_names()['npcs'].get('0001A66E') == ['Petra', 'Petra'], 10)
        ctx.disconnect_client()
        ctx.rejoin_client()
        ctx.expect_names_agree(seconds=10)
        ctx.console_on('client', 'showracemenu')
        ctx.finishes_race_menu(what='names the character again', name='Bj\u00f6rn')
        ctx.expect_names_agree(seconds=15)
        ctx.host_loads_party(quick=False)
        ctx.expect_names_agree(seconds=15)
        ctx.console_on('client', 'player.kill')
        ctx.wait_for('[client] the player is down', lambda: ctx.game_state('client')['death']['down'], 15)
        ctx.expect_names_agree(seconds=5)
        ctx.wait_for('[client] the player respawns', lambda: not ctx.game_state('client')['death']['down'], 60)
        ctx.expect_names_agree(seconds=15)
    finally:  # the duo's character again, at home
        ctx.unstage()
        ctx.restart_client(load_order, dropped=bool(ctx.game_state('client').get('online')))
        ctx.rejoin_client()
        ctx.forget_scene()
        ctx.reset_baseline()
