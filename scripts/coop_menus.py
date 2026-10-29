"""Feature 15: personal menus leave the host's world and each guest's own simulation running."""
import functools

import coop_test as ct

PAUSE_FLAGS = 0x1000021


def assert_live(game, menu):
    """Read the engine's flags, then observe regeneration while the menu keeps its input."""
    ct.wait_for(f"[{game or 'host'}] {menu} opens", lambda: menu in ct.menus(game), 10)
    before = ct.player_snapshot(game)["values"]["stamina"]
    hour = ct.game_state("")["time"]["hour"]

    def running():
        state = ct.player_command(game, {"cmd": "menu_flags", "name": menu})
        if state["flags"] is None:
            raise ct.TestFailure(f"{menu} closed before its simulation check")
        if state["flags"] & PAUSE_FLAGS or state["pauses"]:
            raise ct.TestFailure(f"{menu} still pauses: {state}")
        return ct.player_snapshot(game)["values"]["stamina"] > before + 5

    ct.wait_for(f"[{game or 'host'}] its player regenerates stamina with {menu} open", running, 15)
    ct.wait_for("the host's world clock keeps advancing", lambda: ct.game_state("")["time"]["hour"] != hour, 5)


def damage_stamina(game):
    ct.console_on(game, "player.restoreav stamina 1000")
    ct.console_on(game, "player.damageav stamina 70")


def close(game, menu):
    if menu in ct.menus(game):
        ct.tap_key(game, ct.TAB_KEY)
        ct.wait_for(f"[{game or 'host'}] {menu} closes", lambda: menu not in ct.menus(game), 10)


def map_runs(game):
    try:
        ct.both_go(ct.OUTDOOR_CELL)
        ct.player_command(game, {"cmd": "first_person"})
        was_hidden = ct.player_command(game, {"cmd": "menu_flags", "name": "MapMenu"})["first_person_hidden"]
        damage_stamina(game)
        ct.open_world_map(game)
        local = lambda: ct.player_command(game, {"cmd": "menu_flags", "name": "MapMenu"})["local_map"]
        ct.wait_for("the map hides first-person arms", lambda: ct.player_command(game,
                    {"cmd": "menu_flags", "name": "MapMenu"})["first_person_hidden"] is True, 5)
        assert_live(game, "MapMenu")
        ct.tap_key(game, 0x26)
        ct.wait_for("the map switches to local view", lambda: local() is True, 10)
        ct.wait_for("the local map takes input", lambda: ct.player_command(game,
                    {"cmd": "menu_flags", "name": "MapMenu"})["local_map_ready"] is True, 10)
        damage_stamina(game)
        assert_live(game, "MapMenu")
        ct.tap_key(game, 0x26)
        ct.wait_for("the map returns to world view", lambda: not local() and ct.map_takes_input(game), 10)
        close(game, "MapMenu")
        ct.wait_for("the player's first-person camera returns",
                    lambda: ct.player_command(game, {"cmd": "camera"})["state"] == 0, 5)
        ct.wait_for("the first-person model regains its original visibility", lambda: ct.player_command(game,
                    {"cmd": "menu_flags", "name": "MapMenu"})["first_person_hidden"] == was_hidden, 5)
    finally:
        close(game, "MapMenu")
        ct.both_go(ct.HOME_CELL)


def journal_runs(game):
    try:
        damage_stamina(game)
        ct.tap_key(game, ct.ESCAPE_KEY)
        assert_live(game, "Journal Menu")
    finally:
        close(game, "Journal Menu")


def lock_runs(game):
    door = ct.inner_door()
    previous = ct.door_lock("", door)
    picks = ct.item_count(game, 0x14, 0xA)
    try:
        if not picks:
            ct.console_on(game, "player.additem a 1")
        ct.lock_on("", door, True, 25)
        ct.wait_for("the lock reaches the guest", lambda: ct.door_lock("client", door) == (True, 25), 10)
        damage_stamina(game)
        ct.place_before(game, door, 100)
        ct.activate_on(game, door)
        assert_live(game, "Lockpicking Menu")
    finally:
        close(game, "Lockpicking Menu")
        ct.lock_on("", door, previous[0], previous[1] or 0)
        if not picks:
            ct.console_on(game, "player.removeitem a 1")


