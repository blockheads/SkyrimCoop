"""Party items (docs/design/party-items.md): keys, quest items and the story's rewards are the party's. Every player
carries a copy named "[P] <name>"; a drop or a store takes it from everyone, a pickup or a take gives it back to
everyone. Each step is checked on every player: each game's own player and its copy of the other."""
from ..common import HEIRLOOM, ACTIVATE_KEY, DOWN_KEY, FORM_DELETED, FORM_DISABLED, MENU_RIGHT_KEY, SMALL_CHEST, TAB_KEY, UP_KEY
from ..errors import TestFailure
from ..utilities import log

KEY = 0x93B08  # Key to Breezehome: a key (KEYM) is the party's however it arrives
TAGGED = "[P] Key to Breezehome"


def name(game):
    return game or "host"


def other(game):
    return "" if game == "client" else "client"


def tagged_keys(ctx, game, holder):
    return sum(st["count"] for st in ctx.item_stacks(game, holder) if st["item"] == KEY and st["name"] == TAGGED)


def carried(ctx):
    """The tagged keys each player carries on each game: {"host player": 1, "host copy": 1, ...}."""
    counts = {}
    for game in ("", "client"):
        for who, holder in (("player", 0x14), ("copy", ctx.player_copy(game))):
            if holder is not None:
                counts[f"{name(game)} {who}"] = tagged_keys(ctx, game, holder)
    return counts


def all_carry(ctx, count, step):
    try:
        ctx.wait_for(f"every player carries {count} of {TAGGED} ({step})",
                     lambda: len(now := carried(ctx)) == 4 and set(now.values()) == {count}, 10)
    except TestFailure as error:
        stacks = {f"{name(g)} {who}": [(f"{st['item']:X}", st["name"], st["count"]) for st in ctx.item_stacks(g, holder) if st["item"] == KEY]
                  for g in ("", "client") for who, holder in (("player", 0x14), ("copy", ctx.player_copy(g))) if holder is not None}
        raise ctx.failure(f"{error}: carried {carried(ctx)}; the key's stacks {stacks}") from None


def loose_keys(ctx, game):
    return [ref for ref, fields in ctx.world_state(game).items()
            if fields["base"] == KEY and not fields["flags"] & (FORM_DELETED | FORM_DISABLED)]


def scenario_party_item_moves(ctx, finder):
    """The finder gets a key: every player carries it tagged, and every game's chat tells it. The other player drops
    it: nobody carries it. The finder picks it up: everyone does. The other stores it in a chest: nobody. The finder
    takes it out: everyone."""
    keeper = other(finder)
    chest = SMALL_CHEST
    before = ctx.container_items("", chest)
    held = {item: ctx.item_count(finder, 0x14, item) or 0 for item in before}
    lying = {game: set(loose_keys(ctx, game)) for game in ("", "client")}
    try:
        if any(carried(ctx).values()):
            raise ctx.failure(f"a player carries {TAGGED} to begin with: {carried(ctx)}")
        ctx.player_command(finder, {"cmd": "give_item", "item": KEY})
        all_carry(ctx, 1, f"the {name(finder)}'s player found it")
        for game in ("", "client"):
            ctx.wait_for(f"[{name(game)}] its chat tells the party got the key",
                         lambda game=game: any("Key to Breezehome for the party" in line for line in ctx.system_chat(game)), 10)

        ctx.player_command(keeper, {"cmd": "drop", "item": KEY})
        all_carry(ctx, 0, f"the {name(keeper)}'s player dropped it")
        dropped = ctx.wait_for(f"[{name(finder)}] the dropped key lies in its world",
                               lambda: next(iter(set(loose_keys(ctx, finder)) - lying[finder]), None), 10)
        ctx.player_command(finder, {"cmd": "activate", "object": dropped})
        all_carry(ctx, 1, f"the {name(finder)}'s player picked it up")

        ctx.player_command(keeper, {"cmd": "store", "item": KEY, "container": chest})
        all_carry(ctx, 0, f"the {name(keeper)}'s player stored it in the chest")
        for game in ("", "client"):
            ctx.wait_for(f"[{name(game)}] the chest holds the key",
                         lambda game=game: (ctx.item_count(game, chest, KEY) or 0) == 1, 10)
        ctx.take_all(finder, chest)
        all_carry(ctx, 1, f"the {name(finder)}'s player took it out")
        log(f"carried: {carried(ctx)}")
    finally:
        for game in ("", "client"):
            if (keys := ctx.item_count(game, 0x14, KEY) or 0) > 0:
                ctx.console_on(game, f"player.removeitem {KEY:X} {keys}")
            for ref in set(loose_keys(ctx, game)) - lying[game]:
                ctx.console_on(game, f"{ref:X}.disable")
                ctx.console_on(game, f"{ref:X}.markfordelete")
        if (stored := ctx.item_count("", chest, KEY) or 0) > 0:
            ctx.console_on("", f"{chest:X}.removeitem {KEY:X} {stored}")
        for item, count in before.items():  # what Take All took, back in the chest
            if (taken := (ctx.item_count(finder, 0x14, item) or 0) - held[item]) > 0:
                ctx.console_on(finder, f"player.removeitem {item:X} {taken}")
            if (missing := count - (ctx.item_count("", chest, item) or 0)) > 0:
                ctx.console_on("", f"{chest:X}.additem {item:X} {missing}")


