"""An NPC leaning on a wall leaves it: its copy on the guest's game moves with it, never shaking back and forth.

docs/re/systems/actor-movement.md, "A copy in furniture"."""
# hand-written: the bug is understood; the check reads the jitter readout, which a recording cannot replay.
NAME = 'copy-leaves-furniture-steady'
TAGS = ['movement', 'scene']
DOC = 'Embry leaves his wall lean in Riverwood; his copy on the guest never turns back on itself frame by frame.'

# Embry (his wall lean), Frodnar and Hod: Riverwood's NPCs seen shaking.
NPCS = {0x3550C: 'Embry', 0x1348B: 'Frodnar', 0x1348A: 'Hod'}
FRAMES = 1200  # about 20 s at 60 fps, for each to leave its furniture and take the next
MOST_REVERSALS = 10  # the bug: 55 to 264


def run(ctx):
    try:
        ctx.scene(at='coc Riverwood', pin=False)
        for game in ('', 'client'):
            ctx.console_on(game, 'player.moveto 3550C')
        ctx.player_command('client', {'cmd': 'jitter', 'start': True})
        for npc in NPCS:
            ctx.console_on('', f'{npc:X}.resetai')
        ctx.wait_for('the guest to watch them', lambda: ctx.player_command('client', {'cmd': 'jitter'})['frames'] >= FRAMES, 60,
                     _game=ctx.game('client'))
        actors = ctx.player_command('client', {'cmd': 'jitter', 'stop': True})['actors']
        places = {name: actors[str(npc)]['place'] for npc, name in NPCS.items()}
        print('copy places:', places)
        if all(place['path'] < 5 for place in places.values()):
            raise ctx.failure(f'no copy moved ({places}): nothing was shown')
        shaking = {name: place['reversals'] for name, place in places.items() if place['reversals'] > MOST_REVERSALS}
        if shaking:
            raise ctx.failure(f'copies shook, frames turned back: {shaking}')
    finally:
        ctx.unstage()
