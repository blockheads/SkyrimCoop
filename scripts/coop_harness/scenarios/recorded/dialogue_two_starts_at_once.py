"""The host presses E at Brenuin in the same frame as the guest presses E at Andurs (together). One conversation at a time: exactly one opens (the host's) and stays the only one; once it ends the guest talks to Andurs, its refused ask forgotten.

Recorded with the repro MCP (docs/design/repro-mcp.md)."""
# recorded: 20261109-110602-dialogue-two-starts-at-once.jsonl, 16 calls (scripts/check_scenarios.py); edited: the scene's stale frozen stage and reads dropped
NAME = 'dialogue-two-starts-at-once'
TAGS = ['dialogue', 'together']
DOC = "The host presses E at Brenuin in the same frame as the guest presses E at Andurs (together). One conversation at a time: exactly one opens (the host's) and stays the only one; once it ends the guest talks to Andurs, its refused ask forgotten."


def run(ctx):
    try:
        ctx.stage({'npcs': [{'name': 'andurs', 'base': '00013BA8', 'frozen': False}], 'players': {'host': {'near': 'andurs', 'gap': 110, 'bearing': 90}, 'guest': {'near': 'andurs', 'gap': 110, 'bearing': 270}}})
        ctx.stage({'npcs': [{'name': 'brenuin', 'base': '00013BA7', 'frozen': False}]})
        ctx.place_before(instance='', target=ctx.resolve('brenuin', ''), gap=110)
        ctx.place_before(instance='client', target=ctx.resolve('andurs', 'client'), gap=110)
        ctx.together([
            lambda: ctx.press_activate_at(instance='', target='brenuin'),
            lambda: ctx.press_activate_at(instance='client', target='andurs'),
        ], sync='frame')
        ctx.expect_one_conversation()
        ctx.close_dialogues()
        ctx.talk('client', 'andurs', [])
    finally:
        ctx.unstage()
