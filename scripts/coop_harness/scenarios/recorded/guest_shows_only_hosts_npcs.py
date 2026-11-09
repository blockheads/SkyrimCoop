"""The host owns every NPC: a guest never shows or runs one the host has not spawned. Whiterun's market; on the guest alone Aela (in Jorrvaskr on the host) is moved beside its player, as the guest's own AI walked her, Ria and Farkas there on 2026-11-09. The guest must show no NPC of its own (copy-contract.md, "A guest's own NPCs").

Recorded with the repro MCP (docs/design/repro-mcp.md)."""
# recorded: 20261109-023851-guest_shows_only_hosts_npcs.jsonl, 9 calls (scripts/check_scenarios.py)
NAME = 'guest_shows_only_hosts_npcs'
TAGS = ['copies', 'guest', 'scene']
DOC = 'The host owns every NPC: a guest never shows or runs one the host has not spawned. Whiterun\'s market; on the guest alone Aela (in Jorrvaskr on the host) is moved beside its player, as the guest\'s own AI walked her, Ria and Farkas there on 2026-11-09. The guest must show no NPC of its own (copy-contract.md, "A guest\'s own NPCs").'


def run(ctx):
    try:
        ctx.scene(save=None, at='coc WhiterunOrigin', pin=False)
        ctx.expect('guest', 'guest_npcs', within=5.0, seconds=60.0)
        ctx.player_command('client', {'cmd': 'actor_place', 'actor': 108183})
        ctx.player_command('', {'cmd': 'actor_place', 'actor': 108183})
        ctx.player_command('client', {'cmd': 'move_actor', 'actor': 108183, 'position': [19786, -7458, -3640]})
        ctx.expect('guest', 'guest_npcs', within=5.0, seconds=10.0)
        ctx.player_command('client', {'cmd': 'actor_place', 'actor': 108183})
    finally:
        ctx.unstage()
