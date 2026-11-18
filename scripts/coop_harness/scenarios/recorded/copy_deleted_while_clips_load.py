"""A copy deleted while its movement idle's clip load is queued leaves no load holding its lock. The guest froze on 2026-11-16: a copy's turn-to-walk load (ActionClips, kind 47) kept the deleted copy's process lock, and the animation file job (63997) spun on it once the memory was reused.

Recorded with the repro MCP (docs/design/repro-mcp.md)."""
# recorded: 20261117-201818-copy-deleted-while-clips-load.jsonl, 7 calls (scripts/check_scenarios.py)
NAME = 'copy_deleted_while_clips_load'
TAGS = ['animation', 'copies']
DOC = "A copy deleted while its movement idle's clip load is queued leaves no load holding its lock. The guest froze on 2026-11-16: a copy's turn-to-walk load (ActionClips, kind 47) kept the deleted copy's process lock, and the animation file job (63997) spun on it once the memory was reused."


def run(ctx):
    try:
        ctx.hold_clip_loads(instance='client', held=True)
        ctx.copy_goes_while_turn_loads(base='000A2C8E')
        ctx.expect_clip_loads_hold_live_locks(instance='client')
        ctx.hold_clip_loads(instance='client', held=False)
    finally:
        ctx.unstage()