def scenario_party_item_moves_host(ctx):
    scenario_party_item_moves(ctx, "")


def scenario_party_item_moves_guest(ctx):
    scenario_party_item_moves(ctx, "client")


# FreeformCidhnaMineA's stage 35 (Fragment_3: Game.GetPlayer().AddItem(Skooma, 1)): a fixed item. A leveled reward
# (HeartwoodMill's) can roll nothing at a low level: its AddItem ran and added nothing (2026-10-30).
REWARD = 0x57A7A  # Skooma
REWARD_QUEST, REWARD_STAGE = 0xDB851, 35
CLAW_QUEST, CLAW_ALIAS, CLAW = 0x39645, 11, 0x39647  # MS13 and its GoldenClaw alias (a quest object)


def holders(ctx):
    return [(f"{name(game)} {who}", game, holder) for game in ("", "client")
            for who, holder in (("player", 0x14), ("copy", ctx.player_copy(game))) if holder is not None]


def stacks_of(ctx, item, match):
    """{"host player": n, ...}: each player's count of the item's stacks with extra data that match (a match of None:
    every stack, plain ones too)."""
    if match is None:
        return {who: ctx.item_count(game, holder, item) or 0 for who, game, holder in holders(ctx)}
    return {who: sum(st["count"] for st in ctx.item_stacks(game, holder) if st["item"] == item and match(st))
            for who, game, holder in holders(ctx)}


def everyone_has(ctx, item, match, count, what):
    try:
        ctx.wait_for(f"every player carries {count} of {what}",
                     lambda: len(now := stacks_of(ctx, item, match)) == 4 and set(now.values()) == {count}, 15)
    except TestFailure as error:
        everything = stacks_of(ctx, item, None)
        raise ctx.failure(f"{error}: matching {stacks_of(ctx, item, match)}, of any kind {everything}") from None


