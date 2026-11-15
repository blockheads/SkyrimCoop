"""The guest's player swings a sword at a ladle: its swing knocks the ladle in its own game, and the host's ladle must move alike and both rest where the host's does (it snapped back to the host's untouched pose: the host never moved it).

Recorded with the repro MCP (docs/design/repro-mcp.md), trimmed by hand: the raw body reads dropped, the failing swing_at kept; a ladle (QASmoke's own one's base) for the recorded bucket (a sword barely moves a bucket; a cabbage rolls on and never rests)."""
# recorded: 20261113-090157-guest-swing-moves-host.jsonl, 14 calls (scripts/check_scenarios.py)
NAME = 'guest-swing-moves-host'
TAGS = ['combat', 'objects', 'physics']
LADLE = 0x32AFE  # QASmoke's ladle (guest-bump-moves-host's): its base
DOC = "The guest's player swings a sword at a ladle: its swing knocks the ladle in its own game, and the host's ladle must move alike and both rest where the host's does (it snapped back to the host's untouched pose: the host never moved it)."


def run(ctx):
    # A ladle of its own (QASmoke's own one's base): unstage deletes it, nothing to put back.
    base = ctx.world_state('', [LADLE])[LADLE]['base']
    try:
        ctx.stage({'objects': [{'name': 'ladle', 'base': base if isinstance(base, str) else f'{base:08X}'}], 'items': [{'who': 'guest', 'item': '00012EB7', 'equip': True}]})
        ctx.stage({'players': {'host': {'near': 'ladle', 'gap': 300}, 'guest': {'near': 'ladle', 'gap': 70}}})
        ctx.swing_at(name='ladle', game='guest')
    finally:
        ctx.unstage()
