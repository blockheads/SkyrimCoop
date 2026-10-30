"""Scenarios v1 for the co-op harness."""
from . import followers
import functools
from .combat import scenario_death_wipe_checkpoint
from ..common import (
    ACTIVATE_KEY,
    CHICKEN,
    DOWN_KEY,
    FIRE_DAMAGE_ENCHANTMENT,
    HEIRLOOM,
    INN,
    INN_DOOR_INSIDE,
    IRON_SWORD,
    ITEM_LISTS,
    JOURNAL_ROOT,
    JOURNAL_SYSTEM,
    MENU_RIGHT_KEY,
    SMALL_CHEST,
    SWEETROLL,
    TAB_KEY,
    UP_KEY,
    WOLF,
)
from ..errors import TestFailure
from ..utilities import log

import argparse
import time


def quit_to_menu(ctx, instance):
    value = lambda field: ctx.menu_var(instance, "Journal Menu", JOURNAL_SYSTEM + field)
    ctx.tap_key(instance, 0x01)
    ctx.wait_for("the journal opens", lambda: "Journal Menu" in ctx.menus(instance), 5)
    ctx.move_cursor(instance, (910, 100))
    ctx.click(instance)
    ctx.wait_for("the system tab opens",
                lambda: ctx.menu_var(instance, "Journal Menu", JOURNAL_ROOT + ".iCurrentTab") == 2, 5)
    ctx.move_cursor(instance, (700, 300))
    for _ in range(12):
        selected = value("CategoryList.selectedIndex")
        if value(f"CategoryList.entryList.{int(selected)}.text") == "$QUIT":
            break
        ctx.tap_key(instance, DOWN_KEY)
        ctx.wait_for("the category highlight moves", lambda: value("CategoryList.selectedIndex") != selected, 5)
    else:
        raise ctx.failure("Quit is absent from the system menu")
    ctx.tap_key(instance, ACTIVATE_KEY)
    ctx.wait_for("the quit list opens", lambda: value("iCurrentState") == 8, 5)
    ctx.move_cursor(instance, (700, 150))
    if value("PCQuitList.selectedIndex") != 0:
        ctx.tap_key(instance, UP_KEY)
        ctx.wait_for("Main Menu is selected", lambda: value("PCQuitList.selectedIndex") == 0, 5)
    ctx.tap_key(instance, ACTIVATE_KEY)
    ctx.wait_for("the quit confirmation opens", lambda: value("iCurrentState") == 9, 5)
    ctx.move_cursor(instance, (700, 560))
    ctx.click(instance)
    ctx.wait_for("the game returns to the main menu", lambda: ctx.game_state(instance).get("main_menu"), 30)

def guest_quits(ctx):
    """A guest uses System > Quit > Main Menu, leaves its host running, and joins again."""
    quit_to_menu(ctx, "client")
    ctx.wait_for("the host removes the guest", lambda: not ctx.session_players(""), 15)
    if not ctx.game_state("").get("hosting") or ctx.game_state("client").get("online"):
        raise ctx.failure("Quit did not leave the guest offline and the host hosting")
    ctx.joins_with_world("joins after quitting")

def host_quits(ctx):
    """The host quits through the native menu: both games leave, then can play together again."""
    _game = ctx.game()
    name = "SkyrimCoopV1Exit"
    save = ctx.save_game("", name)
    ctx.wait_for("the host's checkpoint is written", ctx.saves_written(save), 15)
    quit_to_menu(ctx, "")
    ctx.wait_for("the guest returns to the main menu", lambda: ctx.game_state("client").get("main_menu"), 30)
    if ctx.game_state("").get("hosting") or ctx.game_state("client").get("online"):
        raise ctx.failure("The ended session is still active")
    if ctx.join_status("client").get("reason") != "host_left":
        raise ctx.failure("The guest did not receive the host-left reason")
    ctx.console_on("", f"load {name}")
    ctx.wait_for("the host loads its checkpoint and hosts again", lambda: ctx.game_state("").get("hosting"), 60)
    ctx.joins_with_world("joins after the host ended the session")

def guest_leaves(ctx):
    ctx.player_command("client", {"cmd": "leave_session"})
    ctx.wait_for("leaving returns the guest to the main menu", lambda: ctx.game_state("client").get("main_menu"), 30)
    ctx.wait_for("the host removes the leaving guest", lambda: not ctx.session_players(""), 15)
    if not ctx.game_state("").get("hosting"):
        raise ctx.failure("The guest's leave stopped the host")
    ctx.joins_with_world("joins after leaving")

