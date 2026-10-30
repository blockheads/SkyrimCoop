"""Scenarios join for the co-op harness."""
import json
import math
import shutil
import time
from ..common import (
    ARMSMAN,
    FIREBOLT_SPELL,
    GOLD_ITEM,
    HOME_CELL,
    INN,
    INN_DOOR_INSIDE,
    INN_DOOR_OUTSIDE,
    IRON_SWORD,
    KHAJIIT_RACE,
    LOAD_REFUSAL,
    NEW_CHARACTER_NAME,
    NORD_VAMPIRE_RACE,
    QUICKLOAD_KEY,
    QUICKSAVE_KEY,
    RACE_MENU,
    SAVE_REFUSAL,
    TRACES,
)
from ..errors import TestFailure
from ..comparisons import (
    distance,
    world_differences,
)


def scenario_guest_joins_by_invite(ctx):
    """A friend joins the host's session from a Steam invite, no address or port (two accounts: docs/design/testing.md,
    "Two Steam accounts"): the client leaves its direct session; the host's game makes a lobby and invites the
    client's account; the client's player accepts (its Steam opens the invite's join link); its game joins the lobby,
    connects to the host through Valve's network, and joins the session through that connection (enet tunnelled over
    it). Each game shows the other player and the party panels list each other; leaving the Steam session
    drops the client's character from the host's game. Ends joined directly again."""
    _game = ctx.game()
    ctx.disconnect_client()
    ctx.wait_on("", "the client's character leaves the host's game", lambda actors: ctx.other_player(actors) is None)
    try:
        ctx.client_accepts_steam_invite()
        ctx.wait_for("[client] is in the host's session, through Steam", lambda: ctx.game_state("client").get("online") is True, 30, _game=_game)
        ctx.wait_for("Each game shows the other player, joined through Steam", ctx.both_see_each_other, 30, _game=_game)
        ctx.party_panel_lists_the_other("joined through Steam")
        _game = ctx.game('client')
        _game.rpc({"cmd": "steam_leave"})
        ctx.wait_on("", "the client's character leaves the host's game with the Steam session",
                lambda actors: ctx.other_player(actors) is None)
    finally:
        ctx.steam_sessions_end()
        if ctx.game_state("client").get("online") is not True:
            ctx.rejoin_client()

def scenario_loaded_save_hosts_then_joins_friend(ctx):
    """A player loads a save and is hosting on Steam by itself (TESLoadGameEvent; its chat says so); a friend's invite
    then reaches it, and its game, hosting alone, steps down and joins the friend's session through Steam. Each chat
    says who joined. Ends joined directly again."""
    host_id, _ = ctx.steam_accounts()
    host_name, guest_name = ctx.steam_status("").get("name"), ctx.steam_status("client").get("name")
    ctx.disconnect_client()
    ctx.wait_on("", "the client's character leaves the host's game", lambda actors: ctx.other_player(actors) is None)
    try:
        ctx.console_on("client", "save SkyrimCoopSteamHost")
        ctx.console_on("client", "load SkyrimCoopSteamHost")
        ctx.wait_for("[client] the save loads and its game hosts on Steam",
                 lambda: ctx.steam_status("client").get("session", {}).get("state") == "hosting"
                 and ctx.game_state("client").get("hosting") is True, 90)
        ctx.wait_for("[client] its chat says it hosts",
                 lambda: "Hosting: friends can join from Steam (Shift+Tab, Invite to Game)." in ctx.system_chat("client"), 10)
        ctx.client_accepts_steam_invite()
        ctx.wait_for("[client] stops hosting and is in the host's session, through Steam",
                 lambda: ctx.game_state("client").get("online") is True and ctx.game_state("client").get("hosting") is not True, 30)
        ctx.wait_for("Each game shows the other player, joined through Steam", ctx.both_see_each_other, 30)
        ctx.wait_for(f"[client] its chat says it joined {host_name}'s game",
                 lambda: f"Joined {host_name}'s game." in ctx.system_chat("client"), 10)
        character = ctx.player_record("client")["name"]
        ctx.wait_for(f"[host] its chat says {character} joined", lambda: f"{character} joined the party." in ctx.system_chat(""), 10)
    finally:
        ctx.steam_sessions_end()
        ctx.player_command("client", {"cmd": "disconnect"})
        if ctx.game_state("client").get("online") is not True:
            ctx.rejoin_client()

