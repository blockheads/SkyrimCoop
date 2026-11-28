"""Two players on one horse and a frozen wolf just ahead of it: the host's swings from the saddle land on the wolf, not on the guest riding with it.

Recorded with the repro MCP (docs/design/repro-mcp.md)."""
# recorded: 20261128-011026-riders-hit-through-each-other.jsonl, 35 calls (scripts/check_scenarios.py)
# hand-written from the recording: its spell steps left out (no rider casts on a horse, measured), the wolf by its
# spawn's id, the checks and cleanup added
import time

from ...errors import TestFailure
from ...utilities import log

NAME = 'riders-hit-through-each-other'
TAGS = ['combat', 'mounts', 'scene']
DOC = "Two players on one horse and a frozen wolf just ahead of it: the host's swings from the saddle land on the wolf, not on the guest riding with it."


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
        wolf = ctx.spawn_beside_host(base='00023ABE')
        ctx.name_handle('wolf', {'host': wolf})
        # The guest's seated copy stands at the host's own x, y: the attack cone (IsInAttackCone 47297) takes its
        # direction from a zero vector, 180 degrees off the heading, so with the horse facing south it is the right
        # swing's best target (measured: 13.6 degrees), and a wolf 30 degrees off the swing loses to it before the fix.
        ctx.console_on('', f"{ctx.resolve('horse', ''):x}.setangle z 180")
        ctx.npc_beside_rider(instance='', npc='wolf', horse='horse', right=95, ahead=55)
        ctx.console_on('', f'{wolf:x}.modav health 2000')  # it outlives the swings
        ctx.equip_weapon('', 77495)
        ctx.draw(instance='')

        def guest_health():
            value = ctx.player_health('client')
            if value is None:
                raise TestFailure("[client] no health in the guest's player snapshot")
            return value

        full = guest_health()
        wolf_health = lambda: ctx.host_npc_state(wolf)['health']
        before = wolf_health()
        for _ in range(4):
            ctx.attack(instance='')
        ctx.wait_for("[host] the last swing is over", lambda: not ctx.graph_bools('', 'IsAttacking')['IsAttacking'], 3)
        # Each blow takes about 7 from the wolf: three of four swings at least.
        ctx.wait_for("[host] the wolf took the swings the guest's copy would have taken", lambda: before - wolf_health() >= 18, 3,
                     explain=lambda: f"wolf {before} -> {wolf_health()}")
        ended = time.monotonic() + 3  # the guest's own player holds through the last blow's delivery
        while time.monotonic() < ended:
            if guest_health() < full:
                raise TestFailure(f"[client] took the host's swings from the same horse: health {full} -> {guest_health()}")
            time.sleep(0.1)
    finally:
        for instance in ('client', ''):
            try:
                if (ctx.player_snapshot(instance).get('placement') or {}).get('riding'):
                    ctx.get_off(instance=instance)
            except TestFailure as error:  # the run's own failure stays the one reported
                log(f"cleanup: [{instance or 'host'}] could not get off: {error}")
        ctx.remove('wolf')
        ctx.remove('horse')
        ctx.unstage()
