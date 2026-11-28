"""The guest hands Lucan the golden claw; while he answers, the host presses E on him. The guest's conversation runs on to the end of Lucan's goodbye. Before the fix the host's E opened its menu on the conversation (DialogueBridge::ShowMenu, 52857) and the menu's fill shut it at once on forceGoodbye (51515; set at the pick by 35283), cutting the line for everyone (playtest 2026-11-26).

Recorded with the repro MCP (docs/design/repro-mcp.md), trimmed by hand: the staging false starts (Lucan staged
into QASmoke, moved back) dropped and the checks the recording lacked added."""
# recorded: 20261127-201830-dialogue-host-talks-during-guest-turn-in.jsonl, 37 calls (scripts/check_scenarios.py)
NAME = 'dialogue-host-talks-during-guest-turn-in'
TAGS = ['dialogue', 'scene', 'together']
DOC = "The guest hands Lucan the golden claw; while he answers, the host presses E on him. The guest's conversation runs on to the end of Lucan's goodbye. Before the fix the host's E opened its menu on the conversation (DialogueBridge::ShowMenu, 52857) and the menu's fill shut it at once on forceGoodbye (51515; set at the pick by 35283), cutting the line for everyone (playtest 2026-11-26)."


def run(ctx):
    try:
        ctx.scene(save=None, at='coc RiverwoodRiverwoodTrader', pin=True)
        ctx.console_on('', 'resetquest 39645')
        ctx.console_on('', 'startquest 39645')
        ctx.console_on('', 'stopquest MS13LucanCamillaScene')
        ctx.console_on('', 'setstage 39645 60')
        ctx.console_on('client', 'player.additem 39647 1')
        ctx.expect('39645', 'stage', within=5.0, value=[60], seconds=0.0)
        ctx.name_handle('lucan', {'host': 0x13487, 'guest': 0x13487})
        ctx.stage({'players': {'host': {'near': 'lucan', 'gap': 110, 'bearing': 90}, 'guest': {'near': 'lucan', 'gap': 110, 'bearing': 270}}})
        ctx.talk('client', 'lucan', ['Did something happen?'])
        ctx.wait_takes_pick(instance='client')
        ctx.dialogue_press(instance='client')  # "Wait, you mean this golden claw?", Lucan's goodbye answer
        ctx.wait_for("[host] Lucan says the guest's goodbye", lambda: ctx.dialogue('').get('flags', {}).get('force_goodbye'), 10)
        ctx.press_talk('', 'lucan')
        ctx.expect_stays_talking('client', 'lucan', seconds=3.0)
        ctx.wait_for('[host] the claw quest reaches stage 100', lambda: ctx.quest_stage('', 0x39645) == 100, 60)
        ctx.wait_for('[guest] the goodbye ends its conversation', lambda: ctx.menu_closed('client'), 60)
    finally:
        ctx.close_dialogues()
        ctx.console_on('', 'resetquest 39645')
        ctx.unstage()