def guest_takes_extra_stack(ctx):
    """A native chest-menu take retains the exact tempered, enchanted, named stack."""
    chest = SMALL_CHEST
    swords, held, carried = ctx.heirloom_start()
    stored = ctx.heirlooms("", chest)
    try:
        ctx.player_command("", {"cmd": "give_item", **HEIRLOOM})
        ctx.player_command("", {"cmd": "store", "item": IRON_SWORD, "container": chest})
        ctx.wait_for("the guest sees the stored heirloom", lambda: ctx.heirlooms("client", chest) == stored + 1, 10)
        ctx.place_before("client", chest, 110)
        ctx.activate_on("client", chest)
        ctx.wait_for("the chest's item list takes input",
                    lambda: ctx.menu_var("client", "ContainerMenu", ITEM_LISTS + ".currentState") == 2, 5)
        ctx.select_entry("client", "ContainerMenu", ITEM_LISTS + ".ItemsList", HEIRLOOM["name"])
        ctx.tap_key("client", ACTIVATE_KEY)
        ctx.wait_for("the guest receives the exact stack",
                    lambda: ctx.heirlooms("client") == carried["client"] + 1 and ctx.heirlooms("", chest) == stored, 10)
        ctx.wait_for("the host's copy carries the exact stack",
                    lambda: ctx.heirlooms("", ctx.client_on_host()) == carried["client"] + 1, 10)
    finally:
        if "ContainerMenu" in ctx.menus("client"):
            ctx.tap_key("client", TAB_KEY)
            ctx.wait_for("the container menu closes", lambda: "ContainerMenu" not in ctx.menus("client"), 5)
        extra = ctx.heirlooms("", chest) - stored
        if extra > 0:
            ctx.console_on("", f"{chest:X}.removeitem {IRON_SWORD:X} {extra}")
        ctx.put_back_heirlooms(swords, held, carried)

def guest_loots_corpse(ctx):
    """Looting an NPC's corpse removes its stock and grants it to the guest and its copy once."""
    npc, _, copy = ctx.spawn_host_npc(CHICKEN, restrained=True)
    before = {}
    try:
        ctx.console_on("", f"{npc:X}.additem {SWEETROLL:X} 2")
        ctx.wait_for("the NPC copy carries the loot", lambda: ctx.item_count("client", copy, SWEETROLL) == 2, 5)
        before = {item: ctx.item_count("client", 0x14, item) or 0 for item in ctx.container_items("", npc)}
        ctx.wait_for("the NPC copy has faded in", lambda: ctx.actor_state("client", copy).get("mod", {}).get("fade_state") == 0, 10)
        ctx.console_on("", f"{npc:X}.kill")
        ctx.wait_on("client", "the NPC copy dies", lambda actors: actors.get(str(copy), {}).get("dead"))
        ctx.take_all("client", copy)
        ctx.wait_for("the guest takes the corpse's loot once",
                    lambda: ctx.item_count("client", 0x14, SWEETROLL) == before[SWEETROLL] + 2 and
                    ctx.item_count("", npc, SWEETROLL) == 0 and ctx.item_count("client", copy, SWEETROLL) == 0, 10)
    finally:
        if "ContainerMenu" in ctx.menus("client"):
            ctx.tap_key("client", TAB_KEY)
        for item, count in before.items():
            extra = (ctx.item_count("client", 0x14, item) or 0) - count
            if extra > 0:
                ctx.console_on("client", f"player.removeitem {item:X} {extra}")
        ctx.remove_host_npc(npc)

def knowledge(ctx, instance, form):
    return ctx.player_command(instance, {"cmd": "knowledge", "form": form})

def learn_ingredient(ctx, instance):
    """Eat an ingredient whose first effect the player has not learned yet."""
    for form in (0x77E1C, 0x4B0BA, 0x727E0, 0x59B86):
        before = knowledge(ctx, instance, form)
        if not before["effects"] & 1 and (not instance or not knowledge(ctx, "", form)["effects"] & 1):
            break
    else:
        raise ctx.failure("The fixture needs an ingredient with an unknown first effect")
    ctx.console_on(instance, f"player.additem {form:x} 1")
    ctx.tap_key(instance, TAB_KEY)
    ctx.wait_for("the character menu opens", lambda: "TweenMenu" in ctx.menus(instance), 5)
    ctx.tap_key(instance, MENU_RIGHT_KEY)
    ctx.tap_key(instance, ACTIVATE_KEY)
    ctx.wait_for("Inventory opens", lambda: "InventoryMenu" in ctx.menus(instance), 5)
    root = "_root.Menu_mc.InventoryLists_mc"
    ctx.select_entry(instance, "InventoryMenu", root + ".CategoriesList", "$Ingredients")
    ctx.tap_key(instance, MENU_RIGHT_KEY)
    ctx.wait_for("the ingredient list opens",
                lambda: ctx.menu_var(instance, "InventoryMenu", root + ".currentState") == 2, 5)
    ctx.select_entry(instance, "InventoryMenu", root + ".ItemsList", before["name"])
    ctx.tap_key(instance, ACTIVATE_KEY)
    ctx.wait_for("eating teaches the first ingredient effect", lambda: knowledge(ctx, instance, form)["effects"] & 1, 10)
    ctx.tap_key(instance, 0x0F)
    ctx.wait_for("Inventory closes", lambda: "InventoryMenu" not in ctx.menus(instance), 5)
    return form, knowledge(ctx, instance, form)["effects"]