def training_runs(game):
    npc, _, copy = ct.spawn_host_npc(ct.AMREN, restrained=True)
    try:
        damage_stamina(game)
        # The actual native invoked by the trainer's dialogue fragment.
        ct.player_script_call(game, "Game", "ShowTrainingMenu", [{"form": copy if game else npc}])
        assert_live(game, "Training Menu")
    finally:
        close(game, "Training Menu")
        ct.remove_host_npc(npc)


def crafting_runs(game):
    try:
        damage_stamina(game)
        ct.place_before(game, ct.COOKING_SPIT, 100)
        ct.activate_on(game, ct.COOKING_SPIT)
        ct.wait_for("the player reaches the cooking spit",
                    lambda: ct.sit_state(game, 0x14) == ct.IN_FURNITURE, 15)
        assert_live(game, "Crafting Menu")
    finally:
        close(game, "Crafting Menu")
        if ct.sit_state(game, 0x14) == ct.IN_FURNITURE:
            ct.activate_on(game, ct.COOKING_SPIT)
        ct.wait_for("the player finishes leaving the spit", lambda: ct.sit_state(game, 0x14) == 0, 10)


def barter_runs(game):
    npc, _, copy = ct.spawn_host_npc("00013BA1", restrained=True)  # Belethor
    try:
        damage_stamina(game)
        ct.player_script_call(game, "Actor", "ShowBarterMenu", on=copy if game else npc)
        assert_live(game, "BarterMenu")
    finally:
        close(game, "BarterMenu")
        ct.remove_host_npc(npc)


def npcs_keep_simulating():
    npc, _, copy = ct.spawn_host_npc(ct.AMREN, restrained=True)
    try:
        for game in ("", "client"):
            ct.tap_key(game, ct.ESCAPE_KEY)
            ct.wait_for("both journals open", lambda game=game: "Journal Menu" in ct.menus(game), 10)
        floor = ct.host_npc_state(npc)["position"]
        raised = [floor[0], floor[1], floor[2] + 400]
        ct.player_command("", {"cmd": "move_actor", "actor": npc, "position": raised})
        ct.wait_for("the host shows the raised NPC",
                    lambda: ct.host_npc_state(npc)["position"][2] > floor[2] + 200, 5)
        ct.wait_for("the guest shows the raised NPC copy",
                    lambda: ct.actors_on("client")[str(copy)]["actual"][2] > floor[2] + 100, 5)
        ct.wait_for("the host's NPC falls while both journals stay open",
                    lambda: ct.host_npc_state(npc)["position"][2] < floor[2] + 80, 10)
        ct.wait_for("the guest's NPC copy follows the fall",
                    lambda: ct.actors_on("client")[str(copy)]["actual"][2] < floor[2] + 80, 10)
        for game in ("", "client"):
            if "Journal Menu" not in ct.menus(game):
                raise ct.TestFailure("a journal closed during the fall check")
    finally:
        for game in ("", "client"):
            close(game, "Journal Menu")
        ct.remove_host_npc(npc)


