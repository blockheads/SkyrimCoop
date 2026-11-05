"""A guest loots an owned chest through the container menu while its owner watches: the party's bounty rises in both games (docs/re/systems/crime.md).

Recorded with the repro MCP (docs/design/repro-mcp.md), trimmed to the steps that hold: the session's reads, retries
and hot reloads are left out. The witness's detection is read (`wait_seen`), never requested."""
# recorded: 20261104-223926-guest-theft-witnessed.jsonl, trimmed (scripts/check_scenarios.py)
NAME = 'guest-theft-witnessed'
TAGS = ['crime', 'scene']
DOC = "A guest loots an owned chest through the container menu while its owner watches: the party's bounty rises in both games (docs/re/systems/crime.md)."


def run(ctx):
    try:
        ctx.name_players()
        ctx.report_crimes()
        ctx.name_handle('chest', {'host': 0xC2CE8, 'guest': 0xC2CE8})
        ctx.stage({'players': {'host': {'near': 'chest', 'gap': 450}}})
        ctx.stage({'npcs': [{'name': 'witness', 'base': '13479', 'frozen': False}]})
        ctx.own(target='chest', owner='13479')
        ctx.console_on('', 'C2CE8.additem f 100')
        ctx.stage({'players': {'guest': {'near': 'witness', 'gap': 150}}})
        ctx.witness_faces(npc='witness', toward='guest_player')
        ctx.wait_seen(npc='witness', of='guest_player')
        ctx.take('client', 'chest')
        ctx.expect_bounty(at_least=1, seconds=10)
    finally:
        ctx.unstage()
