"""Both players talk to Andurs and wait past his greeting; both leave and the guest asks him again at once, while he still voices a line of the closed conversation. The guest's menu must keep the greeting's three choices. It lost them (8 of 8): StartDialogue showed the list at once, the host's hidden-start check for anything left to say (FirstOption 35267) left the engine's cursor on the first option, and the build after the old line listed that option's choices ("What did you lose?").

Recorded with the repro MCP (docs/design/repro-mcp.md)."""
# recorded: 20261117-074927-dialogue-guest-reasks-after-close.jsonl, 7 calls (scripts/check_scenarios.py); edited: the session's earlier stages dropped
NAME = 'dialogue-guest-reasks-after-close'
TAGS = ['dialogue']
DOC = 'Both players talk to Andurs and wait past his greeting; both leave and the guest asks him again at once, while he still voices a line of the closed conversation. The guest\'s menu must keep the greeting\'s three choices. It lost them (8 of 8): StartDialogue showed the list at once, the host\'s hidden-start check for anything left to say (FirstOption 35267) left the engine\'s cursor on the first option, and the build after the old line listed that option\'s choices ("What did you lose?").'


def run(ctx):
    try:
        ctx.stage({'npcs': [{'name': 'andurs', 'base': '00013BA8', 'frozen': False}], 'players': {'host': {'near': 'andurs', 'gap': 110, 'bearing': 90}, 'guest': {'near': 'andurs', 'gap': 110, 'bearing': 270}}})
        ctx.expect_reask_keeps_choices(name='andurs')
    finally:
        ctx.unstage()
