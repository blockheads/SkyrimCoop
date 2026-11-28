"""The courier (WICourier) greets the party with WICourierTopic 39F79, an Invisible Continue line in a blocking branch: the host's engine says WICourierDeliveries by itself once it ends (35285), so no menu may offer "(Invisible Continues Linked to Self)" as a list (playtest 2026-11-27). The delivery starts the real way: WIEventsEnabled and WICourierItemCount set, then the party enters Riverwood (the Story Manager's Change Location node WICourierNodeSharesEvent).

Recorded with the repro MCP (docs/design/repro-mcp.md)."""
# recorded: 20261127-204224-courier-force-greet-continues.jsonl, 7 calls (scripts/check_scenarios.py)
NAME = 'courier-force-greet-continues'
TAGS = ['dialogue', 'scene']
DOC = 'The courier (WICourier) greets the party with WICourierTopic 39F79, an Invisible Continue line in a blocking branch: the host\'s engine says WICourierDeliveries by itself once it ends (35285), so no menu may offer "(Invisible Continues Linked to Self)" as a list (playtest 2026-11-27). The delivery starts the real way: WIEventsEnabled and WICourierItemCount set, then the party enters Riverwood (the Story Manager\'s Change Location node WICourierNodeSharesEvent).'


def run(ctx):
    try:
        ctx.console_on('', 'set WIEventsEnabled to 1')
        ctx.console_on('', 'set WICourierItemCount to 1')
        ctx.scene(save=None, at='coc Riverwood', pin=True)
        ctx.expect_no_list_offered(speaker=237495)
    finally:
        ctx.unstage()
