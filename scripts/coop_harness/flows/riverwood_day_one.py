"""riverwood-day-one: a party's first stretch in Riverwood, from the Dragonslayer late save. A Lovely Letter from start to
reward (Faendal, Camilla in the Riverwood Trader, Faendal again), then Embershard Mine: the lookout outside, in through
the door, the first bandit, his loot, and out. Ids and lines from the MCP play of 2026-11-05
(docs/design/playthrough-flows.md)."""
from .flow import Step, carries, quest_at, same_items, together

NAME = "riverwood-day-one"
DOC = __doc__.split("\n\n")[0].replace("\n", " ")
BOOT = {"save": "LateAfterDragonslayerBackinSkyrim"}

QUEST, LETTER = 0x74A83, 0x5C846  # FreeformRiverwood01 "A Lovely Letter"; Faendal's Fake Letter from Sven
TRADER_OUT, TRADER_IN = 0x1341F, 0x133E9  # the Riverwood Trader's front door, outside and in
MINE_OUT, MINE_IN = 0xB6CCE, 0xB6CCD  # Embershard Mine's entrance, outside and in
STEEL_SWORD = 0x13989
INSIDE = 8000.0  # the whole mine: clear fights who it can walk to, nearest along the way


def equip_party(ctx):
    """Both players able to win a fight (the save's character has 100 health): a steel sword and more health."""
    for game in ("", "client"):
        ctx.console_on(game, f"player.additem {STEEL_SWORD:X} 1")
        ctx.console_on(game, f"player.equipitem {STEEL_SWORD:X}")
        ctx.console_on(game, "player.modav health 400")


def arrive(ctx):
    equip_party(ctx)
    ctx.scene(at="coc Riverwood")


def faendal(ctx):
    ctx.find("Faendal", "faendal")
    ctx.go_to("", "faendal", 150.0)
    ctx.go_to("client", "faendal", 250.0)
    ctx.talk("", "faendal", ["problem with Sven", "hope"])  # by text: an index replays whatever sits there
    ctx.close_dialogues()


def trader(ctx):
    ctx.door("", f"{TRADER_OUT:X}")
    ctx.find("Camilla", "camilla")
    ctx.talk("", "camilla", ["a letter for you, from Sven"])
    ctx.close_dialogues()


def reward(ctx):
    ctx.door("", f"{TRADER_IN:X}")
    ctx.find("Faendal", "faendal")
    ctx.go_to("", "faendal", 150.0)
    ctx.go_to("client", "faendal", 250.0)  # the party walks over together
    ctx.talk("", "faendal", ["spending any more time with Sven"])
    ctx.close_dialogues()


def lookout(ctx):
    """The party travels to the mine (a console trip stands in for the road until a road verb exists) and the guest
    fights the bandit lookout; the guest loots him."""
    ctx.go_far("", f"player.moveto {MINE_OUT:X}")
    ctx.arrives_with_party("client")
    ctx.clear("client", radius=1500.0)
    ctx.take("client", "enemy_1")


def enter(ctx):
    ctx.door("", f"{MINE_OUT:X}")


def fight(ctx):
    """The host fights inside; the guest stays at the entrance: its engine refuses a path to the host's copy down the
    tunnel (docs/design/repro-mcp.md, "Next for the MCP", 5), so the party is apart until the door out."""
    ctx.clear("", radius=INSIDE)


def loot(ctx):
    ctx.take("", "enemy_1")


def leave(ctx):
    """The guest, still at the entrance, leads the party out; the tether brings the host. From where the fight ends
    the host's engine refuses a path to the door or the guest (open: docs/design/playthrough-flows.md)."""
    ctx.door("client", f"{MINE_IN:X}")


STEPS = [
    Step("arrive", arrive, [together], checkpoint=True),
    Step("faendal", faendal, [quest_at(QUEST, 10), carries("", LETTER)], checkpoint=True),
    Step("trader", trader, [quest_at(QUEST, 210), carries("", LETTER, 0)], checkpoint=True),
    Step("reward", reward, [quest_at(QUEST, 310)], checkpoint=True),
    Step("lookout", lookout, sees=[same_items("enemy_1")], checkpoint=True),
    Step("enter", enter, checkpoint=True),
    Step("fight", fight, allow=frozenset({"together"})),
    Step("loot", loot, sees=[same_items("enemy_1")], allow=frozenset({"together"})),
    Step("leave", leave),
]
