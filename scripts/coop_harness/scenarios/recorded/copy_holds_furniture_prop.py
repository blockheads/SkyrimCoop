"""The party leaves Riverwood and comes back while NPCs sit in furniture idles holding props (Alvor's forge hammer): each copy on the guest holds its owner's props (docs/re/systems/animation-objects.md).

Recorded with the repro MCP (docs/design/repro-mcp.md)."""
# recorded: 20261116-200857-copy-holds-furniture-prop.jsonl, 8 calls (scripts/check_scenarios.py)
NAME = 'copy_holds_furniture_prop'
TAGS = ['animation', 'copies', 'scene']
DOC = "The party leaves Riverwood and comes back while NPCs sit in furniture idles holding props (Alvor's forge hammer): each copy on the guest holds its owner's props (docs/re/systems/animation-objects.md)."


def run(ctx):
    try:
        ctx.scene(save=None, at='coc Riverwood', pin=True)
        ctx.scene(save=None, at='coc RiverwoodSleepingGiantInn', pin=True)
        ctx.scene(save=None, at='coc Riverwood', pin=True)
        ctx.scene(save=None, at='coc RiverwoodSleepingGiantInn', pin=True)
        ctx.scene(save=None, at='coc Riverwood', pin=True)
        ctx.expect('all', 'props', within=5.0, seconds=15.0)
    finally:
        ctx.unstage()
