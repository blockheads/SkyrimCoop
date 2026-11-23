"""Playtest 2026-11-22 crash 3: a placed leveled wolf (DC8E3, LvlAnimalCanyonPredator) the guest's game re-rolls to another animal (level 50, RecycleActor: 37323) takes the host's pick (EncWolf 23ABE) as a new temporary base, and a second guest re-roll (which destroys the current base, GC 36460) leaves EncWolf alive: the host's next announcement still applies. Before the fix the copy got the EncWolf record itself and the re-roll destroyed it.

Recorded with the repro MCP (docs/design/repro-mcp.md)."""
# recorded: 20261123-122012-guest-releveled-copy-keeps-pick.jsonl, 15 calls (scripts/check_scenarios.py)
NAME = 'guest-releveled-copy-keeps-pick'
TAGS = ['copies', 'crash', 'leveled', 'scene']
DOC = "Playtest 2026-11-22 crash 3: a placed leveled wolf (DC8E3, LvlAnimalCanyonPredator) the guest's game re-rolls to another animal (level 50, RecycleActor: 37323) takes the host's pick (EncWolf 23ABE) as a new temporary base, and a second guest re-roll (which destroys the current base, GC 36460) leaves EncWolf alive: the host's next announcement still applies. Before the fix the copy got the EncWolf record itself and the re-roll destroyed it."


ENC_WOLF, WOLF = 0x23ABE, 0xDC8E3  # the host's pick at level 1; the placed LvlAnimalCanyonPredator


def run(ctx):
    try:
        ctx.scene(save=None, at='player.moveto DC8E3', pin=True)
        # The guest's game re-rolls its copy at level 50 (RecycleActor: 37323): another animal than the host's.
        ctx.console_on('client', 'player.setlevel 50')
        ctx.console_on('client', 'DC8E3.recycleactor')
        picked = ctx.player_command('client', {'cmd': 'npc_templates', 'actor': WOLF})
        assert picked.get('selected') not in (None, ENC_WOLF), f"the guest's re-roll picked {picked.get('selected')}, not another animal"
        # The host's re-roll announces it again: the copy takes EncWolf as a new temporary base.
        ctx.console_on('', 'DC8E3.recycleactor')
        ctx.expect('wolf', 'actor', within=5.0, game='guest', value=ENC_WOLF, field='identity.template_base', seconds=10.0)
        # The guest's reset destroys the copy's current base (GC 36460); EncWolf must outlive it.
        ctx.console_on('client', 'DC8E3.recycleactor')
        ctx.console_on('', 'DC8E3.recycleactor')
        ctx.expect('wolf', 'actor', within=5.0, game='guest', value=ENC_WOLF, field='identity.template_base', seconds=10.0)
        ctx.expect('wolf', 'actor', within=5.0, game='guest', value=True, field='identity.temporary_base', seconds=5.0)
        ctx.expect_no_double_free(instance='client')
    finally:
        ctx.unstage()
