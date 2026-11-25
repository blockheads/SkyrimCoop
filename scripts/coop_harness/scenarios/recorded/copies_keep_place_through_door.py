"""The guest leads the party out of the Sleeping Giant Inn by its load door and back in: no standing copy races across the screen on arrival.

Recorded with the repro MCP (docs/design/repro-mcp.md)."""
# recorded: 20261125-090828-copies_keep_place_through_door.jsonl, 11 calls (scripts/check_scenarios.py)
NAME = 'copies_keep_place_through_door'
TAGS = ['copies', 'scene', 'travel']
DOC = 'The guest leads the party out of the Sleeping Giant Inn by its load door and back in: no standing copy races across the screen on arrival.'


def run(ctx):
    try:
        ctx.scene(save=None, at='coc RiverwoodSleepingGiantInn', pin=True)
        ctx.name_handle('door_out', {'host': 78873, 'guest': 78873})
        ctx.name_handle('door_in', {'host': 78884, 'guest': 78884})
        ctx.watch_races()
        ctx.door(instance='client', target='door_out')
        ctx.expect_no_copy_races(seconds=4, frames=1)
        ctx.watch_races()
        ctx.door(instance='client', target='door_in')
        ctx.expect_no_copy_races(seconds=4, frames=1)
    finally:
        ctx.unstage()