def ingredient_rejoin(ctx):
    """A guest's ingredient knowledge survives a host save and a new guest process."""
    _game = ctx.game()
    form, learned = learn_ingredient(ctx, "client")
    if knowledge(ctx, "", form)["effects"] & 1:
        raise ctx.failure("The host learned the guest's ingredient effect before the persistence check")
    key = ctx.player_record("client")["key"]
    ctx.wait_for("the host keeps the guest's learned ingredient effect",
                lambda: ctx.kept_records("").get(key, {}).get("ingredient_effects", {}).get(f"0:{form:X}", 0) & learned == learned, 15)
    save = ctx.save_game("", "SkyrimCoopIngredientKnowledge")
    ctx.wait_for("the host save is written", ctx.saves_written(save), 15)
    ctx.restart_client_at_main_menu()
    ctx.joins_with_world("returns with the saved character")
    after = knowledge(ctx, "client", form)["effects"]
    if after & learned != learned:
        raise ctx.failure(f"Guest ingredient knowledge lost on rejoin: {form:08X}, {learned:#x} -> {after:#x}")

def ingredient_host_control(ctx):
    """The same native ingredient knowledge survives the host's own save/load."""
    _game = ctx.game()
    form, learned = learn_ingredient(ctx, "")
    name = "SkyrimCoopIngredientControl"
    save = ctx.save_game("", name)
    ctx.wait_for("the host control save is written", ctx.saves_written(save), 15)
    reload_host_save(ctx, name)
    after = knowledge(ctx, "", form)["effects"]
    if after & learned != learned:
        raise ctx.failure(f"Host ingredient knowledge lost: {learned:#x} -> {after:#x}")

def learn_enchantment(ctx, instance):
    form = FIRE_DAMAGE_ENCHANTMENT
    if knowledge(ctx, instance, form)["known"] or (instance and knowledge(ctx, "", form)["known"]):
        raise ctx.failure("The fixture needs an unknown Fire Damage enchantment")
    ctx.player_command(instance, {"cmd": "give_item", "item": IRON_SWORD, "count": 1,
                                "enchantment": form, "enchant_charge": 1000, "name": "V1 Fire Sword"})
    ctx.place_before(instance, 0x101971, 110)
    ctx.activate_on(instance, 0x101971)
    ctx.wait_for("the enchanter opens", lambda: "Crafting Menu" in ctx.menus(instance), 10)
    root = "_root.Menu.InventoryLists"
    ctx.wait_for("the enchanting categories take input",
                lambda: ctx.menu_var(instance, "Crafting Menu", root + ".currentState") == 1, 10)
    ctx.select_entry(instance, "Crafting Menu", root + ".CategoriesList", "$Disenchant")
    ctx.tap_key(instance, MENU_RIGHT_KEY)
    ctx.wait_for("the disenchantment items take input",
                lambda: ctx.menu_var(instance, "Crafting Menu", root + ".currentState") == 2, 5)
    ctx.select_entry(instance, "Crafting Menu", root + ".ItemsList", "V1 Fire Sword")
    ctx.tap_key(instance, ACTIVATE_KEY)
    ctx.wait_for("the disenchantment confirmation opens", lambda: "MessageBoxMenu" in ctx.menus(instance), 5)
    ctx.presses_box_button(instance, 0)
    ctx.wait_for("disenchanting teaches Fire Damage", lambda: knowledge(ctx, instance, form)["known"], 10)
    ctx.wait_for("the learned-enchantment notice opens",
                lambda: "MessageBoxMenu" in ctx.menus(instance) and
                "You learned" in (ctx.menu_var(instance, "MessageBoxMenu", "_root.MessageMenu.MessageText.text") or ""), 5)
    ctx.presses_box_button(instance, 0)
    ctx.wait_for("the learned-enchantment notice closes", lambda: "MessageBoxMenu" not in ctx.menus(instance), 5)
    ctx.tap_key(instance, TAB_KEY)
    ctx.wait_for("the enchanter closes", lambda: "Crafting Menu" not in ctx.menus(instance), 10)
    ctx.wait_for("the player leaves the enchanter", lambda: ctx.sit_state(instance, 0x14) == 0, 10)
    return form

