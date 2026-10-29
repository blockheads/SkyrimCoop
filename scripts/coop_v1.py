"""Native v1 exit and character-knowledge checks. All learning uses normal menu input."""
import argparse

import coop_test as ct


def quit_to_menu(instance):
    value = lambda field: ct.menu_var(instance, "Journal Menu", ct.JOURNAL_SYSTEM + field)
    ct.tap_key(instance, 0x01)
    ct.wait_for("the journal opens", lambda: "Journal Menu" in ct.menus(instance), 5)
    ct.move_cursor(instance, (910, 100))
    ct.click(instance)
    ct.wait_for("the system tab opens",
                lambda: ct.menu_var(instance, "Journal Menu", ct.JOURNAL_ROOT + ".iCurrentTab") == 2, 5)
    ct.move_cursor(instance, (700, 300))
    for _ in range(12):
        selected = value("CategoryList.selectedIndex")
        if value(f"CategoryList.entryList.{int(selected)}.text") == "$QUIT":
            break
        ct.tap_key(instance, ct.DOWN_KEY)
        ct.wait_for("the category highlight moves", lambda: value("CategoryList.selectedIndex") != selected, 5)
    else:
        raise ct.TestFailure("Quit is absent from the system menu")
    ct.tap_key(instance, ct.ACTIVATE_KEY)
    ct.wait_for("the quit list opens", lambda: value("iCurrentState") == 8, 5)
    ct.move_cursor(instance, (700, 150))
    if value("PCQuitList.selectedIndex") != 0:
        ct.tap_key(instance, ct.UP_KEY)
        ct.wait_for("Main Menu is selected", lambda: value("PCQuitList.selectedIndex") == 0, 5)
    ct.tap_key(instance, ct.ACTIVATE_KEY)
    ct.wait_for("the quit confirmation opens", lambda: value("iCurrentState") == 9, 5)
    ct.move_cursor(instance, (700, 560))
    ct.click(instance)
    ct.wait_for("the game returns to the main menu", lambda: ct.game_state(instance).get("main_menu"), 30)


def guest_quits():
    """A guest uses System > Quit > Main Menu, leaves its host running, and joins again."""
    quit_to_menu("client")
    ct.wait_for("the host removes the guest", lambda: not ct.session_players(""), 15)
    if not ct.game_state("").get("hosting") or ct.game_state("client").get("online"):
        raise ct.TestFailure("Quit did not leave the guest offline and the host hosting")
    ct.joins_with_world("joins after quitting")


def host_quits():
    """The host quits through the native menu: both games leave, then can play together again."""
    name = "SkyrimCoopV1Exit"
    save = ct.save_game("", name)
    ct.wait_for("the host's checkpoint is written", ct.saves_written(save), 15)
    quit_to_menu("")
    ct.wait_for("the guest returns to the main menu", lambda: ct.game_state("client").get("main_menu"), 30)
    if ct.game_state("").get("hosting") or ct.game_state("client").get("online"):
        raise ct.TestFailure("The ended session is still active")
    if ct.join_status("client").get("reason") != "host_left":
        raise ct.TestFailure("The guest did not receive the host-left reason")
    ct.console_on("", f"load {name}")
    ct.wait_for("the host loads its checkpoint and hosts again", lambda: ct.game_state("").get("hosting"), 60)
    ct.joins_with_world("joins after the host ended the session")


def guest_leaves():
    ct.player_command("client", {"cmd": "leave_session"})
    ct.wait_for("leaving returns the guest to the main menu", lambda: ct.game_state("client").get("main_menu"), 30)
    ct.wait_for("the host removes the leaving guest", lambda: not ct.session_players(""), 15)
    if not ct.game_state("").get("hosting"):
        raise ct.TestFailure("The guest's leave stopped the host")
    ct.joins_with_world("joins after leaving")


