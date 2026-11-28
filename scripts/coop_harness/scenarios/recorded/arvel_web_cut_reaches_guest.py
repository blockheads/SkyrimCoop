"""Playtest 2026-11-26 (Bleak Falls Barrow, 1FC43): the host killed Arvel in his web and cut it; the guest's stayed whole and blocking. E3demo copy (Arvel 8478F, web 847B0, MS13FXspiderWebKitDestructible): the host kills Arvel and cuts his web with real swings; the guest's web shows cut (destroyed, its stage model, health 0).

Recorded with the repro MCP (docs/design/repro-mcp.md)."""
# recorded: 20261127-191728-arvel-web-cut-reaches-guest.jsonl, 11 calls (scripts/check_scenarios.py)
# hand-edited: the death waited for as actor state (same_place reads only distance), the earlier stage and read dropped
NAME = 'arvel-web-cut-reaches-guest'
TAGS = ['objects', 'scene', 'world_state']
DOC = "Playtest 2026-11-26 (Bleak Falls Barrow, 1FC43): the host killed Arvel in his web and cut it; the guest's stayed whole and blocking. E3demo copy (Arvel 8478F, web 847B0, MS13FXspiderWebKitDestructible): the host kills Arvel and cuts his web with real swings; the guest's web shows cut (destroyed, its stage model, health 0)."


def run(ctx):
    try:
        ctx.scene(save=None, at='coc E3demoBleakFallsBarrow', pin=True)
        ctx.stage({'players': {'host': {'near': '0x847B0', 'gap': 150, 'bearing': 0}, 'guest': {'near': '0x847B0', 'gap': 300, 'bearing': 0}}, 'objects': [{'name': 'arvel_web', 'ref': '0x847B0'}], 'npcs': [{'name': 'arvel', 'ref': '0x8478F', 'frozen': False}]})
        ctx.console_on('', '8478F.setghost 0')
        ctx.console_on('', '8478F.kill')
        ctx.expect('arvel', 'actor', value=True, field='state.dead', seconds=10.0)
        ctx.cut(name='arvel_web', game='host', gap=70, above=90, swings=8)
        ctx.expect_cut(name='arvel_web')
    finally:
        ctx.unstage()
