"""The guest takes Belethor's worn clothes from his dead copy with the loot menu left open: once the host moves them and the copy's items are reconciled, the open menu lists the clothes once (the guest's), not still on the corpse. Before the fix the menu kept the corpse's freed entry: the 2026-11-22 playtest's guest crashes in GetExtraData (12329) on the next take or hover.

Recorded with the repro MCP (docs/design/repro-mcp.md)."""
# recorded: 20261122-225822-guest-loot-menu-follows-corpse.jsonl, 20 calls, tidied: the spawn's ids, no exploration (scripts/check_scenarios.py)
NAME = 'guest-loot-menu-follows-corpse'
TAGS = ['death', 'inventory', 'items']
DOC = "The guest takes Belethor's worn clothes from his dead copy with the loot menu left open: once the host moves them and the copy's items are reconciled, the open menu lists the clothes once (the guest's), not still on the corpse. Before the fix the menu kept the corpse's freed entry: the 2026-11-22 playtest's guest crashes in GetExtraData (12329) on the next take or hover."


def run(ctx):
    try:
        npc, _, copy = ctx.spawn_host_npc(base='00013BA1', restrained=True)
        ctx.console_on('', f'{npc:X}.additem 64B3D 3')
        ctx.wait_for("[client] Belethor's copy carries the sweet rolls", lambda: ctx.item_count('client', copy, 0x64B3D) == 3, 5)
        ctx.console_on('', f'{npc:X}.kill')
        ctx.wait_on('client', "[client] Belethor's copy dies", lambda actors: actors.get(str(copy), {}).get('dead'))
        # His worn clothes (261C0) are a stack held in its own extra data list (ExtraWorn).
        ctx.loot(instance='client', target=f'{copy:X}', item='Clothes')
        ctx.expect_menu_lists(instance='client', item='Clothes', count=1)
    finally:
        ctx.unstage()
