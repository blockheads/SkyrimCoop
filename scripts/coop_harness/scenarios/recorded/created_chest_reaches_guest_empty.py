"""The host creates a leveled chest; it should hold the same on the guest, whose copy is empty (docs/bugs.md, Containers).

Recorded with the repro MCP (docs/design/repro-mcp.md)."""
NAME = 'created-chest-reaches-guest-empty'
TAGS = ['containers', 'recorded']
DOC = 'The host creates a leveled chest; it should hold the same on the guest, whose copy is empty (docs/bugs.md, Containers).'


def run(ctx):
    try:
        ctx.stage({'objects': [{'name': 'chest', 'base': 'F68AE'}]})
        ctx.use('client', 'chest')
        ctx.expect('chest', 'items')
        ctx.hold_key('client', 'Tab', 0.1)
    finally:
        ctx.unstage()
