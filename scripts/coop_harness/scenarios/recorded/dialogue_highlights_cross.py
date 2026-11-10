"""Two players move the conversation's one highlight in the same frame over a 40 ms line (together, line_delay): the host up, the guest down from the middle option. Both menus must end on the same option; each took the other's move instead (host 2, guest 0), because the host relayed a move to everyone but its mover.

Recorded with the repro MCP (docs/design/repro-mcp.md)."""
# recorded: 20261109-102230-dialogue-highlights-cross.jsonl, 10 calls (scripts/check_scenarios.py); edited: one stage (recorded twice), Andurs unfrozen (a frozen speaker's lines never end)
NAME = 'dialogue-highlights-cross'
TAGS = ['dialogue', 'together']
DOC = "Two players move the conversation's one highlight in the same frame over a 40 ms line (together, line_delay): the host up, the guest down from the middle option. Both menus must end on the same option; each took the other's move instead (host 2, guest 0), because the host relayed a move to everyone but its mover."


def run(ctx):
    try:
        ctx.stage({'npcs': [{'name': 'andurs', 'base': '00013BA8', 'frozen': False}], 'players': {'host': {'near': 'andurs', 'gap': 110, 'bearing': 90}, 'guest': {'near': 'andurs', 'gap': 110, 'bearing': 270}}})
        ctx.line_delay(ms=40)
        ctx.talk('client', 'andurs', [])
        ctx.talk('', 'andurs', [])
        ctx.dialogue_highlight(instance='client', index=1)
        ctx.expect_same_highlight(seconds=2)
        ctx.together([
            lambda: ctx.dialogue_move(instance='', direction='up'),
            lambda: ctx.dialogue_move(instance='client', direction='down'),
        ], sync='frame')
        ctx.expect_same_highlight(seconds=2)
    finally:
        ctx.unstage()