def scenario_guest_cannot_save(ctx):
    """A guest's game refuses every save while it is in the host's session (Save_Impl's hook, SaveService) and its chat
    says why: the console's `save`, the quicksave key (F5) and the engine's autosave at a load door write nothing (the
    autosave says so once a session). The host's game saves."""
    _game = ctx.game()
    ctx.disconnect_client()
    ctx.rejoin_client()  # a new session: the autosave's notice is due again
    before = ctx.save_files("client")
    refusals = ctx.system_chat("client").count(SAVE_REFUSAL)
    ctx.console_on("client", "save SkyrimCoopGuestSave")
    ctx.wait_for("[client] its chat says only the host saves (console save)",
             lambda: ctx.system_chat("client").count(SAVE_REFUSAL) == refusals + 1, 10, _game=_game)
    ctx.press_key("client", QUICKSAVE_KEY)
    ctx.wait_for("[client] its chat says only the host saves (quicksave)",
             lambda: ctx.system_chat("client").count(SAVE_REFUSAL) == refusals + 2, 10, _game=_game)
    try:
        ctx.both_go(INN)
        ctx.travel("client", lambda: ctx.activate_on("client", INN_DOOR_INSIDE), ctx.player_cell("client"))
        ctx.wait_for("[client] its chat says only the host saves (the load door's autosave)",
                 lambda: ctx.system_chat("client").count(SAVE_REFUSAL) == refusals + 3, 10, _game=_game)
        ctx.travel("client", lambda: ctx.activate_on("client", INN_DOOR_OUTSIDE), ctx.player_cell("client"))
        if (told := ctx.system_chat("client").count(SAVE_REFUSAL)) != refusals + 3:
            raise ctx.failure(f"[client] its chat told the autosave's refusal again ({told - refusals} lines, expected 3)")
    finally:
        ctx.both_go(HOME_CELL)
    if written := ctx.save_files("client") - before:
        raise ctx.failure(f"[client] wrote saves while a guest: {sorted(written)}")
    _game = ctx.game('')
    host_save = _game.my_games / "Saves" / "SkyrimCoopHostSave.ess"
    host_save.unlink(missing_ok=True)
    try:
        ctx.console_on("", "save SkyrimCoopHostSave")
        ctx.wait_for("[host] its save is written", host_save.exists, 10, _game=_game)
    finally:
        host_save.unlink(missing_ok=True)
        host_save.with_suffix(".skse").unlink(missing_ok=True)

def scenario_guest_cannot_load(ctx):
    """A guest's load does nothing and explains why in chat, even when a real save exists."""
    _game = ctx.game()
    ctx.disconnect_client()
    ctx.joins_with_world("stages an authorized load before refusing player loads")
    name = f"GuestLoadRefused_{int(time.time())}"
    checkpoint = ctx.save_game("", name)
    ctx.wait_for("the host writes a real save to try loading", ctx.saves_written(checkpoint), 15)
    # A real file ensures the refusal precedes the engine opening it. This is test setup, not a guest save.
    target = ctx.save_file("client", name)
    shutil.copy2(checkpoint, target)
    shutil.copy2(checkpoint.with_suffix(".skse"), target.with_suffix(".skse"))
    # A quicksave of the loaded character gives the guest's Load menu a genuine selectable entry.
    quick = ctx.make_host_quicksave()
    quick_target = ctx.save_file("client", quick.stem)
    copied = []
    for source in (quick, quick.with_suffix(".skse")):
        destination = quick_target.with_suffix(source.suffix)
        if not destination.exists():
            shutil.copy2(source, destination)
            copied.append(destination)
    before = ctx.system_chat("client").count(LOAD_REFUSAL)
    try:
        ctx.console_on("client", f"load {name}")
        ctx.wait_for("the guest's chat explains its refused load",
                 lambda: ctx.system_chat("client").count(LOAD_REFUSAL) == before + 1, 10)
        ctx.press_key("client", QUICKLOAD_KEY)
        ctx.wait_for("the guest's chat explains its refused quickload",
                 lambda: ctx.system_chat("client").count(LOAD_REFUSAL) == before + 2, 10)
        ctx.load_latest_from_menu("client")
        ctx.wait_for("the guest's chat explains its refused menu load",
                 lambda: ctx.system_chat("client").count(LOAD_REFUSAL) == before + 3, 10)
        ctx.wait_for("the refused menu load gives control back to the guest",
                 lambda: not ({"Journal Menu", "Fader Menu"} & ctx.menus("client")) and ctx.takes_input("client"), 10)
        if not ctx.game_state("client").get("online"):
            raise ctx.failure("the refused load disconnected the guest")
    finally:
        for path in (target, target.with_suffix(".skse"), checkpoint, checkpoint.with_suffix(".skse"), *copied):
            path.unlink(missing_ok=True)

