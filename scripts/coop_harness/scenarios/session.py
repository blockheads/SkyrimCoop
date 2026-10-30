"""Scenarios session for the co-op harness."""
import subprocess
from ..common import (
    GAME,
    IRON_SWORD,
    LYDIA,
    MAIN_LIST,
    MAIN_MENU_STATE,
    TEST_PLUGIN,
)
from ..utilities import log
from ..objects import StandInHost


def scenario_session_roster(ctx):
    """Everyone connected is in the session (there are no parties): each game lists the other
    player, and both agree the host hosts."""
    ctx.wait_for("[host] lists the client, not as the host",
             lambda: len(ctx.session_players("")) == 1 and not any(ctx.session_players("").values()), 10)
    ctx.wait_for("[client] lists the host, as the host", lambda: list(ctx.session_players("client").values()) == [True], 10)
    if not ctx.game_state("").get("hosting") or ctx.game_state("client").get("hosting"):
        raise ctx.failure("the host's game must host and the client's must not")

def scenario_party_panel(ctx):
    """The party panel (the in-game UI) lists the other player in each game, with its health, and
    still does after both games hot reload."""
    _game = ctx.game()
    ctx.party_panel_lists_the_other("now")
    ctx.reload_games()
    ctx.party_panel_lists_the_other("after a hot reload")

def scenario_party_shows_levels(ctx):
    """Each party panel shows the other player's level, and a level-up in either game (the console's
    advlevel, as a level-up raises it) reaches the other's panel. The baseline puts the levels back."""
    levels = {instance: ctx.game_state(instance)["level"] for instance in ("", "client")}
    for instance, other in (("", "client"), ("client", "")):
        ctx.party_panel_shows_level(instance, levels[other], "now")
    for instance, other in (("client", ""), ("", "client")):
        ctx.console_on(instance, "player.advlevel")
        ctx.wait_for(f"[{instance or 'host'}] the player is level {levels[instance] + 1}",
                 lambda: ctx.game_state(instance)["level"] == levels[instance] + 1, 5)
        ctx.party_panel_shows_level(other, levels[instance] + 1, f"after the {instance or 'host'}'s level-up")
        copy = ctx.player_copy(other)
        if copy is None:
            raise ctx.failure(f"[{other or 'host'}] no copy to check its base level")
        ctx.waits_until_same(f"[{other or 'host'}] the existing copy's base has its owner's level",
                         lambda: "" if ctx.actor_state(other, copy).get("identity", {}).get("level") ==
                         levels[instance] + 1 else "base level differs", 5)
        if ctx.player_copy(other) != copy:
            raise ctx.failure("the level change replaced the player's copy")

def scenario_late_join_client(ctx):
    """The client leaves and joins again while the host's world changed: it must see the world as it
    is now (the host's player and NPC as they are), and the host must drop the client's character
    while it is away."""
    npc, session_id, _ = ctx.spawn_host_npc(LYDIA)
    ctx.console_on("", f"{npc:08X}.additem {IRON_SWORD:08X} 1")
    ctx.console_on("", f"{npc:08X}.equipitem {IRON_SWORD:08X}")
    ctx.give_sword("")
    ctx.disconnect_client()
    ctx.wait_on("", "the client's character leaves the host's game", lambda actors: ctx.other_player(actors) is None)
    # While the client is away.
    ctx.draw("")
    ctx.console_on("", f"{npc:08X}.damageav health 50")
    ctx.rejoin_client()
    ctx.party_panel_lists_the_other("after the client joined again")
    copy = ctx.copy_of("client", session_id)
    ctx.wait_on("client", "the NPC's copy holds the sword", lambda actors: IRON_SWORD in actors.get(str(copy), {}).get("equipped", []))
    ctx.copy_health_matches(npc, copy)
    ctx.wait_on("client", "the host's character has the sword drawn",
            lambda actors: ctx.other_player(actors) is not None and actors[str(ctx.other_player(actors))].get("weapon_drawn"))
    ctx.hold()

def scenario_host_rejoins(ctx):
    """The host leaves (which ends the session for everyone: the host is the session) and hosts
    again: the client lost its host, its join panel says it reconnects, and it does by itself
    (JoinService). The host must own the NPCs it spawns, not the client."""
    _game = ctx.game()
    ctx.player_command("", {"cmd": "disconnect"})
    ctx.wait_for("[host] stopped hosting", lambda: not ctx.game_state("").get("hosting") and ctx.game_state("").get("online") is False, 15, _game=_game)
    ctx.wait_for("[client] the session ended", lambda: ctx.game_state("client").get("online") is False, 15, _game=_game)
    ctx.wait_for("[client] its join panel says it reconnects",
             lambda: ctx.join_status("client").get("state") == "reconnecting" and ctx.ui_element("client", "join-message").get("text")
             == "Lost connection to the host. Reconnecting...", 10, _game=_game)
    _game = ctx.game('')
    ctx.start_session("", _game=_game)
    ctx.wait_for("[client] reconnected by itself", lambda: ctx.game_state("client").get("online") is True, 40, _game=_game)
    ctx.wait_for("[client] its join panel is gone", lambda: not ctx.ui_element("client", "join-panel").get("shown"), 5, _game=_game)
    ctx.wait_for("Each game shows the other player again", ctx.both_see_each_other, 30, _game=_game)
    npc, session_id, copy = ctx.spawn_host_npc(LYDIA)
    log(f"host's NPC {npc:08X} (session id {session_id:X}) reaches the client as {copy:08X}")

