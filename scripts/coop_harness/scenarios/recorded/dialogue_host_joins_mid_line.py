"""The guest picks Andurs's third answer; while he answers, the host talks to him (joins) and presses S and E. Exactly one line is said (the guest's), and every menu shows what the guest's does: the pick, then the next list. The host's menu showed the previous step's list instead while the NPC answered (DialogueBridge::ShowMenu shows the engine's current list, 52857), and its highlight moved alone.

Recorded with the repro MCP (docs/design/repro-mcp.md)."""
# recorded: 20261109-110244-dialogue-host-joins-mid-line.jsonl, 19 calls (scripts/check_scenarios.py); edited: the scene's stale frozen stage and reads dropped
NAME = 'dialogue-host-joins-mid-line'
TAGS = ['dialogue', 'together']
DOC = "The guest picks Andurs's third answer; while he answers, the host talks to him (joins) and presses S and E. Exactly one line is said (the guest's), and every menu shows what the guest's does: the pick, then the next list. The host's menu showed the previous step's list instead while the NPC answered (DialogueBridge::ShowMenu shows the engine's current list, 52857), and its highlight moved alone."


def run(ctx):
    try:
        ctx.stage({'npcs': [{'name': 'andurs', 'base': '00013BA8', 'frozen': False}], 'players': {'host': {'near': 'andurs', 'gap': 110, 'bearing': 90}, 'guest': {'near': 'andurs', 'gap': 110, 'bearing': 270}}})
        ctx.talk('client', 'andurs', [])
        ctx.dialogue_mark()
        ctx.dialogue_highlight(instance='client', index=2)
        ctx.wait_takes_pick(instance='client')
        ctx.dialogue_press(instance='client')
        ctx.talk('', 'andurs', [])
        ctx.dialogue_move(instance='', direction='down', wait=False)
        ctx.dialogue_press(instance='')
        ctx.expect_lines_said(count=1)
        ctx.expect('andurs', 'dialogue', within=5.0, seconds=0.0)
    finally:
        ctx.unstage()
