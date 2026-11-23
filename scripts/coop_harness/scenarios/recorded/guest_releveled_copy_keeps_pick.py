"""Playtest 2026-11-22 crash 3: a placed leveled wolf (DC8E3, LvlAnimalCanyonPredator) the guest's game re-rolls to another animal (level 50, RecycleActor: 37323) takes the host's pick (EncWolf 23ABE) as a new temporary base, and a second guest re-roll (which destroys the current base, GC 36460) leaves EncWolf alive: the host's next announcement still applies. Before the fix the copy got the EncWolf record itself and the re-roll destroyed it.

Recorded with the repro MCP (docs/design/repro-mcp.md)."""
# recorded: 20261123-122012-guest-releveled-copy-keeps-pick.jsonl, 15 calls (scripts/check_scenarios.py)
NAME = 'guest-releveled-copy-keeps-pick'
TAGS = ['copies', 'crash', 'leveled', 'scene', 'own-checks']  # the wilds around: animals walk
DOC = "Playtest 2026-11-22 crash 3: a placed leveled wolf (DC8E3, LvlAnimalCanyonPredator) the guest's game re-rolls to another animal (level 50, RecycleActor: 37323) takes the host's pick (EncWolf 23ABE) as a new temporary base, and a second guest re-roll (which destroys the current base, GC 36460) leaves EncWolf alive: the host's next announcement still applies. Before the fix the copy got the EncWolf record itself and the re-roll destroyed it."


WOLF = 0xDC8E3  # a placed LvlAnimalCanyonPredator (EncWolf in Fabien's games)


def guest_base(ctx):
    return (ctx.actor_state('client', WOLF).get('identity') or {}).get('base', 0)


def reroll(ctx, game):
    # On the guest the console's re-roll drops a copy's 3D at once: between world updates, not under its movement job.
    if game == 'client':
        ctx.freeze_world(True)
    ctx.console_on(game, 'DC8E3.recycleactor')
    if game == 'client':
        ctx.freeze_world(False)


def shows_copy(ctx):
    # The host's re-roll announces DC8E3 under a new session id: the guest's copy is followed by it (refresh_handles).
    ctx.wait_for("the guest shows DC8E3's copy", lambda: ctx.refresh_handles() or 'guest' in ctx.handles()['dc8e3'], 15)


def run(ctx):
    try:
        # RecycleActor sends the wolf home (cell 95F5): it starts there, alive, and fights nobody (tcai, both games).
        ctx.console_on('', 'DC8E3.resurrect')
        reroll(ctx, '')
        ctx.scene(save=None, at='player.moveto DC8E3', pin=True)
        ctx.console_on('', 'tcai')
        ctx.console_on('client', 'tcai')
        ctx.wait_for("DC8E3 and its copy are in 3D", lambda: any(pair[0] == WOLF for pair in ctx.copy_pairs().values()), 30)
        ctx.name_handle('dc8e3', {'host': WOLF, 'guest': WOLF})  # a placed reference: one id in both games
        # The guest's game re-rolls its copy at level 50 (RecycleActor: 37323): another animal than the host's.
        ctx.console_on('client', 'player.setlevel 50')
        # Staging: the roll is random, so the guest re-rolls until its pick differs from the host's.
        for _ in range(8):
            reroll(ctx, 'client')
            picks = {game: ctx.player_command(game, {'cmd': 'npc_templates', 'actor': WOLF}).get('selected', 0) for game in ('', 'client')}
            if all(picks.values()) and picks[''] != picks['client']:
                break
        assert all(picks.values()) and picks[''] != picks['client'], f"the games' picks {picks} are not two animals"
        # The host's re-roll announces it again: the copy takes the host's pick as a new temporary base.
        reroll(ctx, '')
        shows_copy(ctx)
        ctx.expect('dc8e3', 'actor', within=5.0, value=None, field='identity.template_base', seconds=10.0)
        # The guest's reset destroys the copy's current base (GC 36460); the host's pick must outlive it.
        before = guest_base(ctx)
        reroll(ctx, 'client')
        assert before, "the guest's DC8E3 has no base before its reset"
        ctx.wait_for(f"the guest's reset gives DC8E3 a new base (was {before:X})",
                     lambda: guest_base(ctx) not in (0, before), 10, explain=lambda: f"base {guest_base(ctx):X}")
        reroll(ctx, '')
        shows_copy(ctx)
        ctx.expect('dc8e3', 'actor', within=5.0, value=None, field='identity.template_base', seconds=10.0)
        ctx.expect('dc8e3', 'actor', within=5.0, game='guest', value=True, field='identity.temporary_base', seconds=5.0)
        ctx.expect_no_double_free(instance='client')
    finally:
        ctx.unstage()
