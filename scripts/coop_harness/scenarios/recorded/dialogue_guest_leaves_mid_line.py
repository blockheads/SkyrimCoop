"""The guest talks to Andurs and picks his long answer; while he answers, the guest leaves the session to the main menu and joins again. Leaving deletes the guest's copies, the speaker among them, with its menu still open: its engine updated the deleted speaker and crashed (UpdateInDialogue 37541 on no process). The guest must leave and rejoin, and the host's NPC must not start a line over one still playing; the same with both players in the host's conversation.

Recorded with the repro MCP (docs/design/repro-mcp.md)."""
# recorded: 20261124-000014-dialogue-guest-leaves-mid-line.jsonl, 20 calls (scripts/check_scenarios.py); edited: the
# second leave's failed join dropped (the host cannot save with its menu open), the party joined again after its menu closes
NAME = 'dialogue-guest-leaves-mid-line'
TAGS = ['dialogue', 'join']
DOC = "The guest talks to Andurs and picks his long answer; while he answers, the guest leaves the session to the main menu and joins again. Leaving deletes the guest's copies, the speaker among them, with its menu still open: its engine updated the deleted speaker and crashed (UpdateInDialogue 37541 on no process). The guest must leave and rejoin, and the host's NPC must not start a line over one still playing; the same with both players in the host's conversation."


def run(ctx):
    try:
        ctx.stage({'npcs': [{'name': 'andurs', 'base': '00013BA8', 'frozen': False}], 'players': {'host': {'near': 'andurs', 'gap': 200, 'bearing': 90}, 'guest': {'near': 'andurs', 'gap': 110, 'bearing': 270}}})
        ctx.talk('client', 'andurs', [])
        ctx.dialogue_highlight(instance='client', index=2)
        ctx.wait_takes_pick(instance='client')
        ctx.dialogue_press(instance='client')
        ctx.voice_mark(name='andurs')
        ctx.guest_leaves_to_main_menu()
        ctx.joins_with_world(what='rejoins from the main menu')
        ctx.expect_no_line_layered()
        ctx.talk('', 'andurs', [])
        ctx.stage({'players': {'guest': {'near': 'andurs', 'gap': 110, 'bearing': 270}}})
        ctx.talk('client', 'andurs', [])
        ctx.dialogue_highlight(instance='', index=2)
        ctx.wait_takes_pick(instance='')
        ctx.dialogue_press(instance='')
        ctx.voice_mark(name='andurs')
        ctx.guest_leaves_to_main_menu()
        ctx.expect_no_line_layered()
        ctx.leave_dialogue('')
        ctx.joins_with_world(what='rejoins once the host leaves the conversation')
    finally:
        ctx.unstage()
