"""In a game the Journal's Load lists only the loaded playthrough: with another character's save in the Saves folder, the Character Selection (T) shows neither it nor Show all saves (SaveListHooks.cpp: BuildSaveList 35743, rows 52917; save-game.md, Only this playthrough's saves).

Recorded with the repro MCP (docs/design/repro-mcp.md)."""
# recorded: 20261127-172912-load-lists-only-playthrough.jsonl, 5 calls (scripts/check_scenarios.py)
NAME = 'load-lists-only-playthrough'
TAGS = ['load', 'saves']
DOC = "In a game the Journal's Load lists only the loaded playthrough: with another character's save in the Saves folder, the Character Selection (T) shows neither it nor Show all saves (SaveListHooks.cpp: BuildSaveList 35743, rows 52917; save-game.md, Only this playthrough's saves)."


def run(ctx):
    try:
        ctx.make_host_quicksave()
        ctx.plant_other_playthrough_save(instance='')
        ctx.expect_rows_only(instance='')
    finally:
        ctx.drop_planted_saves(instance='')
        ctx.unstage()
