"""Scenarios menus for the co-op harness."""
from ..common import (
    ACTIVATE_KEY,
    AMREN,
    COOKING_SPIT,
    CREATED_ID,
    DOWN_KEY,
    ESCAPE_KEY,
    HOME_CELL,
    IN_FURNITURE,
    ITEM_LISTS,
    JOURNAL_ROOT,
    JOURNAL_SYSTEM,
    LISTS_SHOWN,
    MENU_RIGHT_KEY,
    OUTDOOR_CELL,
    SMALL_CHEST,
    SPEECH_SKILL,
    SWEETROLL,
    TAB_KEY,
    UP_KEY,
)
from ..errors import TestFailure
from ..utilities import log

PAUSE_FLAGS = 0x1000021


ITEM_NAMES = ITEM_LISTS + ".ItemsListHolder.List_mc.entryList"


def assert_live(ctx, game, menu):
    """Read the engine's flags, then observe regeneration while the menu keeps its input."""
    ctx.wait_for(f"[{game or 'host'}] {menu} opens", lambda: menu in ctx.menus(game), 10)
    before = ctx.player_snapshot(game)["values"]["stamina"]
    hour = ctx.game_state("")["time"]["hour"]

    def running():
        state = ctx.player_command(game, {"cmd": "menu_flags", "name": menu})
        if state["flags"] is None:
            raise ctx.failure(f"{menu} closed before its simulation check")
        if state["flags"] & PAUSE_FLAGS or state["pauses"]:
            raise ctx.failure(f"{menu} still pauses: {state}")
        return ctx.player_snapshot(game)["values"]["stamina"] > before + 5

    ctx.wait_for(f"[{game or 'host'}] its player regenerates stamina with {menu} open", running, 15)
    ctx.wait_for("the host's world clock keeps advancing", lambda: ctx.game_state("")["time"]["hour"] != hour, 5)

def damage_stamina(ctx, game):
    ctx.console_on(game, "player.restoreav stamina 1000")
    ctx.console_on(game, "player.damageav stamina 70")

def close(ctx, game, menu):
    if menu in ctx.menus(game):
        ctx.tap_key(game, TAB_KEY)
        ctx.wait_for(f"[{game or 'host'}] {menu} closes", lambda: menu not in ctx.menus(game), 10)

def map_runs(ctx, game):
    try:
        ctx.both_go(OUTDOOR_CELL)
        ctx.player_command(game, {"cmd": "first_person"})
        was_hidden = ctx.player_command(game, {"cmd": "menu_flags", "name": "MapMenu"})["first_person_hidden"]
        damage_stamina(ctx, game)
        ctx.open_world_map(game)
        local = lambda: ctx.player_command(game, {"cmd": "menu_flags", "name": "MapMenu"})["local_map"]
        ctx.wait_for("the map hides first-person arms", lambda: ctx.player_command(game,
                    {"cmd": "menu_flags", "name": "MapMenu"})["first_person_hidden"] is True, 5)
        assert_live(ctx, game, "MapMenu")
        ctx.tap_key(game, 0x26)
        ctx.wait_for("the map switches to local view", lambda: local() is True, 10)
        ctx.wait_for("the local map takes input", lambda: ctx.player_command(game,
                    {"cmd": "menu_flags", "name": "MapMenu"})["local_map_ready"] is True, 10)
        damage_stamina(ctx, game)
        assert_live(ctx, game, "MapMenu")
        ctx.tap_key(game, 0x26)
        ctx.wait_for("the map returns to world view", lambda: not local() and ctx.map_takes_input(game), 10)
        close(ctx, game, "MapMenu")
        ctx.wait_for("the player's first-person camera returns",
                    lambda: ctx.player_command(game, {"cmd": "camera"})["state"] == 0, 5)
        ctx.wait_for("the first-person model regains its original visibility", lambda: ctx.player_command(game,
                    {"cmd": "menu_flags", "name": "MapMenu"})["first_person_hidden"] == was_hidden, 5)
    finally:
        close(ctx, game, "MapMenu")
        ctx.both_go(HOME_CELL)

def journal_runs(ctx, game):
    try:
        damage_stamina(ctx, game)
        ctx.tap_key(game, ESCAPE_KEY)
        assert_live(ctx, game, "Journal Menu")
    finally:
        close(ctx, game, "Journal Menu")

