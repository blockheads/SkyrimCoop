"""Two players on one horse: the host drives, the guest rides along, the host swings its sword from the saddle. Its blows pass through the guest: no damage, no reaction on either game.

Recorded with the repro MCP (docs/design/repro-mcp.md)."""
# recorded: 20261127-235110-riders-pass-through-each-other.jsonl, 15 calls (scripts/check_scenarios.py)
import time

from ...errors import TestFailure

NAME = 'riders-pass-through-each-other'
TAGS = ['combat', 'mounts', 'scene']
DOC = 'Two players on one horse: the host drives, the guest rides along, the host swings its sword from the saddle. Its blows pass through the guest: no damage, no reaction on either game.'


def run(ctx):
    try:
        ctx.scene(save=None, at='cow Tamriel 0 -4', pin=False)
        ctx.console_on('', 'player.placeatme 109E3D 1')
        ctx.find('Horse', 'horse')
        ctx.use('', 'horse')
        ctx.wait_seated(instance='')
        ctx.place_before(instance='client', target='horse', gap=130, bearing=90)
        ctx.use('client', 'horse')
        ctx.passenger_seated(instance='client', horse='horse')
        ctx.equip_weapon('', 77495)
        ctx.draw('')  # hand-written: the recording's draw timed out mounted (no equip clip counted; the verb now knows)
        health = lambda: round((ctx.player_snapshot('client').get('values') or {}).get('health', 0), 1)
        full = health()
        # hand-written: the recording logged the host's every swing taking 7.3 from the guest ("delta health 2:-7.29")
        for _ in range(7):
            ctx.attack(instance='')
        # holds through the last blow's message reaching the guest (half a second before the fix)
        ended = time.monotonic() + 3
        while time.monotonic() < ended:
            if health() != full:
                raise TestFailure(f"[client] took the host's swings from the same horse: health {full} -> {health()}")
            time.sleep(0.1)
        # hand-written: both off before the horse goes (removed under its riders, its copy on the guest was left ownerless)
        ctx.get_off(instance='client')
        ctx.get_off(instance='')
    finally:
        ctx.remove('horse')
        ctx.unstage()