def guest_takes_extra_stack():
    """A native chest-menu take retains the exact tempered, enchanted, named stack."""
    chest = ct.SMALL_CHEST
    swords, held, carried = ct.heirloom_start()
    stored = ct.heirlooms("", chest)
    try:
        ct.player_command("", {"cmd": "give_item", **ct.HEIRLOOM})
        ct.player_command("", {"cmd": "store", "item": ct.IRON_SWORD, "container": chest})
        ct.wait_for("the guest sees the stored heirloom", lambda: ct.heirlooms("client", chest) == stored + 1, 10)
        ct.place_before("client", chest, 110)
        ct.activate_on("client", chest)
        ct.wait_for("the chest's item list takes input",
                    lambda: ct.menu_var("client", "ContainerMenu", ct.ITEM_LISTS + ".currentState") == 2, 5)
        select_entry("client", "ContainerMenu", ct.ITEM_LISTS + ".ItemsList", ct.HEIRLOOM["name"])
        ct.tap_key("client", ct.ACTIVATE_KEY)
        ct.wait_for("the guest receives the exact stack",
                    lambda: ct.heirlooms("client") == carried["client"] + 1 and ct.heirlooms("", chest) == stored, 10)
        ct.wait_for("the host's copy carries the exact stack",
                    lambda: ct.heirlooms("", ct.client_on_host()) == carried["client"] + 1, 10)
    finally:
        if "ContainerMenu" in ct.menus("client"):
            ct.tap_key("client", ct.TAB_KEY)
            ct.wait_for("the container menu closes", lambda: "ContainerMenu" not in ct.menus("client"), 5)
        extra = ct.heirlooms("", chest) - stored
        if extra > 0:
            ct.console_on("", f"{chest:X}.removeitem {ct.IRON_SWORD:X} {extra}")
        ct.put_back_heirlooms(swords, held, carried)


def guest_loots_corpse():
    """Looting an NPC's corpse removes its stock and grants it to the guest and its copy once."""
    npc, _, copy = ct.spawn_host_npc(ct.CHICKEN, restrained=True)
    before = {}
    try:
        ct.console_on("", f"{npc:X}.additem {ct.SWEETROLL:X} 2")
        ct.wait_for("the NPC copy carries the loot", lambda: ct.item_count("client", copy, ct.SWEETROLL) == 2, 5)
        before = {item: ct.item_count("client", 0x14, item) or 0 for item in ct.container_items("", npc)}
        ct.wait_for("the NPC copy has faded in", lambda: ct.actor_state("client", copy).get("mod", {}).get("fade_state") == 0, 10)
        ct.console_on("", f"{npc:X}.kill")
        ct.wait_on("client", "the NPC copy dies", lambda actors: actors.get(str(copy), {}).get("dead"))
        ct.take_all("client", copy)
        ct.wait_for("the guest takes the corpse's loot once",
                    lambda: ct.item_count("client", 0x14, ct.SWEETROLL) == before[ct.SWEETROLL] + 2 and
                    ct.item_count("", npc, ct.SWEETROLL) == 0 and ct.item_count("client", copy, ct.SWEETROLL) == 0, 10)
    finally:
        if "ContainerMenu" in ct.menus("client"):
            ct.tap_key("client", ct.TAB_KEY)
        for item, count in before.items():
            extra = (ct.item_count("client", 0x14, item) or 0) - count
            if extra > 0:
                ct.console_on("client", f"player.removeitem {item:X} {extra}")
        ct.remove_host_npc(npc)


def knowledge(instance, form):
    return ct.player_command(instance, {"cmd": "knowledge", "form": form})


def select_entry(instance, menu, listing, wanted):
    """Walk a menu's actual highlight; fail if a named entry is absent."""
    value = lambda suffix: ct.menu_var(instance, menu, listing + suffix)
    matches = lambda text: bool(text) and text.lstrip("$").split(" (")[0].casefold() == wanted.lstrip("$").casefold()
    count = value(".entryList.length")
    if not isinstance(count, (float, int)):
        raise ct.TestFailure(f"{menu}: no entries at {listing}")
    names = [value(f".entryList.{i}.text") for i in range(int(count))]
    target = next((i for i, text in enumerate(names) if matches(text)), None)
    if target is None:
        raise ct.TestFailure(f"{menu}: {wanted} is absent ({names})")
    for _ in range(int(count) + 1):
        selected = value(".selectedIndex")
        text = value(".selectedEntry.text")
        if matches(text):
            return
        ct.tap_key(instance, ct.DOWN_KEY if selected < target else ct.UP_KEY)
        ct.wait_for(f"{menu}: the highlight moves toward {wanted}",
                    lambda: value(".selectedIndex") != selected, 5)
    raise ct.TestFailure(f"{menu}: {wanted} is absent")


