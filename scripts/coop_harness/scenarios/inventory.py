"""Scenarios inventory for the co-op harness."""
import sys
from ..common import (
    BLUE_MOUNTAIN_FLOWER,
    DEATHBELL,
    GOLD,
    HEIRLOOM,
    HOME_CELL,
    IRON_SWORD,
    LYDIA,
    NIGHTSHADE,
    OUTDOOR_CELL,
    POISON_YES,
    REFR_LOCK,
    SMALL_CHEST,
    SWEETROLL,
    WHEAT,
)
from ..errors import TestFailure
from ..utilities import log


def scenario_player_equipment_host(ctx):
    """The host equips, draws, sheathes and removes a sword; its guest copy follows each action."""
    ctx.equip_roundtrip("", "player", "client", ctx.other_player, "host's character", ready_weapon=True)

def scenario_player_equipment_guest(ctx):
    """The guest equips, draws, sheathes and removes a sword; its host copy follows each action."""
    ctx.equip_roundtrip("client", "player", "", ctx.other_player, "client's character", ready_weapon=True)

def scenario_npc_equipment_lifecycle(ctx):
    """A host NPC equips and removes a sword, then its copy recovers from an equipment drift."""
    npc, _, copy = ctx.spawn_host_npc(LYDIA, restrained=True)
    ctx.equip_roundtrip("", f"{npc:08X}", "client", lambda actors: copy if str(copy) in actors else None,
                        f"NPC copy {copy:08X}")
    def equipment(instance, form_id):
        _game = ctx.game(instance)
        return set((((_game.read_status() or {}).get("world") or {}).get(str(form_id)) or {}).get("equipment") or [])
    owner = equipment("", npc)
    if not owner:
        raise ctx.failure("the host's NPC wears nothing: nothing to drift")
    ctx.player_command("client", {"cmd": "strip", "actor": copy})
    ctx.wait_for("[client] the copy's equipment drifted", lambda: equipment("client", copy) != owner, 5)
    ctx.wait_for("[client] the copy wears the owner's equipment again", lambda: equipment("client", copy) == owner, 10)

