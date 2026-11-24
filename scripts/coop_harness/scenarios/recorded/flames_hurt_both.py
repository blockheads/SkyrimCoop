"""Morvunskar's flame jets (1041C4-1041C6, plate 1041C7): the player in front of them burns on its own game, whoever steps on the plate; the guest's burn is the host's cast relayed to it (MagicTarget's world cast), its own copy of the cast (relayed as the trap's Spell.Cast, NativeReach kCast) only shows.

Recorded with the repro MCP (docs/design/repro-mcp.md)."""
# hand-written: from the recording below, its second tcai (a toggle back on) dropped
# recorded: 20261123-154300-flames-hurt-both.jsonl, 6 calls (scripts/check_scenarios.py)
NAME = 'flames_hurt_both'
TAGS = ['magic', 'scene', 'traps']
DOC = "Morvunskar's flame jets (1041C4-1041C6, plate 1041C7): the player in front of them burns on its own game, whoever steps on the plate; the guest's burn is the host's cast relayed to it (MagicTarget's world cast), its own copy of the cast (relayed as the trap's Spell.Cast, NativeReach kCast) only shows."


def run(ctx):
    try:
        ctx.scene(save=None, at='coc Morvunskar01', pin=True)
        ctx.console_on('', 'tcai')  # the mages here: no fights while the trap is watched
        ctx.expect_trap_hurts(who='guest', at=[-1345, -40, 230], away=[-1345, -500, 230], rounds=2, seconds=7, health=100, holder_at=[-1447, -82, 230])
        ctx.expect_trap_hurts(who='host', at=[-1345, -40, 230], away=[-1345, -500, 230], rounds=2, seconds=7, health=100, holder_at=[-1447, -82, 230])
    finally:
        ctx.console_on('', 'tcai')
        ctx.unstage()