def scenario_host_menu_loads_party(ctx):
    """The host selects a save from the System Load menu; both games load it and keep their saved characters."""
    ctx.host_loads_party(False, menu=True)

def scenario_host_loads_party(ctx):
    """A chosen host load restores an older save on both games, with each player's own character and place."""
    ctx.host_loads_party(False)

def scenario_host_quickloads_party(ctx):
    """The host presses quicksave then quickload: both games load it and rejoin as their saved characters."""
    ctx.host_loads_party(True)

def scenario_guest_arrival_leaves_host_in_place(ctx):
    """The client's game starts again at the main menu and joins with the host's world, as a friend joining: the host's
    player stays where it stands. When it moves, the failure says when (the host's frames, harness body_trace) and
    what the host's game logged around then (its copy of the client spawning, where)."""
    _game = ctx.game()
    start = ctx.player_snapshot("")["placement"]["position"]
    _game = ctx.game('')
    if not (_game.rpc({"cmd": "body_trace", "refs": [0x14]}) or {}).get("ok"):
        raise ctx.failure("[host] the body trace did not start")
    _, mark = ctx.log_trail("", 1 << 62)
    frames = []
    def take(stop=False):
        _game = ctx.game('')
        frames.extend((_game.rpc({"cmd": "body_trace", **({"stop": True} if stop else {})}, timeout=10) or {}).get("entries", []))
    try:
        ctx.restart_client_at_main_menu()
        ctx.joins_with_world("arrives")
        if copy := ctx.player_copy(""):
            _game = ctx.game('')
            _game.rpc({"cmd": "body_trace", "refs": [copy]})
        arrived = time.time()
        ctx.wait_for("[host] its frames cover 2 s after the arrival", lambda: take() or (frames and frames[-1]["t"] / 1e6 > arrived + 2), 10, _game=_game)
    finally:
        take(stop=True)
    folder = TRACES / f"{time.strftime('%Y%m%d-%H%M%S')}-guest-arrival"
    folder.mkdir(parents=True, exist_ok=True)
    (folder / "bodies.json").write_text(json.dumps(frames))
    host = [f for f in frames if f["id"] == 0x14 and f.get("position")]
    moves = [(b["t"] / 1e6, math.dist(a["position"], b["position"]), b["position"]) for a, b in zip(host, host[1:])
             if math.dist(a["position"], b["position"]) > 0.1]
    end = ctx.player_snapshot("")["placement"]["position"]
    if math.dist(start[:2], end[:2]) > 10:
        lines, _ = ctx.log_trail("", mark)
        first = moves[0][0] if moves else time.time()
        around = [f"{l['time_ms'] / 1000 - first:+.2f}s {l['text']}" for l in lines if abs(l["time_ms"] / 1000 - first) < 1.5]
        traced = f"{len(host)} frames traced" + (f" from {[round(v) for v in host[0]['position']]} to "
                                                 f"{[round(v) for v in host[-1]['position']]}" if host else "")
        raise ctx.failure(f"[host] its player moved {math.dist(start[:2], end[:2]):.0f} units while the client arrived "
                          f"({[round(v) for v in start]} to {[round(v) for v in end]}; {traced}): "
                          f"{len(moves)} frames moved, the first at {time.strftime('%H:%M:%S', time.localtime(first))} "
                          f"({', '.join(f'{d:.1f}' for _, d, _ in moves[:8])} units); the host's log around it: "
                          + " | ".join(around[-25:]) + f" (frames: {folder / 'bodies.json'})")

