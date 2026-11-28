"""Actions for the co-op harness."""
import argparse
import time
from ..common import (
    BODY_CATEGORY,
    BODY_TAB,
    DOWN_KEY,
    EYES_CATEGORY,
    FACE_CATEGORY,
    FINISH_NAMING_OK,
    HAIR_CATEGORY,
    HEAD_CATEGORY,
    JOURNAL_ROOT,
    JOURNAL_SYSTEM,
    KHAJIIT_RACE,
    MENU_RIGHT_KEY,
    NAME_ACCEPT,
    NEW_CHARACTER_NAME,
    NPC_FACE,
    NPC_RACE,
    NPC_SKILLS,
    QUICKLOAD_KEY,
    QUICKSAVE_KEY,
    RACE_MENU,
    RACE_MENU_DONE_KEY,
    RACE_PANELS,
    UP_KEY,
    WAR_PAINT_TINT,
)
from ..errors import TestFailure
from ..comparisons import distance
from ..utilities import log
from ..objects import Watch


class JoinActions:
    def client_accepts_steam_invite(ctx):
        """The host's game makes its lobby and invites the client's account; the client's player accepts; returns once
        the client's game is connected to the host over Steam."""
        _game = ctx.game()
        host_id, guest_id = ctx.steam_accounts()
        _game = ctx.game('')
        if ctx.steam_status("").get("session", {}).get("state") != "hosting":
            if not (_game.rpc({"cmd": "steam_host"}) or {}).get("ok"):
                raise ctx.failure(f"[host] could not host on Steam: {ctx.steam_status('')}")
        ctx.wait_for("[host] its lobby is made and it listens",
                 lambda: ctx.steam_status("").get("session", {}).get("state") == "hosting", 30, _game=_game)
        lobby = ctx.steam_status("")["session"]["lobby"]
        _game = ctx.game('')
        if not (_game.rpc({"cmd": "steam_invite", "friend": guest_id}) or {}).get("ok"):
            raise ctx.failure("[host] Steam refused the invite")
        ctx.client_steam_opens(f"steam://joinlobby/489830/{lobby}/{host_id}")
        session = lambda: ctx.steam_status("client").get("session", {})
        try:
            ctx.wait_for("[client] joins the lobby and connects to the host", lambda: session().get("state") == "connected", 60, _game=_game)
        except TestFailure:
            raise ctx.failure(f"[client] Steam session {session().get('state')}: {session().get('error')}")
        guest_name = ctx.steam_status("client").get("name")
        ctx.wait_for(f"[host] has {guest_name}'s hello",
                 lambda: any(p.get("name") == guest_name for p in ctx.steam_status("").get("session", {}).get("peers", [])), 10, _game=_game)
        for name, game in (("host", ""), ("client", "client")):
            log(f"[{name}] Steam peers: {ctx.steam_status(game)['session']['peers']}")


    def steam_sessions_end(ctx):
        _game = ctx.game()
        for game in ("client", ""):
            _game = ctx.game(game)
            _game.rpc({"cmd": "steam_leave"})


    def save_files(ctx, instance):
        """The game's save files, by name (its prefix's Saves folder)."""
        _game = ctx.game(instance)
        saves = _game.my_games / "Saves"
        return {path.name for path in saves.glob("*.ess")} if saves.exists() else set()


    def expect_no_scratch_saves(ctx, instance):
        """The game's Saves folder holds only players' saves: the main menu's Continue loads the newest .ess of any
        character (52898), and a scratch save's name files it under character 0 (save-game.md, Continue)."""
        saves = ctx.game(instance).my_games / "Saves"
        scratch = sorted(path.name for stem in ("SkyrimCoopJoin", "SkyrimCoopWipe", "SkyrimCoopTransfer")
                         for path in saves.glob(stem + ".*") if path.suffix in (".ess", ".skse"))
        if scratch:
            raise ctx.failure(f"[{instance or 'host'}] scratch saves left in the Saves folder: {scratch}")
        return {"scratch": scratch}


    def plant_other_playthrough_save(ctx, instance, character="0C0FFEE0"):
        """Another playthrough's save in the game's Saves folder: the newest save's files under a name with another
        character id (the id is only in the name, save-game.md)."""
        saves = ctx.game(instance).my_games / "Saves"
        newest = max(saves.glob("*.ess"), key=lambda path: path.stat().st_mtime)
        name = f"Save900_{character}_0_4F74686572_Tamriel_000001_20260101120000_1_1"
        for suffix in (".ess", ".skse"):
            source = newest.with_suffix(suffix)
            if source.exists():
                (saves / (name + suffix)).write_bytes(source.read_bytes())
        return {"planted": name, "from": newest.name}


    def scan_saves(ctx, instance):
        """The main menu's scan of every save (harness scan_saves: PopulateSaveList 35760, as Continue runs it), as a
        player coming from the main menu has had: the Journal's character map then knows every playthrough in the
        folder. The characters it saw."""
        reply = ctx.game(instance).rpc({"cmd": "scan_saves"}) or {}
        return {"characters": sorted({f"{int(c) & 0xFFFFFFFF:08X}" for c in reply.get("characters", [])})}


    def expect_scan_sees(ctx, instance, characters):
        """The scan of every save saw each of these characters (so vanilla's Character Selection would list them)."""
        seen = ctx.scan_saves(instance)["characters"]
        missing = [character for character in characters if character.upper() not in seen]
        if missing:
            raise ctx.failure(f"[{instance or 'host'}] the scan of every save saw {seen}, not {missing}")
        return {"seen": seen}


    def drop_planted_saves(ctx, instance):
        """The saves plant_other_playthrough_save put in the game's Saves folder, gone."""
        saves = ctx.game(instance).my_games / "Saves"
        for path in saves.glob("Save900_*"):
            path.unlink()


    def journal_character_rows(ctx, instance):
        """The player opens Load, then the Journal's Character Selection (T, quest_journal.swf SystemPage): each row's
        character id (4294967295 for Show all saves), the Journal then closed again."""
        value = JoinActions.open_journal_load(ctx, instance)
        ctx.wait_for("the load list or the character list opens", lambda: value("iCurrentState") in (1, 17), 10)
        if value("iCurrentState") == 1:
            ctx.tap_key(instance, 0x14)  # T: Character Selection
        ctx.wait_for("the character list opens", lambda: value("iCurrentState") == 17, 10)
        # The rows come after a save-thread scan (52882: 0x50000008, then 52890); vanilla always has Show all saves.
        length = lambda: int(value("SaveLoadListHolder.List_mc.entryList.length") or 0)
        try:
            ctx.wait_for("the character rows arrive", length, 5)
        except TestFailure:
            pass
        rows = [int(value(f"SaveLoadListHolder.List_mc.entryList.{i}.id")) & 0xFFFFFFFF
                for i in range(int(value("SaveLoadListHolder.List_mc.entryList.length") or 0))]
        for _ in range(3):
            if "Journal Menu" not in ctx.menus(instance):
                break
            ctx.tap_key(instance, 0x01)
            time.sleep(0.5)
        return {"rows": [f"{row:08X}" for row in rows]}


    def expect_rows_only(ctx, instance):
        """The Journal's character rows are exactly this playthrough's character's: no Show all saves, no other
        character (character 0 never has a row: the folder scan, 35799, makes rows for nonzero ids only)."""
        character = ctx.current_character(instance)["character"]
        rows = ctx.journal_character_rows(instance)["rows"]
        if rows != ([character] if int(character, 16) else []):
            raise ctx.failure(f"[{instance or 'host'}] the Journal's character rows are {rows}, not only {character}")
        return {"rows": rows}


    def current_character(ctx, instance, character=None):
        """The engine's current character id (harness set_character), set first when given, as loading that
        character's save would (SetCurrentCharacter 35751). Its modded flag is this game's, read from the newest save's
        name: a flag other than the game's makes the next save add a catalogue entry under Save_Impl (35749, crashed)."""
        request = {"cmd": "set_character"}
        if character is not None:
            saves = ctx.game(instance).my_games / "Saves"
            own = [path for path in saves.glob("*.ess") if not path.name.startswith(("SkyrimCoop", "Save900_"))]
            newest = max(own, key=lambda path: path.stat().st_mtime).stem.split("_")
            request.update(id=int(character, 16), modded=len(newest) == 9 and newest[2] == "1")
        reply = ctx.game(instance).rpc(request) or {}
        return {"character": f"{int(reply.get('character', 0)) & 0xFFFFFFFF:08X}"}


    def press_key(ctx, instance, scan):
        """The player presses and releases a keyboard key (DirectInput scan code), through the game's input dispatch."""
        ctx.wait_for(f"[{instance or 'host'}] the game takes input", lambda: ctx.takes_input(instance), 10)
        ctx.player_command(instance, {"cmd": "key", "scan": scan, "down": True})
        ctx.held_a_frame(instance)
        ctx.player_command(instance, {"cmd": "key", "scan": scan, "down": False})


    def make_host_quicksave(ctx):
        _game = ctx.game()
        before = ctx.save_files("")
        ctx.press_key("", QUICKSAVE_KEY)
        written = ctx.wait_for("the host's quicksave appears", lambda: ctx.save_files("") - before, 15, _game=_game)
        _game = ctx.game('')
        path = _game.my_games / "Saves" / next(iter(written))
        ctx.wait_for("the quicksave and co-save finish writing", ctx.saves_written(path), 15, _game=_game)
        return path


    def host_loads_party(ctx, quick, menu=False):
        """The host's chosen save rolls back the whole party, keeping each character and saved place."""
        _game = ctx.game()
        names = {instance: ctx.player_record(instance)["name"] for instance in ("", "client")}
        guest_level = ctx.player_record("client")["level"]
        ctx.console_on("", "set gamehour to 12")
        # Each at rest first: a player still landing from an arrival is saved where it is not.
        positions = {instance: ctx.player_rests(instance) for instance in ("", "client")}
        # Save sync: each save the host makes goes ahead to the guest's store (My Games/.../SkyrimCoop/worlds).
        worlds = ctx.game("client").my_games / "SkyrimCoop" / "worlds"
        held = set(worlds.glob("*.world"))
        if quick or menu:
            ctx.make_host_quicksave()
        else:
            name = f"HostLoadSelected_{int(time.time())}"
            checkpoint = ctx.save_game("", name)
            ctx.wait_for("the chosen save and its co-save are written", ctx.saves_written(checkpoint), 15)
        ctx.wait_for("the guest holds the host's new save", lambda: set(worlds.glob("*.world")) - held, 30)
        ctx.moves_past_save(guest_level)
        if quick:
            ctx.press_key("", QUICKLOAD_KEY)
        elif menu:
            ctx.load_latest_from_menu("")
        else:
            # A later save catches accidentally choosing PreviousSave() instead of the selected one.
            newer = ctx.save_game("", name + "_newer")
            ctx.wait_for("the newer save is written", ctx.saves_written(newer), 15)
            ctx.console_on("", f"load {name}")
        timing = lambda: ctx.join_status("client").get("timings", {})
        ctx.wait_for("the guest loads the save from its store and rejoins",
                     lambda: timing().get("from_store") and timing().get("outcome") == "joined", 120)
        if timing().get("payload_bytes"):
            raise ctx.failure(f"the guest held the save, and the load sent it again: {timing()}")
        ctx.party_restored(guest_level)
        for instance in ("", "client"):
            if ctx.player_record(instance)["name"] != names[instance]:
                raise ctx.failure(f"[{instance or 'host'}] the load restored another player's character")
            if distance(at := ctx.my_position(instance), positions[instance]) > 100:
                raise ctx.failure(f"[{instance or 'host'}] did not return to its saved place: at "
                                  f"{[round(v) for v in at]}, saved at {[round(v) for v in positions[instance]]}")


    def player_rests(ctx, instance, frames=5, timeout=10):
        """That game's player has not moved for `frames` game frames; where it rests."""
        seen = {}
        def rests():
            frame, at = ctx.game_frame(instance), ctx.my_position(instance)
            if "at" not in seen or distance(at, seen["at"]) >= 1:
                seen.update(frame=frame, at=at)
            return frame >= seen["frame"] + frames and (at,)
        return ctx.wait_for(f"[{instance or 'host'}] rests where it stands", rests, timeout)[0]


    def moves_past_save(ctx, guest_level):
        """The party plays on beyond a save made at noon: later hour, the guest a level up."""
        ctx.console_on("client", f"player.setlevel {guest_level + 1}")
        ctx.console_on("", "set gamehour to 17")
        ctx.wait_for("the party moves beyond the chosen save", lambda: ctx.game_state("client").get("time", {}).get("hour", 0) > 16, 10)


    def party_restored(ctx, guest_level):
        """Both games are back in the noon save with the guest's saved level, show each other, keep updating and can
        spawn a fresh actor."""
        def restored():
            return all(ctx.game_state(instance).get("online") and
                       abs(ctx.game_state(instance).get("time", {}).get("hour", 24) - 12) < 0.3
                       for instance in ("", "client")) and ctx.player_record("client")["level"] == guest_level
        ctx.wait_for("the whole party loads and rejoins in the saved world", restored, 120)
        ctx.wait_for("both games show their restored characters", ctx.both_see_each_other, 30)
        frames = {instance: ctx.game_frame(instance) for instance in ("", "client")}
        ctx.wait_for("both games keep updating after restoration",
                 lambda: all(ctx.game_frame(instance) > frame + 180 for instance, frame in frames.items()), 15)
        ctx.remove_host_npc(ctx.spawn_beside_host("0001A696"))
        if ctx.player_record("client")["level"] != guest_level:
            raise ctx.failure("the guest did not load its saved character: its later level survived")


    def open_journal_load(ctx, instance):
        """The player opens the Journal's System tab and presses Load; the System page's menu variables."""
        value = lambda field: ctx.menu_var(instance, "Journal Menu", JOURNAL_SYSTEM + field)
        ctx.tap_key(instance, 0x01)
        ctx.wait_for("the journal opens", lambda: "Journal Menu" in ctx.menus(instance), 5)
        ctx.move_cursor(instance, (910, 100))
        ctx.click(instance)
        ctx.wait_for("the system tab opens", lambda: ctx.menu_var(instance, "Journal Menu", JOURNAL_ROOT + ".iCurrentTab") == 2, 5)
        ctx.move_cursor(instance, (700, 300))
        for _ in range(12):
            selected = value("CategoryList.selectedIndex")
            if value(f"CategoryList.entryList.{int(selected)}.text") == "$LOAD":
                break
            ctx.tap_key(instance, DOWN_KEY)
            ctx.wait_for("the category highlight moves", lambda: value("CategoryList.selectedIndex") != selected, 5)
        else:
            raise ctx.failure("Load is absent from the system menu")
        ctx.tap_key(instance, 0x12)  # E, the menu's Accept action
        return value


    def load_latest_from_menu(ctx, instance):
        """The player opens System, Load, selects the newest save and confirms through menu input."""
        value = JoinActions.open_journal_load(ctx, instance)
        # With no save of this character the Journal shows the character list (state 17): E takes the first.
        ctx.wait_for("the load list opens", lambda: value("iCurrentState") in (1, 17), 10)
        if value("iCurrentState") == 17:
            ctx.tap_key(instance, 0x12)
        ctx.wait_for("the load list opens", lambda: value("SaveLoadListHolder.List_mc.entryList.length") and value("iCurrentState") == 1, 10)
        ctx.move_cursor(instance, (700, 150))
        for _ in range(40):
            selected = value("SaveLoadListHolder.List_mc.selectedIndex")
            if selected == 0:
                break
            ctx.tap_key(instance, DOWN_KEY if selected < 0 else UP_KEY)
            ctx.wait_for("the save highlight moves", lambda: value("SaveLoadListHolder.List_mc.selectedIndex") != selected, 5)
        else:
            raise ctx.failure("couldn't select the newest save")
        ctx.tap_key(instance, 0x12)
        ctx.wait_for("the load confirmation opens", lambda: value("iCurrentState") == 2, 5)
        ctx.move_cursor(instance, (700, 560))  # the confirmation's Yes button
        ctx.click(instance)


    def player_record(ctx, instance):
        """The game's player's character as the join flow reads it (harness `player_record`): key, name, level, perks..."""
        return ctx.player_command(instance, {"cmd": "player_record"})["record"]


    def kept_records(ctx, instance):
        """{key: record} of the characters the game keeps (its loaded save's, and on a host its guests')."""
        return {record["key"]: record for record in ctx.player_command(instance, {"cmd": "records"})["records"]}


    def session_port(ctx):
        _game = ctx.game('')
        port = ((_game.read_status() or {}).get("session") or {}).get("port")
        if not port:
            raise ctx.failure("[host] no session port")
        return port


    def joins_with_world(ctx, what, timeout=240, make_character=False):
        """The client's game joins the host's session with the host's world (harness `join_world`, as the join screen
        will): gate, the host's save received and loaded, its character put on, then the session. With
        make_character, a world that has no character of this player's (a late save's first join) gets a new one
        made in the creator the join opens."""
        ctx.player_command("client", {"cmd": "join_world", "port": ctx.session_port()})
        ctx.wait_for(f"[client] {what}: the host's gate passes", lambda: ctx.join_status("client").get("state") in
                 ("receiving_world", "loading", "connecting", "joined") or ctx.game_state("client").get("online")
                 or (make_character and ctx.race_panel("_SubList1.selectedIndex") is not None), 30)
        if make_character:
            def creating_or_joined():
                if ctx.game_state("client").get("online") and not ctx.join_status("client").get("joining"):
                    return "joined"
                return "creating" if ctx.race_panel("_SubList1.selectedIndex") is not None else None
            if ctx.wait_for(f"[client] {what}: joins, or the creator opens", creating_or_joined, timeout) == "creating":
                ctx.finishes_race_menu(f"{what}: names a new character", "Courier")
        def joined():
            status = ctx.join_status("client")
            if status.get("state") == "refused":
                raise ctx.failure(f"[client] couldn't join ({status.get('reason')}): {status.get('message')}")
            return ctx.game_state("client").get("online") and not status.get("joining")
        ctx.wait_for(f"[client] {what}: receives and loads the host's save, then joins", joined, timeout)
        ctx.wait_for("Each game shows the other player", ctx.both_see_each_other, 60)
        ctx.wait_for(f"[client] {what}: takes the host's weather and calendar after its load", lambda: not ctx.world_sync_problems(), 30)


    def restart_client_at_main_menu(ctx):
        """The client's game starts again and stays at the main menu (no save loaded), as a friend's game before joining."""
        _game = ctx.game('client')
        ctx.stop_game(_game=_game)
        ctx.wait_for("[host] drops the client whose game ended", lambda: not ctx.game_state("").get("players"), 40, _game=_game)
        _game = ctx.game('client')
        ctx.launch(argparse.Namespace(cell=None, save=None, main_menu=True, boot_timeout=240, world_timeout=180),
               ctx.new_run_dir("main-menu"), _game=_game)


    def race_panel(ctx, field):
        return ctx.menu_var("client", RACE_MENU, f"{RACE_PANELS}.{field}")


    def press_until(ctx, what, code, done, presses=12):
        """The client's player presses the key until done() holds, each press waiting for the menu to move."""
        for _ in range(presses):
            if done():
                return
            before = done.watch()
            ctx.tap_key("client", code)
            ctx.wait_for(f"[client] {what}: the menu takes the press", lambda: done.watch() != before, 5)
        if not done():
            raise ctx.failure(f"[client] {what}: not reached after {presses} presses")


    def race_category(ctx, index):
        """The client's player clicks the next category's tab until category `index` is open."""
        for _ in range(8):
            before = ctx.race_panel("_CategoriesList.selectedIndex")
            if before == index:
                return
            ctx.move_cursor("client", BODY_TAB)
            ctx.click("client")
            ctx.wait_for(f"[client] the race menu opens its next category (for {index})",
                     lambda: ctx.race_panel("_CategoriesList.selectedIndex") != before, 10)
        raise ctx.failure(f"[client] the race menu never opened category {index}")


    def race_option(ctx, text):
        """The client's player moves the highlight down to the open category's option `text`."""
        ctx.press_until(f"{text} is highlighted", DOWN_KEY, Watch(ctx, lambda: ctx.race_panel("_SubList2.selectedEntry.text"), lambda t: t == text))


    def slides_until(ctx, what, done):
        """The client's player moves the highlighted slider right until done() holds of the engine's data."""
        ctx.press_until(what, MENU_RIGHT_KEY, Watch(ctx, lambda: ctx.race_panel("_SubList2.SelectedEntry.SliderInstance.position"), lambda _: done()))


    def styles_character(ctx):
        """In the race menu the client's player picks a war paint and its colour, a nose height, an eye colour, a hair style
        and a hair colour, each press waiting on the engine's data (the tints, the base's face section)."""
        face = lambda: ctx.form_save("client", 0x14, base=True, flags=NPC_FACE)["data"]
        paint = lambda: next(((t["name"], t["color"]) for t in ctx.player_record("client")["tints"]
                              if t["type"] == WAR_PAINT_TINT and t["alpha"] > 0), None)
        ctx.race_category(HEAD_CATEGORY)
        ctx.race_option("War Paint")
        ctx.slides_until("a war paint is on", lambda: paint() is not None)
        worn = paint()
        ctx.race_option("War Paint Color")
        ctx.slides_until("the war paint takes a colour", lambda: paint() not in (None, worn))
        for category, option in ((FACE_CATEGORY, "Nose Height"), (EYES_CATEGORY, "Eye Color"), (HAIR_CATEGORY, "Hair"),
                                 (HAIR_CATEGORY, "Hair Color")):
            ctx.race_category(category)
            ctx.race_option(option)
            was = face()
            ctx.slides_until(f"the {option.lower()} changes", lambda: face() != was)


    def finishes_race_menu(ctx, what, name):
        """The client's player finishes the race menu and names the character."""
        ctx.tap_key("client", RACE_MENU_DONE_KEY)
        ctx.clicks_box_button(what, FINISH_NAMING_OK)
        ctx.wait_for("[client] the name box takes text", lambda: ctx.input_state("client")["text_entry"] > 0, 10)
        ctx.player_command("client", {"cmd": "char", "text": name})
        ctx.move_cursor("client", NAME_ACCEPT)
        ctx.click("client")


    def tint_key(ctx, tint):
        return tint["name"], tint["color"], round(tint["alpha"], 3)


    def copy_face_differences(ctx, copy):
        """How the host's copy's head differs from the client's player's: painted with other tints than the client's head
        shows ("" when none)."""
        _game = ctx.game()
        shows = sorted(map(ctx.tint_key, ((ctx.player_snapshot("client").get("mod") or {}).get("face") or {}).get("tints") or []))
        _game = ctx.game('')
        face = ((((_game.read_status() or {}).get("world") or {}).get(str(copy)) or {}).get("mod") or {}).get("face") or {}
        if not face.get("painted"):
            return f"its head is not painted (head {face.get('head')}, painted {face.get('paints')} times)"
        painted = sorted(map(ctx.tint_key, face.get("tints") or []))
        if painted == shows:
            return ""
        lacks, extra = sorted(set(shows) - set(painted))[:4], sorted(set(painted) - set(shows))[:4]
        return f"{len(painted)} tints painted, the client shows {len(shows)}; lacks {lacks}, also {extra}"


    def waits_until_same(ctx, what, differences, timeout):
        """Waits until differences() is "", failing with the last ones it found."""
        last = [""]

        def same():
            last[0] = differences()
            return not last[0]
        try:
            ctx.wait_for(what, same, timeout)
        except TestFailure:
            raise ctx.failure(f"{what}: {last[0]}") from None


    def copy_looks_differences(ctx, copy):
        """Where the host's copy of the client's player looks otherwise: its base's save bytes and its head's tints."""
        return "; ".join(filter(None, [ctx.copy_base_differences(copy), ctx.copy_face_differences(copy)]))


    def clicks_box_button(ctx, what, button):
        ctx.wait_for(f"[client] {what}: the box is up", lambda: "MessageBoxMenu" in ctx.menus("client"), 30)
        ctx.move_cursor("client", (button[0], button[1] + 80))
        ctx.move_cursor("client", button)
        ctx.click("client")
        ctx.wait_for(f"[client] {what}: the box is closed", lambda: "MessageBoxMenu" not in ctx.menus("client"), 10)


    def selects_race(ctx, race, identity):
        """Up through the race list until the engine wears `race`. Each press must move the list's highlight (the menu took
        it), then the engine must put that race on (it loads the race's body behind the fade)."""
        for _ in range(12):
            if identity().get("race") == race:
                return
            index, worn = ctx.race_panel("_SubList1.selectedIndex"), identity().get("race")
            ctx.tap_key("client", UP_KEY)
            ctx.wait_for("[client] the race list takes the press", lambda: ctx.race_panel("_SubList1.selectedIndex") != index, 5)
            ctx.wait_for("[client] the engine puts on the highlighted race", lambda: identity().get("race") != worn, 15)
        raise ctx.failure(f"[client] race {race:X} not reached after 12 presses")


    def makes_new_character(ctx, race=KHAJIIT_RACE):
        """Customize and name the creator opened by joining, without manual New Game or startup prompts."""
        ctx.wait_for("[client] joining opens character creation", lambda: ctx.race_panel("_SubList1.selectedIndex") is not None, 90)
        identity = lambda: ctx.player_snapshot("client").get("identity") or {}
        ctx.selects_race(race, identity)
        ctx.move_cursor("client", BODY_TAB)
        ctx.click("client")
        ctx.wait_for("[client] the Body category is open", lambda: ctx.race_panel("_CategoriesList.selectedIndex") == BODY_CATEGORY, 10)
        ctx.press_until("female", MENU_RIGHT_KEY, Watch(ctx, lambda: identity().get("female"), bool))
        ctx.styles_character()
        # Naming resumes joining immediately: capture the chosen face before confirming it.
        made = identity()
        sections = {flag: ctx.form_save("client", 0x14, base=True, flags=flag)["data"]
                    for flag in (NPC_FACE, NPC_RACE, 0x1000000)}
        tints = ctx.player_record("client")["tints"]
        ctx.finishes_race_menu("finishes and names the character", NEW_CHARACTER_NAME)
        made["name"] = NEW_CHARACTER_NAME
        return made, sections, tints


    def section_shown(ctx, data):
        """A save section's hex in a failure: whole when short."""
        return data if len(data) <= 128 else f"{len(data) // 2} bytes"


    def unpadded_skills(ctx, data):
        """The skills section's hex without the bytes TESNPC::SaveGame (24777) never fills: it builds the 52 bytes on its
        stack and leaves 0x2A-0x2B and 0x31-0x33 as they were, so each save has other garbage there."""
        raw = bytearray(bytes.fromhex(data))
        for offset in (*range(0x2A, 0x2C), *range(0x31, 0x34)):
            if offset < len(raw):
                raw[offset] = 0
        return raw.hex()


    def copy_base_differences(ctx, copy):
        """Where the host's copy's base saves other bytes than the client's player's base, section by section of the client's
        recorded change flags ("" when none). The race section only by its race: a copy's base has no original race
        (TESNPC::Create clears it for facegen)."""
        recorded = ctx.form_save("client", 0x14, base=True)["flags"]
        own = {1 << bit: ctx.form_save("client", 0x14, base=True, flags=1 << bit)["data"] for bit in range(32) if recorded & (1 << bit)}
        theirs = {flag: ctx.form_save("", copy, base=True, flags=flag)["data"] for flag in own}
        if NPC_RACE in own:
            own[NPC_RACE], theirs[NPC_RACE] = own[NPC_RACE][:8], theirs[NPC_RACE][:8]
        if NPC_SKILLS in own:
            own[NPC_SKILLS], theirs[NPC_SKILLS] = ctx.unpadded_skills(own[NPC_SKILLS]), ctx.unpadded_skills(theirs[NPC_SKILLS])
        return ", ".join(f"{flag:#x} ({ctx.section_shown(own[flag])} -> {ctx.section_shown(theirs[flag])})" for flag in own if theirs[flag] != own[flag])


    def input_state(ctx, instance):
        return ctx.player_command(instance, {"cmd": "input_state"})


    def client_copy_looks_differences(ctx):
        """copy_looks_differences of the host's copy of the client's player, once the host shows one."""
        _game = ctx.game('')
        copy = next(iter(ctx.other_players(_game=_game)), None)
        return ctx.copy_looks_differences(copy) if copy else "the host shows no copy of the client"