def journal_save(game):
    """Choose New Save through the journal, including the guest's refusal through the same callback."""
    folder = ct.save_file(game, "menus").parent
    before = {p.name: p.stat().st_mtime_ns for p in folder.glob("*.ess")}
    value = lambda field: ct.menu_var(game, "Journal Menu", ct.JOURNAL_SYSTEM + field)
    try:
        ct.tap_key(game, ct.ESCAPE_KEY)
        ct.wait_for("the journal opens", lambda: "Journal Menu" in ct.menus(game), 5)
        ct.move_cursor(game, (910, 100))
        ct.click(game)
        ct.wait_for("the system tab opens", lambda: ct.menu_var(game, "Journal Menu", ct.JOURNAL_ROOT + ".iCurrentTab") == 2, 5)
        ct.move_cursor(game, (700, 300))
        for _ in range(12):
            selected = value("CategoryList.selectedIndex")
            if value(f"CategoryList.entryList.{int(selected)}.text") == "$SAVE":
                break
            ct.tap_key(game, ct.DOWN_KEY)
            ct.wait_for("the category highlight moves", lambda: value("CategoryList.selectedIndex") != selected, 5)
        else:
            raise ct.TestFailure("Save is absent from the system menu")
        ct.tap_key(game, ct.ACTIVATE_KEY)
        ct.wait_for("the save list opens", lambda: value("iCurrentState") == 1 and
                    value("SaveLoadListHolder.List_mc.entryList.length"), 10)
        ct.move_cursor(game, (700, 150))
        for _ in range(40):
            selected = value("SaveLoadListHolder.List_mc.selectedIndex")
            if selected == 0:
                break
            ct.tap_key(game, ct.DOWN_KEY if selected < 0 else ct.UP_KEY)
            ct.wait_for("the save highlight moves", lambda: value("SaveLoadListHolder.List_mc.selectedIndex") != selected, 5)
        else:
            raise ct.TestFailure("couldn't select New Save")
        ct.tap_key(game, ct.ACTIVATE_KEY)
        if game:
            ct.wait_for("the guest's journal save is refused in chat", lambda: any("Only the host saves" in
                        line["text"] for line in ct.chat(game).get("lines", [])), 10)
            after = {p.name: p.stat().st_mtime_ns for p in folder.glob("*.ess")}
            if after != before:
                raise ct.TestFailure("the guest's journal wrote a save")
        else:
            saved = ct.wait_for("the host's journal writes its save", lambda: next((p for p in folder.glob("*.ess")
                                if before.get(p.name) != p.stat().st_mtime_ns), None), 15)
            ct.wait_for("the journal's save and co-save finish", ct.saves_written(saved), 15)
    finally:
        # A save may already have closed the journal. Force-hide only cleans up a refused save's list.
        if "Journal Menu" in ct.menus(game):
            ct.player_command(game, {"cmd": "menu", "name": "Journal Menu", "show": False})
            ct.wait_for("the journal closes after saving", lambda: "Journal Menu" not in ct.menus(game), 10)


def offline_journal_pauses():
    ct.disconnect_client()
    try:
        ct.tap_key("client", ct.ESCAPE_KEY)
        ct.wait_for("the offline journal opens", lambda: "Journal Menu" in ct.menus("client"), 5)
        state = ct.player_command("client", {"cmd": "menu_flags", "name": "Journal Menu"})
        if not state["flags"] & 1 or not state["pauses"]:
            raise ct.TestFailure(f"the offline journal no longer pauses: {state}")
    finally:
        close("client", "Journal Menu")
        ct.rejoin_client()


def book_runs(game):
    book = 0x1ACD1  # Skyrim.esm Book2CommonManualMixedUnitTactics, no skill or quest side effects
    before = set(ct.world_state(""))
    made = set()
    try:
        ct.console_on("", f"player.placeatme {book:X} 1")
        made = ct.wait_for("the host's book is placed", lambda: set(ct.world_state("")) - before, 5)
        ref = next(r for r in made if ct.world_state("", [r])[r]["base"] == book)
        if game:
            twin = ct.wait_for("the guest shows the book", lambda: next((r for r, v in ct.world_state(game).items()
                              if r >= ct.CREATED_ID and v["base"] == book), None), 10)
        else:
            twin = ref
        damage_stamina(game)
        ct.place_before(game, twin, 100)
        ct.activate_on(game, twin)
        assert_live(game, "Book Menu")
    finally:
        close(game, "Book Menu")
        for ref in made:
            ct.console_on("", f"{ref:X}.disable")
            ct.console_on("", f"{ref:X}.markfordelete")


