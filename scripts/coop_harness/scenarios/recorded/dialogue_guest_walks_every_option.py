"""The guest says every option of Andurs's first list in turn (each answered, then Tab and talk again) while the host joins each conversation by talking and never picks. Every list on the way is the same on both menus, the menus agree all through, and the host's game says exactly the guest's three lines.

Recorded with the repro MCP (docs/design/repro-mcp.md)."""
# recorded: 20261109-121254-dialogue-guest-walks-every-option.jsonl, 6 calls (scripts/check_scenarios.py); edited: the log mark before the walk, the read made a check
NAME = 'dialogue-guest-walks-every-option'
TAGS = ['dialogue']
DOC = "The guest says every option of Andurs's first list in turn (each answered, then Tab and talk again) while the host joins each conversation by talking and never picks. Every list on the way is the same on both menus, the menus agree all through, and the host's game says exactly the guest's three lines."


def run(ctx):
    try:
        ctx.stage({'npcs': [{'name': 'andurs', 'base': '00013BA8', 'frozen': False}], 'players': {'host': {'near': 'andurs', 'gap': 110, 'bearing': 90}, 'guest': {'near': 'andurs', 'gap': 110, 'bearing': 270}}})
        ctx.dialogue_mark()
        ctx.dialogue_walk(instance='client', target='andurs', watcher='host', steps=1)
        ctx.expect('andurs', 'dialogue', within=5.0, seconds=0.0)
        ctx.expect_lines_said(count=3)
    finally:
        ctx.unstage()
