"""What the party owns (docs/design/party-model.md, "Items"): a room, a house or a horse bought by one player is every
player's. Each scenario has a player buy it the way a player does, then checks the owner on both games and that the
other player uses it as its own: no crime, sleeping in the owned bed, taking from the house's containers, riding."""
from ..common import (
    ACTIVATE_KEY,
    BANNERED_MARE,
    ESCAPE_KEY,
    HOME_CELL,
    HULDA,
    PLAYER_BASE,
    RENT_ROOM_TOPIC,
    TAKE_ALL_KEY,
)
from ..errors import TestFailure
from ..utilities import log
from .crime import bounty

PLAYER_FACTION = 0xDB1
SLEEP_MENU = "Sleep/Wait Menu"
DRAGONSREACH = "WhiterunDragonsreach"
BREEZEHOME, BREEZEHOME_CELL, BREEZEHOME_KEY = "WhiterunBreezehome", 0x165A8, 0x93B08
BREEZEHOME_DOOR_OUTSIDE = 0x1A6F9  # its front door, on Whiterun's side (locked: requires the key)
PARTY_KEY_NAME = "[P] Key to Breezehome"  # a party item (party-items.md)
PROVENTUS = 0x1A67D  # Whiterun's steward (Proventus Avenicci's reference)
HOUSE_WHITERUN_TOPIC, HOUSE_TAKE_IT_TOPIC = 0xA7B19, 0xA7B18
HOUSE_LINES = (0xA7B31, 0xA7B32)  # the topic's lines (Skyrim.esm INFO records)
HOUSE_PURCHASE = 0xA7B33
WHITERUN_JARLS = (0x1A677, 0x1A6A1)  # Balgruuf, Vignar: HousePurchase's Jarl aliases
HOUSE_PRICE = 5000  # HPWhiterun
SKULVAR = 0x1A69C  # Whiterun's stablemaster (Skulvar Sable-Hilt's reference)
BUY_HORSE_TOPIC, BUY_HORSE_YES_TOPIC = 0x68CBA, 0x68CB6
HORSE_PRICE = 1000  # HorseCost
PLAYER_HORSE_FACTION = 0x68D78


def other(buyer):
    return "" if buyer == "client" else "client"


def name(game):
    return game or "host"


def talk(ctx, buyer, npc):
    """The buyer walks up to the host's NPC (its copy on a guest's game) and talks: (the NPC on the buyer's game)."""
    target = npc if buyer == "" else ctx.copy_of("client", ctx.host_npc_state(npc)["session_id"])
    ctx.place_before(buyer, target, 110)
    ctx.wait_for(f"[{name(buyer)}] takes input", lambda: ctx.takes_input(buyer), 10)
    ctx.activate_on(buyer, target)
    ctx.wait_for(f"[{name(buyer)}] the menu opens on {target:08X}", lambda: ctx.talking_to(buyer, target), 10)
    ctx.wait_for(f"[{name(buyer)}] the menu shows its list", lambda: ctx.list_shown(buyer), 30)
    return target


def owner(ctx, game, form):
    return ctx.player_command(game, {"cmd": "form", "form": form}).get("owner")


def owned_on_both(ctx, form, expected, what):
    for game in ("", "client"):
        ctx.wait_for(f"[{name(game)}] {what} {form:X} is owned by {expected:X}", lambda game=game: owner(ctx, game, form) == expected, 10)


def sleeps_in(ctx, game, bed):
    """That game's player lies down in the bed as in its own: the engine opens the sleep menu (an owned bed of
    someone else's refuses: "You cannot sleep in an owned bed")."""
    ctx.place_before(game, bed, 90)
    ctx.activate_on(game, bed)
    try:
        ctx.wait_for(f"[{name(game)}] sleeping in {bed:X} opens the sleep menu", lambda: SLEEP_MENU in ctx.menus(game), 10)
    finally:
        if SLEEP_MENU in ctx.menus(game):
            ctx.tap_key(game, ESCAPE_KEY)
            ctx.wait_for(f"[{name(game)}] the sleep menu closes", lambda: SLEEP_MENU not in ctx.menus(game), 5)
    ctx.wait_for(f"[{name(game)}] the player gets up", lambda: ctx.takes_input(game), 15)


def no_crime(ctx, before, what):
    if (now := bounty(ctx)) != before:
        raise ctx.failure(f"[host] {what} cost the party a bounty: {before} -> {now}")


