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


def register(scenarios, tags):
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
    tags["offline-journal-pauses"] = {"menus", "ui", "session"}
