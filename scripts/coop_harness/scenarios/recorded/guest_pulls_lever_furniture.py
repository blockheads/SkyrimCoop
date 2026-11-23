"""Playtest 2026-11-22 item 11: Bleak Falls Temple's pillar lever (AA8C3) is a lever furniture (KeywordActivatorFurnitureNoPlayer): TESFurniture::Activate 17420 sends its pull to its own graph for this game's player only. The guest pulls it, then the host: each game's lever gets the pull (Lever_PlayerPush) once (docs/re/systems/activation.md, "Lever furniture").

Recorded with the repro MCP (docs/design/repro-mcp.md)."""
# recorded: 20261123-100853-guest-pulls-lever-furniture.jsonl, 6 calls (a stage before the handle was named left out) (scripts/check_scenarios.py)
NAME = 'guest-pulls-lever-furniture'
TAGS = ['cells', 'objects', 'scene', 'scripts']
DOC = 'Playtest 2026-11-22 item 11: Bleak Falls Temple\'s pillar lever (AA8C3) is a lever furniture (KeywordActivatorFurnitureNoPlayer): TESFurniture::Activate 17420 sends its pull to its own graph for this game\'s player only. The guest pulls it, then the host: each game\'s lever gets the pull (Lever_PlayerPush) once (docs/re/systems/activation.md, "Lever furniture").'


def run(ctx):
    try:
        ctx.scene(save=None, at='coc BleakFallsBarrow01', pin=True)
        ctx.name_handle('pillar_lever', {'host': 698563, 'guest': 698563})
        ctx.stage({'players': {'guest': {'near': 'pillar_lever', 'gap': 120}, 'host': {'near': 'pillar_lever', 'gap': 250}}})
        ctx.use_object_graphs(instance='client', target='pillar_lever')
        ctx.use_object_graphs(instance='', target='pillar_lever')
    finally:
        ctx.unstage()
