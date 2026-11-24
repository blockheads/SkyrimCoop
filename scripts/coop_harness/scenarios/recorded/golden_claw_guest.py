"""Playtest 2026-11-22 item 15: the guest loots the Golden Claw from Arvel's body (MS13's GoldenClaw alias creates it in Arvel's inventory); the host's quest must reach stage 40 in both games. The host's take for a guest removed the claw to nowhere, unbinding the alias's script (inventory-changes.md, "An alias-held item leaving a container"). Arvel is ghosted by MS13 while in the web (vanilla), so setghost 0 before the kill.

Recorded with the repro MCP (docs/design/repro-mcp.md)."""
# recorded: 20261123-141923-golden-claw-guest.jsonl, 15 calls (scripts/check_scenarios.py)
NAME = 'golden-claw-guest'
TAGS = ['container-transfer', 'late-save', 'party-items', 'quests']
DOC = 'Playtest 2026-11-22 item 15: the guest loots the Golden Claw from Arvel\'s body (MS13\'s GoldenClaw alias creates it in Arvel\'s inventory); the host\'s quest must reach stage 40 in both games. The host\'s take for a guest removed the claw to nowhere, unbinding the alias\'s script (inventory-changes.md, "An alias-held item leaving a container"). Arvel is ghosted by MS13 while in the web (vanilla), so setghost 0 before the kill.'


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
        ctx.item_count(instance='', holder=20, item=235079)
        ctx.item_count(instance='client', holder=20, item=235079)
    finally:
        ctx.unstage()
