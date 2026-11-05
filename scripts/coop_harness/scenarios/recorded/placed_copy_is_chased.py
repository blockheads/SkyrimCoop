"""An NPC fighting the guest's copy keeps closing on it after the copy is placed far off (a warp, not a step): combat
reads the copy's predicted place, and its movement controller's is stale (docs/re/systems/combat-targets.md)."""
# hand-written: the mechanism was read in IDA and measured on the duo before this was written
import math

from ...objects import GodMode

NAME = 'placed-copy-is-chased'
TAGS = ['combat', 'scene']
DOC = 'An NPC fighting the guest keeps closing on its copy after the copy is placed far off.'

ORGNAR = 0x13486
FAR_SPOT = (-1100, -290)  # open floor, across the inn from Orgnar's post


def run(ctx):
    try:
        ctx.name_players()
        ctx.scene(save=None, at='coc RiverwoodSleepingGiantInn', pin=True)
        with GodMode(ctx):
            copy = ctx.client_on_host()
            ctx.console_on('client', f'player.moveto {ORGNAR:X}')
            ctx.console_on('', f'{ORGNAR:X}.startcombat {copy:X}')
            read = lambda: ctx.player_command('', {'cmd': 'combat_targets', 'actor': ORGNAR})
            ctx.wait_for('Orgnar fights the copy', lambda: read().get('controller_target') == copy, 15)
            ctx.console_on('client', f'player.setpos x {FAR_SPOT[0]}')
            ctx.console_on('client', f'player.setpos y {FAR_SPOT[1]}')

            def closed_in():
                combat = read()
                predicted = math.dist(combat['target_position'], combat['target_predicted'])
                if predicted > 100:
                    raise ctx.failure(f"combat predicts the copy {predicted:.0f} units from where it stands: {combat}")
                place = [float(ctx.console_on('', f'{ORGNAR:X}.getpos {axis}')[0].split('>>')[1]) for axis in 'xy']
                return math.dist(place, combat['target_position'][:2]) < 300

            ctx.wait_for('Orgnar closes on the placed copy', closed_in, 25)
    finally:
        ctx.unstage()
