"""The host buys Skulvar's horse: the guest's copy keeps the horse's name, its crosshair takes it, and the guest gets on it.

Recorded with the repro MCP (docs/design/repro-mcp.md)."""
# recorded: 20261123-214813-guest-mounts-bought-horse.jsonl, 11 calls (scripts/check_scenarios.py)
from ..ownership import BUY_HORSE_TOPIC, BUY_HORSE_YES_TOPIC, PLAYER_FACTION, PLAYER_HORSE_FACTION, SKULVAR, owner, talk

NAME = 'guest-mounts-bought-horse'
TAGS = ['dialogue', 'mounts', 'ownership', 'scene']
DOC = "The host buys Skulvar's horse: the guest's copy keeps the horse's name, its crosshair takes it, and the guest gets on it."


def run(ctx):
    horse = sold_by = None
    try:
        ctx.scene(save=None, at='player.moveto 1A69C', pin=True)
        sold_by = owner(ctx, '', ctx.resolve('horse', ''))  # hand-written: the stable's again afterwards (buys_horse)
        ctx.set_party_gold(count=2000)
        # hand-written: the purchase as buys_horse makes it (the recorded talk's aim missed Skulvar standing on him)
        talk(ctx, '', SKULVAR)
        ctx.pick_with_keys('', BUY_HORSE_TOPIC)
        ctx.wait_for("[host] the menu shows the answers", lambda: ctx.lists_topic('', BUY_HORSE_YES_TOPIC), 30)
        ctx.pick_with_keys('', BUY_HORSE_YES_TOPIC)
        ctx.close_dialogues()
        # hand-written: the handle is the horse bought (the party's), and the guest's copy shows the host's name for it
        horse = ctx.resolve('horse', '')
        ctx.wait_for("[host] the horse is the party's", lambda: owner(ctx, '', horse) == PLAYER_FACTION, 10)
        named = lambda: {game: ctx.player_command(game, {"cmd": "name_data", "ref": ctx.resolve('horse', game)}).get("name")
                         for game in ('', 'client')}
        ctx.wait_for("the guest's copy shows the bought horse's name", lambda: len(set(named().values())) == 1 and
                     named()[''] != 'Horse', 10, explain=lambda: f"names {named()}")
        ctx.place_before(instance='client', target='horse', gap=130)
        ctx.expect('horse', 'crosshair', within=5.0, game='guest', seconds=0.0)
        # hand-written: the get-on; left on (no room to get off inside the shed: mounts.md)
        ctx.use('client', 'horse')
        ctx.wait_seated('client')
    finally:
        if horse is not None:  # hand-written: for sale again, as buys_horse leaves it
            ctx.player_command('', {"cmd": "crime", "action": "set_owner", "target": horse, "owner": sold_by or 0})
            ctx.console_on('', f"{horse:08X}.removefromfaction {PLAYER_HORSE_FACTION:X}")
        ctx.unstage()
