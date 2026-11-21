"""A guest casts Conjure Familiar: the host's game makes the one familiar (commanded by its copy of the guest), the guest shows its copy, and they agree, the familiar's spectral shader (3B6CB) included. Before one-simulator.md piece 7 the guest's own engine made a familiar it held without 3D and the host made none (bugs.md 'A guest's summon'); then the copy lacked its owner's shader, played before its 3D and never again (summons.md).

Recorded with the repro MCP (docs/design/repro-mcp.md)."""
# recorded: 20261121-011548-guest-summons-familiar.jsonl, 6 calls, the commander's check and the players' names added (PR #88 review) (scripts/check_scenarios.py)
NAME = 'guest-summons-familiar'
TAGS = ['magic', 'summon']
DOC = "A guest casts Conjure Familiar: the host's game makes the one familiar (commanded by its copy of the guest), the guest shows its copy, and they agree, the familiar's spectral shader (3B6CB) included. Before one-simulator.md piece 7 the guest's own engine made a familiar it held without 3D and the host made none (bugs.md 'A guest's summon'); then the copy lacked its owner's shader, played before its 3D and never again (summons.md)."


def run(ctx):
    try:
        ctx.name_players()
        ctx.give_spell(instance='client', spell=409782)
        ctx.cast_spell(instance='client', target=None, spell=409782)
        ctx.find('Familiar', 'familiar')
        ctx.expect('familiar', 'agree', within=5.0, seconds=0.0)
        ctx.expect_commanded('familiar', 'guest_player')
        ctx.expect('familiar', 'guest_npcs', within=5.0, seconds=5.0)
    finally:
        ctx.unstage()