def scenario_host_kicks_client(ctx):
    """The host removes the client from the session: the client is disconnected, its character
    leaves the host's game and the host no longer lists it. The client can join again."""
    players = ctx.game_state("").get("players", [])
    if len(players) != 1:
        raise ctx.failure(f"[host] expected the client in the session, got {players}")
    ctx.player_command("", {"cmd": "kick", "player": players[0]["id"]})
    ctx.wait_for("[client] disconnected", lambda: ctx.game_state("client").get("online") is False, 10)
    ctx.wait_for("[client] its join panel says the host removed it, and it does not reconnect",
             lambda: (status := ctx.join_status("client")).get("state") == "ended" and status.get("reason") == "kicked"
             and ctx.ui_element("client", "join-message").get("text") == "The host removed you from the session.", 10)
    ctx.wait_for("[host] no longer lists the client", lambda: not ctx.game_state("").get("players"), 10)
    ctx.wait_on("", "the client's character leaves the host's game", lambda actors: ctx.other_player(actors) is None)
    ctx.wait_for("[client] the kicked guest returns to the main menu",
             lambda: ctx.game_state("client").get("main_menu"), 30)
    ctx.rejoin_client()

def scenario_join_refused_shows_why(ctx):
    """A refused game's join status says why (a reason code and a sentence), and the host's chat says
    who could not join. The client asks stand-in hosts (coop_peer) on another build, on another
    Skyrim and SKSE, and a full one; games on another build and on another Skyrim ask the real host.
    The client then joins the host again."""
    _game = ctx.game()
    ctx.disconnect_client()
    for refuse, reason in (("wrong_version", "wrong_build"), ("wrong_game", "wrong_game"), ("full", "full")):
        with StandInHost(ctx, refuse) as port:
            ctx.client_joins(port)
            ctx.wait_for(f"[client] refused: {reason}",
                     lambda: (status := ctx.join_status("client"))["state"] == "refused" and status["reason"] == reason
                     and status["message"], 15, _game=_game)
        if ctx.game_state("client").get("online"):
            raise ctx.failure("[client] refused, yet online")

    _game = ctx.game('')
    port = (_game.read_status() or {}).get("session", {}).get("port")
    host_build = ctx.game_state("")["build"]
    for name, options, answer, said in (
            ("Intruder", ("--version", "v0.0.0-stand-in"), "refused wrong_version", "they run SkyrimCoop v0.0.0-stand-in"),
            ("Visitor", ("--version", host_build, "--game", "1.6.640", "--skse", "2.2.3"), "refused wrong_game",
             "they run Skyrim 1.6.640 with SKSE 2.2.3")):
        result = subprocess.run(ctx.run_peer("join", "--port", port, "--name", name, *options),
                                capture_output=True, text=True, timeout=20).stdout.strip()
        if result != answer:
            raise ctx.failure(f"the host answered {name} with {result!r}, expected {answer!r}")
        ctx.wait_for(f"[host] the chat says {name} couldn't join: {said}",
                 lambda: any(line.startswith(f"{name} couldn't join: {said}") for line in ctx.system_chat("")), 10, _game=_game)
    if ctx.game_state("").get("players"):
        raise ctx.failure(f"[host] lists players after a refusal: {ctx.game_state('').get('players')}")

    ctx.rejoin_client()
    ctx.wait_for("[client] joined", lambda: ctx.join_status("client")["state"] == "joined", 5, _game=_game)

