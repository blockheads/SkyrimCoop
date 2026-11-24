"""Storm Call, shouted by the host and then by the guest outdoors in Riverwood: every lightning bolt one game's engine launches (magicProjectileStormScript's Spell.RemoteCast from an activator) shows on the other game too (bug 18: only the casting game saw the lightning).

Recorded with the repro MCP (docs/design/repro-mcp.md)."""
# recorded: 20261124-012016-storm-call-bolts-reach-guest.jsonl, 4 calls (scripts/check_scenarios.py)
NAME = 'storm-call-bolts-reach-guest'
TAGS = ['combat', 'magic', 'own-checks', 'scene']  # a live place: the storm's created activators differ (progress.md, loose ends)
DOC = "Storm Call, shouted by the host and then by the guest outdoors in Riverwood: every lightning bolt one game's engine launches (magicProjectileStormScript's Spell.RemoteCast from an activator) shows on the other game too (bug 18: only the casting game saw the lightning)."


def run(ctx):
    try:
        ctx.scene(save=None, at='coc Riverwood', pin=True)
        ctx.storm_call(instance='')
        ctx.storm_call(instance='client')
    finally:
        ctx.unstage()
