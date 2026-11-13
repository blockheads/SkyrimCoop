"""The guest talks to the host's NPC (Lydia) from behind her, the host on her other side: she turns her body to the guest, as she turns to the host's player when it talks to her from behind (Character::ShouldRotateToTrack 38011 turns the body only to this game's player while talking to it; docs/re/systems/dialogue.md, "Who the speaker looks at").

Recorded with the repro MCP (docs/design/repro-mcp.md)."""
# recorded: 20261113-101954-npc-turns-body-to-guest-talker.jsonl, 12 calls (scripts/check_scenarios.py)
NAME = 'npc-turns-body-to-guest-talker'
TAGS = ['ai', 'dialogue']
DOC = 'The guest talks to the host\'s NPC (Lydia) from behind her, the host on her other side: she turns her body to the guest, as she turns to the host\'s player when it talks to her from behind (Character::ShouldRotateToTrack 38011 turns the body only to this game\'s player while talking to it; docs/re/systems/dialogue.md, "Who the speaker looks at").'


# edited by hand: the host talks first so she faces away from the guest (vanilla turns her to the host)
def run(ctx):
    try:
        ctx.name_players()
        ctx.stage({'players': {'host': {'near': 'lydia', 'gap': 150, 'bearing': 90}, 'guest': {'near': 'lydia', 'gap': 110, 'bearing': 270}}, 'npcs': [{'name': 'lydia', 'base': 'A2C8E', 'frozen': False}]})
        ctx.talk('', 'lydia', [])
        ctx.expect('lydia', 'faces', within=20.0, of='host_player', seconds=4.0)
        ctx.close_dialogues()
        ctx.talk('client', 'lydia', [])
        ctx.expect('lydia', 'faces', within=20.0, of='guest_player', seconds=4.0)
    finally:
        ctx.unstage()
