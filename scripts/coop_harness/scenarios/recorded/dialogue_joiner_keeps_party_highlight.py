"""The guest talks to Hulda and moves the highlight; the host joins by talking (after a topic still listed was said, so its engine's history holds one). The host's menu shows the party's highlight, at the top and then on the third option, and nobody's highlight moves.

Recorded with the repro MCP (docs/design/repro-mcp.md)."""
# recorded: 20261109-132529-dialogue-joiner-keeps-party-highlight.jsonl, 12 calls (scripts/check_scenarios.py); edited: the guest's opening and Hulda's magic line (before the recording), reads made checks, the say-once lines reset
NAME = 'dialogue-joiner-keeps-party-highlight'
TAGS = ['dialogue']
DOC = "The guest talks to Hulda and moves the highlight; the host joins by talking (after a topic still listed was said, so its engine's history holds one). The host's menu shows the party's highlight, at the top and then on the third option, and nobody's highlight moves."


def run(ctx):
    try:
        ctx.stage({'npcs': [{'name': 'hulda', 'base': '00013BA3', 'frozen': False}], 'players': {'host': {'near': 'hulda', 'gap': 110, 'bearing': 90}, 'guest': {'near': 'hulda', 'gap': 110, 'bearing': 270}}})
        for game in ('', 'client'):  # Hulda's magic topic: two say-once lines, unsaid again for every run
            for info in (0x8EC38, 0x8EC39):
                ctx.info_said(game, info, False)
        ctx.talk('client', 'hulda', [])
        ctx.wait_npc_quiet()
        ctx.dialogue_highlight(instance='client', index=2)
        ctx.dialogue_mark()
        ctx.dialogue_press(instance='client')
        ctx.expect_lines_said(count=1)
        ctx.dialogue_highlight(instance='client', index=0)
        ctx.talk('', 'hulda', [])
        ctx.wait_list_shown(instance='')
        ctx.expect_same_highlight(seconds=3, index=0)
        ctx.leave_dialogue(instance='')
        ctx.dialogue_highlight(instance='client', index=2)
        ctx.talk('', 'hulda', [])
        ctx.expect_same_highlight(seconds=3, index=2)
    finally:
        ctx.unstage()
