"""The host goes ahead outdoors (setpos: not a teleport, so no party travel) into cells the guest has not loaded, and the guest follows into the host's grid. The host's NPCs there are announced before the guest's game has their references, so each spawn waits; once the guest's grid loads them, their held 3D jobs end in QueuedReference::End (13023) and the spawns apply: the guest sees the host's wolf (playtest 2026-11-22, report 1951-468). The soft leash counts down while the guest is out of the host's grid and stops when it comes back.

Recorded with the repro MCP (docs/design/repro-mcp.md)."""
# recorded: 20261122-223204-host-ahead-npcs-reach-guest-2.jsonl, 25 calls (scripts/check_scenarios.py); the exploratory reads and the
# strict 'all' checks dropped by hand (847DD's reference is elsewhere on the guest: party-travel.md, "Soft leash")
NAME = 'host-ahead-npcs-reach-guest'
TAGS = ['recorded', 'scene', 'own-checks']
DOC = "The host goes ahead outdoors (setpos: not a teleport, so no party travel) into cells the guest has not loaded, and the guest follows into the host's grid. The host's NPCs there are announced before the guest's game has their references, so each spawn waits; once the guest's grid loads them, their held 3D jobs end in QueuedReference::End (13023) and the spawns apply: the guest sees the host's wolf (playtest 2026-11-22, report 1951-468). The soft leash counts down while the guest is out of the host's grid and stops when it comes back."


def run(ctx):
    try:
        ctx.scene(save=None, at='coc Riverwood', pin=True)
        ctx.god_mode(instance='', on=True)
        ctx.god_mode(instance='client', on=True)
        ctx.console_on('', 'player.setpos z 3000')
        ctx.console_on('', 'player.setpos x 26500')
        ctx.console_on('', 'player.setpos y -33500')
        ctx.console_on('client', 'player.setpos z 3000')
        ctx.console_on('client', 'player.setpos x 24000')
        ctx.console_on('client', 'player.setpos y -40000')
        ctx.console_on('client', 'player.setpos x 26300')
        ctx.console_on('client', 'player.setpos y -33700')
        ctx.find('Wolf', None)
        ctx.expect('wolf', 'agree', within=5.0, seconds=5.0)
    finally:
        ctx.unstage()
