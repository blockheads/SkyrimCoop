"""After a host load the Saves folders hold no scratch save: the main menu's Continue loads the newest .ess of any character (52898), and a scratch save's name files it under character 0, so the Load list showed another playthrough's saves (save-game.md, Continue and the Load list).

Recorded with the repro MCP (docs/design/repro-mcp.md)."""
# recorded: 20261127-160101-continue-keeps-playthrough.jsonl, 4 calls (scripts/check_scenarios.py)
NAME = 'continue-keeps-playthrough'
TAGS = ['load', 'saves']
DOC = "After a host load the Saves folders hold no scratch save: the main menu's Continue loads the newest .ess of any character (52898), and a scratch save's name files it under character 0, so the Load list showed another playthrough's saves (save-game.md, Continue and the Load list)."


def run(ctx):
    try:
        ctx.host_loads_party(quick=False)
        ctx.expect_no_scratch_saves(instance='')
        ctx.expect_no_scratch_saves(instance='client')
    finally:
        ctx.unstage()
