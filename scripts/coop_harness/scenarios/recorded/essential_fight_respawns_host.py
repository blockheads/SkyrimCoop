"""A guest fights an essential NPC, who cannot be beaten out of the fight: the downed host still respawns, a minute on
(HostDeath::kCombatHoldSeconds, docs/re/systems/combat-targets.md)."""
# hand-written: the wait was measured through the MCP before the fix (over two minutes, only the guest's death ended it).
import time

NAME = 'essential-fight-respawns-host'
TAGS = ['combat', 'death', 'scene']
DOC = 'The guest fights Delphine (essential) and the host dies: the fight holds the respawn a minute at most.'

DELPHINE = 0x13485
HOLD_AND_RECOVERY = 90  # kCombatHoldSeconds + kRecoverySeconds, and travel
HELD_AT_LEAST = 50  # back sooner and the fight ended by itself: nothing was shown


def run(ctx):
    try:
        ctx.name_players()
        ctx.scene(save=None, at='coc RiverwoodSleepingGiantInn', pin=True)
        ctx.console_on('client', 'tgm')
        ctx.console_on('client', f'player.moveto {DELPHINE:X}')
        ctx.console_on('', f'{DELPHINE:X}.startcombat {ctx.client_on_host():X}')
        ctx.console_on('', 'player.kill')
        ctx.wait_for('the fight holds the host', lambda: ctx.death_state('').get('status') == 'Party in combat', 15)
        started = time.monotonic()

        def respawned():
            if ctx.death_state('').get('down'):
                return False
            if (held := time.monotonic() - started) < HELD_AT_LEAST:
                raise ctx.failure(f'the host respawned after {held:.0f} s: the fight ended, the cap was not reached')
            return True

        ctx.wait_for('the host respawns while the guest still fights', respawned, HOLD_AND_RECOVERY)
    finally:
        ctx.console_on('client', 'tgm')
        ctx.unstage()
        ctx.scene(at='coc QASmoke', pin=False)  # the inn's references differ between the games: not this flow's check
