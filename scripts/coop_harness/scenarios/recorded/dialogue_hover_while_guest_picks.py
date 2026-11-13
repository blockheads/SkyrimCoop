"""The host runs its mouse over Andurs's answers while the guest presses E in the middle of it (after three of the host's moves). The guest says the option the host's mouse had highlighted on every menu, exactly one line, and both menus agree all through.

Recorded with the repro MCP (docs/design/repro-mcp.md)."""
# recorded: 20261109-110539-dialogue-hover-while-guest-picks.jsonl, 10 calls (scripts/check_scenarios.py); edited: the scene's stale frozen stage and reads dropped
NAME = 'dialogue-hover-while-guest-picks'
TAGS = ['dialogue', 'together']
DOC = "The host runs its mouse over Andurs's answers while the guest presses E in the middle of it (after three of the host's moves). The guest says the option the host's mouse had highlighted on every menu, exactly one line, and both menus agree all through."


def run(ctx):
    try:
        ctx.stage({'npcs': [{'name': 'andurs', 'base': '00013BA8', 'frozen': False}], 'players': {'host': {'near': 'andurs', 'gap': 110, 'bearing': 90}, 'guest': {'near': 'andurs', 'gap': 110, 'bearing': 270}}})
        ctx.talk('client', 'andurs', [])
        ctx.talk('', 'andurs', [])
        ctx.wait_list_shown(instance='')
        ctx.wait_npc_quiet()
        ctx.dialogue_mark()
        ctx.together([
            lambda: ctx.dialogue_sweep(instance='', rounds=3),
            lambda: ctx.dialogue_press_after_moves(instance='client', watch='', moves=3),
        ], sync='start')
        ctx.expect_lines_said(count=1)
        ctx.expect('andurs', 'dialogue', within=5.0, seconds=0.0)
    finally:
        ctx.unstage()