def rents_room(ctx, buyer):
    """A player rents Hulda's room: the bed is the player's (0x7) on both games, and the other player sleeps in it."""
    user = other(buyer)
    gold = ctx.party_gold()
    npc, _, _ = ctx.spawn_host_npc(HULDA, restrained=True)
    bed, bed_owner = None, None
    try:
        if gold < 10:
            ctx.set_party_gold(10)
        bed = ctx.script_var("", npc, "RentRoomScript", "Bed")
        bed_owner = owner(ctx, "", bed)
        talk(ctx, buyer, npc)
        party = ctx.party_gold()
        ctx.pick_with_keys(buyer, RENT_ROOM_TOPIC)
        ctx.wait_party_gold("the party paid for the room once", party - 10)
        ctx.wait_for(f"[host] the rented bed {bed:X} is the player's", lambda: owner(ctx, "", bed) == PLAYER_BASE, 10)
        ctx.close_dialogues()
        ctx.both_go(BANNERED_MARE)
        owned_on_both(ctx, bed, PLAYER_BASE, "the rented bed")
        before = bounty(ctx)
        sleeps_in(ctx, user, bed)
        no_crime(ctx, before, f"[{name(user)}] sleeping in the room")
    finally:
        ctx.close_dialogues()
        if bed is not None and bed_owner != PLAYER_BASE:
            ctx.player_command("", {"cmd": "crime", "action": "set_owner", "target": bed, "owner": bed_owner or 0})
        if ctx.player_cell("") != ctx.load_home("")["cell"]:
            ctx.both_go(HOME_CELL)
        ctx.remove_host_npc(npc)
        ctx.set_party_gold(gold, wait=False)


def world_refs(ctx, game):
    return {r["id"]: r for r in ctx.player_command(game, {"cmd": "world_state"})["references"]}


