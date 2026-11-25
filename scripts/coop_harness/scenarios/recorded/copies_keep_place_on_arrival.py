"""The guest leads the party between Riverwood and the Bannered Mare: no standing copy races across the screen on arrival (it was stepped toward its place in the cell it left until two snapshots came).

Recorded with the repro MCP (docs/design/repro-mcp.md)."""
# recorded: 20261124-232504-copies-keep-place-on-arrival.jsonl, 13 calls (scripts/check_scenarios.py)
NAME = 'copies_keep_place_on_arrival'
TAGS = ['copies', 'scene', 'travel']
DOC = 'The guest leads the party between Riverwood and the Bannered Mare: no standing copy races across the screen on arrival (it was stepped toward its place in the cell it left until two snapshots came).'


def run(ctx):
    try:
        ctx.scene(save=None, at='coc Riverwood', pin=True)
        ctx.go_far('client', 'coc WhiterunBanneredMare')
        ctx.arrives_with_party(instance='')
        ctx.watch_races()
        ctx.go_far('client', 'coc Riverwood')
        ctx.expect_no_copy_races(seconds=4, frames=1)
        ctx.watch_races()
        ctx.go_far('client', 'coc WhiterunBanneredMare')
        ctx.expect_no_copy_races(seconds=4, frames=1)
        ctx.watch_races()
        ctx.go_far('client', 'coc Riverwood')
        ctx.expect_no_copy_races(seconds=4, frames=1)
    finally:
        ctx.unstage()