def scenario_host_save_keeps_guest_character(ctx):
    """The host's game keeps the guest's character from the guest's game: from its joining, and on its changes (gold
    the guest picks up reaches the host's record within seconds); a save of the host's game writes it into the save's
    SKSE co-save (SaveService, CoSave)."""
    _game = ctx.game()
    key = ctx.player_record("client")["key"]
    ctx.wait_for("[host] keeps the client's character", lambda: key in ctx.kept_records(""), 15, _game=_game)
    kept_gold = lambda: ctx.kept_records("")[key]["items"].get(GOLD_ITEM, 0)
    before = kept_gold()
    ctx.console_on("client", "player.additem f 123")
    ctx.wait_for("[host] its record of the client has the gold the client has now",
             lambda: kept_gold() != before and kept_gold() == ctx.player_record("client")["items"].get(GOLD_ITEM, 0), 15, _game=_game)
    _game = ctx.game('')
    save = _game.my_games / "Saves" / "SkyrimCoopRecords.ess"
    co_save = save.with_suffix(".skse")
    try:
        ctx.console_on("", "save SkyrimCoopRecords")
        ctx.wait_for("[host] its save and co-save are written", co_save.exists, 10, _game=_game)
        data = co_save.read_bytes()
        if key.encode() not in data:
            raise ctx.failure(f"[host] its co-save ({len(data)} bytes) has no character under {key}")
    finally:
        save.unlink(missing_ok=True)
        co_save.unlink(missing_ok=True)

def scenario_guest_brings_character_into_hosts_world(ctx):
    """A friend new to the host's save joins from a game of its own (a direct connection): its game takes its
    character (level 9, Armsman, Firebolt, an iron sword), receives the host's save and loads it, and plays that
    character in the host's world, which the host's game then keeps (JoinService, HostSaveTransfer, JoinBridge)."""
    key = ctx.player_record("client")["key"]
    ctx.disconnect_client()
    ctx.wait_on("", "the client's character leaves the host's game", lambda actors: ctx.other_player(actors) is None)
    ctx.player_command("", {"cmd": "forget_record", "key": key})
    for line in ("player.setlevel 9", "player.addperk BABE4", "player.addspell 12FD0", f"player.additem {IRON_SWORD:X} 1"):
        ctx.console_on("client", line)
    own = ctx.player_record("client")
    if own["level"] != 9 or ARMSMAN not in own["perks"] or FIREBOLT_SPELL not in own["spells"]:
        raise ctx.failure(f"[client] its own character did not take the changes: {own}")
    try:
        ctx.joins_with_world("from its own game")
    finally:
        if ctx.game_state("client").get("online") is not True and not ctx.join_status("client").get("joining"):
            ctx.rejoin_client()
    record = ctx.player_record("client")
    for what, ok in (("level 9", record["level"] == 9), ("Armsman", ARMSMAN in record["perks"]),
                     ("Firebolt", FIREBOLT_SPELL in record["spells"]), ("the sword", f"0:{IRON_SWORD:X}" in record["items"]),
                     ("its name", record["name"] == own["name"])):
        if not ok:
            raise ctx.failure(f"[client] plays another character in the host's world: no {what} ({record})")
    ctx.wait_for("[host] keeps the character the client brought",
             lambda: (kept := ctx.kept_records("").get(key)) and kept["level"] == 9 and ARMSMAN in kept["perks"], 15)
    gap = distance(ctx.my_position(""), ctx.my_position("client"))
    if gap < 80 or gap > 250:
        raise ctx.failure(f"the joining player stands {gap:.0f} units from the host; expected room beside it")
    ctx.wait_for("both joining players settle without pushing each other",
             ctx.comes_to_rest(lambda: ctx.my_position("client"), still=1), 10)

