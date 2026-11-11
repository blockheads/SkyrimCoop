"""A guest pickpockets Alvor's sweet roll unseen (it leaves the host's Alvor and the copy), then is caught on a second try: the party's bounty and its HUD line reach both games and Alvor goes for the nearest player, the guest (docs/re/systems/crime.md, "A guest's pickpocket").

Recorded with the repro MCP (docs/design/repro-mcp.md)."""
# recorded: 20261110-184104-guest-pickpocket.jsonl, 37 calls; the earlier session's staging, probes of the menu's list path and reads taken out, the raw console lines made the stock verb, the unseen take's count check added (scripts/check_scenarios.py)
NAME = 'guest-pickpocket'
TAGS = ['crime']
DOC = 'A guest pickpockets Alvor\'s sweet roll unseen (it leaves the host\'s Alvor and the copy), then is caught on a second try: the party\'s bounty and its HUD line reach both games and Alvor goes for the nearest player, the guest (docs/re/systems/crime.md, "A guest\'s pickpocket").'

SWEET_ROLL = '64B3D'


def run(ctx):
    try:
        ctx.name_players()
        ctx.report_crimes()
        ctx.stage({'npcs': [{'name': 'victim', 'base': '13475', 'frozen': False}], 'players': {'guest': {'near': 'victim', 'gap': 200}, 'host': {'near': 'victim', 'gap': 900}}})
        held = ctx.stock(npc='victim', item=SWEET_ROLL)['holds']
        ctx.pickpocket(instance='client', target='victim', item=SWEET_ROLL, name='Sweet Roll')
        ctx.expect_holds('victim', SWEET_ROLL, held - 1)
        ctx.stock(npc='victim', item=SWEET_ROLL)
        before = ctx.bounty_total()
        mark = ctx.hud_mark()
        ctx.pickpocket(instance='client', target='victim', item=SWEET_ROLL, name='Sweet Roll', caught=True)
        ctx.expect_bounty(at_least=before + 1, seconds=10)
        ctx.expect_bounty_message(mark=mark, seconds=5)
        ctx.expect_target(npc='victim', player='guest_player', seconds=10)
    finally:
        ctx.unstage()
