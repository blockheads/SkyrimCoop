"""The guest pickpockets a leveled bandit chief's copy (LvlBanditBoss 3DF17, a temporary NPC in each game): the sweetroll is stolen on the guest's own game, owned by the leveled form every game shares (ExtraLeveledCreature's original base, 11810; docs/re/systems/inventory-changes.md, "What 16059's target does").

Recorded with the repro MCP (docs/design/repro-mcp.md)."""
# recorded: 20261119-145230-guest-pickpockets-leveled-npc-stolen.jsonl, 8 calls (scripts/check_scenarios.py)
# hand-written: the bandit's id is this run's (the recording's FF000859); the two form lookups made while recording
# and the readout after the expect are dropped; the guest's sweetrolls go after, as in guest-container-theft-is-stolen
NAME = 'guest-pickpockets-leveled-npc-stolen'
TAGS = ['crime', 'items']
DOC = 'The guest pickpockets a leveled bandit chief\'s copy (LvlBanditBoss 3DF17, a temporary NPC in each game): the sweetroll is stolen on the guest\'s own game, owned by the leveled form every game shares (ExtraLeveledCreature\'s original base, 11810; docs/re/systems/inventory-changes.md, "What 16059\'s target does").'


def run(ctx):
    try:
        ctx.stage({'npcs': [{'name': 'bandit', 'base': '3DF17', 'frozen': True}]})
        ctx.console_on('', f"{ctx.resolve('bandit', 'host'):X}.additem 64B3D 2")
        ctx.pickpocket('client', 'bandit', 'Sweet Roll', False)
        ctx.expect('64B3D', 'stolen', within=5.0, game='guest', value=[1], of='3DF17', seconds=5.0)  # by hand: owned by LvlBanditBoss
    finally:
        ctx.console_on('client', 'player.removeitem 64B3D 99')
        ctx.unstage()
