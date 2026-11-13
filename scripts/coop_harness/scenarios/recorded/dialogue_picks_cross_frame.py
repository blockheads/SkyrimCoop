"""Over a 40 ms line, the host moves the highlight up and presses E in the same frame as the guest moves it down and presses E (together). The host's game says exactly one line, and both menus agree all through: the pick, then the next list.

Recorded with the repro MCP (docs/design/repro-mcp.md)."""
# recorded: 20261109-110410-dialogue-picks-cross-frame.jsonl, 14 calls (scripts/check_scenarios.py); edited: the scene's stale frozen stage and reads dropped
NAME = 'dialogue-picks-cross-frame'
TAGS = ['dialogue', 'together']
DOC = "Over a 40 ms line, the host moves the highlight up and presses E in the same frame as the guest moves it down and presses E (together). The host's game says exactly one line, and both menus agree all through: the pick, then the next list."


def run(ctx):
    try:
        ctx.stage({'npcs': [{'name': 'andurs', 'base': '00013BA8', 'frozen': False}], 'players': {'host': {'near': 'andurs', 'gap': 110, 'bearing': 90}, 'guest': {'near': 'andurs', 'gap': 110, 'bearing': 270}}})
        ctx.line_delay(ms=40)
        ctx.talk('client', 'andurs', [])
        ctx.talk('', 'andurs', [])
        ctx.dialogue_highlight(instance='client', index=1)
        ctx.expect_same_highlight()
        ctx.wait_npc_quiet()
        ctx.dialogue_mark()
        ctx.together([
            lambda: ctx.dialogue_move_press(instance='', direction='up'),
            lambda: ctx.dialogue_move_press(instance='client', direction='down'),
        ], sync='frame')
        ctx.expect_lines_said(count=1)
        ctx.expect('andurs', 'dialogue', within=5.0, seconds=0.0)
    finally:
        ctx.unstage()