def scenario_chest_take_all(ctx):
    """Both players use Take All: items reach the acting player and its copy.
    The other game keeps its menu closed and its own player's inventory unchanged."""
    chest = SMALL_CHEST
    before = ctx.container_items("", chest)
    # Gold is the party's (party-purse.md): set back once, not per player.
    held = {name: {item: ctx.item_count(name, 0x14, item) or 0 for item in [*before, SWEETROLL] if item != GOLD}
            for name in ("", "client")}
    gold = ctx.party_gold()
    try:
        if (theirs := ctx.container_items("client", chest)) != before:
            raise ctx.failure(f"[guest] chest {chest:X} holds other items than the host's to begin with: "
                              + ctx.item_differences(before, theirs, "host", "guest"))
        if SWEETROLL in before:
            raise ctx.failure(f"[host] chest {chest:X} holds sweetrolls to begin with")
        ctx.console_on("", f"{chest:X}.additem {SWEETROLL:X} 2")
        for name in ("", "client"):
            ctx.wait_for(f"[{'guest' if name else 'host'}] chest {chest:X} holds 2 sweetrolls",
                     lambda name=name: ctx.item_count(name, chest, SWEETROLL) == 2, 5)
        ctx.take_all("client", chest)
        ctx.wait_for("[guest] the host grants Take All and empties the chest",
                 lambda: ctx.item_count("client", chest, SWEETROLL) == 0 and
                         ctx.item_count("client", 0x14, SWEETROLL) == held["client"][SWEETROLL] + 2, 5)
        if "ContainerMenu" in ctx.menus(""):
            raise ctx.failure("[host] the guest's opening of the chest opened the host's menu")
        left = ctx.container_items("client", chest)
        if SWEETROLL in left:
            raise ctx.failure(f"[guest] Take All left {left[SWEETROLL]} sweetrolls in the chest")
        try:
            ctx.wait_for(f"[host] chest {chest:X} holds what the guest's does ({len(left)} kinds)",
                     lambda: ctx.container_items("", chest) == left, 5)
        except TestFailure as error:
            raise ctx.failure(f"{error}: {ctx.item_differences(ctx.container_items('', chest), left, 'host', 'guest')}") from None
        copy = ctx.client_on_host()
        ctx.wait_for("[host] the guest player's copy holds the sweetrolls", lambda: ctx.item_count("", copy, SWEETROLL) == 2, 5)
        if ctx.item_count("", 0x14, SWEETROLL) != held[""][SWEETROLL]:
            raise ctx.failure("[host] the guest's Take All changed the host player's inventory")
        if (rolls := ctx.item_count("client", 0x14, SWEETROLL)) != held["client"][SWEETROLL] + 2:
            raise ctx.failure(f"[guest] the player holds {rolls} sweetrolls, not {held['client'][SWEETROLL] + 2}")
        # The host's own take must also empty the guest's copy of the chest.
        ctx.console_on("", f"{chest:X}.additem {SWEETROLL:X} 1")
        ctx.wait_for("[guest] the sweetroll is in its chest", lambda: ctx.item_count("client", chest, SWEETROLL) == 1, 5)
        ctx.take_all("", chest)
        if "ContainerMenu" in ctx.menus("client"):
            raise ctx.failure("[guest] the host's Take All opened the guest's container menu")
        ctx.wait_for("[host] Take All puts the sweetroll in the host player's inventory",
                 lambda: ctx.item_count("", 0x14, SWEETROLL) == held[""][SWEETROLL] + 1, 5)
        copy = ctx.other_player(ctx.actors_on("client"))
        ctx.wait_for("[guest] the host player's copy holds the sweetroll",
                 lambda: ctx.item_count("client", copy, SWEETROLL) == held[""][SWEETROLL] + 1, 5)
        ctx.wait_for(f"[guest] chest {chest:X} holds no sweetroll", lambda: ctx.item_count("client", chest, SWEETROLL) == 0, 5)
        if ctx.container_items("client", chest) != ctx.container_items("", chest):
            raise ctx.failure(f"[guest] chest {chest:X} holds other items than the host's")
        if (rolls := ctx.item_count("client", 0x14, SWEETROLL)) != held["client"][SWEETROLL] + 2:
            raise ctx.failure(f"[guest] the player holds {rolls} sweetrolls: the host's take reached it")
    finally:
        passed = sys.exc_info()[0] is None  # a failed step's name is the failure, not the clean-up's
        for name in ("", "client"):
            if "ContainerMenu" in ctx.menus(name):
                ctx.player_command(name, {"cmd": "menu", "name": "ContainerMenu", "show": False})
                ctx.wait_for(f"[{'guest' if name else 'host'}] the container menu is closed after cleanup",
                         lambda name=name: "ContainerMenu" not in ctx.menus(name), 5)
            for item, count in held[name].items():
                if (taken := (ctx.item_count(name, 0x14, item) or 0) - count) > 0:
                    ctx.console_on(name, f"player.removeitem {item:X} {taken}")
        ctx.set_party_gold(gold, wait=False)
        for item, count in before.items():
            if (missing := count - (ctx.item_count("", chest, item) or 0)) > 0:
                ctx.console_on("", f"{chest:X}.additem {item:X} {missing}")
        if extra := ctx.item_count("", chest, SWEETROLL):
            ctx.console_on("", f"{chest:X}.removeitem {SWEETROLL:X} {extra}")
        if passed:
            ctx.wait_for("[guest] the chest is as it was", lambda: ctx.container_items("client", chest) == before, 5)

