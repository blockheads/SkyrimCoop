"""In a game the Journal's Load lists only the loaded playthrough: the host plays character 0BADF00D (set as loading its save would, modded flag included) and saves; with another character's save in the folder, Character Selection (T) lists only 0BADF00D, neither the other nor Show all saves (SaveListHooks.cpp: BuildSaveList 35743, rows 52917; save-game.md, Only this playthrough's saves).

Recorded with the repro MCP (docs/design/repro-mcp.md)."""
# recorded: 20261127-175926-load-lists-only-playthrough.jsonl, 6 calls (scripts/check_scenarios.py)
NAME = 'load-lists-only-playthrough'
TAGS = ['load', 'saves']
DOC = "In a game the Journal's Load lists only the loaded playthrough: the host plays character 0BADF00D (set as loading its save would, modded flag included) and saves; with another character's save in the folder, Character Selection (T) lists only 0BADF00D, neither the other nor Show all saves (SaveListHooks.cpp: BuildSaveList 35743, rows 52917; save-game.md, Only this playthrough's saves)."


def run(ctx):
    try:
        ctx.current_character(instance='', character='0BADF00D')
        ctx.make_host_quicksave()
        ctx.plant_other_playthrough_save(instance='')
        ctx.expect_rows_only(instance='')
    finally:
        ctx.drop_planted_saves(instance='')
        ctx.unstage()