def lock_runs(ctx, game):
    door = ctx.inner_door()
    previous = ctx.door_lock("", door)
    picks = ctx.item_count(game, 0x14, 0xA)
    try:
        if not picks:
            ctx.console_on(game, "player.additem a 1")
        ctx.lock_on("", door, True, 25)
        ctx.wait_for("the lock reaches the guest", lambda: ctx.door_lock("client", door) == (True, 25), 10)
        damage_stamina(ctx, game)
        ctx.place_before(game, door, 100)
        ctx.activate_on(game, door)
        assert_live(ctx, game, "Lockpicking Menu")
    finally:
        close(ctx, game, "Lockpicking Menu")
        ctx.lock_on("", door, previous[0], previous[1] or 0)
        if not picks:
            ctx.console_on(game, "player.removeitem a 1")

def training_runs(ctx, game):
    npc, _, copy = ctx.spawn_host_npc(AMREN, restrained=True)
    try:
        damage_stamina(ctx, game)
        # The actual native invoked by the trainer's dialogue fragment.
        ctx.player_script_call(game, "Game", "ShowTrainingMenu", [{"form": copy if game else npc}])
        assert_live(ctx, game, "Training Menu")
    finally:
        close(ctx, game, "Training Menu")
        ctx.remove_host_npc(npc)

def crafting_runs(ctx, game):
    try:
        damage_stamina(ctx, game)
        ctx.place_before(game, COOKING_SPIT, 100)
        ctx.activate_on(game, COOKING_SPIT)
        ctx.wait_for("the player reaches the cooking spit",
                    lambda: ctx.sit_state(game, 0x14) == IN_FURNITURE, 15)
        assert_live(ctx, game, "Crafting Menu")
    finally:
        close(ctx, game, "Crafting Menu")
        if ctx.sit_state(game, 0x14) == IN_FURNITURE:
            ctx.activate_on(game, COOKING_SPIT)
        ctx.wait_for("the player finishes leaving the spit", lambda: ctx.sit_state(game, 0x14) == 0, 10)

def barter_runs(ctx, game):
    npc, _, copy = ctx.spawn_host_npc("00013BA1", restrained=True)  # Belethor
    try:
        damage_stamina(ctx, game)
        ctx.player_script_call(game, "Actor", "ShowBarterMenu", on=copy if game else npc)
        assert_live(ctx, game, "BarterMenu")
    finally:
        close(ctx, game, "BarterMenu")
        ctx.remove_host_npc(npc)

def npcs_keep_simulating(ctx):
    npc, _, copy = ctx.spawn_host_npc(AMREN, restrained=True)
    try:
        for game in ("", "client"):
            ctx.tap_key(game, ESCAPE_KEY)
            ctx.wait_for("both journals open", lambda game=game: "Journal Menu" in ctx.menus(game), 10)
        floor = ctx.host_npc_state(npc)["position"]
        raised = [floor[0], floor[1], floor[2] + 400]
        ctx.player_command("", {"cmd": "move_actor", "actor": npc, "position": raised})
        ctx.wait_for("the host shows the raised NPC",
                    lambda: ctx.host_npc_state(npc)["position"][2] > floor[2] + 200, 5)
        ctx.wait_for("the guest shows the raised NPC copy",
                    lambda: ctx.actors_on("client")[str(copy)]["actual"][2] > floor[2] + 100, 5)
        ctx.wait_for("the host's NPC falls while both journals stay open",
                    lambda: ctx.host_npc_state(npc)["position"][2] < floor[2] + 80, 10)
        ctx.wait_for("the guest's NPC copy follows the fall",
                    lambda: ctx.actors_on("client")[str(copy)]["actual"][2] < floor[2] + 80, 10)
        for game in ("", "client"):
            if "Journal Menu" not in ctx.menus(game):
                raise ctx.failure("a journal closed during the fall check")
    finally:
        for game in ("", "client"):
            close(ctx, game, "Journal Menu")
        ctx.remove_host_npc(npc)

