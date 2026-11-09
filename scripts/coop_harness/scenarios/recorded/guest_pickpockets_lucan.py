"""The guest pickpockets Lucan's copy: an uncaught take reaches the host once, a caught one raises the host's alarm and the party's bounty (docs/re/systems/crime.md, Pickpocket)."""
# recorded: 20261108-233517-guest-pickpockets-lucan.jsonl, 10 calls (scripts/check_scenarios.py)
# hand-written: Lucan's pockets are rolled (the recording's Amethyst): he gets two sweetrolls to take; the whole-inventory
# expect became the take's change in each game (a Honey Nut Treat already differed)
NAME = 'guest-pickpockets-lucan'
TAGS = ['crime', 'items']
DOC = "The guest pickpockets Lucan's copy: an uncaught take reaches the host once, a caught one raises the host's alarm and the party's bounty (docs/re/systems/crime.md, Pickpocket)."

GAMES = (("host", ""), ("guest", "client"))
WHITERUN_CRIME = '267EA'
LUCAN, SWEETROLL = 0x13487, 0x64B3D


def run(ctx):
    guest_rolls = ctx.item_count('client', 0x14, SWEETROLL) or 0
    lucan_rolls = ctx.container_items('', LUCAN).get(SWEETROLL, 0)
    rolls = lucan_rolls + 2
    try:
        ctx.name_players()
        ctx.stage({'npcs': [{'name': 'lucan', 'ref': '13487', 'frozen': True}]})
        held = lambda: {g: ctx.container_items(i, ctx.resolve('lucan', g)) for g, i in GAMES}
        ctx.console_on('', f'{LUCAN:X}.additem {SWEETROLL:X} 2')
        ctx.wait_for("Lucan carries the two sweetrolls in both games", lambda: all(h.get(SWEETROLL, 0) == rolls for h in held().values()), 10)
        before = held()
        ctx.pickpocket('client', 'lucan', 'Sweet Roll', False)

        def taken():
            now = held()
            return {g: {item: before[g].get(item, 0) - now[g].get(item, 0)
                        for item in set(before[g]) | set(now[g]) if before[g].get(item, 0) != now[g].get(item, 0)} for g in now}
        try:
            ctx.wait_for("the take leaves Lucan once, the same in both games",
                         lambda: (t := taken())["host"] == t["guest"] and sum(t["host"].values()) == 1, 5)
        except Exception:
            raise ctx.failure(f"lucan: the take changed {taken()} (host vs guest)") from None
        ctx.expect('guest_player', 'items', within=5.0, seconds=0.0)
        ctx.pickpocket('client', 'lucan', 'Sweet Roll', True)
        ctx.expect(WHITERUN_CRIME, 'bounty', within=5.0, value=[1], seconds=5.0)
    finally:
        if (extra := (ctx.item_count('client', 0x14, SWEETROLL) or 0) - guest_rolls) > 0:
            ctx.console_on('client', f'player.removeitem {SWEETROLL:X} {extra}')
        if (left := ctx.container_items('', LUCAN).get(SWEETROLL, 0) - lucan_rolls) > 0:
            ctx.console_on('', f'{LUCAN:X}.removeitem {SWEETROLL:X} {left}')
        ctx.crime_command('', 'reset_alarm', actor=LUCAN)
        ctx.crime_command('', 'set', faction=int(WHITERUN_CRIME, 16), violent=0, nonviolent=0)
        ctx.unstage()