def scenario_party_gold_is_one(ctx):
    """The party's gold is one purse (docs/design/party-purse.md): the host's player's gold, which every player and
    every copy carries. The client takes 50 gold from a chest (Take All): every player carries it. The host's player
    spends 30: every player has less. The host's game gives the client's character 25, as a quest reward its action
    ran does (rule 2): every player carries it."""
    chest = SMALL_CHEST
    before = ctx.container_items("", chest)
    held = {item: ctx.item_count("client", 0x14, item) or 0 for item in before if item != GOLD}
    gold = ctx.party_gold()
    try:
        in_chest = before.get(GOLD, 0)
        ctx.console_on("", f"{chest:X}.additem f 50")
        ctx.wait_for(f"[guest] chest {chest:X} holds the 50 gold",
                 lambda: ctx.item_count("client", chest, GOLD) == in_chest + 50, 5)
        ctx.take_all("client", chest)
        purse = gold + in_chest + 50
        ctx.wait_party_gold(f"the client's take is the party's: every player carries {purse}", purse)

        ctx.console_on("", "player.removeitem f 30")
        ctx.wait_party_gold(f"the host's spending is the party's: every player carries {purse - 30}", purse - 30)
        # The guest's HUD shows the host's spending as the host's engine did ("Gold (30) Removed").
        ctx.wait_for("[client] its HUD shows the host's own notice of the 30 gold",
                     lambda: (notice := next((m for m in reversed(ctx.hud_messages("")) if "(30)" in m), None))
                     and notice in ctx.hud_messages("client"), 10)
        for game in ("", "client"):  # everyone hears who spent it (party-purse.md, "Spending in chat")
            ctx.wait_for(f"[{game or 'host'}] its chat tells the host's player spent 30 gold",
                         lambda game=game: any(line.endswith(" 30 gold") for line in ctx.system_chat(game)), 10)

        copy = ctx.client_on_host()
        ctx.console_on("", f"{copy:X}.additem f 25")
        ctx.wait_party_gold(f"a reward on the client's character is the party's: every player carries {purse - 5}", purse - 5)
        ctx.wait_for("[client] its HUD shows the reward its character got from the host's script",
                     lambda: any("(25)" in m for m in ctx.hud_messages("client")), 10)
        ctx.hold()
    finally:
        if "ContainerMenu" in ctx.menus("client"):
            ctx.player_command("client", {"cmd": "menu", "name": "ContainerMenu", "show": False})
        for item, count in held.items():
            if (taken := (ctx.item_count("client", 0x14, item) or 0) - count) > 0:
                ctx.console_on("client", f"player.removeitem {item:X} {taken}")
        for item, count in before.items():
            if (missing := count - (ctx.item_count("", chest, item) or 0)) > 0:
                ctx.console_on("", f"{chest:X}.additem {item:X} {missing}")
            elif missing < 0:
                ctx.console_on("", f"{chest:X}.removeitem {item:X} {-missing}")
        ctx.set_party_gold(gold, wait=False)

def scenario_host_drop_reaches_guest(ctx):
    """Feature 6: the host's player drops an enchanted, tempered, renamed sword. The guest's world shows one sword
    with all of it; the guest's player picks it up, and it is gone from both worlds, in the guest's player's inventory
    (extra data and all) and on the host's copy of it."""
    swords, held, carried = ctx.heirloom_start()
    try:
        ctx.player_command("", {"cmd": "give_item", **HEIRLOOM})
        ctx.wait_for("[host] the player carries the heirloom", lambda: ctx.heirlooms("") == carried[""] + 1, 5)
        dropped = ctx.player_command("", {"cmd": "drop", "item": IRON_SWORD})["object"]
        stacks = ctx.item_stacks("", dropped)
        if not stacks or not ctx.is_heirloom(stacks[0]):
            raise ctx.failure(f"[host] the dropped sword {dropped:X} lost its extra data: {ctx.describe_stacks(stacks)}")
        twin = ctx.wait_for("[guest] one sword like the host's dropped one lies in its world",
                        lambda: ctx.new_loose_sword("client", swords["client"]), 10)
        try:
            ctx.wait_for("[guest] its sword carries the heirloom's extra data", lambda: ctx.is_heirloom(ctx.item_stacks("client", twin)[0]), 5)
        except TestFailure as error:
            raise ctx.failure(f"{error}: {ctx.describe_stacks(ctx.item_stacks('client', twin))}") from None
        ctx.player_command("client", {"cmd": "activate", "object": twin})
        ctx.wait_for("[guest] its player carries the heirloom", lambda: ctx.heirlooms("client") == carried["client"] + 1, 10)
        ctx.wait_for("[host] the dropped sword is gone from its world", lambda: dropped not in ctx.loose_swords(""), 10)
        ctx.wait_for("[guest] the sword is gone from its world", lambda: twin not in ctx.loose_swords("client"), 10)
        copy = ctx.client_on_host()
        ctx.wait_for("[host] the guest player's copy carries the sword", lambda: (ctx.item_count("", copy, IRON_SWORD) or 0) > held["client"], 5)
        if ctx.heirlooms("") != carried[""] or ctx.heirlooms("client") != carried["client"] + 1:
            raise ctx.failure(f"[both] the heirloom is carried {ctx.heirlooms('') - carried['']} more times on the host, "
                              f"{ctx.heirlooms('client') - carried['client']} on the guest")
    finally:
        ctx.put_back_heirlooms(swords, held, carried)

