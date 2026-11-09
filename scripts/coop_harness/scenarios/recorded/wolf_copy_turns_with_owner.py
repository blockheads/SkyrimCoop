"""The wolf near Riverwood turns and idles; its copy's graph is its owner's at every update over 180 s, as is every other streamed copy's (the graph input stream's live check, docs/design/graph-input-stream.md).

Recorded with the repro MCP (docs/design/repro-mcp.md)."""
# recorded: 20261107-214417-wolf-copy-turns-with-owner.jsonl, 6 calls (scripts/check_scenarios.py)
NAME = 'wolf_copy_turns_with_owner'
TAGS = ['animation', 'copies', 'scene']
DOC = 'The wolf near Riverwood turns and idles; its copy\'s graph is its owner\'s at every update over 180 s, as is every other streamed copy\'s (the graph input stream\'s live check, docs/design/graph-input-stream.md).'


def run(ctx):
    try:
        ctx.scene(save=None, at='coc Riverwood', pin=True)
        ctx.name_owner(name='wolf', owner='9B0DD')
        # hand-written: the live check replaces the recorded `graph` expect (the MCP runs dev's harness until this merges)
        ctx.expect('wolf', 'stream', seconds=180.0)
    finally:
        ctx.unstage()
