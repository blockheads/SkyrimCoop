"""The party shares a horse: the host drives, the guest boards it with Activate and rides along seated on both games, the host drives them both, then gets off and the guest drives on.

Recorded with the repro MCP (docs/design/repro-mcp.md)."""
# recorded: 20261117-175049-party-shares-a-horse.jsonl, 32 calls (scripts/check_scenarios.py)
NAME = 'party-shares-a-horse'
TAGS = ['mounts', 'movement', 'ownership', 'scene']
DOC = 'The party shares a horse: the host drives, the guest boards it with Activate and rides along seated on both games, the host drives them both, then gets off and the guest drives on.'


def run(ctx):
    try:
        ctx.scene(save=None, at='cow Tamriel 0 -4', pin=False)
        ctx.console_on(instance='', command='player.placeatme 109E3D 1')
        ctx.find('Horse', 'horse')
        ctx.use('', 'horse')
        ctx.wait_seated(instance='')
        ctx.place_before(instance='client', target='horse', gap=130, bearing=90)  # beside it: head-on the crosshair takes the rider
        ctx.prompt_offers(instance='client', target='horse')  # hand-written: the ridden horse offers Ride (24716)
        ctx.watch_riders()  # hand-written: the playtest's T-pose as the guest got on
        ctx.use('client', 'horse')
        ctx.wait_seated(instance='client')
        ctx.passenger_seated(instance='client', horse='horse')
        ctx.riders_smooth(step='the guest gets on', snap=130.0)  # its snap to the mark is vanilla (76-106 measured)
        ctx.horse_owned_by(horse='horse', owner='host')
        ctx.drives(instance='', horse='horse')
        ctx.expect('horse', 'agree', within=5.0, seconds=3.0)
        ctx.riders_with_horse(horse='horse')  # hand-written: a seated copy's reference rides along (20318)
        ctx.passenger_seated(instance='client', horse='horse')
        ctx.watch_riders()  # hand-written: the playtest's warp as the host got off
        ctx.get_off(instance='')
        ctx.horse_owned_by(horse='horse', owner='guest')
        ctx.riders_smooth(step='the host gets off')
        # hand-written from here: the recording's failing steps and its debugging calls left out (the new driver steers
        # once its horse leaves the old driver's exit pair, docs/re/systems/player-mount.md)
        ctx.drives(instance='client', horse='horse')
        ctx.riders_with_horse(horse='horse')
        ctx.get_off(instance='client')
        ctx.horse_owned_by(horse='horse', owner='host')
    finally:
        ctx.remove('horse')
        ctx.unstage()
