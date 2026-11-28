"""The Golden Claw shows no [P] on its holder: the guest loots it from Arvel, the host's player holds the quest object's real stack (MS13's GoldenClaw alias), and the item menu names it by its list's ExtraTextDisplayData (InventoryEntryData::GetName 16018), which TagStackInPlace never set on a quest object's list. Every player's claw must read "[P] Golden Claw".

Recorded with the repro MCP (docs/design/repro-mcp.md)."""
# recorded: 20261127-184708-golden-claw-party-tag.jsonl, 11 calls (scripts/check_scenarios.py)
NAME = 'golden-claw-party-tag'
TAGS = ['container-transfer', 'late-save', 'party-items', 'quests']
DOC = 'The Golden Claw shows no [P] on its holder: the guest loots it from Arvel, the host\'s player holds the quest object\'s real stack (MS13\'s GoldenClaw alias), and the item menu names it by its list\'s ExtraTextDisplayData (InventoryEntryData::GetName 16018), which TagStackInPlace never set on a quest object\'s list. Every player\'s claw must read "[P] Golden Claw".'


def run(ctx):
    try:
        ctx.needs_save('LateOutsideHelgen')
        ctx.console_on('', 'setstage 39645 10')
        ctx.console_on('', 'setstage 39645 30')
        ctx.stage({'npcs': [{'name': 'arvel', 'ref': 235093, 'frozen': False}], 'players': {'host': {'near': 'arvel', 'gap': 150}, 'guest': {'near': 'arvel', 'gap': 150, 'bearing': 90}}})
        ctx.console_on('', '39655.setghost 0')
        ctx.console_on('', '39655.kill')
        ctx.take('client', 'arvel')
        ctx.expect('39645', 'stage', within=5.0, value=[40], seconds=10.0)
        ctx.expect('39647', 'party_tag', within=5.0, seconds=5.0)
    finally:
        ctx.unstage()