def journal_save(ctx, game):
    """Choose New Save through the journal, including the guest's refusal through the same callback."""
    folder = ctx.save_file(game, "menus").parent
    before = {p.name: p.stat().st_mtime_ns for p in folder.glob("*.ess")}
    value = lambda field: ctx.menu_var(game, "Journal Menu", JOURNAL_SYSTEM + field)
    try:
        ctx.tap_key(game, ESCAPE_KEY)
        ctx.wait_for("the journal opens", lambda: "Journal Menu" in ctx.menus(game), 5)
        ctx.move_cursor(game, (910, 100))
        ctx.click(game)
        ctx.wait_for("the system tab opens", lambda: ctx.menu_var(game, "Journal Menu", JOURNAL_ROOT + ".iCurrentTab") == 2, 5)
        ctx.move_cursor(game, (700, 300))
        for _ in range(12):
            selected = value("CategoryList.selectedIndex")
            if value(f"CategoryList.entryList.{int(selected)}.text") == "$SAVE":
                break
            ctx.tap_key(game, DOWN_KEY)
            ctx.wait_for("the category highlight moves", lambda: value("CategoryList.selectedIndex") != selected, 5)
        else:
            raise ctx.failure("Save is absent from the system menu")
        ctx.tap_key(game, ACTIVATE_KEY)
        ctx.wait_for("the save list opens", lambda: value("iCurrentState") == 1 and
                    value("SaveLoadListHolder.List_mc.entryList.length"), 10)
        ctx.move_cursor(game, (700, 150))
        for _ in range(40):
            selected = value("SaveLoadListHolder.List_mc.selectedIndex")
            if selected == 0:
                break
            ctx.tap_key(game, DOWN_KEY if selected < 0 else UP_KEY)
            ctx.wait_for("the save highlight moves", lambda: value("SaveLoadListHolder.List_mc.selectedIndex") != selected, 5)
        else:
            raise ctx.failure("couldn't select New Save")
        ctx.tap_key(game, ACTIVATE_KEY)
        if game:
            ctx.wait_for("the guest's journal save is refused in chat", lambda: any("Only the host saves" in
                        line["text"] for line in ctx.chat(game).get("lines", [])), 10)
            after = {p.name: p.stat().st_mtime_ns for p in folder.glob("*.ess")}
            if after != before:
                raise ctx.failure("the guest's journal wrote a save")
        else:
            saved = ctx.wait_for("the host's journal writes its save", lambda: next((p for p in folder.glob("*.ess")
                                if before.get(p.name) != p.stat().st_mtime_ns), None), 15)
            ctx.wait_for("the journal's save and co-save finish", ctx.saves_written(saved), 15)
    finally:
        # A save may already have closed the journal. Force-hide only cleans up a refused save's list.
        if "Journal Menu" in ctx.menus(game):
            ctx.player_command(game, {"cmd": "menu", "name": "Journal Menu", "show": False})
            ctx.wait_for("the journal closes after saving", lambda: "Journal Menu" not in ctx.menus(game), 10)

def offline_journal_pauses(ctx):
    ctx.disconnect_client()
    try:
        ctx.tap_key("client", ESCAPE_KEY)
        ctx.wait_for("the offline journal opens", lambda: "Journal Menu" in ctx.menus("client"), 5)
        state = ctx.player_command("client", {"cmd": "menu_flags", "name": "Journal Menu"})
        if not state["flags"] & 1 or not state["pauses"]:
            raise ctx.failure(f"the offline journal no longer pauses: {state}")
    finally:
        close(ctx, "client", "Journal Menu")
        ctx.rejoin_client()

def book_runs(ctx, game):
    book = 0x1ACD1  # Skyrim.esm Book2CommonManualMixedUnitTactics, no skill or quest side effects
    before = set(ctx.world_state(""))
    made = set()
    try:
        ctx.console_on("", f"player.placeatme {book:X} 1")
        made = ctx.wait_for("the host's book is placed", lambda: set(ctx.world_state("")) - before, 5)
        ref = next(r for r in made if ctx.world_state("", [r])[r]["base"] == book)
        if game:
            twin = ctx.wait_for("the guest shows the book", lambda: next((r for r, v in ctx.world_state(game).items()
                              if r >= CREATED_ID and v["base"] == book), None), 10)
        else:
            twin = ref
        damage_stamina(ctx, game)
        ctx.place_before(game, twin, 100)
        ctx.activate_on(game, twin)
        assert_live(ctx, game, "Book Menu")
    finally:
        close(ctx, game, "Book Menu")
        for ref in made:
            ctx.console_on("", f"{ref:X}.disable")
            ctx.console_on("", f"{ref:X}.markfordelete")

