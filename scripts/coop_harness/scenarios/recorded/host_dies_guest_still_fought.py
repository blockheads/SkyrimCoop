"""The host dies in a fight the guest is also in: the attack gives the guest's copy no combat AI of its own, and the NPC's
target moves from the dead host to the copy (docs/re/systems/combat-targets.md). Whether it then closes in rests on the
engine's detection, which in this dim inn loses the real player as often as the copy: not checked here."""
# hand-written: understood through the MCP session first; the copy's form id changes each boot, a recording would keep one.

NAME = 'host-dies-guest-still-fought'
TAGS = ['combat', 'death', 'scene']
DOC = "The host dies with an NPC on the guest too: the NPC turns on the guest's copy, which took no combat controller from the attack."

ORGNAR = 0x13486


def run(ctx):
    copy_of_guest = None
    host_god = False
    try:
        ctx.name_players()
        ctx.scene(save=None, at='coc RiverwoodSleepingGiantInn', pin=True)
        copy_of_guest = ctx.client_on_host()
        ctx.console_on('', f'{ORGNAR:X}.resurrect')
        ctx.console_on('client', 'tgm')
        ctx.console_on('', 'tgm')  # the host stands until it is killed on purpose
        host_god = True
        ctx.console_on('', f'player.moveto {ORGNAR:X} 80 0 0')
        ctx.console_on('client', f'player.moveto {ORGNAR:X} -80 0 0')
        combat = lambda: ctx.player_command('', {'cmd': 'combat_targets', 'actor': ORGNAR})
        ctx.console_on('', f'{ORGNAR:X}.startcombat 14')
        ctx.wait_for('the NPC fights the host', lambda: combat().get('controller_target') == 0x14, 15)

        # The copy assaulted back: the console's StartCombat runs the same path as an attack (ActorProcess 39359,
        # AIProcess::StartAssault, then Actor::StartCombat 38561), which HookStartCombat refuses and counts.
        refused = lambda: ctx.counter('', 'copy combat refused')
        before = refused()
        ctx.console_on('', f'{copy_of_guest:X}.startcombat {ORGNAR:X}')
        ctx.wait_for("the assault reaches the copy's StartCombat (38561), refused", lambda: refused() > before, 15)
        own = ctx.player_command('', {'cmd': 'combat_targets', 'actor': copy_of_guest})
        if own.get('behavior') or own.get('controller_target'):
            raise ctx.failure(f"the assault gave the guest's copy a combat tree of its own, on {own.get('controller_target'):X}: "
                              f"{own.get('behavior')[-1:]}")

        ctx.console_on('', f'{ORGNAR:X}.startcombat {copy_of_guest:X}')
        candidates = lambda: {c['actor'] for c in combat().get('candidates', [])}
        ctx.wait_for("both players are the NPC's targets", lambda: {0x14, copy_of_guest} <= candidates(), 15)
        ctx.console_on('', 'tgm')
        host_god = False
        ctx.console_on('', 'player.kill')
        ctx.wait_for('the host is down', lambda: ctx.death_state('').get('down'), 15)
        ctx.wait_for("the NPC's target is the guest's copy, the host dead",
                     lambda: combat().get('controller_target') == copy_of_guest, 15)
        if ctx.death_state('').get('down') is not True:
            raise ctx.failure('the host came back while the guest still fought (death.md: the respawn waits)')
    finally:
        if host_god:
            ctx.console_on('', 'tgm')
        ctx.console_on('', f'{ORGNAR:X}.stopcombat')
        ctx.wait_for('the host respawns once the fight is over', lambda: not ctx.death_state('').get('down'), 30)
        ctx.console_on('client', 'tgm')
        ctx.unstage()
        ctx.scene(at='coc QASmoke', pin=False)  # the inn's references differ between the games: not this flow's check
