"""The guest's three-word shout hits the host lying in its ragdoll at 10 health: the host's game judges the shout's disintegrate effect with the guest as the player who cast it (GetIsID Player on the caster, JudgedFor's caster mode) and it kills the host (PR #113 review 2).

Recorded with the repro MCP (docs/design/repro-mcp.md)."""
# hand-written: shout-kills-lying-guest's verb the other way round (the guest shouts at the host), its full-health
# shout dropped: only the disintegrating one reads the caster's identity
NAME = 'shout-disintegrates-lying-host'
TAGS = ['combat', 'death', 'friendly-fire', 'magic']
DOC = "The guest's three-word shout hits the host lying in its ragdoll at 10 health: the host's game judges the shout's disintegrate effect with the guest as the player who cast it (GetIsID Player on the caster, JudgedFor's caster mode) and it kills the host (PR #113 review 2)."


def run(ctx):
    ctx.shout_at_lying_player(attacker='client', health=10)
