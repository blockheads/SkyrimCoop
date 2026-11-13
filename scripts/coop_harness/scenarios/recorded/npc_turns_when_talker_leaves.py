"""The guest starts a conversation with the host's NPC (Lydia), the host joins, the guest leaves: every game's NPC turns from the guest to the host, the player still in it (ai_state.head_track; NotifyDialogueFace, docs/re/systems/dialogue.md, "Who the speaker looks at").

Recorded with the repro MCP (docs/design/repro-mcp.md)."""
# recorded: 20261113-100123-npc-turns-when-talker-leaves.jsonl, 18 calls (scripts/check_scenarios.py)
NAME = 'npc-turns-when-talker-leaves'
TAGS = ['ai', 'dialogue']
DOC = 'The guest starts a conversation with the host\'s NPC (Lydia), the host joins, the guest leaves: every game\'s NPC turns from the guest to the host, the player still in it (ai_state.head_track; NotifyDialogueFace, docs/re/systems/dialogue.md, "Who the speaker looks at").'


# trimmed by hand: earlier stagings and the presses xdotool dropped ('tab') left out
def run(ctx):
    try:
        ctx.name_players()
        ctx.stage({'players': {'host': {'near': 'lydia', 'gap': 150, 'bearing': 90}, 'guest': {'near': 'lydia', 'gap': 110, 'bearing': 0}}, 'npcs': [{'name': 'lydia', 'base': 'A2C8E', 'frozen': False}]})
        ctx.talk('client', 'lydia', [])
        ctx.expect('lydia', 'actor', within=5.0, value='guest_player', field='ai_state.head_track', seconds=5.0)
        ctx.talk('', 'lydia', [])
        ctx.hold_key('client', 'Tab', 0.2)
        ctx.expect('lydia', 'actor', within=5.0, value='host_player', field='ai_state.head_track', seconds=5.0)
    finally:
        ctx.unstage()
