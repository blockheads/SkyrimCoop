"""The host's player grabs a cheese wheel on Orgnar's bar while he leans on it and lets it drop: its rest reaches the guest though Havok keeps the host's island awake (Orgnar's animated body shares it, 78910). A rest is now a body's own stillness (BodyPrediction::Still, physics-sync.md "One rule"); before, the host waited for Havok's island sleep, which never came beside a leaning Orgnar, and the guest kept playing frames (the old rule: 6 of 7 drops without a rest).

Recorded with the repro MCP (docs/design/repro-mcp.md), trimmed by hand: a window key tap (hold_key), which does not let go of a grab, dropped."""
# recorded: 20261113-153024-object-rests-beside-orgnar.jsonl, 15 calls (scripts/check_scenarios.py)
NAME = 'object-rests-beside-orgnar'
TAGS = ['physics', 'scene']
DOC = 'The host\'s player grabs a cheese wheel on Orgnar\'s bar while he leans on it and lets it drop: its rest reaches the guest though Havok keeps the host\'s island awake (Orgnar\'s animated body shares it, 78910). A rest is now a body\'s own stillness (BodyPrediction::Still, physics-sync.md "One rule"); before, the host waited for Havok\'s island sleep, which never came beside a leaning Orgnar, and the guest kept playing frames (the old rule: 6 of 7 drops without a rest).'


def run(ctx):
    try:
        ctx.scene(save=None, at='coc RiverwoodSleepingGiantInn', pin=True)
        ctx.name_handle(name='cheese', ids={'host': 209544, 'guest': 209544})
        ctx.stage({'players': {'host': {'near': 'cheese', 'gap': 90}, 'guest': {'near': 'delphine', 'gap': 300}}})
        ctx.expect('orgnar', 'actor', within=5.0, game='host', value=3, field='state.sit_sleep', seconds=60.0)
        ctx.turn_to(instance='', face=209544)
        ctx.expect('cheese', 'crosshair', within=5.0, game='host', seconds=0.0)
        ctx.grab(instance='', target='cheese')
        ctx.press(instance='', button='activate')
        ctx.release(instance='', button='activate')
        ctx.guest_gets_rest(name='cheese')
    finally:
        ctx.unstage()
