"""The guest talks to Andurs, the host joins and hovers an option: only the host's menu highlights it. The guest moves the party's highlight to the top with W while the host's cursor rests on that row: every menu ends on the top (the row sliding under the still cursor took it back before). The host hovers again (its own menu only), then presses S: every menu follows.

Recorded with the repro MCP (docs/design/repro-mcp.md)."""
# recorded: 20261116-225033-dialogue-resting-cursor-keeps-party-highlight-v2.jsonl, 19 calls (scripts/check_scenarios.py); edited: the session's earlier stages dropped; the guest's read is a wait
NAME = 'dialogue-resting-cursor-keeps-party-highlight'
TAGS = ['dialogue', 'together']
DOC = "The guest talks to Andurs, the host joins and hovers an option: only the host's menu highlights it. The guest moves the party's highlight to the top with W while the host's cursor rests on that row: every menu ends on the top (the row sliding under the still cursor took it back before). The host hovers again (its own menu only), then presses S: every menu follows."


def run(ctx):
    try:
        ctx.stage({'npcs': [{'name': 'andurs', 'base': '00013BA8', 'frozen': False}], 'players': {'host': {'near': 'andurs', 'gap': 110, 'bearing': 90}, 'guest': {'near': 'andurs', 'gap': 110, 'bearing': 270}}})
        ctx.talk('client', 'andurs', [])
        ctx.talk('', 'andurs', [])
        ctx.dialogue_hover(instance='', index=2)
        ctx.dialogue_highlight(instance='client', index=1)
        ctx.dialogue_highlight(instance='client', index=0)
        ctx.expect_same_highlight(seconds=2, index=0)
        ctx.dialogue_hover(instance='', index=1)
        ctx.wait_for("the guest's menu keeps the party's highlight", lambda: ctx.cursor_index('client') == 0, 2)
        ctx.dialogue_move(instance='', direction='down', wait=True)
        ctx.expect_same_highlight(seconds=2, index=2)
    finally:
        ctx.unstage()
