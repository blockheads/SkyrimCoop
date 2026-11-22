"""A held copy shown again as the session ends frees no worn list twice. The guest crashed and hung on 2026-11-22 (guest-ivarstead-fine-mouse-sweep): Wilhelm's copy wore an outfit item by our forced equip (its list: leveled base, worn, cannot wear) beside an equal unworn one; held with no owner, then enabled at the disconnect, its inventory reset removed the item with no process and the engine freed the list twice (16059, 19694).

Recorded with the repro MCP (docs/design/repro-mcp.md)."""
# recorded: 20261122-130050-held-copy-shown-again-frees-no-list-twice.jsonl, 10 calls (scripts/check_scenarios.py)
NAME = 'held_copy_shown_again_frees_no_list_twice'
TAGS = ['copies', 'equipment', 'session']
DOC = "A held copy shown again as the session ends frees no worn list twice. The guest crashed and hung on 2026-11-22 (guest-ivarstead-fine-mouse-sweep): Wilhelm's copy wore an outfit item by our forced equip (its list: leveled base, worn, cannot wear) beside an equal unworn one; held with no owner, then enabled at the disconnect, its inventory reset removed the item with no process and the engine freed the list twice (16059, 19694)."


def run(ctx):
    try:
        ctx.count_double_frees()
        ctx.both_go(cell='IvarsteadVilemyrInn')
        ctx.expect_stack_extra(instance='client', holder=105992, item=114202, extra=61)
        ctx.both_go(cell='QASmoke')
        ctx.host_loads_party(quick=False)
        ctx.expect_no_double_free(instance='client')
        ctx.expect_no_double_free(instance='')
    finally:
        ctx.unstage()