def barter_speech(game):
    """A real merchant line opens barter on the picker, whose purchase earns Speech on that game alone."""
    ct.both_go("WhiterunBelethorsGeneralGoods")
    actors = set(ct.local_player("").get("local_actors", {}))
    ct.console_on("", "player.placeatme 13BA1 1")
    npc = int(next(iter(ct.wait_for("a merchant is loaded inside the shop", lambda:
                  set(ct.local_player("").get("local_actors", {})) - actors, 15))))
    ct.console_on("", f"{npc:X}.setrestrained 1")
    ct.wait_for("the merchant has a session id", lambda: ct.host_npc_state(npc).get("session_id", -1) >= 0, 10)
    copy = ct.copy_of("client", ct.host_npc_state(npc)["session_id"])
    gold = ct.party_gold()
    other = "" if game else "client"
    target = copy if game else npc
    menu = "BarterMenu"
    root = "_root.Menu_mc."
    lists = root + "InventoryLists_mc."
    before = {g: ct.skill(g, ct.SPEECH_SKILL) for g in ("", "client")}
    try:
        ct.set_party_gold(max(gold, 10000))
        ct.place_before(game, target, 110)
        ct.wait_for("the merchant has faded in and can be activated", lambda:
                    ct.actor_state(game, target).get("mod", {}).get("fade_state") == 0, 10)
        ct.activate_on(game, target)
        try:
            ct.wait_for("the merchant offers its barter line", lambda: any(
                "sale" in o.get("text", "").lower() for o in ct.dialogue(game).get("options", [])), 30)
        except ct.TestFailure:
            ct.log(f"merchant {npc:X}, copy {copy:X}: host={ct.dialogue('')}, guest={ct.dialogue('client')}")
            raise
        topic = next(o["topic"] for o in ct.dialogue(game)["options"] if "sale" in o.get("text", "").lower())
        ct.pick_with_keys(game, topic)
        ct.wait_for("the picker opens barter", lambda: menu in ct.menus(game), 15)
        if menu in ct.menus(other):
            raise ct.TestFailure("the non-trader opened barter")
        ct.wait_for("the merchant's categories take input", lambda:
                    ct.menu_var(game, menu, lists + "currentState") == 1, 10)
        ct.tap_key(game, ct.MENU_RIGHT_KEY)  # D: categories -> items
        ct.wait_for("the merchant's item list takes input", lambda:
                    ct.menu_var(game, menu, lists + "currentState") == 2, 10)
        ct.wait_for("the purchase has a price", lambda:
                    (ct.menu_var(game, menu, root + "ItemCard_mc.itemInfo.value") or 0) > 0, 10)
        ct.tap_key(game, ct.ACTIVATE_KEY)
        # A stack opens the native quantity slider; one item is enough for this trade.
        if ct.menu_var(game, menu, root + "ItemCard_mc.QuantityMenu_mc._visible"):
            ct.tap_key(game, 0x1C)
        ct.wait_for("the trader's native Speech gains the purchase", lambda:
                    ct.skill(game, ct.SPEECH_SKILL) > before[game], 15)
        if ct.skill(other, ct.SPEECH_SKILL) != before[other]:
            raise ct.TestFailure("the other player's Speech gained the trade")
    finally:
        close(game, menu)
        ct.close_dialogues()
        ct.remove_host_npc(npc)
        ct.set_party_gold(gold)
        ct.both_go(ct.HOME_CELL)


# Live menus (v1 checklist): what changes under an open menu while the world runs. Skyrim Souls RE is the reference
# (AutoCloseManager closes a menu whose target goes, ItemMenuUpdater refreshes its lists, SaveHandler saves).
ITEM_NAMES = ct.ITEM_LISTS + ".ItemsListHolder.List_mc.entryList"


def listed(game, menu, name):
    """Whether the open menu's item list shows an entry named `name`, as the player sees it."""
    count = int(ct.menu_var(game, menu, ITEM_NAMES + ".length") or 0)
    return any(name in str(ct.menu_var(game, menu, f"{ITEM_NAMES}.{i}.text") or "") for i in range(count))


def open_chest(game, chest):
    ct.place_before(game, chest, 100)
    ct.activate_on(game, chest)
    ct.wait_for(f"[{game or 'host'}] the chest's menu shows its lists",
                lambda: ct.menu_var(game, "ContainerMenu", ct.ITEM_LISTS + ".currentState") in ct.LISTS_SHOWN, 5)


def chest_list_follows(game):
    """A player holds a chest's menu open while the other player's game takes the sweetrolls out of it: the open
    list stops showing them (it is not left listing items that are gone)."""
    other = "" if game else "client"
    chest = ct.SMALL_CHEST
    try:
        ct.console_on("", f"{chest:X}.additem {ct.SWEETROLL:X} 2")
        for name in ("", "client"):
            ct.wait_for(f"[{name or 'host'}] the chest holds 2 sweetrolls",
                        lambda name=name: ct.item_count(name, chest, ct.SWEETROLL) == 2, 5)
        open_chest(game, chest)
        ct.wait_for("the open list shows the sweetrolls", lambda: listed(game, "ContainerMenu", "Sweet Roll"), 5)
        ct.console_on(other, f"{chest:X}.removeitem {ct.SWEETROLL:X} 2")
        ct.wait_for(f"[{game or 'host'}] its chest holds none", lambda: ct.item_count(game, chest, ct.SWEETROLL) == 0, 5)
        ct.wait_for("the open list no longer shows them", lambda: not listed(game, "ContainerMenu", "Sweet Roll"), 5)
    finally:
        if "ContainerMenu" in ct.menus(game):
            ct.player_command(game, {"cmd": "menu", "name": "ContainerMenu", "show": False})
        if extra := ct.item_count("", chest, ct.SWEETROLL):
            ct.console_on("", f"{chest:X}.removeitem {ct.SWEETROLL:X} {extra}")


