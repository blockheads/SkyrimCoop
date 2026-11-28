"""Bleak Falls Barrow's word wall teaches the party and enables the draugr's trigger whoever looks: the guest alone; the guest in first looking away, then the host looking (the 2026-11-26 playtest, where nothing happened until a reload); the host alone. The host's wall script asks Actor.HasLOS of the guest's character, which only the guest's camera can answer (GuestSight), and the wall's timer loop asks the party.

Recorded with the repro MCP (docs/design/repro-mcp.md)."""
# recorded: 20261127-215304-guest-reads-word-wall.jsonl, 19 calls (scripts/check_scenarios.py)
NAME = 'guest-reads-word-wall'
TAGS = ['own-checks', 'scene']
DOC = "Bleak Falls Barrow's word wall teaches the party and enables the draugr's trigger whoever looks: the guest alone; the guest in first looking away, then the host looking (the 2026-11-26 playtest, where nothing happened until a reload); the host alone. The host's wall script asks Actor.HasLOS of the guest's character, which only the guest's camera can answer (GuestSight), and the wall's timer loop asks the party."


def run(ctx):
    try:
        ctx.scene(save=None, at='coc BleakFallsBarrow02', pin=True)
        ctx.checkpoint('wall-before')
        ctx.place_before(instance='client', target=461268, gap=150)
        ctx.expect_word_wall_read(word=81442, enables=299959, seconds=15)
        ctx.retry('wall-before')
        ctx.place_before(instance='client', target=461268, gap=290, bearing=90)
        ctx.face(instance='client', target='draugr')
        ctx.expect_wall_waits(instance='client', look_target=461268, word=81442)  # edited: the recorded readouts, asserted
        ctx.place_before(instance='', target=461268, gap=220)
        ctx.expect_word_wall_read(word=81442, enables=299959, seconds=15)
        ctx.retry('wall-before')
        ctx.place_before(instance='', target=461268, gap=220)
        ctx.expect_word_wall_read(word=81442, enables=299959, seconds=15)
    finally:
        ctx.unstage()