def scenario_guest_joins_from_main_menu_as_kept_character(ctx):
    """A friend the host's save knows joins from the main menu (its game just started, no save loaded): the host's
    save comes, and its player is the character the host's game keeps (level and perks from its last session)."""
    key = ctx.player_record("client")["key"]
    ctx.console_on("client", "player.setlevel 11")
    ctx.console_on("client", "player.addperk BABE4")
    # The guest sends its record a few seconds after a change: the level's can go before the perk is in.
    ctx.wait_for("[host] its record of the client is at level 11, with Armsman",
             lambda: (kept := ctx.kept_records("").get(key)) and kept["level"] == 11 and ARMSMAN in kept["perks"], 20)
    ctx.restart_client_at_main_menu()
    ctx.joins_with_world("from the main menu")
    record = ctx.player_record("client")
    if record["level"] != 11 or ARMSMAN not in record["perks"] or record["key"] != key:
        raise ctx.failure(f"[client] plays another character than the host's save keeps: {record}")
    # The load replays the cell's animated objects' last animation (QASmoke's cover plates): they end where the host's are.
    differences = {}
    def poses_agree():
        nonlocal differences
        differences = {form: fields["pose"] for form, fields in
                       world_differences(ctx.world_state(""), ctx.world_state("client")).items() if "pose" in fields}
        return not differences
    try:
        ctx.wait_for("[client] the loaded cell's animated objects end as the host's", poses_agree, 10)
    except TestFailure as error:
        raise ctx.failure(f"{error}: {differences}") from None

def scenario_guest_makes_character_then_joins(ctx, steam=False, race=KHAJIIT_RACE):
    """An unknown friend joins first, then creates and names Rinara and arrives automatically. The host's
    copy matches her chosen appearance, spells and inventory; another invite uses the kept character."""
    _game = ctx.game()
    key = f"steam:{ctx.steam_accounts()[1]}" if steam else ctx.player_record("client")["key"]
    _game = ctx.game('client')
    load_order = ctx.load_order_file(_game=_game).read_bytes()
    ctx.restart_client_at_main_menu()
    try:
        ctx.player_command("", {"cmd": "forget_record", "key": key})
        if steam:
            ctx.client_accepts_steam_invite()
        else:
            ctx.player_command("client", {"cmd": "join_world", "port": ctx.session_port()})
        made, mine, made_tints = ctx.makes_new_character(race)
        sections = list(mine)
        base_sections = lambda: {flag: ctx.form_save("client", 0x14, base=True, flags=flag)["data"] for flag in sections}
        ctx.wait_for("[client] naming automatically joins the original host",
                 lambda: ctx.game_state("client").get("online") and not ctx.join_status("client").get("joining"), 240, _game=_game)
        ctx.wait_for("Each game shows the other player", ctx.both_see_each_other, 60, _game=_game)
        after = ctx.player_snapshot("client").get("identity") or {}
        for what in ("name", "race", "female", "weight"):
            if after.get(what) != made[what]:
                raise ctx.failure(f"[client] its {what} in the host's world is {after.get(what)!r}, made {made[what]!r}")
        tint_key = lambda tint: (tint["name"], tint["color"], round(tint["alpha"], 3))
        if (now_tints := sorted(map(tint_key, ctx.player_record("client")["tints"]))) != sorted(map(tint_key, made_tints)):
            lost = sorted(set(map(tint_key, made_tints)) - set(now_tints))[:5]
            raise ctx.failure(f"[client] its face tints in the host's world are not the ones it made: {len(now_tints)} "
                              f"masks, {len(made_tints)} made; missing {lost}")
        now = base_sections()
        if differ := [f"{flag:#x} ({ctx.shown(mine[flag])} -> {ctx.shown(now[flag])})" for flag in sections if now[flag] != mine[flag]]:
            raise ctx.failure(f"[client] its base in the host's world saves other bytes than the character it made, "
                              f"in the sections of change flags {', '.join(differ)}")
        kit = {"0:3C9FE": 1, "0:3CA00": 1}
        actual_kit = {k: v for k, v in ctx.player_record("client")["items"].items() if k != "0:F"}
        if actual_kit != kit:
            raise ctx.failure(f"[client] the starting kit changed: {actual_kit}, expected {kit}")
        _game = ctx.game('')
        copy = next(iter(ctx.other_players(_game=_game)), None)
        seen = ((_game.read_status() or {}).get("actors") or {}).get(str(copy), {}).get("identity") or {}
        for what in ("name", "race", "female", "weight"):
            if seen.get(what) != made[what]:
                raise ctx.failure(f"[host] its copy of the client has {what} {seen.get(what)!r}, made {made[what]!r}")
        if differ := ctx.copy_base_differences(copy):
            raise ctx.failure(f"[host] its copy's base {copy:X} saves other bytes than the client's own, in the sections of "
                              f"change flags {differ}")
        ctx.wait_for("[host] the copy carries the starting kit",
                 lambda: ctx.item_count("", copy, 0x3C9FE) == 1 and ctx.item_count("", copy, 0x3CA00) == 1, 15, _game=_game)
        race_spells = (0xAA01D, 0xAA01E) if race == KHAJIIT_RACE else (0x640B6,)
        for spell in (0x12FCD, 0x12FCC, *race_spells):
            if not ctx.has_spell("client", 0x14, spell) or not ctx.has_spell("", copy, spell):
                raise ctx.failure(f"starting spell {spell:X} did not survive arrival on both games")
        ctx.waits_until_same(f"[host] its copy {copy:X} wears the face the client made", lambda: ctx.copy_face_differences(copy), 15)
        ctx.wait_for("[host] keeps the character the client made",
                 lambda: (kept := ctx.kept_records("").get(key)) and kept["name"] == NEW_CHARACTER_NAME, 15, _game=_game)
        checkpoint = ctx.save_game("", "SkyrimCoopNewCharacter")
        ctx.wait_for("[host] saves the new character with its world", ctx.saves_written(checkpoint), 20, _game=_game)
        ctx.restart_client_at_main_menu()
        if steam:
            ctx.client_accepts_steam_invite()
            ctx.wait_for("[client] the next Steam invite loads its kept character",
                     lambda: ctx.game_state("client").get("online") and not ctx.join_status("client").get("joining"), 240, _game=_game)
        else:
            ctx.joins_with_world("the next invite loads the kept character")
        if (ctx.player_snapshot("client").get("identity") or {}).get("name") != NEW_CHARACTER_NAME:
            raise ctx.failure("the next invite did not restore the newly kept character")
    finally:  # the duo's character again, in QASmoke, joined
        if steam:
            ctx.steam_sessions_end()
        ctx.restart_client(load_order, dropped=bool(ctx.game_state("client").get("online")))
        ctx.rejoin_client()

