"""Two players on one horse: the host drives, the guest rides along, the host swings its sword from the saddle. Its blows pass through the guest: no damage, no reaction on either game.

Recorded with the repro MCP (docs/design/repro-mcp.md)."""
# recorded: 20261127-235110-riders-pass-through-each-other.jsonl, 15 calls (scripts/check_scenarios.py)
import time

from ...common import KILL_MOVE_RANDOM
from ...errors import TestFailure
from ...objects import AnimTrace
from ...utilities import log

NAME = 'riders-pass-through-each-other'
TAGS = ['combat', 'mounts', 'scene']
DOC = 'Two players on one horse: the host drives, the guest rides along, the host swings its sword from the saddle. Its blows pass through the guest: no damage, no reaction on either game.'

# hand-written: the reactions a landed blow plays on its victim's graph
REACTIONS = ('staggerStart', 'recoilStart', 'recoilLargeStart', 'StaggerPlayer')


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
        ctx.draw('')  # hand-written: the recording's draw timed out mounted (the verb now waits for the rider's clip)

        # hand-written from here: the recording logged each host swing taking 7.3 from the guest ("delta health 2:-7.29")
        def health():
            value = ctx.player_health('client')
            if value is None:
                raise TestFailure("[client] no health in the guest's player snapshot")
            return round(value, 1)

        def swings(count, step):
            """`count` host swings; the guest's health and graph hold through them and the last one's delivery."""
            full = health()
            copy = ctx.player_copy('')
            with AnimTrace(ctx, step, {'client': {0x14: 'guest'}, '': {copy: 'guest copy'}}, video=False) as trace:
                for _ in range(count):
                    ctx.attack(instance='')
                ctx.wait_for("[host] the last swing is over", lambda: not ctx.graph_bools('', 'IsAttacking')['IsAttacking'], 3)
                # A blow's health change reaches the guest within half a second (measured before the fix): held 3 s.
                ended = time.monotonic() + 3
                while time.monotonic() < ended:
                    if health() < full:  # regeneration may raise it
                        raise TestFailure(f"{step}: [client] took the host's swings from the same horse: health {full} -> {health()}")
                    if ctx.graph_bools('', 'bIsSynced')['bIsSynced']:
                        raise TestFailure(f"{step}: [host] a kill move paired it with the guest riding with it")
                    time.sleep(0.1)
                trace.take()
                reacted = [f"[{e['game'] or 'host'}] {e['who']} {e['event']}" for e in trace.entries
                           if e['kind'] == 'graph' and e.get('event') in REACTIONS]
                if reacted:
                    raise TestFailure(f"{step}: the guest reacted to a blow that passed through: {', '.join(reacted)}")

        swings(7, 'riders-swing')
        # Nearly dead (as kill-move-kills-guest stages it): no kill move picks the guest either (measured, no gate needed).
        ctx.console_on('', 'set KillMoveRandom to 100')
        ctx.console_on('client', f"player.damageav health {health() - 3:.6f}")
        ctx.wait_for("[client] the guest at 3 health", lambda: health() <= 3.5, 5)
        swings(3, 'riders-kill-move')
        if ctx.is_dead(ctx.player_snapshot('client')):
            raise TestFailure("[client] the guest died riding with the host")
    finally:
        # Both off before the horse goes (removed under its riders, its copy on the guest was left ownerless).
        for instance in ('client', ''):
            try:
                if (ctx.player_snapshot(instance).get('placement') or {}).get('riding'):
                    ctx.get_off(instance=instance)
            except TestFailure as error:  # the run's own failure stays the one reported
                log(f"cleanup: [{instance or 'host'}] could not get off: {error}")
        ctx.console_on('', f'set KillMoveRandom to {KILL_MOVE_RANDOM}')
        ctx.console_on('client', 'player.restoreav health 10000')
        ctx.remove('horse')
        ctx.unstage()