def barter_speech(ctx, game, original=False):
    """A real merchant line opens barter on the picker, whose purchase earns Speech on that game alone."""
    ctx.both_go("WhiterunBelethorsGeneralGoods")
    if original:
        npc = 0x1A672
        ctx.wait_for("the original Belethor is loaded", lambda: ctx.actor_state("", npc).get("placement", {}).get("has_3d"), 15)
    else:
        actors = set(ctx.local_player("").get("local_actors", {}))
        ctx.console_on("", "player.placeatme 13BA1 1")
        npc = int(next(iter(ctx.wait_for("a merchant is loaded inside the shop", lambda:
                      set(ctx.local_player("").get("local_actors", {})) - actors, 15))))
        ctx.console_on("", f"{npc:X}.setrestrained 1")
    ctx.wait_for("the merchant has a session id", lambda: ctx.host_npc_state(npc).get("session_id", -1) >= 0, 10)
    copy = ctx.copy_of("client", ctx.host_npc_state(npc)["session_id"])
    gold = ctx.party_gold()
    other = "" if game else "client"
    target = copy if game else npc
    menu = "BarterMenu"
    root = "_root.Menu_mc."
    lists = root + "InventoryLists_mc."
    before = {g: ctx.skill(g, SPEECH_SKILL) for g in ("", "client")}
    try:
        ctx.set_party_gold(max(gold, 10000))
        ctx.place_before(game, target, 110)
        ctx.wait_for("the merchant has faded in and can be activated", lambda:
                    ctx.actor_state(game, target).get("mod", {}).get("fade_state") == 0, 10)
        for transaction in range(2):
            log(f"[{game or 'host'}] {'original' if original else 'spawned'} Belethor transaction {transaction + 1}")
            before = {g: ctx.skill(g, SPEECH_SKILL) for g in ("", "client")}
            if transaction:
                ctx.wait_for("the merchant finishes the previous goodbye", lambda: all(
                    not ctx.actor_state(g, actor).get("ai", {}).get("talking")
                    for g, actor in (("", npc), ("client", copy))), 10)
            ctx.activate_on(game, target)
            try:
                ctx.wait_for("the merchant offers its barter line", lambda: any(
                    "sale" in o.get("text", "").lower() for o in ctx.dialogue(game).get("options", [])), 30)
            except TestFailure:
                log(f"merchant {npc:X}, copy {copy:X}: host={ctx.dialogue('')}, guest={ctx.dialogue('client')}")
                raise
            topic = next(o["topic"] for o in ctx.dialogue(game)["options"] if "sale" in o.get("text", "").lower())
            ctx.pick_with_keys(game, topic)
            ctx.wait_for("the picker opens barter", lambda: menu in ctx.menus(game), 15)
            if menu in ctx.menus(other):
                raise ctx.failure("the non-trader opened barter")
            ctx.wait_for("the merchant's categories take input", lambda:
                        ctx.menu_var(game, menu, lists + "currentState") == 1, 10)
            ctx.tap_key(game, MENU_RIGHT_KEY)  # D: categories -> items
            ctx.wait_for("the merchant's item list takes input", lambda:
                        ctx.menu_var(game, menu, lists + "currentState") == 2, 10)
            ctx.wait_for("the purchase has a price", lambda:
                        (ctx.menu_var(game, menu, root + "ItemCard_mc.itemInfo.value") or 0) > 0, 10)
            ctx.tap_key(game, ACTIVATE_KEY)
            # A stack opens the native quantity slider; one item is enough for this trade.
            if ctx.menu_var(game, menu, root + "ItemCard_mc.QuantityMenu_mc._visible"):
                ctx.tap_key(game, 0x1C)
            ctx.wait_for("the trader's native Speech gains the purchase", lambda:
                        ctx.skill(game, SPEECH_SKILL) > before[game], 15)
            if ctx.skill(other, SPEECH_SKILL) != before[other]:
                raise ctx.failure("the other player's Speech gained the trade")
            close(ctx, game, menu)
            ctx.close_dialogues()
            # The original reference also participates in the shop's scene when a spawned merchant trades.
            def scenes_agree():
                for owner, mirrored in {(npc, copy), (0x1A672, 0x1A672)}:
                    native = ctx.actor_state("", owner).get("mod", {}).get("runtime", {})
                    effective = ctx.actor_state("client", mirrored).get("mod", {}).get("runtime", {})
                    if not native or not effective or native["native_scene"] != effective["effective_scene"]:
                        return False
                return True
            ctx.wait_for("both merchants' gameplay scenes follow their owners after barter", scenes_agree, 5)
            for owner, mirrored in sorted({(npc, copy), (0x1A672, 0x1A672)}):
                log(f"scene {owner:X}/{mirrored:X}: owner={ctx.actor_state('', owner)['mod']['runtime']}, "
                    f"guest={ctx.actor_state('client', mirrored)['mod']['runtime']}")
    finally:
        close(ctx, game, menu)
        ctx.close_dialogues()
        if not original:
            ctx.remove_host_npc(npc)
        ctx.set_party_gold(gold)
        ctx.both_go(HOME_CELL)