def enchantment_rejoin(ctx):
    """A guest learns by disenchanting, then starts again from the host's saved world."""
    _game = ctx.game()
    form = learn_enchantment(ctx, "client")
    if knowledge(ctx, "", form)["known"]:
        raise ctx.failure("The host learned the guest's enchantment before the persistence check")
    key = ctx.player_record("client")["key"]
    base = knowledge(ctx, "client", form)["form"]
    ctx.wait_for("the host keeps the guest's learned enchantment",
                lambda: f"0:{base:X}" in ctx.kept_records("").get(key, {}).get("enchantments", []), 15)
    save = ctx.save_game("", "SkyrimCoopEnchantmentKnowledge")
    ctx.wait_for("the host save is written", ctx.saves_written(save), 15)
    ctx.restart_client_at_main_menu()
    ctx.joins_with_world("returns with the saved character")
    if not knowledge(ctx, "client", form)["known"]:
        raise ctx.failure("Guest Fire Damage enchantment knowledge lost on rejoin")

def enchantment_host_control(ctx):
    """The host learns by disenchanting and retains that knowledge across its own save/load."""
    _game = ctx.game()
    form = learn_enchantment(ctx, "")
    name = "SkyrimCoopEnchantmentControl"
    save = ctx.save_game("", name)
    ctx.wait_for("the host control save is written", ctx.saves_written(save), 15)
    reload_host_save(ctx, name)
    if not knowledge(ctx, "", form)["known"]:
        raise ctx.failure("Host Fire Damage enchantment knowledge lost on load")

def reload_host_save(ctx, name):
    _game = ctx.game()
    if ctx.game_state("client").get("online"):
        ctx.disconnect_client()
    _game = ctx.game('')
    ctx.stop_game(_game=_game)
    ctx.launch(argparse.Namespace(cell=None, save=name, boot_timeout=240, world_timeout=180),
              ctx.new_run_dir("knowledge-host-control"), _game=_game)
    _game = ctx.game('')
    ctx.start_session("", _game=_game)
    ctx.joins_with_world("rejoins the host's saved control world")

def save_does_not_update_frozen_world(ctx):
    """Saving drains a frozen VM: that drain must not run the full co-op world update."""
    _game = ctx.game()
    before = ctx.player_command("", {"cmd": "memory_sites"})["updates"]
    save = ctx.save_game("", "SkyrimCoopUpdateFreeze")
    ctx.wait_for("the host save and co-save are written", ctx.saves_written(save), 30)
    after = ctx.player_command("", {"cmd": "memory_sites"})["updates"]
    if after["frozen"] != before["frozen"]:
        raise ctx.failure(f"the save ran {after['frozen'] - before['frozen']} full world updates while the VM was frozen")
    if after["update_overlaps"] != before["update_overlaps"]:
        raise ctx.failure("full co-op world updates overlapped during the save")

def plays_on(ctx, after, seconds=60):
    """The party plays on after a load: a wolf, spawned where the host stands, fights the guest's character on the
    host's game while every ~10 s both games must still be advancing (no freeze; a crash fails any wait), with no
    lock-order report of ours; then the copies agree with their owners and the players kept their items."""
    items = {game: ctx.player_record(game)["items"] for game in ("", "client")}
    known = ctx.lock_order_reports()
    wolf = ctx.spawn_beside_host(WOLF)
    try:
        ctx.console_on("", f"{wolf:X}.startcombat {ctx.client_on_host():X}")
        end = time.monotonic() + seconds
        while time.monotonic() < end:
            for game in ("", "client"):
                ctx.console_on(game, "player.restoreav health 10000")
            # Each round is 300 frames of both games (~5-10 s): a frozen game fails it.
            frames = {game: ctx.game_frame(game) for game in ("", "client")}
            ctx.wait_for(f"[both] keep playing after the {after}", lambda: all(ctx.game_frame(game) > frame + 300
                                                                            for game, frame in frames.items()), 20)
            ours = [f"{game}: {report}" for sites, (game, report, mine) in ctx.lock_order_reports().items()
                    if mine and sites not in known]
            if ours:
                raise ctx.failure(f"lock order after the {after} (our code on the stack): " + " | ".join(ours))
    finally:
        ctx.console_on("", f"{wolf:X}.kill")
        ctx.remove_host_npc(wolf)
    if problems := ctx.check_desync():
        raise ctx.failure(f"desync after the {after}: " + " | ".join(problems))
    for game, before in items.items():
        if ctx.player_record(game)["items"] != before:
            raise ctx.failure(f"[{game or 'host'}] its items changed while playing on after the {after}")

