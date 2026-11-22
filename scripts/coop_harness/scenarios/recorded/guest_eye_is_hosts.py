"""The guest hits an NPC and it fights back: the guest's stealth eye shows what the host's NPCs see of it (detected, 100), not its own game's estimate (its NPC copies have no combat group, so in combat it showed hidden). docs/re/systems/stealth.md, "The eye is the host's".

Recorded with the repro MCP (docs/design/repro-mcp.md)."""
# recorded: 20261121-121143-guest-eye-is-hosts.jsonl, 9 calls (scripts/check_scenarios.py)
NAME = 'guest-eye-is-hosts'
TAGS = ['combat', 'stealth']
DOC = 'The guest hits an NPC and it fights back: the guest\'s stealth eye shows what the host\'s NPCs see of it (detected, 100), not its own game\'s estimate (its NPC copies have no combat group, so in combat it showed hidden). docs/re/systems/stealth.md, "The eye is the host\'s".'


def run(ctx):
    try:
        ctx.name_players()
        ctx.stage({'npcs': [{'name': 'andurs', 'base': '00013BA8', 'frozen': False}], 'players': {'guest': {'near': 'andurs', 'gap': 110, 'bearing': 0}, 'host': {'near': 'andurs', 'gap': 700, 'bearing': 180}}})
        ctx.god_mode(instance='client', on=True)
        ctx.fight('client', 'andurs', 3, False)
        ctx.expect('guest_player', 'eye', within=5.0, value=100, seconds=3.0)
    finally:
        ctx.god_mode(instance='client', on=False)
        ctx.unstage()
