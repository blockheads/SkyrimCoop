"""Alternate Start's ship start (ARTHLALRumorsOfWarQuest stage 1, the Solitude choice) dresses the player with Player.EquipItem (Actor.EquipItem 54661 adds a missing item past AddInventoryItem): every player gets the fine clothes and wears them, not only the host.

Recorded with the repro MCP (docs/design/repro-mcp.md)."""
# recorded: 20261123-234044-quest-dresses-every-player.jsonl, 11 calls (scripts/check_scenarios.py)
NAME = 'quest-dresses-every-player'
TAGS = ['items', 'mods', 'party-items', 'restart']
DOC = "Alternate Start's ship start (ARTHLALRumorsOfWarQuest stage 1, the Solitude choice) dresses the player with Player.EquipItem (Actor.EquipItem 54661 adds a missing item past AddInventoryItem): every player gets the fine clothes and wears them, not only the host."


def run(ctx):
    with ctx.loads_mods():  # by hand: recorded on a duo booted with --plugins (duo_start plugins=...)
        try:
            # by hand: the recording's first setstage ran no branch (the start menu leaves the choice at -1) and its
            # item_stacks reads are dropped
            ctx.console_on('', 'setpqv ARTHLALChargenQuest ImmigrationMenuChoice 0')  # the statue's Solitude ship
            ctx.console_on('', 'resetquest ARTHLALRumorsOfWarQuest')
            ctx.console_on('', 'setstage ARTHLALRumorsOfWarQuest 1')
            # ClothesFineClothes01, ClothesFineBoots01, ClothesFineHat02
            ctx.expect('guest', 'wears', within=5.0, value=['86991', '86993', 'CEE84'], seconds=10.0)
            ctx.arrives_with_party('client')  # by hand: the start moved the host to Solitude's docks
            ctx.both_go('QASmoke')  # by hand: the party back where the baseline compares it
        finally:
            ctx.unstage()
