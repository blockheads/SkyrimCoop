"""In a game the Journal's Load lists only the loaded playthrough. The host plays character 0BADF00D (set as loading its save would, modded flag included) and saves; another playthrough's save (0C0FFEE0) is planted and the game scans every save as the main menu does (both seen). Character Selection (T) must list exactly 0BADF00D: vanilla also lists Show all saves and 0C0FFEE0 (SaveListHooks.cpp: BuildSaveList 35743, rows 52917; save-game.md, Only this playthrough's saves).

Recorded with the repro MCP (docs/design/repro-mcp.md)."""
# recorded: 20261127-183227-load-lists-only-playthrough.jsonl, 6 calls (scripts/check_scenarios.py)
NAME = 'load-lists-only-playthrough'
TAGS = ['load', 'saves']
DOC = "In a game the Journal's Load lists only the loaded playthrough. The host plays character 0BADF00D (set as loading its save would, modded flag included) and saves; another playthrough's save (0C0FFEE0) is planted and the game scans every save as the main menu does (both seen). Character Selection (T) must list exactly 0BADF00D: vanilla also lists Show all saves and 0C0FFEE0 (SaveListHooks.cpp: BuildSaveList 35743, rows 52917; save-game.md, Only this playthrough's saves)."


def run(ctx):
    try:
        ctx.current_character(instance='', character='0BADF00D')
        ctx.make_host_quicksave()
        ctx.plant_other_playthrough_save(instance='')
        ctx.expect_scan_sees(instance='', characters=['0BADF00D', '0C0FFEE0'])
        ctx.expect_rows_only(instance='')
    finally:
        ctx.drop_planted_saves(instance='')
        ctx.unstage()
