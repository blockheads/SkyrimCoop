"""The guest barters with Belethor's copy, which carries a named sword (its own stack), and leaves the menu open; the host's script takes the sword from Belethor. Once the copy's items are reconciled, barter no longer lists it. Before the fix barter kept the merchant's freed entry (the same aliasing as the loot menu crashes: barter's stock merges the merchant's own lists, 0x14023D080).

Recorded with the repro MCP (docs/design/repro-mcp.md)."""
# recorded: 20261123-094323-guest-barter-follows-merchant-stock.jsonl, 8 calls, tidied: the spawn's ids, waits for state (scripts/check_scenarios.py)
NAME = 'guest-barter-follows-merchant-stock'
TAGS = ['barter', 'inventory', 'items']
DOC = "The guest barters with Belethor's copy, which carries a named sword (its own stack), and leaves the menu open; the host's script takes the sword from Belethor. Once the copy's items are reconciled, barter no longer lists it. Before the fix barter kept the merchant's freed entry (the same aliasing as the loot menu crashes: barter's stock merges the merchant's own lists, 0x14023D080)."


def run(ctx):
    sword, name = 0x12EB7, "Merchant's Blade"
    try:
        npc, _, copy = ctx.spawn_host_npc(base='00013BA1', restrained=True)
        ctx.player_command(instance='', request={'cmd': 'give_item', 'item': sword, 'name': name, 'holder': npc})
        ctx.wait_for(f"[client] Belethor's copy carries {name} (a stack of its own)",
                     lambda: any(s["item"] == sword and s["name"] == name for s in ctx.item_stacks(instance='client', holder=copy)), 5)
        ctx.barter_open(instance='client', merchant=f'{copy:X}', category=['', 'WEAPONS'])
        ctx.expect_menu_lists(instance='client', item=name, count=1, menu='BarterMenu')
        ctx.script_call(instance='', klass='ObjectReference', function='RemoveItem', args=[{'form': sword}, 1, True, None], on=npc)
        ctx.wait_for("[client] the copy's reconcile takes the sword", lambda: ctx.item_count('client', copy, sword) == 0, 5)
        ctx.expect_menu_lists(instance='client', item=name, count=0, menu='BarterMenu', seconds=5)
    finally:
        ctx.unstage()
