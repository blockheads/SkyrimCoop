"""A guest's join while it is still joined is refused and keeps its connection; then it leaves and rejoins, from its running game and from the main menu, and Lucan's copy looks at whom his owner looks (bugs.md 'A copy that looks at nobody': a look target named before its copy exists). Riverwood Trader, 2026-11-18: before the fix the second connect lost the guest's live connection, the host kept showing it and every join after was unreachable.

Recorded with the repro MCP (docs/design/repro-mcp.md), trimmed by hand: the refused join waited for, an empty look
check before the staging dropped."""
# recorded: 20261118-201809-guest_rejoins_while_joined.jsonl, 18 calls (scripts/check_scenarios.py)
NAME = 'guest-rejoins-while-joined'
TAGS = ['copies', 'join', 'scene', 'session']
DOC = "A guest's join while it is still joined is refused and keeps its connection; then it leaves and rejoins, from its running game and from the main menu, and Lucan's copy looks at whom his owner looks (bugs.md 'A copy that looks at nobody': a look target named before its copy exists). Riverwood Trader, 2026-11-18: before the fix the second connect lost the guest's live connection, the host kept showing it and every join after was unreachable."


def run(ctx):
    try:
        ctx.scene(save=None, at='coc RiverwoodRiverwoodTrader', pin=True)
        ctx.client_joins(port=ctx.session_port())
        session = lambda: (ctx.game('client').read_status() or {}).get('session') or {}
        ctx.wait_for('[client] its join while joined is refused', lambda: session().get('state') == 'failed', 10)
        ctx.wait_for('Each game still shows the other player', ctx.both_see_each_other, 5)
        ctx.stage({'players': {'host': {'near': 'lucan_valerius', 'gap': 90}, 'guest': {'near': 'lucan_valerius', 'gap': 250}}})
        ctx.expect('lucan_valerius', 'actor', within=5.0, game='host', value='set', field='ai_state.head_track', seconds=15.0)
        ctx.expect('lucan_valerius', 'actor', within=5.0, field='ai_state.head_track', seconds=10.0)
        ctx.disconnect_client()
        ctx.rejoin_client()
        ctx.expect('lucan_valerius', 'actor', within=5.0, field='ai_state.head_track', seconds=10.0)
        ctx.expect('lucan_valerius', 'stays', within=5.0, field='ai_state.head_track', seconds=10.0)
        ctx.guest_leaves_to_main_menu()
        ctx.rejoin_client()
        ctx.expect('lucan_valerius', 'stays', within=5.0, field='ai_state.head_track', seconds=10.0)
    finally:
        ctx.unstage()
