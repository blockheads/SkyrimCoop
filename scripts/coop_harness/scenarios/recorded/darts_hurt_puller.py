"""Playtest 2026-11-22: a wrong pull of Bleak Falls Barrow's pillar lever (AA8C3) fires the darts (the host's ports) and they hurt the player who pulled it on its own game, the guest as the host: the guest's darts are the host's, relaunched from its own ports (WorldProjectileRequest). The ports fire at random, so each pull need not hit; three pulls must.

Recorded with the repro MCP (docs/design/repro-mcp.md)."""
# hand-written: from the recording below, least=1 added (the ports fire at random: a pull need not hit)
# recorded: 20261123-152940-darts-hurt-puller.jsonl, 5 calls (scripts/check_scenarios.py)
NAME = 'darts_hurt_puller'
TAGS = ['combat', 'scene', 'traps']
DOC = "Playtest 2026-11-22: a wrong pull of Bleak Falls Barrow's pillar lever (AA8C3) fires the darts (the host's ports) and they hurt the player who pulled it on its own game, the guest as the host: the guest's darts are the host's, relaunched from its own ports (WorldProjectileRequest). The ports fire at random, so each pull need not hit; three pulls must."


def run(ctx):
    try:
        ctx.scene(save=None, at='coc BleakFallsBarrow01', pin=True)
        ctx.console_on('', '39FE1.kill')  # a draugr in the room wakes and fights whoever stands here
        ctx.expect_trap_hurts(who='guest', at=[5000, -1050, -767], away=[5000, -500, -754], rounds=3, use='AA8C3', seconds=4, health=100, least=1)
        ctx.expect_trap_hurts(who='host', at=[5000, -1050, -767], away=[5000, -500, -754], rounds=3, use='AA8C3', seconds=4, health=100, least=1)
    finally:
        ctx.unstage()
