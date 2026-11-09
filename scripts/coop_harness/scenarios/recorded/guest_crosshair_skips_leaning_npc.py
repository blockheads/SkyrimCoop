"""A guest's crosshair never takes an NPC copy leaning on a counter (Lucan, sit state 3): no name, no Talk prompt, so the guest can't talk to him (2026-11-08 playtest: Orgnar, Lucan). Standing, the same copy is taken; the host's crosshair takes him either way. Fails at the guest's crosshair expect once Lucan is back on his counter.

Recorded with the repro MCP (docs/design/repro-mcp.md); trimmed to the leaning case (the standing pass depends on his scene's timing)."""
# recorded: 20261108-224227-guest-crosshair-skips-leaning-npc.jsonl, 11 calls (scripts/check_scenarios.py)
NAME = 'guest-crosshair-skips-leaning-npc'
TAGS = ['copies', 'dialogue', 'scene']
DOC = "A guest's crosshair never takes an NPC copy leaning on a counter (Lucan, sit state 3): no name, no Talk prompt, so the guest can't talk to him (2026-11-08 playtest: Orgnar, Lucan). Standing, the same copy is taken; the host's crosshair takes him either way. Fails at the guest's crosshair expect once Lucan is back on his counter."


def run(ctx):
    try:
        ctx.scene(save=None, at='coc RiverwoodRiverwoodTrader', pin=True)
        ctx.stage({'players': {'host': {'near': 'lucan_valerius', 'gap': 110, 'bearing': 30}, 'guest': {'near': 'lucan_valerius', 'gap': 110, 'bearing': 90}}})
        ctx.expect('lucan_valerius', 'actor', within=5.0, value=3, field='state.sit_sleep', seconds=120.0)
        ctx.expect('lucan_valerius', 'crosshair', within=5.0, game='host', seconds=0.0)
        ctx.expect('lucan_valerius', 'crosshair', within=5.0, game='guest', seconds=0.0)
    finally:
        ctx.unstage()
