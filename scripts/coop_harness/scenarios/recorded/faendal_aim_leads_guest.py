"""Faendal (the host's NPC) fights the guest's copy with his bow while the guest strafes over a 60 ms line: his guess at the copy's place (Actor::GetPredictedPosition 47309, what his arrow is aimed at) must land within 35 units, median, of where the guest's game has the guest when the arrow, flown again there, arrives (docs/re/systems/combat-aim.md).

Recorded with the repro MCP (docs/design/repro-mcp.md)."""
# recorded: 20261114-024457-faendal-aim-leads-guest.jsonl, 15 calls (scripts/check_scenarios.py)
# Edited after export: a repeated scene and the vanilla comparison after the check dropped; the check (then aim_leads,
# a failing verb the export left out) put back as expect_aim_leads.
NAME = 'faendal-aim-leads-guest'
TAGS = ['combat', 'scene']
DOC = "Faendal (the host's NPC) fights the guest's copy with his bow while the guest strafes over a 60 ms line: his guess at the copy's place (Actor::GetPredictedPosition 47309, what his arrow is aimed at) must land within 35 units, median, of where the guest's game has the guest when the arrow, flown again there, arrives (docs/re/systems/combat-aim.md)."


def run(ctx):
    try:
        ctx.scene(save=None, at='coc Riverwood', pin=True)
        ctx.console_on('client', 'tgm')
        ctx.console_on('', 'player.moveto 1348C -400 -400 0')
        ctx.equip_weapon('client', 77495)
        ctx.provoke(npc='faendal')
        ctx.console_on('client', 'player.moveto 1348C 750 0 0')
        ctx.line_delay(ms=60, jitter_ms=0)
        ctx.expect_aim_leads(npc='faendal', seconds=16, strafe=2.0, delay_ms=60, within=35)
    finally:
        ctx.unstage()
