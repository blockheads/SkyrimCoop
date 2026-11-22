"""Riverwood's sawmill (71DFC) is driven by its worker's own graph events: when Hod works it on the host, the mill's log moves the same on the guest, whose copy of Hod sits in it (docs/re/systems/furniture-graphs.md).

Recorded with the repro MCP (docs/design/repro-mcp.md)."""
# recorded: 20261122-114600-copy-worker-drives-furniture.jsonl, 5 calls (scripts/check_scenarios.py)
NAME = 'copy-worker-drives-furniture'
TAGS = ['copies', 'furniture', 'world']
DOC = "Riverwood's sawmill (71DFC) is driven by its worker's own graph events: when Hod works it on the host, the mill's log moves the same on the guest, whose copy of Hod sits in it (docs/re/systems/furniture-graphs.md)."


def run(ctx):
    try:
        # edited: the place (the recording's duo booted in Riverwood), and the wait for the mill's own state to move
        # (its state id, graph.IsLoaded) before comparing: an idle mill agrees too (PR #99 review)
        ctx.scene(save=None, at='coc Riverwood', pin=True)
        ctx.name_handle('mill', {'host': 466428, 'guest': 466428})
        ctx.find('Hod', None)
        ctx.expect('hod', 'actor', within=5.0, game='host', value='mill', field='ai.furniture', seconds=300.0)
        ctx.reference_follows('mill', 'graph.IsLoaded', seconds=300.0)
        ctx.expect('mill', 'agree', within=5.0, seconds=20.0)
    finally:
        ctx.unstage()
