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
        watched = {name: actors[str(npc)] for npc, name in NPCS.items() if str(npc) in actors}
        print('copies:', {name: {key: actor[key] for key in ('copy', 'frames', 'furniture', 'furniture_path', 'furniture_reversals')} | actor['place']
                          for name, actor in watched.items()})
        # Only a copy seen moving in furniture shows anything: walking never had this bug.
        shown = {name: actor for name, actor in watched.items() if actor['copy'] and actor['frames'] >= FRAMES // 2 and
                 actor['furniture'] >= MOST_REVERSALS and actor['furniture_path'] >= 1}
        if not shown:
            raise ctx.failure(f'no copy was seen moving in furniture: nothing was shown ({list(watched)})')
        shaking = {name: actor['place']['reversals'] for name, actor in shown.items() if actor['place']['reversals'] > MOST_REVERSALS}
        if shaking:
            raise ctx.failure(f'copies shook, frames turned back: {shaking}')
    finally:
        ctx.unstage()