def learn_ingredient(instance):
    """Eat an ingredient whose first effect the player has not learned yet."""
    for form in (0x77E1C, 0x4B0BA, 0x727E0, 0x59B86):
        before = knowledge(instance, form)
        if not before["effects"] & 1 and (not instance or not knowledge("", form)["effects"] & 1):
            break
    else:
        raise ct.TestFailure("The fixture needs an ingredient with an unknown first effect")
    ct.console_on(instance, f"player.additem {form:x} 1")
    ct.tap_key(instance, ct.TAB_KEY)
    ct.wait_for("the character menu opens", lambda: "TweenMenu" in ct.menus(instance), 5)
    ct.tap_key(instance, ct.MENU_RIGHT_KEY)
    ct.tap_key(instance, ct.ACTIVATE_KEY)
    ct.wait_for("Inventory opens", lambda: "InventoryMenu" in ct.menus(instance), 5)
    root = "_root.Menu_mc.InventoryLists_mc"
    select_entry(instance, "InventoryMenu", root + ".CategoriesList", "$Ingredients")
    ct.tap_key(instance, ct.MENU_RIGHT_KEY)
    ct.wait_for("the ingredient list opens",
                lambda: ct.menu_var(instance, "InventoryMenu", root + ".currentState") == 2, 5)
    select_entry(instance, "InventoryMenu", root + ".ItemsList", before["name"])
    ct.tap_key(instance, ct.ACTIVATE_KEY)
    ct.wait_for("eating teaches the first ingredient effect", lambda: knowledge(instance, form)["effects"] & 1, 10)
    ct.tap_key(instance, 0x0F)
    ct.wait_for("Inventory closes", lambda: "InventoryMenu" not in ct.menus(instance), 5)
    return form, knowledge(instance, form)["effects"]


def ingredient_rejoin():
    """A guest's ingredient knowledge survives a host save and a new guest process."""
    form, learned = learn_ingredient("client")
    if knowledge("", form)["effects"] & 1:
        raise ct.TestFailure("The host learned the guest's ingredient effect before the persistence check")
    key = ct.player_record("client")["key"]
    ct.wait_for("the host keeps the guest's learned ingredient effect",
                lambda: ct.kept_records("").get(key, {}).get("ingredient_effects", {}).get(f"0:{form:X}", 0) & learned == learned, 15)
    save = ct.save_game("", "SkyrimCoopIngredientKnowledge")
    ct.wait_for("the host save is written", ct.saves_written(save), 15)
    ct.restart_client_at_main_menu()
    ct.joins_with_world("returns with the saved character")
    after = knowledge("client", form)["effects"]
    if after & learned != learned:
        raise ct.TestFailure(f"Guest ingredient knowledge lost on rejoin: {form:08X}, {learned:#x} -> {after:#x}")


def ingredient_host_control():
    """The same native ingredient knowledge survives the host's own save/load."""
    form, learned = learn_ingredient("")
    name = "SkyrimCoopIngredientControl"
    save = ct.save_game("", name)
    ct.wait_for("the host control save is written", ct.saves_written(save), 15)
    reload_host_save(name)
    after = knowledge("", form)["effects"]
    if after & learned != learned:
        raise ct.TestFailure(f"Host ingredient knowledge lost: {learned:#x} -> {after:#x}")


def learn_enchantment(instance):
    form = ct.FIRE_DAMAGE_ENCHANTMENT
    if knowledge(instance, form)["known"] or (instance and knowledge("", form)["known"]):
        raise ct.TestFailure("The fixture needs an unknown Fire Damage enchantment")
    ct.player_command(instance, {"cmd": "give_item", "item": ct.IRON_SWORD, "count": 1,
                                "enchantment": form, "enchant_charge": 1000, "name": "V1 Fire Sword"})
    ct.place_before(instance, 0x101971, 110)
    ct.activate_on(instance, 0x101971)
    ct.wait_for("the enchanter opens", lambda: "Crafting Menu" in ct.menus(instance), 10)
    root = "_root.Menu.InventoryLists"
    ct.wait_for("the enchanting categories take input",
                lambda: ct.menu_var(instance, "Crafting Menu", root + ".currentState") == 1, 10)
    select_entry(instance, "Crafting Menu", root + ".CategoriesList", "$Disenchant")
    ct.tap_key(instance, ct.MENU_RIGHT_KEY)
    ct.wait_for("the disenchantment items take input",
                lambda: ct.menu_var(instance, "Crafting Menu", root + ".currentState") == 2, 5)
    select_entry(instance, "Crafting Menu", root + ".ItemsList", "V1 Fire Sword")
    ct.tap_key(instance, ct.ACTIVATE_KEY)
    ct.wait_for("the disenchantment confirmation opens", lambda: "MessageBoxMenu" in ct.menus(instance), 5)
    ct.presses_box_button(instance, 0)
    ct.wait_for("disenchanting teaches Fire Damage", lambda: knowledge(instance, form)["known"], 10)
    ct.wait_for("the learned-enchantment notice opens",
                lambda: "MessageBoxMenu" in ct.menus(instance) and
                "You learned" in (ct.menu_var(instance, "MessageBoxMenu", "_root.MessageMenu.MessageText.text") or ""), 5)
    ct.presses_box_button(instance, 0)
    ct.wait_for("the learned-enchantment notice closes", lambda: "MessageBoxMenu" not in ct.menus(instance), 5)
    ct.tap_key(instance, ct.TAB_KEY)
    ct.wait_for("the enchanter closes", lambda: "Crafting Menu" not in ct.menus(instance), 10)
    ct.wait_for("the player leaves the enchanter", lambda: ct.sit_state(instance, 0x14) == 0, 10)
    return form


