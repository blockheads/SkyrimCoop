"""The guest talks to the host's NPC (Lydia): every game's NPC turns its head to the guest's character, the player talking, not the host's player (ai_state.head_track: the dialogue headtrack slot, ActorProcess::SetDialogueHeadtrackSlot 39465; docs/re/systems/dialogue.md, "Who the speaker looks at").

Recorded with the repro MCP (docs/design/repro-mcp.md)."""
# recorded: 20261113-090948-npc-looks-at-guest-talker.jsonl, 7 calls (scripts/check_scenarios.py)
NAME = 'npc-looks-at-guest-talker'
TAGS = ['ai', 'dialogue']
DOC = 'The guest talks to the host\'s NPC (Lydia): every game\'s NPC turns its head to the guest\'s character, the player talking, not the host\'s player (ai_state.head_track: the dialogue headtrack slot, ActorProcess::SetDialogueHeadtrackSlot 39465; docs/re/systems/dialogue.md, "Who the speaker looks at").'


def run(ctx):
    try:
        ctx.name_players()
        # frozen off by hand: a frozen owner's slot never reached its copy (dialogue.md, Who the speaker looks at)
        ctx.stage({'players': {'host': {'near': 'lydia', 'gap': 150, 'bearing': 90}, 'guest': {'near': 'lydia', 'gap': 110, 'bearing': 0}}, 'npcs': [{'name': 'lydia', 'base': 'A2C8E', 'frozen': False}]})
        ctx.talk('client', 'lydia', [])
        ctx.expect('lydia', 'actor', within=5.0, value='guest_player', field='ai_state.head_track', seconds=5.0)
    finally:
        ctx.unstage()