def scenario_party_reward_reaches_everyone(ctx):
    """A quest's own stage fragment gives the player an item (Cidhna Mine's skooma): it is the party's, so every player
    carries it as "[P] <name>", and every game's chat says the party received it."""
    tagged_now = lambda game: {(st["item"], st["name"]) for st in ctx.item_stacks(game, 0x14) if st["name"].startswith("[P] ")}
    before = tagged_now("")
    carried = ctx.container_items("", 0x14)
    got = []
    try:
        ctx.console_on("", f"resetquest {REWARD_QUEST:X}")
        ctx.console_on("", f"startquest {REWARD_QUEST:X}")
        ctx.wait_for("[host] the favour's quest runs", lambda: ctx.quest_snapshot("", REWARD_QUEST)["flags"] & 1, 10)
        ctx.reset_quest_fragments()
        ctx.console_on("", f"setstage {REWARD_QUEST:X} {REWARD_STAGE}")
        try:
            got[:] = ctx.wait_for("[host] its player carries a new party item", lambda: list(tagged_now("") - before), 15)
        except TestFailure as error:
            now = ctx.container_items("", 0x14)
            changed = {f"{i:X}": now.get(i, 0) - carried.get(i, 0) for i in set(now) | set(carried) if now.get(i, 0) != carried.get(i, 0)}
            raise ctx.failure(f"{error}: the host's player's items changed by {changed}; the quest "
                              f"{ctx.quest_snapshot('', REWARD_QUEST)}; its fragments run "
                              f"{ctx.player_command('', {'cmd': 'quest_fragments', 'quest': REWARD_QUEST})}") from None
        item, tagged = got[0]
        log(f"the reward: {tagged} ({item:X})")
        everyone_has(ctx, item, lambda st: st["name"] == tagged, 1, tagged)
        for game in ("", "client"):
            ctx.wait_for(f"[{name(game)}] its chat says the party received it",
                         lambda game=game: any(line.endswith(f"The party received {tagged[4:]}") for line in ctx.system_chat(game)), 10)
    finally:
        for item, _ in got:
            for game in ("", "client"):
                if (rewards := ctx.item_count(game, 0x14, item) or 0) > 0:
                    ctx.console_on(game, f"player.removeitem {item:X} {rewards}")
        ctx.console_on("", f"resetquest {REWARD_QUEST:X}")
        ctx.console_on("", f"stopquest {REWARD_QUEST:X}")


def scenario_party_quest_item(ctx, finder):
    """The finder picks up the Golden Claw while MS13's GoldenClaw alias holds it (a quest object, so a quest item):
    every player carries one claw, as the engine's quest item on the finder's game, and never more."""
    try:
        if any(stacks_of(ctx, CLAW, None).values()):
            raise ctx.failure("a player carries the Golden Claw to begin with")
        ctx.console_on("", f"resetquest {CLAW_QUEST:X}")
        ctx.console_on("", f"setstage {CLAW_QUEST:X} 10")
        ctx.wait_for("[host] MS13 runs", lambda: ctx.quest_stage("", CLAW_QUEST) == 10, 10)
        # A claw lying before the host's player, held by the quest's alias as the barrow's is.
        before = set(ctx.world_state(""))
        ctx.console_on("", f"player.placeatme {CLAW:X} 1")
        claw = ctx.wait_for("[host] the claw lies in its world", lambda: next(
            (r for r, f in ctx.world_state("").items() if r not in before and f["base"] == CLAW), None), 10)
        ctx.player_command("", {"cmd": "quest_force_alias", "quest": CLAW_QUEST, "alias": CLAW_ALIAS, "ref": claw})
        ctx.wait_for(f"[host] MS13's GoldenClaw alias holds {claw:X}",
                     lambda: ctx.quest_snapshot("", CLAW_QUEST)["aliases"].get(CLAW_ALIAS, (0,))[0] == claw, 5)
        target = claw if finder == "" else ctx.wait_for("[client] the claw lies in its world",
                                                        lambda: next((r for r, f in ctx.world_state("client").items()
                                                                      if f["base"] == CLAW and not f["flags"] & (FORM_DELETED | FORM_DISABLED)), None), 10)
        ctx.player_command(finder, {"cmd": "activate", "object": target})
        everyone_has(ctx, CLAW, None, 1, "the Golden Claw")
        ctx.hold()
        everyone_has(ctx, CLAW, None, 1, "the Golden Claw, still one a moment later")
        log(f"claws: {stacks_of(ctx, CLAW, None)}")
    finally:
        ctx.console_on("", f"resetquest {CLAW_QUEST:X}")
        for game in ("", "client"):
            if (claws := ctx.item_count(game, 0x14, CLAW) or 0) > 0:
                ctx.console_on(game, f"player.removeitem {CLAW:X} {claws}")
            for ref, fields in ctx.world_state(game).items():
                if fields["base"] == CLAW and ref >> 24 == 0xFF and not fields["flags"] & (FORM_DELETED | FORM_DISABLED):
                    ctx.console_on(game, f"{ref:X}.disable")
                    ctx.console_on(game, f"{ref:X}.markfordelete")