def scenario_guest_drop_reaches_host(ctx):
    """Feature 6: the guest's player drops the heirloom. The host's world makes the one sword the party sees, with all
    its extra data; the guest's own stays its twin (one sword, not two); the host's player picks it up and it is gone
    from both worlds."""
    swords, held, carried = ctx.heirloom_start()
    try:
        ctx.player_command("client", {"cmd": "give_item", **HEIRLOOM})
        ctx.wait_for("[guest] the player carries the heirloom", lambda: ctx.heirlooms("client") == carried["client"] + 1, 5)
        dropped = ctx.player_command("client", {"cmd": "drop", "item": IRON_SWORD})["object"]
        made = ctx.wait_for("[host] one sword like the guest's dropped one lies in its world",
                        lambda: ctx.new_loose_sword("", swords[""]), 10)
        try:
            ctx.wait_for("[host] its sword carries the heirloom's extra data", lambda: ctx.is_heirloom(ctx.item_stacks("", made)[0]), 5)
        except TestFailure as error:
            raise ctx.failure(f"{error}: {ctx.describe_stacks(ctx.item_stacks('', made))}") from None
        # The host's creation came back to the guest: its own drop is the twin, no second sword.
        ctx.settled(made)
        if (now := set(ctx.loose_swords("client")) - swords["client"]) != {dropped}:
            raise ctx.failure(f"[guest] lying in its world: {', '.join(f'{r:X}' for r in sorted(now))}, not its own drop {dropped:X} alone")
        ctx.player_command("", {"cmd": "activate", "object": made})
        ctx.wait_for("[host] its player carries the heirloom", lambda: ctx.heirlooms("") == carried[""] + 1, 10)
        ctx.wait_for("[guest] its dropped sword is gone from its world", lambda: dropped not in ctx.loose_swords("client"), 10)
        if ctx.heirlooms("client") != carried["client"]:
            raise ctx.failure("[guest] the player carries the heirloom again")
    finally:
        ctx.put_back_heirlooms(swords, held, carried)

