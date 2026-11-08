"""The wolf near Riverwood turns and idles; its copy's state machines follow its owner's: never apart for 3 s over 180 s, through at least 2 of its owner's canned turns (CannedTurnState) (docs/re/systems/animation-replay-recovery.md, "A copy reads its owner's integers ahead").

Recorded with the repro MCP (docs/design/repro-mcp.md)."""
# recorded: 20261107-214417-wolf-copy-turns-with-owner.jsonl, 6 calls (scripts/check_scenarios.py)
NAME = 'wolf_copy_turns_with_owner'
TAGS = ['animation', 'copies', 'scene']
DOC = 'The wolf near Riverwood turns and idles; its copy\'s state machines follow its owner\'s: never apart for 3 s over 180 s, through at least 2 of its owner\'s canned turns (CannedTurnState) (docs/re/systems/animation-replay-recovery.md, "A copy reads its owner\'s integers ahead").'


def run(ctx):
    try:
        ctx.scene(save=None, at='coc Riverwood', pin=True)
        ctx.name_owner(name='wolf', owner='9B0DD')
        ctx.expect('wolf', 'graph', within=3.0, value=2, field='CannedTurnState', seconds=180.0)
    finally:
        ctx.unstage()