def listed(ctx, game, menu, name):
    """Whether the open menu's item list shows an entry named `name`, as the player sees it."""
    count = int(ctx.menu_var(game, menu, ITEM_NAMES + ".length") or 0)
    return any(name in str(ctx.menu_var(game, menu, f"{ITEM_NAMES}.{i}.text") or "") for i in range(count))

def open_chest(ctx, game, chest):
    ctx.place_before(game, chest, 100)
    ctx.activate_on(game, chest)
    ctx.wait_for(f"[{game or 'host'}] the chest's menu shows its lists",
                lambda: ctx.menu_var(game, "ContainerMenu", ITEM_LISTS + ".currentState") in LISTS_SHOWN, 5)

def chest_list_follows(ctx, game):
    """A player holds a chest's menu open while the other player's game takes the sweetrolls out of it: the open
    list stops showing them (it is not left listing items that are gone)."""
    other = "" if game else "client"
    chest = SMALL_CHEST
    try:
        ctx.console_on("", f"{chest:X}.additem {SWEETROLL:X} 2")
        for name in ("", "client"):
            ctx.wait_for(f"[{name or 'host'}] the chest holds 2 sweetrolls",
                        lambda name=name: ctx.item_count(name, chest, SWEETROLL) == 2, 5)
        open_chest(ctx, game, chest)
        ctx.wait_for("the open list shows the sweetrolls", lambda: listed(ctx, game, "ContainerMenu", "Sweet Roll"), 5)
        ctx.console_on(other, f"{chest:X}.removeitem {SWEETROLL:X} 2")
        ctx.wait_for(f"[{game or 'host'}] its chest holds none", lambda: ctx.item_count(game, chest, SWEETROLL) == 0, 5)
        ctx.wait_for("the open list no longer shows them", lambda: not listed(ctx, game, "ContainerMenu", "Sweet Roll"), 5)
    finally:
        if "ContainerMenu" in ctx.menus(game):
            ctx.player_command(game, {"cmd": "menu", "name": "ContainerMenu", "show": False})
        if extra := ctx.item_count("", chest, SWEETROLL):
            ctx.console_on("", f"{chest:X}.removeitem {SWEETROLL:X} {extra}")

def menus_close_when_target_goes(ctx, game):
    """A menu on something that goes closes (Skyrim Souls' auto-close): barter with a merchant who walks off or dies,
    a chest that is disabled. The host's game makes each change; the player's menu closes on its own."""
    def barter(what, change):
        npc, _, copy = ctx.spawn_host_npc("00013BA1", restrained=True)  # Belethor
        try:
            target = copy if game else npc
            ctx.place_before(game, target, 110)
            ctx.player_script_call(game, "Actor", "ShowBarterMenu", on=target)
            ctx.wait_for(f"[{game or 'host'}] barter opens", lambda: "BarterMenu" in ctx.menus(game), 10)
            change(npc)
            ctx.wait_for(f"[{game or 'host'}] barter closes when the merchant {what}", lambda: "BarterMenu" not in ctx.menus(game), 5)
        finally:
            close(ctx, game, "BarterMenu")
            ctx.remove_host_npc(npc)

    # 700 units from the trading player (on the host, the copy of the guest's): inside QASmoke, past 400.
    trader = "player" if not game else f"{ctx.client_on_host():X}"
    barter("moves away", lambda npc: ctx.console_on("", f"{npc:X}.moveto {trader} 700 0 0"))
    barter("dies", lambda npc: ctx.console_on("", f"{npc:X}.kill"))
    chest = SMALL_CHEST
    try:
        open_chest(ctx, game, chest)
        ctx.console_on("", f"{chest:X}.disable")
        ctx.wait_for(f"[{game or 'host'}] the chest's menu closes when the chest is disabled",
                    lambda: "ContainerMenu" not in ctx.menus(game), 5)
    finally:
        if "ContainerMenu" in ctx.menus(game):
            ctx.player_command(game, {"cmd": "menu", "name": "ContainerMenu", "show": False})
        ctx.console_on("", f"{chest:X}.enable")

def save_with_menu_open(ctx, game):
    """The host saves (console) while the guest's chest menu is open: the save is written, and the open menu still runs
    and closes as usual. (A host's own save comes from the journal, a menu of its own: host-journal-save.)"""
    _game = ctx.game()
    try:
        damage_stamina(ctx, game)
        open_chest(ctx, game, SMALL_CHEST)
        saved = ctx.save_game("", "menus-open")
        ctx.wait_for("[host] the save and co-save are written", ctx.saves_written(saved), 20)
        assert_live(ctx, game, "ContainerMenu")
    finally:
        close(ctx, game, "ContainerMenu")