def enchantment_rejoin():
    """A guest learns by disenchanting, then starts again from the host's saved world."""
    form = learn_enchantment("client")
    if knowledge("", form)["known"]:
        raise ct.TestFailure("The host learned the guest's enchantment before the persistence check")
    key = ct.player_record("client")["key"]
    base = knowledge("client", form)["form"]
    ct.wait_for("the host keeps the guest's learned enchantment",
                lambda: f"0:{base:X}" in ct.kept_records("").get(key, {}).get("enchantments", []), 15)
    save = ct.save_game("", "SkyrimCoopEnchantmentKnowledge")
    ct.wait_for("the host save is written", ct.saves_written(save), 15)
    ct.restart_client_at_main_menu()
    ct.joins_with_world("returns with the saved character")
    if not knowledge("client", form)["known"]:
        raise ct.TestFailure("Guest Fire Damage enchantment knowledge lost on rejoin")


def enchantment_host_control():
    """The host learns by disenchanting and retains that knowledge across its own save/load."""
    form = learn_enchantment("")
    name = "SkyrimCoopEnchantmentControl"
    save = ct.save_game("", name)
    ct.wait_for("the host control save is written", ct.saves_written(save), 15)
    reload_host_save(name)
    if not knowledge("", form)["known"]:
        raise ct.TestFailure("Host Fire Damage enchantment knowledge lost on load")


def reload_host_save(name):
    if ct.game_state("client").get("online"):
        ct.disconnect_client()
    ct.select_instance("")
    ct.stop_game()
    ct.launch(argparse.Namespace(cell=None, save=name, boot_timeout=240, world_timeout=180),
              ct.new_run_dir("knowledge-host-control"))
    ct.select_instance("")
    ct.start_session("")
    ct.joins_with_world("rejoins the host's saved control world")


def save_does_not_update_frozen_world():
    """Saving drains a frozen VM: that drain must not run the full co-op world update."""
    before = ct.player_command("", {"cmd": "memory_sites"})["updates"]
    save = ct.save_game("", "SkyrimCoopUpdateFreeze")
    ct.wait_for("the host save and co-save are written", ct.saves_written(save), 30)
    after = ct.player_command("", {"cmd": "memory_sites"})["updates"]
    if after["frozen"] != before["frozen"]:
        raise ct.TestFailure(f"the save ran {after['frozen'] - before['frozen']} full world updates while the VM was frozen")
    if after["update_overlaps"] != before["update_overlaps"]:
        raise ct.TestFailure("full co-op world updates overlapped during the save")


def register(scenarios, tags):
    entries = {
        "save-does-not-update-frozen-world": (save_does_not_update_frozen_world, {"save", "v1"}),
        "guest-quits-to-main-menu": (guest_quits, {"session", "restart"}),
        "host-quits-to-main-menu": (host_quits, {"session", "restart"}),
        "guest-leaves-to-main-menu": (guest_leaves, {"session", "restart"}),
        "guest-container-take-keeps-extra-data": (guest_takes_extra_stack, {"inventory", "objects"}),
        "guest-loots-corpse-once": (guest_loots_corpse, {"inventory", "objects"}),
        "guest-ingredient-knowledge-rejoin": (ingredient_rejoin, {"inventory", "save", "restart"}),
        "host-ingredient-knowledge-control": (ingredient_host_control, {"inventory", "save", "restart"}),
        "guest-enchantment-knowledge-rejoin": (enchantment_rejoin, {"inventory", "save", "restart"}),
        "host-enchantment-knowledge-control": (enchantment_host_control, {"inventory", "save", "restart"}),
    }
    for name, (run, labels) in entries.items():
        scenarios[name] = run
        tags[name] = labels