def scenario_guest_pickup_race(ctx):
    """Feature 6: the host's player drops the heirloom and both players reach for it at once. The host's game is the
    first to decide: one player carries it, the other nothing, and it lies in neither world."""
    swords, held, carried = ctx.heirloom_start()
    try:
        ctx.player_command("", {"cmd": "give_item", **HEIRLOOM})
        ctx.wait_for("[host] the player carries the heirloom", lambda: ctx.heirlooms("") == carried[""] + 1, 5)
        dropped = ctx.player_command("", {"cmd": "drop", "item": IRON_SWORD})["object"]
        twin = ctx.wait_for("[guest] the sword lies in its world", lambda: ctx.new_loose_sword("client", swords["client"]), 10)
        _, since = ctx.log_trail("")
        ctx.player_command("client", {"cmd": "activate", "object": twin})
        ctx.player_command("", {"cmd": "activate", "object": dropped})
        ctx.wait_for("[both] the sword lies in neither world",
                 lambda: dropped not in ctx.loose_swords("") and twin not in ctx.loose_swords("client"), 10)
        gained = lambda: ctx.heirlooms("") - carried[""] + ctx.heirlooms("client") - carried["client"]
        ctx.wait_for("[both] one player carries it", lambda: gained() == 1, 5)
        if ctx.heirlooms("") > carried[""]:
            # The host's own pickup was first: the guest's request, handled after it, gets nothing.
            ctx.wait_for("[host] the guest's request found the sword gone", lambda: any(
                "already gone" in line["text"] for line in ctx.log_trail("", since)[0] if line["text"].startswith("HostItems:")), 5)
        if gained() != 1:
            raise ctx.failure(f"[both] the heirloom is carried {gained()} more times")
        log(f"the race went to the {'host' if ctx.heirlooms('') > carried[''] else 'guest'}")
    finally:
        ctx.put_back_heirlooms(swords, held, carried)

def scenario_chest_keeps_tempered_item(ctx):
    """Feature 6: a chest keeps its items' extra data. The host's player stores the heirloom in the small chest: the
    guest's chest holds it as it is. The guest's player stores a second one: the host's chest holds both as they are."""
    chest = SMALL_CHEST
    swords, held, carried = ctx.heirloom_start()
    in_chest = ctx.item_count("", chest, IRON_SWORD) or 0
    stored = ctx.heirlooms("", chest)
    try:
        ctx.player_command("", {"cmd": "give_item", **HEIRLOOM})
        ctx.wait_for("[host] the player carries the heirloom", lambda: ctx.heirlooms("") == carried[""] + 1, 5)
        ctx.player_command("", {"cmd": "store", "item": IRON_SWORD, "container": chest})
        ctx.wait_for(f"[host] chest {chest:X} holds the heirloom", lambda: ctx.heirlooms("", chest) == stored + 1, 5)
        try:
            ctx.wait_for(f"[guest] chest {chest:X} holds the heirloom as it is", lambda: ctx.heirlooms("client", chest) == stored + 1, 5)
        except TestFailure as error:
            raise ctx.failure(f"{error}: {ctx.describe_stacks(ctx.item_stacks('client', chest))}") from None
        ctx.player_command("client", {"cmd": "give_item", **HEIRLOOM})
        ctx.wait_for("[guest] the player carries the heirloom", lambda: ctx.heirlooms("client") == carried["client"] + 1, 5)
        ctx.player_command("client", {"cmd": "store", "item": IRON_SWORD, "container": chest})
        try:
            ctx.wait_for(f"[host] chest {chest:X} holds both heirlooms as they are", lambda: ctx.heirlooms("", chest) == stored + 2, 5)
        except TestFailure as error:
            raise ctx.failure(f"{error}: {ctx.describe_stacks(ctx.item_stacks('', chest))}") from None
        ctx.wait_for(f"[guest] chest {chest:X} holds both", lambda: ctx.heirlooms("client", chest) == stored + 2, 5)
    finally:
        if (extra := (ctx.item_count("", chest, IRON_SWORD) or 0) - in_chest) > 0:
            ctx.console_on("", f"{chest:X}.removeitem {IRON_SWORD:X} {extra}")
        ctx.put_back_heirlooms(swords, held, carried)

