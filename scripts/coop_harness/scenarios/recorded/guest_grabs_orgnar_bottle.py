"""The guest holds Activate on a bottle on Orgnar's bar: it grabs it. The bar's bottles wake every few seconds without moving (what wakes them is not read); the host's frames of them keyframed the guest's, the grab (CreateMouseSprings 40555) refused, and the release picked the bottle up (docs/re/systems/physics.md, Woken still).

Recorded with the repro MCP (docs/design/repro-mcp.md)."""
# hand-written: from the recording 20261113-091549-guest-grabs-orgnar-bottle.jsonl, whose failing grabs (the bug) the export dropped
NAME = 'guest-grabs-orgnar-bottle'
TAGS = ['physics', 'scene']
DOC = "The guest holds Activate on a bottle on Orgnar's bar: it grabs it. The bar's bottles wake every few seconds without moving (what wakes them is not read); the host's frames of them keyframed the guest's, the grab (CreateMouseSprings 40555) refused, and the release picked the bottle up (docs/re/systems/physics.md, Woken still)."


def run(ctx):
    try:
        ctx.scene(save=None, at='coc RiverwoodSleepingGiantInn', pin=True)
        ctx.name_handle('bottle', {'host': 0x33288, 'guest': 0x33288})
        ctx.name_handle('bottle2', {'host': 0x33287, 'guest': 0x33287})
        ctx.stage({'players': {'guest': {'near': 'bottle', 'gap': 90}, 'host': {'near': 'delphine', 'gap': 400}}})
        for bottle in ('bottle', 'bottle2'):
            ctx.turn_to(instance='client', face=ctx.resolve(bottle, 'guest'))
            ctx.expect(bottle, 'crosshair', game='guest')
            ctx.grab(instance='client', target=bottle)
            ctx.press('client', 'activate')  # a tap lets go (ActivateHandler 42420)
            ctx.release('client', 'activate')
    finally:
        ctx.unstage()
