"""A host's wolf fights the host's player: on the guest, the wolf's copy and the host's player's copy show their owner's stamina regen rate, the combat one (fCombatStaminaRegenRateMult 0.35). They showed the out-of-combat rate (wolf 10.25 vs 3.59, player 5.0 vs 1.75): Actor::GetActorValueRegenRate (38460) asks IsInCombat, which a copy answered with its own (no combat controller).

Recorded with the repro MCP (docs/design/repro-mcp.md)."""
# recorded: 20261123-195232-copy-regen-rate-in-combat.jsonl, 5 calls (scripts/check_scenarios.py)
NAME = 'copy-regen-rate-in-combat'
TAGS = ['combat', 'values']
DOC = "A host's wolf fights the host's player: on the guest, the wolf's copy and the host's player's copy show their owner's stamina regen rate, the combat one (fCombatStaminaRegenRateMult 0.35). They showed the out-of-combat rate (wolf 10.25 vs 3.59, player 5.0 vs 1.75): Actor::GetActorValueRegenRate (38460) asks IsInCombat, which a copy answered with its own (no combat controller)."


def run(ctx):
    try:
        ctx.name_players()
        ctx.stage({'npcs': [{'name': 'wolf', 'base': '23ABE', 'frozen': False}], 'players': {'host': {'near': 'wolf', 'gap': 150}}})
        ctx.fight('', 'wolf', 1, False)
        ctx.expect('wolf', 'actor', within=5.0, field='values.stamina_regen.rate', seconds=3.0)
        ctx.expect('host_player', 'actor', within=5.0, field='values.stamina_regen.rate', seconds=3.0)
    finally:
        ctx.unstage()