def scenario_party_quest_item_host(ctx):
    scenario_party_quest_item(ctx, "")


def scenario_party_quest_item_guest(ctx):
    scenario_party_quest_item(ctx, "client")


SWORD = 0x12EB7  # Iron Sword
SWORD_TAGGED = "[P] Iron Sword"
BELETHOR = "00013BA1"
BARTER = "BarterMenu"
BARTER_LISTS = "_root.Menu_mc.InventoryLists_mc"


def barter_trade(ctx, game, merchant, category, wanted):
    """That game's player opens barter with the merchant, picks the category ((whose, name): the list names the merchant
    and then the player above each one's categories), then the entry named `wanted`, and trades one (E; the quantity
    slider, if shown, accepted)."""
    ctx.place_before(game, merchant, 110)
    ctx.player_script_call(game, "Actor", "ShowBarterMenu", on=merchant)
    ctx.wait_for(f"[{name(game)}] barter's categories take input",
                 lambda: ctx.menu_var(game, BARTER, BARTER_LISTS + ".currentState") == 1, 10)
    categories = BARTER_LISTS + ".CategoriesList"
    count = int(ctx.menu_var(game, BARTER, categories + ".entryList.length") or 0)
    names = [ctx.menu_var(game, BARTER, f"{categories}.entryList.{i}.text") for i in range(count)]
    log(f"[{name(game)}] barter's categories: {names}")
    index = names.index(category[0]) + names[names.index(category[0]):].index(category[1])
    for _ in range(count + 1):
        if (selected := ctx.menu_var(game, BARTER, categories + ".selectedIndex")) == index:
            break
        ctx.tap_key(game, DOWN_KEY if selected < index else UP_KEY)
        ctx.wait_for(f"[{name(game)}] the category highlight moves", lambda: ctx.menu_var(game, BARTER, categories + ".selectedIndex") != selected, 5)
    ctx.tap_key(game, MENU_RIGHT_KEY)  # D: categories -> items
    ctx.wait_for(f"[{name(game)}] barter's items take input", lambda: ctx.menu_var(game, BARTER, BARTER_LISTS + ".currentState") == 2, 10)
    ctx.select_entry(game, BARTER, BARTER_LISTS + ".ItemsList", wanted)
    card = "_root.Menu_mc.ItemCard_mc.itemInfo"
    ctx.wait_for(f"[{name(game)}] the item card shows {wanted} with its price",
                 lambda: wanted in str(ctx.menu_var(game, BARTER, card + ".name") or "") and (ctx.menu_var(game, BARTER, card + ".value") or 0) > 0, 10)
    log(f"[{name(game)}] before the trade key: {barter_gates(ctx, game)}")
    ctx.tap_key(game, ACTIVATE_KEY)
    if ctx.menu_var(game, BARTER, "_root.Menu_mc.ItemCard_mc.QuantityMenu_mc._visible"):
        ctx.tap_key(game, 0x1C)
    log(f"[{name(game)}] after the trade key: menus {ctx.menus(game)}, lists state "
        f"{ctx.menu_var(game, BARTER, BARTER_LISTS + '.currentState')}, selected "
        f"{ctx.menu_var(game, BARTER, BARTER_LISTS + '.ItemsList.selectedEntry.text')}, quantity menu "
        f"{ctx.menu_var(game, BARTER, '_root.Menu_mc.ItemCard_mc.QuantityMenu_mc._visible')}, HUD {ctx.hud_messages(game)[-3:]}")


