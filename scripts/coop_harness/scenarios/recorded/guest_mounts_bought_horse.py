"""The host buys Skulvar's horse: the guest's copy keeps the horse's name, its crosshair takes it, and the guest gets on it.

Recorded with the repro MCP (docs/design/repro-mcp.md)."""
# recorded: 20261123-214813-guest-mounts-bought-horse.jsonl, 11 calls (scripts/check_scenarios.py)
from ..ownership import PLAYER_FACTION, owner

NAME = 'guest-mounts-bought-horse'
TAGS = ['dialogue', 'mounts', 'ownership', 'scene']
DOC = "The host buys Skulvar's horse: the guest's copy keeps the horse's name, its crosshair takes it, and the guest gets on it."


def run(ctx):
    try:
        ctx.scene(save=None, at='player.moveto 1A69C', pin=True)
        ctx.set_party_gold(count=2000)
        ctx.talk('', 'skulvar_sable_hilt', ['Can I buy a horse?', "Here's my offer"])
        ctx.leave_dialogue(instance='')
        # hand-written: the handle is the horse bought (the party's), and the guest's copy shows the host's name for it
        horse = ctx.resolve('horse', '')
        ctx.wait_for("[host] the horse is the party's", lambda: owner(ctx, '', horse) == PLAYER_FACTION, 10)
        named = lambda: {game: ctx.player_command(game, {"cmd": "name_data", "ref": ctx.resolve('horse', game)}).get("name")
                         for game in ('', 'client')}
        ctx.wait_for("the guest's copy shows the bought horse's name", lambda: len(set(named().values())) == 1 and
                     named()[''] != 'Horse', 10, explain=lambda: f"names {named()}")
        ctx.place_before(instance='client', target='horse', gap=130)
        ctx.expect('horse', 'crosshair', within=5.0, game='guest', seconds=0.0)
        # hand-written: the get-on; getting off this horse is bugs.md's "A rider at a scenario's end"
        ctx.use('client', 'horse')
        ctx.wait_seated('client')
    finally:
        ctx.unstage()