def buys_house(ctx, buyer):
    """A player buys Breezehome from Proventus: the house (its cell) is the party's (PlayerFaction) on both games; the
    other player walks in, takes from its containers and sleeps in its bed as its own, without a crime."""
    user = other(buyer)
    gold = ctx.party_gold()
    keys = {game: ctx.item_count(game, 0x14, BREEZEHOME_KEY) or 0 for game in ("", "client")}
    # A normal game runs HousePurchase from its start (QASmoke's coc start does not).
    started = not ctx.console_number("", f"getquestrunning {HOUSE_PURCHASE:X}")
    cell_owner = None
    try:
        # The house's own owner (its plugin's, read once the cell is loaded) before anyone buys it.
        ctx.both_go(BREEZEHOME)
        cell_owner = owner(ctx, "", BREEZEHOME_CELL)
        log(f"Breezehome unsold: owned by {cell_owner:X} on the host, {owner(ctx, 'client', BREEZEHOME_CELL):X} on the guest")
        ctx.both_go(DRAGONSREACH)
        if started:
            ctx.console_on("", f"startquest {HOUSE_PURCHASE:X}")
            ctx.wait_for("[host] HousePurchase fills its Whiterun Jarl",
                         lambda: any("'WhiterunJarl'->" in line and "NONE" not in line
                                     for line in ctx.console_on("", f"sqv {HOUSE_PURCHASE:X}")), 15)
        steward = PROVENTUS
        # Walking to his chair he takes no conversation: talk once he sits.
        ctx.wait_for(f"[host] Proventus {steward:X} sits", lambda: (ctx.actor_state("", steward).get("ai") or {}).get("furniture")
                     and (ctx.actor_state("", steward).get("placement") or {}).get("has_3d"), 30)
        # His line asks for the Jarl's friend (the HousePurchase Jarl alias's GetRelationshipRank >= 2): the party's
        # standing, the host's player's, whoever picks it (JudgedRules), as the Jarl's favour leaves it.
        for jarl in WHITERUN_JARLS:
            ctx.console_on("", f"{jarl:08X}.setrelationshiprank player 2")
        ctx.set_party_gold(gold + HOUSE_PRICE)
        talk(ctx, buyer, steward)
        try:
            ctx.pick_with_keys(buyer, HOUSE_WHITERUN_TOPIC)
        except TestFailure as error:
            # Which of the line's conditions fails, judged on the host for the buyer's character.
            character = 0x14 if buyer == "" else ctx.client_on_host()
            checks = {f"{info:X}": [f"{c['function']}{'' if c['true'] else ' FAILS'}" for c in ctx.player_command(
                "", {"cmd": "info_conditions", "info": info, "speaker": PROVENTUS, "player": character})["conditions"]]
                for info in HOUSE_LINES}
            responses = {f"[{name(g)}] {info:X}": (ctx.game(g).rpc({"cmd": "info_responses", "info": info}),
                                                    ctx.game(g).rpc({"cmd": "form", "form": info}))
                         for g in ("", "client") for info in (*HOUSE_LINES, HOUSE_WHITERUN_TOPIC)}
            raise ctx.failure(f"{error}; the house line's conditions for the {name(buyer)}'s character: {checks}; "
                              f"its responses: {responses}") from None
        ctx.wait_for(f"[{name(buyer)}] the menu shows the answers", lambda: ctx.lists_topic(buyer, HOUSE_TAKE_IT_TOPIC), 30)
        ctx.pick_with_keys(buyer, HOUSE_TAKE_IT_TOPIC)
        ctx.wait_party_gold("the party paid for the house once", gold)
        ctx.close_dialogues()
        ctx.wait_for(f"[host] Breezehome {BREEZEHOME_CELL:X} is the party's", lambda: owner(ctx, "", BREEZEHOME_CELL) == PLAYER_FACTION, 10)
        log("the house key: " + ", ".join(f"[{name(g)}] {ctx.item_count(g, 0x14, BREEZEHOME_KEY)}" for g in ("", "client")))
        # The key is the party's: every player carries it, tagged (party-items.md).
        for game in ("", "client"):
            ctx.wait_for(f"[{name(game)}] its player carries {PARTY_KEY_NAME}",
                         lambda game=game: any(st.get("item") == BREEZEHOME_KEY and st.get("name") == PARTY_KEY_NAME
                                               for st in ctx.player_command(game, {"cmd": "item_stacks", "object": 0x14})["stacks"]), 10)
        # The other player opens the house's locked front door with its own copy of the key.
        ctx.both_go("Breezehome's door", f"player.moveto {BREEZEHOME_DOOR_OUTSIDE:X}")
        outside = ctx.player_cell(user)
        ctx.travel(user, lambda: ctx.activate_on(user, BREEZEHOME_DOOR_OUTSIDE), outside)
        ctx.follows(other(user), outside)
        if ctx.player_cell(user) != BREEZEHOME_CELL:
            raise ctx.failure(f"[{name(user)}] the front door led to {ctx.player_cell(user):X}, not Breezehome")
        state = ctx.player_command(user, {"cmd": "crime", "action": "trespass_state", "actor": 0x14})
        log(f"[{name(user)}] in Breezehome: {state}")
        if state.get("trespass"):
            raise ctx.failure(f"[{name(user)}] trespasses in the party's house: {state}")
        refs = world_refs(ctx, user)
        # Enabled ones: the house's decorating upgrades wait disabled (TESForm flag 0x800).
        beds = [i for i, r in refs.items() if r.get("cell") == BREEZEHOME_CELL and not r.get("flags", 0) & 0x800
                and "bed" in ctx.player_command(user, {"cmd": "form", "form": r.get("base", 0)}).get("name", "").lower()]
        log(f"[{name(user)}] Breezehome's beds: " + ", ".join(f"{b:X} (owner {owner(ctx, user, b):X})" for b in beds))
        chests = [i for i, r in refs.items() if r.get("cell") == BREEZEHOME_CELL and r.get("items")]
        if not beds or not chests:
            raise ctx.failure(f"[{name(user)}] Breezehome shows beds {beds} and containers {chests}")
        before = bounty(ctx)
        chest = chests[0]
        ctx.place_before(user, chest, 130)
        ctx.activate_on(user, chest)
        ctx.wait_for(f"[{name(user)}] the container menu opens", lambda: "ContainerMenu" in ctx.menus(user), 10)
        ctx.tap_key(user, TAKE_ALL_KEY)
        ctx.wait_for(f"[{name(user)}] takes all and the menu closes", lambda: ctx.takes_input(user), 10)
        ctx.wait_for("both games show the container emptied the same",
                     lambda: ctx.container_items("", chest) == ctx.container_items("client", chest), 10)
        no_crime(ctx, before, f"[{name(user)}] taking from the house's container")
        sleeps_in(ctx, user, beds[0])
        no_crime(ctx, before, f"[{name(user)}] sleeping in the house")
        owned_on_both(ctx, BREEZEHOME_CELL, PLAYER_FACTION, "Breezehome")
    finally:
        ctx.close_dialogues()
        if cell_owner is not None:
            if ctx.player_cell("") != BREEZEHOME_CELL:
                ctx.both_go(BREEZEHOME)
            for game in ("", "client"):  # the house, unsold again (set_cell_owner: the player's cell)
                ctx.player_command(game, {"cmd": "crime", "action": "set_cell_owner", "owner": cell_owner})
        ctx.both_go(HOME_CELL)
        ctx.script_var("", HOUSE_PURCHASE, "HousePurchaseScript", "WhiterunHouseVar", 0)
        if started:
            ctx.console_on("", f"stopquest {HOUSE_PURCHASE:X}")
        for game, count in keys.items():
            if (have := ctx.item_count(game, 0x14, BREEZEHOME_KEY) or 0) > count:
                ctx.console_on(game, f"player.removeitem {BREEZEHOME_KEY:X} {have - count}")
        ctx.set_party_gold(gold, wait=False)