def barter_gates(ctx, game):
    """What BarterMenu's item press depends on (ItemMenu's handleInput / onItemSelect): faded in, the list taking input
    and selection, the highlighted entry enabled and the item card showing it."""
    value = lambda path: ctx.menu_var(game, BARTER, path)
    items = BARTER_LISTS + ".ItemsList"
    return {"faded_in": value("_root.Menu_mc.bFadedIn"), "lists_state": value(BARTER_LISTS + ".currentState"),
            "disable_input": value(items + ".disableInput"), "disable_selection": value(items + ".disableSelection"),
            "selected": value(items + ".selectedIndex"), "entry": value(items + ".selectedEntry.text"),
            "entry_enabled": value(items + ".selectedEntry.enabled"), "card": value("_root.Menu_mc.ItemCard_mc.itemInfo.name"),
            "card_shown": value("_root.Menu_mc.ItemCard_mc._visible"), "quantity": value("_root.Menu_mc.ItemCard_mc.QuantityMenu_mc._visible"),
            # BarterMenu.onQuantityMenuSelect: a sale above the merchant's gold asks to confirm a raw deal first.
            "vendor_gold": value("_root.Menu_mc.iVendorGold"), "price": value("_root.Menu_mc.ItemCard_mc.itemInfo.value")}


def player_name(ctx, game):
    return ctx.player_command(game, {"cmd": "form", "form": 0x14}).get("name", "")


def close_barter(ctx, game):
    if BARTER in ctx.menus(game):
        ctx.tap_key(game, TAB_KEY)
        ctx.wait_for(f"[{name(game)}] barter closes", lambda: BARTER not in ctx.menus(game), 5)


def scenario_party_item_sold(ctx, seller):
    """The seller sells the party's sword to a merchant: no player carries it, and the party's purse holds the price.
    The other player buys it back from the same merchant: every player carries it again."""
    buyer = other(seller)
    tagged = lambda st: st["name"] == SWORD_TAGGED
    npc, _, copy = ctx.spawn_host_npc(BELETHOR, restrained=True)
    gold = ctx.party_gold()
    try:
        if any(stacks_of(ctx, SWORD, tagged).values()):
            raise ctx.failure(f"a player carries {SWORD_TAGGED} to begin with: {stacks_of(ctx, SWORD, tagged)}")
        ctx.set_party_gold(max(gold, 5000))
        # A merchant spawned here carries a few gold: a sale above it asks for a raw deal first (BarterMenu.as).
        ctx.console_on("", f"{npc:X}.additem f 1000")
        ctx.player_command(seller, {"cmd": "give_item", "item": SWORD, "name": SWORD_TAGGED})
        everyone_has(ctx, SWORD, tagged, 1, SWORD_TAGGED)
        purse = ctx.party_gold()
        barter_trade(ctx, seller, copy if seller else npc, (player_name(ctx, seller), "WEAPONS"), SWORD_TAGGED)
        try:
            everyone_has(ctx, SWORD, tagged, 0, f"{SWORD_TAGGED} (the {name(seller)}'s player sold it)")
        except TestFailure as error:
            raise ctx.failure(f"{error}; the seller's HUD since {ctx.hud_messages(seller)[-6:]}, menus {ctx.menus(seller)}, "
                              f"after the press {barter_gates(ctx, seller)}, the purse {ctx.party_gold()} (was {purse})") from None
        ctx.wait_for("the purse holds the sale's price", lambda: ctx.party_gold() > purse, 10)
        close_barter(ctx, seller)
        barter_trade(ctx, buyer, copy if buyer else npc, ("Belethor", "WEAPONS"), SWORD_TAGGED)
        everyone_has(ctx, SWORD, tagged, 1, f"{SWORD_TAGGED} (the {name(buyer)}'s player bought it back)")
    finally:
        for game in ("", "client"):
            try:
                close_barter(ctx, game)
            except TestFailure as error:
                log(f"cleanup: {error}; menus {ctx.menus(game)}")
                ctx.player_command(game, {"cmd": "menu", "name": BARTER, "show": False})
        for game in ("", "client"):
            if (swords := sum(st["count"] for st in ctx.item_stacks(game, 0x14) if st["item"] == SWORD and tagged(st))) > 0:
                try:
                    ctx.player_command(game, {"cmd": "store", "item": SWORD, "count": swords, "container": 0})
                except TestFailure:
                    ctx.console_on(game, f"player.removeitem {SWORD:X} {swords}")
        ctx.remove_host_npc(npc)
        ctx.set_party_gold(gold)


