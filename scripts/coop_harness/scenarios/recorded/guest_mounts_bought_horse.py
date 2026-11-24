"""The host buys Skulvar's horse: the guest's copy keeps the horse's name, its crosshair takes it, and the guest gets on it.

Recorded with the repro MCP (docs/design/repro-mcp.md)."""
# recorded: 20261123-214813-guest-mounts-bought-horse.jsonl, 11 calls (scripts/check_scenarios.py)
NAME = 'guest-mounts-bought-horse'
TAGS = ['dialogue', 'mounts', 'ownership', 'scene']
DOC = "The host buys Skulvar's horse: the guest's copy keeps the horse's name, its crosshair takes it, and the guest gets on it."


def run(ctx):
    try:
        ctx.scene(save=None, at='player.moveto 1A69C', pin=True)
        ctx.set_party_gold(count=2000)
        ctx.talk('', 'skulvar_sable_hilt', ['Can I buy a horse?', "Here's my offer"])
        ctx.leave_dialogue(instance='')
        ctx.place_before(instance='client', target='horse', gap=130)
        ctx.expect('horse', 'crosshair', within=5.0, game='guest', seconds=0.0)
        # hand-written: the get-on and off (the recorded ride_once failed once getting off, bugs.md "A rider at a
        # scenario's end"); off, the baseline holds
        ctx.use('client', 'horse')
        ctx.wait_seated('client')
        ctx.get_off('client')
    finally:
        ctx.unstage()
