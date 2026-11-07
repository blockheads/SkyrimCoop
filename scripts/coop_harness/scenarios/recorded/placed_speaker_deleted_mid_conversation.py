"""The host's game marks a placed NPC deleted (a quest's Delete, no Disable first) while a guest talks to it: the engine frees the actor's process and its handle stays valid, so UpdateInDialogue (37541) faulted on the null process (GetLipData). The conversation ends on both games and neither crashes.

Recorded with the repro MCP (docs/design/repro-mcp.md)."""
# recorded: 20261106-105213-placed-speaker-deleted-mid-conversation.jsonl, 6 calls (scripts/check_scenarios.py); the end's wait by hand: expect dialogue needs both menus
NAME = 'placed-speaker-deleted-mid-conversation'
TAGS = ['copies', 'dialogue']
DOC = "The host's game marks a placed NPC deleted (a quest's Delete, no Disable first) while a guest talks to it: the engine frees the actor's process and its handle stays valid, so UpdateInDialogue (37541) faulted on the null process (GetLipData). The conversation ends on both games and neither crashes."


def run(ctx):
    try:
        ctx.stage({'players': {'guest': {'near': 'vilkas', 'gap': 110}}, 'npcs': [{'name': 'vilkas', 'ref': '1A695', 'frozen': True}]})
        ctx.talk('client', 'vilkas', [])
        ctx.console_on('', '1A695.markfordelete')
        ctx.wait_for('[client] its menu closes', lambda: ctx.menu_closed('client'), 10)
        ctx.wait_for('[both] the conversation ends', lambda: not ctx.dialogue('').get('speaker') and not ctx.dialogue('client').get('speaker'), 10)
    finally:
        ctx.unstage()