def ride(ctx, user, ridden):
    """That game's player gets on the horse and off again, with no bounty."""
    before = bounty(ctx)
    ctx.place_before(user, ridden, 150)
    ctx.activate_on(user, ridden)
    ctx.wait_for(f"[{name(user)}] its player rides the party's horse",
                 lambda: ctx.actor_state(user, 0x14).get("placement", {}).get("riding"), 15)
    ctx.wait_for(f"[{name(user)}] its player is seated",
                 lambda: not ctx.actor_state(user, 0x14).get("placement", {}).get("getting_on_off"), 10)
    no_crime(ctx, before, f"[{name(user)}] riding the party's horse")
    ctx.tap_key(user, ACTIVATE_KEY)
    ctx.wait_for(f"[{name(user)}] its player gets off", lambda: not ctx.actor_state(user, 0x14).get("placement", {}).get("riding")
                 and not ctx.actor_state(user, 0x14).get("placement", {}).get("getting_on_off"), 15)


def buys_horse(ctx, buyer):
    """A player buys the Whiterun stable's horse from Skulvar: the horse is the party's (PlayerFaction) on both games;
    the other player rides it with no horse-theft bounty."""
    user = other(buyer)
    gold = ctx.party_gold()
    horse = horse_owner = None
    try:
        ctx.both_go("Whiterun stables", f"player.moveto {SKULVAR:X}")
        before_refs = world_refs(ctx, "")
        ctx.set_party_gold(gold + HORSE_PRICE)
        talk(ctx, buyer, SKULVAR)
        ctx.pick_with_keys(buyer, BUY_HORSE_TOPIC)
        ctx.wait_for(f"[{name(buyer)}] the menu shows the answers", lambda: ctx.lists_topic(buyer, BUY_HORSE_YES_TOPIC), 30)
        ctx.pick_with_keys(buyer, BUY_HORSE_YES_TOPIC)
        ctx.wait_party_gold("the party paid for the horse once", gold)
        ctx.close_dialogues()
        horse = ctx.wait_for("[host] the bought horse is the party's",
                             lambda: next((i for i, r in world_refs(ctx, "").items()
                                           if r.get("actor") and r.get("owner") == PLAYER_FACTION), None), 10)
        horse_owner = before_refs.get(horse, {}).get("owner", 0)
        ridden = horse if user == "" else ctx.copy_of("client", ctx.host_npc_state(horse)["session_id"])
        copy = ctx.copy_of("client", ctx.host_npc_state(horse)["session_id"])
        ride(ctx, user, ridden)
        for game, form in (("", horse), ("client", copy)):
            ctx.wait_for(f"[{name(game)}] the horse {form:X} is owned by the player's faction",
                         lambda game=game, form=form: owner(ctx, game, form) == PLAYER_FACTION, 10)
            if (rank := ctx.faction_rank(game, form, PLAYER_HORSE_FACTION)) < 1:
                raise ctx.failure(f"[{name(game)}] the horse {form:X} is not a player's horse (rank {rank})")
    finally:
        ctx.close_dialogues()
        if horse is not None:
            ctx.player_command("", {"cmd": "crime", "action": "set_owner", "target": horse, "owner": horse_owner or 0})
            ctx.console_on("", f"{horse:08X}.removefromfaction {PLAYER_HORSE_FACTION:X}")
        ctx.both_go(HOME_CELL)
        ctx.set_party_gold(gold, wait=False)


def scenario_party_rents_room_host(ctx):
    """The host rents Hulda's room: the guest's game shows the bed owned by the player, and the guest sleeps in it."""
    rents_room(ctx, "")

def scenario_party_rents_room_guest(ctx):
    """The guest rents Hulda's room: both games show the bed owned by the player, and the host sleeps in it."""
    rents_room(ctx, "client")

def scenario_party_buys_house_host(ctx):
    """The host buys Breezehome: the guest's game shows it the party's, and the guest uses it as its own."""
    buys_house(ctx, "")

def scenario_party_buys_house_guest(ctx):
    """The guest buys Breezehome: both games show it the party's, and the host uses it as its own."""
    buys_house(ctx, "client")

def scenario_party_buys_horse_host(ctx):
    """The host buys the stable's horse: the guest's game shows it the party's, and the guest rides it with no crime."""
    buys_horse(ctx, "")

def scenario_party_buys_horse_guest(ctx):
    """The guest buys the stable's horse: both games show it the party's, and the host rides it with no crime."""
    buys_horse(ctx, "client")
