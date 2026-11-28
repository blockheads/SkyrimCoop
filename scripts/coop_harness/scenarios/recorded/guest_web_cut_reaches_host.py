"""Playtest 2026-11-26 (Bleak Falls Barrow): the guest cuts a destructible web (E3demo 758D1, no corpse in it) with real swings; its damage goes to the host as proposals and the host's web is cut too, only by the guest's own swings (a copy's swing damages nothing).

Recorded with the repro MCP (docs/design/repro-mcp.md)."""
# recorded: 20261127-191638-guest-web-cut-reaches-host.jsonl, 6 calls (scripts/check_scenarios.py)
# hand-edited: the guest staged on the web's open side (from 75A7D its place found no room beside the web); the host on the
# web's other side, near enough for its physics to run
NAME = 'guest-web-cut-reaches-host'
TAGS = ['objects', 'own-checks', 'scene', 'world_state']
DOC = "Playtest 2026-11-26 (Bleak Falls Barrow): the guest cuts a destructible web (E3demo 758D1, no corpse in it) with real swings; its damage goes to the host as proposals and the host's web is cut too, only by the guest's own swings (a copy's swing damages nothing)."


def run(ctx):
    try:
        ctx.scene(save=None, at='coc E3demoBleakFallsBarrow', pin=True)
        ctx.stage({'players': {'host': {'near': '0x758D1', 'gap': 300, 'bearing': 180}, 'guest': {'near': '0x758D1', 'gap': 150, 'bearing': 0}}, 'objects': [{'name': 'web', 'ref': '0x758D1'}]})
        ctx.cut(name='web', game='guest', gap=70, above=90, swings=6)
        ctx.expect_cut(name='web')
    finally:
        ctx.unstage()
