"""The host kills a bandit and takes everything from its corpse; both games' bandit then holds the same items. On dev the guest's copy also holds the bandit's three leveled-list forms (37C1B, 4F78D, 8FFFB) as entries: the spawn's inventory listed its base container's leveled lists as items and the copy added them (bugs.md 'A looted corpse keeps items on the guest'; one-simulator.md piece 3).

Recorded with the repro MCP (docs/design/repro-mcp.md)."""
# recorded: 20261119-103553-looted-corpse-empty-on-both.jsonl, 8 calls (scripts/check_scenarios.py)
NAME = 'looted_corpse_empty_on_both'
TAGS = ['items', 'piece-3']
DOC = "The host kills a bandit and takes everything from its corpse; both games' bandit then holds the same items. On dev the guest's copy also holds the bandit's three leveled-list forms (37C1B, 4F78D, 8FFFB) as entries: the spawn's inventory listed its base container's leveled lists as items and the copy added them (bugs.md 'A looted corpse keeps items on the guest'; one-simulator.md piece 3)."


def run(ctx):
    try:
        ctx.stage({'npcs': [{'name': 'bandit', 'base': '0003DE8D', 'frozen': False}], 'players': {'host': {'near': 'bandit', 'gap': 120, 'bearing': 90}, 'guest': {'near': 'bandit', 'gap': 400, 'bearing': 270}}, 'items': [{'who': 'host', 'item': '12EB7', 'count': 1, 'equip': True}]})
        ctx.fight('', 'bandit', 60, True)
        ctx.take('', 'bandit')
        ctx.expect('bandit', 'items', within=5.0, seconds=0.0)
    finally:
        ctx.unstage()
