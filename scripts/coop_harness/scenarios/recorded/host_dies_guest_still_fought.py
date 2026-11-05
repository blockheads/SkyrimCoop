"""The host dies in a fight the guest is also in: the NPC's target becomes the guest's copy and it closes in on a guest who
steps back, and the attack gives the copy no combat AI of its own (docs/re/systems/combat-targets.md)."""
# hand-written: understood through the MCP session first; the copy's form id changes each boot, a recording would keep one.

NAME = 'host-dies-guest-still-fought'
TAGS = ['combat', 'death', 'scene']
DOC = 'The host dies with an NPC on the guest too: it chases the guest, whose copy took no combat controller from the attack.'

ORGNAR = 0x13486
ROOM = 100000  # health: the blows land and the guest stays up


def run(ctx):
    copy_of_guest = None
    try:
        ctx.name_players()
        ctx.scene(save=None, at='coc RiverwoodSleepingGiantInn', pin=True)
        copy_of_guest = ctx.client_on_host()
        ctx.console_on('client', f'player.setav health {ROOM}')
        ctx.console_on('client', f'player.moveto {ORGNAR:X}')
        ctx.console_on('', f'{ORGNAR:X}.startcombat {copy_of_guest:X}')
        ctx.wait_for('a blow of the NPC lands on the guest', lambda: ctx.player_health('client') < ROOM - 1, 30)

        own = ctx.player_command('', {'cmd': 'combat_targets', 'actor': copy_of_guest})
        if own.get('behavior') or own.get('controller_target'):
            raise ctx.failure(f"the attack gave the guest's copy a combat tree of its own, on {own.get('controller_target'):X}: "
                              f"{own.get('behavior')[-1:]}")

        ctx.console_on('', 'player.kill')
        ctx.wait_for('the host is down', lambda: ctx.death_state('').get('down'), 15)
        target = lambda: ctx.player_command('', {'cmd': 'combat_targets', 'actor': ORGNAR}).get('controller_target')
        ctx.wait_for("the NPC fights the guest's copy", lambda: target() == copy_of_guest, 15)

        ctx.closes_in('orgnar', back_seconds=4.0, within=250.0, seconds=15.0)
        if ctx.death_state('').get('down') is not True:
            raise ctx.failure('the host came back while the guest still fought (death.md: the respawn waits)')
    finally:
        ctx.console_on('', f'{ORGNAR:X}.kill')
        ctx.wait_for('the host respawns once the fight is over', lambda: not ctx.death_state('').get('down'), 30)
        ctx.console_on('client', 'player.setav health 100')
        ctx.unstage()
        ctx.scene(at='coc QASmoke', pin=False)  # the inn's references differ between the games: not this flow's check
