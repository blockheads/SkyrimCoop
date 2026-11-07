"""A quest moves the host's placed NPC to an exterior nobody has loaded: her reference has no parent cell there, so the removal must still say where she went, and the guest's copy must not stay on stage with no owner (ReadWhereabouts; bugs.md End comparisons, Olava).

Recorded with the repro MCP (docs/design/repro-mcp.md)."""
# recorded: 20261106-105031-held-copy-goes-with-owner.jsonl, 18 calls (scripts/check_scenarios.py); earlier tries and the inn cut by hand: its containers differ at the end (bugs.md, End comparisons)
NAME = 'held-copy-goes-with-owner'
TAGS = ['copies', 'npc']
DOC = "A quest moves the host's placed NPC to an exterior nobody has loaded: her reference has no parent cell there, so the removal must still say where she went, and the guest's copy must not stay on stage with no owner (ReadWhereabouts; bugs.md End comparisons, Olava)."


def run(ctx):
    try:
        ctx.stage({'npcs': [{'name': 'olava', 'ref': '1A699', 'frozen': True}]})
        ctx.console_on('', '1A699.moveto 1A69C')
        ctx.wait_for('[host] Olava has left', lambda: not ctx.host_npc_state(0x1A699), 10)
        ctx.expect('all', 'agree', within=5.0, seconds=5.0)
    finally:
        ctx.unstage()