def scenario_first_time_join_by_invite(ctx):
    scenario_guest_makes_character_then_joins(ctx, steam=True)

def scenario_first_time_join_breton(ctx):
    scenario_guest_makes_character_then_joins(ctx, race=0x13741)

def scenario_guest_changes_looks_in_session(ctx):
    """The client's player changes its looks while joined: in the race menu (showracemenu: a war paint, a nose, eyes,
    hair, a new name), then by catching vampirism (its race switches to the Nord vampire's, by setrace as the disease's
    script does). Each time the host's existing copy changes: its base saves the client's bytes and its head is painted
    with the tints the client's head shows, a vampire's pale skin too (CharacterService::OnPlayerLooksChange,
    HostCharacters::OnRespawn). The client's game then starts again as the duo's."""
    _game = ctx.game('client')
    load_order = ctx.load_order_file(_game=_game).read_bytes()
    copy = ctx.player_copy("")
    try:
        ctx.console_on("client", "showracemenu")
        ctx.wait_for("[client] the race menu opens", lambda: ctx.race_panel("_SubList2.selectedIndex") is not None, 30, _game=_game)
        ctx.styles_character()
        ctx.finishes_race_menu("finishes and names the character again", "Restyled")
        ctx.wait_for("[client] plays on, the race menu closed", lambda: RACE_MENU not in ctx.menus("client") and ctx.takes_input("client"), 30, _game=_game)
        ctx.waits_until_same("[host] its existing copy wears the client's new looks",
                         ctx.client_copy_looks_differences, 30)
        if ctx.player_copy("") != copy:
            raise ctx.failure("the race menu replaced the existing copy")
        ctx.console_on("client", "player.setrace NordRaceVampire")
        ctx.wait_for("[host] its existing copy changes to a vampire", lambda: ctx.player_race_on_host("client") == NORD_VAMPIRE_RACE, 30, _game=_game)
        ctx.waits_until_same("[host] its vampire copy wears the client's looks, its pale skin too",
                         ctx.client_copy_looks_differences, 30)
        if ctx.player_copy("") != copy:
            raise ctx.failure("the vampire change replaced the existing copy")
    finally:  # the duo's character again, in QASmoke, joined
        ctx.restart_client(load_order, dropped=bool(ctx.game_state("client").get("online")))
        ctx.rejoin_client()