def scenario_join_panel_shows_refusal(ctx):
    """A friend's game at the main menu is refused (a full session): the join panel shows why, Copy puts the
    reason and the sentence on the clipboard, and Back puts the panel away while the main menu takes none of the
    clicks (UiService, UiInput). The client's game then starts again as the duo's."""
    _game = ctx.game('client')
    load_order = ctx.load_order_file(_game=_game).read_bytes()
    ctx.restart_client_at_main_menu()
    try:
        ctx.wait_for("[client] the main menu lists its entries",
                 lambda: ctx.menu_var("client", "Main Menu", f"{MAIN_LIST}.selectedEntry.index") is not None, 30, _game=_game)
        menu_state = ctx.menu_var("client", "Main Menu", MAIN_MENU_STATE)
        with StandInHost(ctx, "full") as port:
            ctx.player_command("client", {"cmd": "join_world", "port": port})
            ctx.wait_for("[client] refused: full", lambda: ctx.join_status("client").get("reason") == "full", 15, _game=_game)
        message = ctx.join_status("client")["message"]
        ctx.wait_for(f"[client] the join panel says why: {message!r}",
                 lambda: (shown := ctx.ui_element("client", "join-message")).get("shown") and shown.get("text") == message, 10, _game=_game)
        ctx.clicks_ui("copies the details", "copy")
        ctx.wait_for("[client] Copy puts the reason and the sentence on the clipboard",
                 lambda: {"reason full", f"message {message}"} <= set(ctx.player_command("client", {"cmd": "clipboard"})["text"].splitlines()), 5, _game=_game)
        ctx.clicks_ui("goes back", "back")
        ctx.wait_for("[client] Back puts the panel away", lambda: not ctx.ui_element("client", "join-panel").get("shown"), 5, _game=_game)
        if (now := ctx.menu_var("client", "Main Menu", MAIN_MENU_STATE)) != menu_state:
            raise ctx.failure(f"[client] the main menu took the clicks: its state is {now!r}, was {menu_state!r}")
    finally:  # the duo's character again, in QASmoke, joined
        ctx.restart_client(load_order, dropped=bool(ctx.game_state("client").get("online")))
        ctx.rejoin_client()

def scenario_join_refused_plugins_differ(ctx):
    """Two real games with different load orders: the client game restarts with one plugin more
    than the host, asks the host to join and is refused with that difference listed; the host's chat
    says why. The client then restarts with the host's load order and joins again."""
    _game = ctx.game('client')
    original = ctx.load_order_file(_game=_game).read_bytes()
    (GAME / "Data" / TEST_PLUGIN).write_bytes(ctx.empty_plugin())  # inactive unless a load order lists it
    try:
        ctx.restart_client(original.rstrip(b"\n") + f"\n*{TEST_PLUGIN}\n".encode())
        _game = ctx.game('')
        port = (_game.read_status() or {}).get("session", {}).get("port")
        ctx.client_joins(port)
        ctx.wait_for(f"[client] refused: mods_differ, extra {TEST_PLUGIN}",
                 lambda: (status := ctx.join_status("client"))["state"] == "refused" and status["reason"] == "mods_differ"
                 and status["details"] == [f"extra {TEST_PLUGIN}"], 30, _game=_game)
        ctx.wait_for("[host] the chat says the client's plugins differ",
                 lambda: any(line.endswith("couldn't join: their plugins differ from yours (1 difference).")
                             for line in ctx.system_chat("")), 10, _game=_game)
        if ctx.game_state("").get("players"):
            raise ctx.failure(f"[host] lists players after a refusal: {ctx.game_state('').get('players')}")
    finally:
        # Put the client back whatever failed: its own load order, the plugin gone.
        (GAME / "Data" / TEST_PLUGIN).unlink(missing_ok=True)
        ctx.restart_client(original, dropped=False)
    ctx.rejoin_client()
    ctx.wait_for("[client] joined", lambda: ctx.join_status("client")["state"] == "joined", 5, _game=_game)

def scenario_difficulty_follows_host(ctx):
    """The session's difficulty is the host's: a joining game takes it, follows the host's changes
    and cannot change away from it; the host's own is never changed; leaving gives the client's game
    its own difficulty back."""
    start = ctx.difficulty("")
    own = (start + 1) % 6    # the client's own, before it joins
    other = (start + 3) % 6  # what the host changes to
    ctx.disconnect_client()
    ctx.set_difficulty("client", own)
    try:
        ctx.rejoin_client()
        ctx.wait_for(f"[client] takes the host's difficulty {start}", lambda: ctx.difficulty("client") == start, 5)
        ctx.set_difficulty("", other)
        ctx.wait_for(f"[client] follows the host to {other}", lambda: ctx.difficulty("client") == other, 5)
        if ctx.difficulty("") != other:
            raise ctx.failure(f"[host] difficulty {ctx.difficulty('')}, set to {other}: the session changed it")
        if ctx.set_difficulty("client", start) != start:
            raise ctx.failure("[client] the difficulty could not be set")
        ctx.wait_for(f"[client] is held at the host's {other}", lambda: ctx.difficulty("client") == other, 5)
        ctx.disconnect_client()
        ctx.wait_for(f"[client] has its own difficulty {own} back", lambda: ctx.difficulty("client") == own, 5)
    finally:
        ctx.set_difficulty("", start)
        if ctx.game_state("client").get("online") is False:
            ctx.rejoin_client()
        ctx.wait_for(f"[client] back at the host's {start}", lambda: ctx.difficulty("client") == start, 5)
