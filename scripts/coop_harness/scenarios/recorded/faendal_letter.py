"""Recorded: the host starts A Lovely Letter by talking to Faendal in Riverwood (late save); both games have it at stage 10.

Recorded with the repro MCP (docs/design/repro-mcp.md)."""
NAME = 'faendal-letter'
TAGS = ['late-save', 'quest']
DOC = 'Recorded: the host starts A Lovely Letter by talking to Faendal in Riverwood (late save); both games have it at stage 10.'


def run(ctx):
    try:
        ctx.needs_save('LateAfterDragonslayerBackinSkyrim')
        ctx.go_far('', 'coc Riverwood')
        ctx.arrives_with_party('client', 3000.0)
        ctx.name_handle('faendal', {'host': 78988, 'guest': 78988})
        ctx.walk_to('', 'faendal', 150.0)
        ctx.walk_to('client', 'faendal', 250.0)
        ctx.talk_to('', 'faendal')
        ctx.pick('', 'problem with Sven')
        ctx.pick('', 'hope')
        ctx.pick('', 0)
        ctx.expect('FreeformRiverwood01', 'stage', within=5.0, value=[10.0])
        ctx.close_dialogues()
    finally:
        ctx.unstage()