def scenario_guest_brewed_potion_reaches_host(ctx):
    """The guest brews a potion at QASmoke's alchemy lab through its menu. Each game makes its own form for it; the
    session names it by its recipe. The host's copy of the guest carries it; the guest drops it, the host's world makes
    it and its player picks it up; the host stores it in the small chest, and the guest's chest holds it too."""
    copy = ctx.player_copy("")
    chest = SMALL_CHEST
    carried = {"": ctx.brewed(""), "client": ctx.brewed("client")}
    recipe = None
    try:
        potion = ctx.brew("client", (BLUE_MOUNTAIN_FLOWER, WHEAT))
        recipe = ctx.recipe_of("client", potion)
        ctx.wait_for("[host] its copy of the guest carries the potion, as the host's own form for its recipe",
                 lambda: ctx.brewed("", copy)[recipe] == carried["client"][recipe] + 1, 10)
        dropped = ctx.player_command("client", {"cmd": "drop", "item": potion, "plain": True})["object"]
        made = ctx.wait_for("[host] the potion lies in its world", lambda: ctx.loose_brewed("", recipe), 10)
        ctx.player_command("", {"cmd": "activate", "object": made})
        ctx.wait_for("[host] its player carries the potion", lambda: ctx.brewed("")[recipe] == carried[""][recipe] + 1, 10)
        ctx.wait_for("[guest] its dropped potion is gone from its world", lambda: ctx.loose_brewed("client", recipe) is None, 10)
        ctx.player_command("", {"cmd": "store", "item": ctx.form_of_recipe("", recipe), "container": chest, "plain": True})
        ctx.wait_for(f"[guest] chest {chest:X} holds the potion", lambda: ctx.brewed("client", chest)[recipe] == 1, 10)
    finally:
        if recipe is not None:
            for instance in ("", "client"):
                for holder in (0x14, chest):
                    if form := ctx.form_of_recipe(instance, recipe, holder):
                        ctx.console_on(instance, f"{holder:X}.removeitem {form:X} 99" if holder != 0x14 else f"player.removeitem {form:X} 99")

def scenario_guest_poison_coating_reaches_host(ctx):
    """The guest brews a poison at the alchemy lab and coats its iron sword with it (a poison equipped, as the inventory
    menu's Use: the weapon's ExtraPoison). It stores the sword in the small chest: the host's chest holds the sword with
    the same poison, the host's own form for its recipe, and as many uses."""
    chest = SMALL_CHEST
    recipe = None
    swords = ctx.item_count("", chest, IRON_SWORD) or 0
    try:
        ctx.console_on("client", f"player.additem {IRON_SWORD:08X} 1")
        ctx.console_on("client", f"player.equipitem {IRON_SWORD:08X}")
        poison = ctx.brew("client", (DEATHBELL, NIGHTSHADE))
        recipe = ctx.recipe_of("client", poison)
        # As the inventory menu's Use: the game asks whether to poison the weapon in hand.
        ctx.console_on("client", f"player.equipitem {poison:08X}")
        ctx.wait_for("[guest] asks whether to poison the sword", lambda: "MessageBoxMenu" in ctx.open_menus("client"), 5)
        ctx.presses_box_button("client", POISON_YES)
        coated = lambda instance, holder: [s for s in ctx.item_stacks(instance, holder)
                                           if s["item"] == IRON_SWORD and s["poison"] and ctx.recipe_of(instance, s["poison"]) == recipe]
        uses = ctx.wait_for("[guest] its sword carries the poison", lambda: (coated("client", 0x14) or [{}])[0].get("poison_count"), 10)
        ctx.player_command("client", {"cmd": "store", "item": IRON_SWORD, "container": chest})
        try:
            stack = ctx.wait_for(f"[host] chest {chest:X} holds the sword with the guest's poison, the host's own form for it",
                             lambda: (coated("", chest) or [None])[0], 10)
        except TestFailure as error:
            raise ctx.failure(f"{error}: {ctx.item_stacks('', chest)}") from None
        if stack["poison_count"] != uses:
            raise ctx.failure(f"[host] the sword's poison has {stack['poison_count']} uses, the guest's {uses}")
    finally:
        if (extra := (ctx.item_count("", chest, IRON_SWORD) or 0) - swords) > 0:
            ctx.console_on("", f"{chest:X}.removeitem {IRON_SWORD:X} {extra}")
        ctx.console_on("client", f"player.removeitem {IRON_SWORD:X} 1")
        if recipe is not None and (form := ctx.form_of_recipe("client", recipe)):
            ctx.console_on("client", f"player.removeitem {form:X} 99")