def scenario_party_item_sold_host(ctx):
    scenario_party_item_sold(ctx, "")


def scenario_party_item_sold_guest(ctx):
    scenario_party_item_sold(ctx, "client")


def scenario_party_item_improved(ctx, improver):
    """The other player gets the party's sword; the improver tempers it, then enchants it, as the tables do: every
    player's and copy's party sword is improved the same, one each."""
    finder = other(improver)
    enchantment = HEIRLOOM["enchantment"]
    tagged = lambda st: st["name"] == SWORD_TAGGED

    def one_each(health, enchanted, step):
        def matches():
            for _, game, holder in holders(ctx):
                stacks = [st for st in ctx.item_stacks(game, holder) if st["item"] == SWORD and tagged(st)]
                if len(stacks) != 1 or stacks[0]["count"] != 1 or abs(stacks[0]["health"] - health) > 0.01 or \
                        (stacks[0]["enchantment"] == enchantment) != enchanted:
                    return False
            return True
        try:
            ctx.wait_for(f"every player carries one {SWORD_TAGGED}, {step}", matches, 15)
        except TestFailure as error:
            seen = {who: [(st["count"], round(st["health"], 2), f"{st['enchantment']:X}") for st in ctx.item_stacks(game, holder)
                          if st["item"] == SWORD and tagged(st)] for who, game, holder in holders(ctx)}
            extras = {who: ctx.game(game).rpc({"cmd": "stack_extras", "object": holder, "item": SWORD}) for who, game, holder in holders(ctx)}
            raise ctx.failure(f"{error}: (count, health, enchantment) {seen}; each stack's extra data types {extras}") from None

    try:
        if any(stacks_of(ctx, SWORD, tagged).values()):
            raise ctx.failure(f"a player carries {SWORD_TAGGED} to begin with: {stacks_of(ctx, SWORD, tagged)}")
        ctx.player_command(finder, {"cmd": "give_item", "item": SWORD, "name": SWORD_TAGGED})
        everyone_has(ctx, SWORD, tagged, 1, SWORD_TAGGED)
        ctx.player_command(improver, {"cmd": "improve", "item": SWORD, "health": 1.2})
        one_each(1.2, False, f"tempered by the {name(improver)}'s player")
        ctx.player_command(improver, {"cmd": "improve", "item": SWORD, "enchantment": enchantment, "charge": 1000})
        one_each(1.2, True, f"enchanted by the {name(improver)}'s player")
    finally:
        for game in ("", "client"):
            for st in ctx.item_stacks(game, 0x14):
                if st["item"] == SWORD and tagged(st):
                    try:
                        ctx.player_command(game, {"cmd": "store", "item": SWORD, "count": st["count"], "container": 0})
                    except TestFailure:
                        ctx.console_on(game, f"player.removeitem {SWORD:X} {st['count']}")


def scenario_party_item_improved_host(ctx):
    scenario_party_item_improved(ctx, "")


def scenario_party_item_improved_guest(ctx):
    scenario_party_item_improved(ctx, "client")
