"""Recorded: the host starts A Lovely Letter by talking to Faendal in Riverwood (late save); both games have it at stage 10.

Recorded with the repro MCP (docs/design/repro-mcp.md)."""
NAME = 'faendal-letter'
TAGS = ['late-save', 'quest', 'scene']
DOC = 'Recorded: the host starts A Lovely Letter by talking to Faendal in Riverwood (late save); both games have it at stage 10.'


def run(ctx):
    try:
        ctx.scene(save='LateAfterDragonslayerBackinSkyrim', at='coc Riverwood', pin=True)
        ctx.go_to('', 'faendal', 150.0, False)
        ctx.go_to('client', 'faendal', 250.0, False)
        ctx.talk('', 'faendal', ['problem with Sven', 'hope', 0])
        ctx.expect('FreeformRiverwood01', 'stage', within=5.0, value=[10.0])
        ctx.close_dialogues()
    finally:
        ctx.unstage()
