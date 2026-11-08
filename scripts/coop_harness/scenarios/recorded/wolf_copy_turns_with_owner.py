"""A Riverwood wolf's copy follows its owner's graph through canned and looping turns: owner and copy, read live for 120 s, are never apart for 3 s or more. Before the fix the copy kept turning, or stayed in its canned turn, for 4 to 23 s while its owner stood idle (docs/re/systems/animation-replay-recovery.md, "A machine's start state is each game's own").

Recorded with the repro MCP (docs/design/repro-mcp.md)."""
# recorded: 20261107-183128-wolf-copy-turns-with-owner.jsonl, 5 calls (scripts/check_scenarios.py)
NAME = 'wolf-copy-turns-with-owner'
TAGS = ['animation', 'copies', 'scene']
DOC = 'A Riverwood wolf\'s copy follows its owner\'s graph through canned and looping turns: owner and copy, read live for 120 s, are never apart for 3 s or more. Before the fix the copy kept turning, or stayed in its canned turn, for 4 to 23 s while its owner stood idle (docs/re/systems/animation-replay-recovery.md, "A machine\'s start state is each game\'s own").'


def run(ctx):
    try:
        ctx.scene(save=None, at='coc Riverwood', pin=True)
        ctx.scene(save=None, at='coc Riverwood', pin=True)
        ctx.name_owner(name='wolf', owner='9B0DD')
        ctx.expect('wolf', 'graph', within=3.0, seconds=120.0)
    finally:
        ctx.unstage()