def menus_close_when_target_goes(game):
    """A menu on something that goes closes (Skyrim Souls' auto-close): barter with a merchant who walks off or dies,
    a chest that is disabled. The host's game makes each change; the player's menu closes on its own."""
    def barter(what, change):
        npc, _, copy = ct.spawn_host_npc("00013BA1", restrained=True)  # Belethor
        try:
            target = copy if game else npc
            ct.place_before(game, target, 110)
            ct.player_script_call(game, "Actor", "ShowBarterMenu", on=target)
            ct.wait_for(f"[{game or 'host'}] barter opens", lambda: "BarterMenu" in ct.menus(game), 10)
            change(npc)
            ct.wait_for(f"[{game or 'host'}] barter closes when the merchant {what}", lambda: "BarterMenu" not in ct.menus(game), 5)
        finally:
            close(game, "BarterMenu")
            ct.remove_host_npc(npc)

    # 700 units from the trading player (on the host, the copy of the guest's): inside QASmoke, past 400.
    trader = "player" if not game else f"{ct.client_on_host():X}"
    barter("moves away", lambda npc: ct.console_on("", f"{npc:X}.moveto {trader} 700 0 0"))
    barter("dies", lambda npc: ct.console_on("", f"{npc:X}.kill"))
    chest = ct.SMALL_CHEST
    try:
        open_chest(game, chest)
        ct.console_on("", f"{chest:X}.disable")
        ct.wait_for(f"[{game or 'host'}] the chest's menu closes when the chest is disabled",
                    lambda: "ContainerMenu" not in ct.menus(game), 5)
    finally:
        if "ContainerMenu" in ct.menus(game):
            ct.player_command(game, {"cmd": "menu", "name": "ContainerMenu", "show": False})
        ct.console_on("", f"{chest:X}.enable")


def save_with_menu_open(game):
    """The host saves (console) while the guest's chest menu is open: the save is written, and the open menu still runs
    and closes as usual. (A host's own save comes from the journal, a menu of its own: host-journal-save.)"""
    try:
        damage_stamina(game)
        open_chest(game, ct.SMALL_CHEST)
        saved = ct.save_game("", "menus-open")
        ct.wait_for("[host] the save and co-save are written", ct.saves_written(saved), 20)
        assert_live(game, "ContainerMenu")
    finally:
        close(game, "ContainerMenu")


def register(scenarios, tags):
    for role, game in (("host", ""), ("guest", "client")):
        name = f"dialogue-{role}-barter-earns-speech"
        scenarios[name] = functools.partial(barter_speech, game)
        tags[name] = {"dialogue", "barter", "inventory", "menus", "spawn"}
    for menu, run in {"map": map_runs, "journal": journal_runs, "lockpicking": lock_runs,
                      "training": training_runs, "book": book_runs, "crafting": crafting_runs,
                      "barter": barter_runs}.items():
        for role, game in (("host", ""), ("guest", "client")):
            name = f"{role}-{menu}-keeps-world-running"
            scenarios[name] = functools.partial(run, game)
            tags[name] = {"menus", "ui", "world"}
    scenarios["journals-keep-npcs-simulating"] = npcs_keep_simulating
    tags["journals-keep-npcs-simulating"] = {"menus", "ui", "world", "physics"}
    for role, game in (("host", ""), ("guest", "client")):
        name = f"{role}-journal-save"
        scenarios[name] = functools.partial(journal_save, game)
        tags[name] = {"menus", "ui", "save"}
    scenarios["offline-journal-pauses"] = offline_journal_pauses
    for check, run in {"chest-list-follows-changes": chest_list_follows, "menus-close-when-target-goes": menus_close_when_target_goes}.items():
        for role, game in (("host", ""), ("guest", "client")):
            name = f"{role}-{check}"
            scenarios[name] = functools.partial(run, game)
            tags[name] = {"menus", "ui", "v1"}
    scenarios["host-saves-with-guest-menu-open"] = functools.partial(save_with_menu_open, "client")
    tags["host-saves-with-guest-menu-open"] = {"menus", "ui", "save", "v1"}
    tags["offline-journal-pauses"] = {"menus", "ui", "session"}