def goes_with_follower(ctx, cell, npc):
    """The party goes to the cell (the harness's coc, which takes no follower along) and the follower is put beside it."""
    ctx.both_go(cell)
    ctx.player_command("", {"cmd": "move_actor", "actor": npc, "position": ctx.my_position("client")})
    ctx.wait_for("the follower is beside the party", lambda: ctx.actor_place("", npc).get("cell") == ctx.player_cell(""), 15)

def door_autosave_loads(ctx, npc):
    """The party walks out of the inn with the follower: the host's game autosaves at the load door, and that
    autosave, loaded from the Load menu, restores the party outside."""
    guest_level = ctx.player_record("client")["level"]
    goes_with_follower(ctx, INN, npc)
    ctx.console_on("", "set gamehour to 12")
    before, guest_cell = ctx.save_files(""), ctx.player_cell("client")
    ctx.travel("", lambda: ctx.activate_on("", INN_DOOR_INSIDE), ctx.player_cell(""))
    ctx.follows("client", guest_cell)
    autosave = ctx.wait_for("the load door autosaves the host's game",
                           lambda: sorted(n for n in ctx.save_files("") - before if n.startswith("Autosave")), 30)[0]
    ctx.wait_for("the autosave and its co-save are written", ctx.saves_written(ctx.save_file("", autosave[:-4])), 15)
    outside = ctx.player_cell("")
    follower_still_follows(ctx, npc, "door, before the load")
    ctx.moves_past_save(guest_level)
    ctx.load_latest_from_menu("")
    ctx.party_restored(guest_level)
    if {ctx.player_cell(game) for game in ("", "client")} != {outside}:
        raise ctx.failure("the door's autosave did not restore the party outside the inn")
    follower_still_follows(ctx, npc, "door autosave, outside")

def follower_still_follows(ctx, npc, after, timeout=15):
    """The follower recruited before the loads is still the party's, following the guest's new copy. After a fight
    the engine's follower first ends combat (its line, then the package's target resolves again: ~10 s)."""
    log(f"=== gate: the follower after the {after}")
    ctx.wait_for("the follower is still in the follower slot, not waiting",
                lambda: ctx.follower_in_slot() == npc and ctx.actor_value("", npc, 95) == 0, 30)
    try:
        followers.follows(ctx, npc, lambda: ctx.player_copy(""), timeout)
        copy = ctx.player_copy("")
        ctx.wait_for("the follower is with its guest", lambda: ctx.actor_place("", npc).get("cell") ==
                    ctx.actor_place("", copy).get("cell"), 30)
    except TestFailure as failure:
        place, ai = ctx.actor_place("", npc), ctx.actor_state("", npc).get("ai", {})
        raise ctx.failure(f"{failure} (the follower in cell {place.get('cell')} at {place.get('position')}, "
                             f"AI {ai}; the guest's copy {ctx.player_copy('')} in cell "
                             f"{ctx.actor_place('', ctx.player_copy('')).get('cell')})") from None

def save_load_gate(ctx):
    """The reload-freeze gate (v1-playtest-todo.md): with a guest in and the follower it recruited, the host's game
    saves and loads each way a player does (quicksave and quickload, a save loaded from the Load menu, the load door's
    autosave, a named save loaded by name, a party wipe back to its checkpoint), and after each the party plays on
    through a fight, checked every ~10 s, with the follower still following its guest."""
    with followers.recruited(ctx) as (npc, _, _):
        followers.follows(ctx, npc, lambda: ctx.player_copy(""))
        # The door last: the party ends outside the inn, and the fight after it is there.
        for how, load in (("quickload", lambda: ctx.host_loads_party(True)),
                          ("Load menu", lambda: ctx.host_loads_party(False, menu=True)),
                          ("named load", lambda: ctx.host_loads_party(False)),
                          ("party wipe", functools.partial(scenario_death_wipe_checkpoint, ctx)),
                          ("door autosave", lambda: door_autosave_loads(ctx, npc))):
            log(f"=== gate: {how}")
            load()
            follower_still_follows(ctx, npc, how)
            plays_on(ctx, how)
            follower_still_follows(ctx, npc, f"{how} and its fight", 30)
