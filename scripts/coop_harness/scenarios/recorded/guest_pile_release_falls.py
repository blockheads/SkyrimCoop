"""The playtest of 2026-11-16: the guest's player carries the top cheese wheel of a stack through a pile of wheels, holds it over the stack and lets go. It must fall as in vanilla (20-37 units in 40 rounds); it hung in the air when the host's move row of a pile wheel's rest slept its island (docs/re/systems/physics.md, "When an island sleeps").

Recorded with the repro MCP (docs/design/repro-mcp.md)."""
# recorded: 20261116-203600-guest_pile_release_falls.jsonl, 2 calls (scripts/check_scenarios.py)
NAME = 'guest_pile_release_falls'
TAGS = ['mover', 'physics']
DOC = 'The playtest of 2026-11-16: the guest\'s player carries the top cheese wheel of a stack through a pile of wheels, holds it over the stack and lets go. It must fall as in vanilla (20-37 units in 40 rounds); it hung in the air when the host\'s move row of a pile wheel\'s rest slept its island (docs/re/systems/physics.md, "When an island sleeps").'


def run(ctx):
    try:
        ctx.expect_collision_like_vanilla(walker='client', kind='pile_carry_release')
    finally:
        ctx.unstage()
