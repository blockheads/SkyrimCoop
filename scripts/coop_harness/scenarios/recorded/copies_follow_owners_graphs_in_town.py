"""Whiterun's market at midday: every copy's graph is its owner's at every update over 120 s, humanoids at their stalls,
chores and walks (the graph input stream's live check, docs/design/graph-input-stream.md)."""
# hand-written: the live check over a busy town, recorded by hand until the MCP runs this branch's harness
NAME = 'copies_follow_owners_graphs_in_town'
TAGS = ['animation', 'copies', 'scene']
DOC = 'Whiterun\'s market at midday: every copy\'s graph is its owner\'s at every update over 120 s (the graph input stream\'s live check).'


def run(ctx):
    try:
        ctx.scene(save=None, at='coc WhiterunOrigin', pin=True)
        ctx.expect('', 'stream', value=10, seconds=120.0)
    finally:
        ctx.unstage()