def scenario_crafted_potion_kept_across_rejoin(ctx):
    """A guest's brewed potions are kept in its record with their recipes (crafted-potions.md): the client brews one, and
    the host's record of it holds the recipe. Its game starts again (a new process: no created forms, an empty recipe
    book) at the main menu and joins with the host's world: its player carries the potion, made again from the
    record's recipe, and the host's copy of it carries it too."""
    key = ctx.player_record("client")["key"]
    recipe = None
    try:
        potion = ctx.brew("client", (BLUE_MOUNTAIN_FLOWER, WHEAT))
        recipe = ctx.recipe_of("client", potion)
        ctx.wait_for("[host] its record of the client keeps the potion's recipe",
                 lambda: recipe in (ctx.kept_records("").get(key) or {}).get("recipes", []), 30)
        ctx.restart_client_at_main_menu()
        ctx.joins_with_world("with its brewed potion")
        ctx.wait_for("[client] its player carries the potion, made again from its recipe", lambda: ctx.brewed("client")[recipe] == 1, 30)
        copy = ctx.player_copy("")
        ctx.wait_for("[host] its copy of the client carries the potion", lambda: ctx.brewed("", copy)[recipe] == 1, 15)
    finally:
        if recipe is not None and (form := ctx.form_of_recipe("client", recipe)):
            ctx.console_on("client", f"player.removeitem {form:X} 99")

def scenario_guest_loads_propose_no_locks(ctx):
    """A guest's own world changes no lock on the host's: its cell loads and its world scripts record lock changes
    (AddLockChange from scripts' OnLoad and the engine's cell jobs), which only a player's lockpick or key may
    propose. The party goes to Riverwood and back: the host takes none of the guest's."""
    _, since = ctx.log_trail("")
    try:
        ctx.both_go(OUTDOOR_CELL)
        ctx.both_go(HOME_CELL)
    finally:
        if taken := ctx.guest_locks_taken(since):
            raise ctx.failure(f"[host] took {len(taken)} lock changes from the guest's loads: " + "; ".join(t[len("HostEngine: took a guest's "):] for t in taken[:5]))

def scenario_form_state_round_trips(ctx):
    """World-state sync step 3: a form's save data through our own buffers. The client's character base
    and the host's copy of it save the same bytes (the appearance went through FormState on spawn, form ids
    as network ids); a door's lock saved on the host, unlocked, and loaded back is locked again."""
    _game = ctx.game('')
    copy = next(iter(ctx.other_players(_game=_game)), None)
    if copy is None:
        raise ctx.failure("[host] no copy of the client's character")
    if differ := ctx.copy_base_differences(copy):
        raise ctx.failure(f"[host] the copy's base {copy:X} saves other bytes than the client's own, in the sections of "
                          f"change flags {differ}")
    log("[host] the copy's base saves the client's bytes")
    door = ctx.inner_door()
    try:
        ctx.lock_on("", door, True, 50)
        ctx.wait_for(f"[host] door {door:X} is locked at 50", lambda: ctx.door_lock("", door) == (True, 50), 10, _game=_game)
        saved = ctx.form_save("", door, flags=REFR_LOCK)
        ctx.lock_on("", door, False)
        ctx.wait_for(f"[host] door {door:X} is unlocked", lambda: not ctx.door_lock("", door)[0], 10, _game=_game)
        ctx.player_command("", {"cmd": "form_load", "form": door, "flags": REFR_LOCK, "data": saved["data"]})
        ctx.wait_for(f"[host] door {door:X} is locked at 50 again", lambda: ctx.door_lock("", door) == (True, 50), 10, _game=_game)
        if ctx.form_save("", door, flags=REFR_LOCK)["data"] != saved["data"]:
            raise ctx.failure(f"[host] door {door:X} saves other bytes after the load")
    finally:
        for name in ("", "client"):
            if ctx.door_lock(name, door)[0]:
                ctx.lock_on(name, door, False)
