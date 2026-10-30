"""Scenarios travel for the co-op harness."""
import math
import time
from ..comparisons import is_close
from ..common import (
    CHICKEN,
    HOME_CELL,
    INN,
    INN_DOOR_INSIDE,
    INN_DOOR_OUTSIDE,
    LYDIA,
    OUTDOOR_CELL,
    RIVER_BANK,
    RIVER_CELL,
    RIVER_SPOT,
)
from ..errors import TestFailure
from ..utilities import log


def scenario_remove_host_npc(ctx):
    """Host disables an NPC it owns; the client's copy must disappear."""
    npc, _, copy = ctx.spawn_host_npc(CHICKEN)
    ctx.console_on("", f"{npc:08X}.disable")
    ctx.wait_for("[host] NPC unloaded", lambda: str(npc) not in ctx.local_player("").get("local_actors", {}), 10)
    ctx.wait_on("client", f"NPC copy {copy:08X} removed", lambda actors: str(copy) not in actors)
    ctx.hold()

def scenario_teleport_keeps_copies(ctx):
    """The client teleports within the cell (moveto): its game keeps the copies it shows (the host's
    character and NPC stay the same actors). A respawned copy loses its state and id."""
    _, session_id, copy = ctx.spawn_host_npc(LYDIA, restrained=True)
    host_copy = ctx.other_player(ctx.actors_on("client"))
    log_file = ctx.plugin_log("client")
    start = log_file.stat().st_size
    ctx.console_on("client", f"player.moveto {copy:08X}")
    ctx.wait_for("[client] the teleport arrives (cell arrival)",
             lambda: "Arrived in cell" in log_file.read_bytes()[start:].decode(errors="replace"), 10)
    end = time.monotonic() + 1.5
    while time.monotonic() < end:
        actors = ctx.actors_on("client")
        if ctx.other_player(actors) != host_copy:
            raise ctx.failure(f"the host's character was respawned: copy {host_copy:08X} is now {ctx.other_player(actors)}")
        now = next((int(a) for a, s in actors.items() if s.get("session_id") == session_id), None)
        if now != copy:
            raise ctx.failure(f"the host's NPC was respawned: copy {copy:08X} is now {now}")
        time.sleep(0.1)

def scenario_copy_leaves_water_with_owner(ctx):
    """The host swims in the river and then stands on its bank (put there, as a script's MoveTo would): its copy
    on the client swims while it does and stops swimming with it, though a copy's own controller never gets the
    support that ends a swim (Character::sub_37946, docs/progress.md)."""
    _game = ctx.game()
    ctx.both_go("river", RIVER_CELL)
    ctx.console_on("", "tcai")
    try:
        for name, spot in (("", RIVER_SPOT), ("client", RIVER_BANK)):
            for axis, value in zip("xyz", spot):
                ctx.console_on(name, f"player.setpos {axis} {value}")
        ctx.wait_for("[host] swims", lambda: ctx.water("").get("swimming"), 10, _game=_game)
        ctx.show("client", ctx.other_player)
        _game = ctx.game('client')
        copy = ctx.other_player((_game.read_status() or {}).get("actors", {}))
        ctx.wait_for("[client] the host's character swims", lambda: ctx.water("client", copy).get("swimming"), 10, _game=_game)
        for axis, value in zip("xyz", (RIVER_BANK[0] + 150, RIVER_BANK[1], RIVER_BANK[2])):
            ctx.console_on("", f"player.setpos {axis} {value}")
        ctx.wait_for("[host] stands on the bank, not swimming", lambda: not ctx.water("").get("swimming"), 10, _game=_game)
        ctx.wait_for("[client] the host's character stops swimming with it", lambda: not ctx.water("client", copy).get("swimming"), 10, _game=_game)
        ctx.hold()
    finally:
        ctx.console_on("", "tcai")
        ctx.both_go(HOME_CELL)

def scenario_dive_host_player(ctx):
    """Host dives, stays under, surfaces; the client's copy of the host follows without its own decisions."""
    ctx.dive_roundtrip("", "client", "host's character")

def scenario_dive_client_player(ctx):
    """Client dives, stays under, surfaces; the host's copy of the client follows without its own decisions."""
    ctx.dive_roundtrip("client", "", "client's character")

def scenario_party_changes_cells(ctx):
    """The party goes to another cell and back together (the party tether): each arrival drops the
    copies the old cell took, and the host sends what is there. In each cell both games show the
    other player again, and the client shows the host's NPCs there."""
    try:
        ctx.both_go(OUTDOOR_CELL)
        ctx.wait_on("client", f"shows the host's NPCs in {OUTDOOR_CELL}", lambda actors: ctx.remote_npcs(actors), 30)
    finally:
        ctx.both_go(HOME_CELL)

def scenario_party_stacks_on_arrival(ctx, *, _game=None):
    """The party arrives on one spot (a coc puts the host on the cell's marker, the tether the client where the
    host arrived): both stand on it, passing through each other on both games, and both games
    show the same view, though each player looked its own way before. The client walks off it (nothing
    else happens) and then into the host, which stops it: its collision is back. And again after travelling home."""
    _game = ctx.game(_game)
    home = False
    try:
        for cell in (INN, HOME_CELL):
            ctx.look_apart()
            ctx.both_go(cell)
            home = cell == HOME_CELL
            ctx.stacked(f"in {cell}, the players stand on one spot")
            ctx.same_view(f"In {cell}, both games show the same view")
            # Ahead, the way the arrival faces (into the room).
            at, heading = ctx.my_position("client"), ctx.local_player("client")["rotation"][2]
            ctx.walk_off("client", (at[0] + 400 * math.sin(heading), at[1] + 400 * math.cos(heading)), "walks off the stack", _game=_game)
            ctx.bumps_into_other("client")
            ctx.hold()
    finally:
        if not home:
            ctx.both_go(HOME_CELL)

def scenario_party_uses_door(ctx):
    """The host walks out of the inn through its door and the client follows (the party tether); then the
    client leads back in and the host follows. Each time both games end up showing the other player and the
    same view (the follower faces the way the leader arrived), and outside the client shows the host's NPCs."""
    try:
        ctx.both_go(INN)
        client_before = ctx.player_cell("client")
        ctx.look_apart()
        ctx.travel("", lambda: ctx.activate_on("", INN_DOOR_INSIDE), ctx.player_cell(""))
        ctx.follows("client", client_before)
        ctx.players_together("outside the inn")
        ctx.wait_for("[client] the arrival's fade is over", lambda: ctx.takes_input("client"), 15)
        ctx.same_view("Outside the inn, both games show the same view")
        ctx.wait_on("client", "shows the host's NPCs outside", lambda actors: ctx.remote_npcs(actors), 30)
        host_before = ctx.player_cell("")
        ctx.look_apart()
        ctx.travel("client", lambda: ctx.activate_on("client", INN_DOOR_OUTSIDE), ctx.player_cell("client"))
        ctx.follows("", host_before)
        ctx.players_together("back in the inn")
        ctx.wait_for("[host] the arrival's fade is over", lambda: ctx.takes_input(""), 15)
        ctx.same_view("Back in the inn, both games show the same view")
        ctx.hold()
    finally:
        ctx.both_go(HOME_CELL)

def scenario_party_quick_travel(ctx):
    """The client walks out of the inn and straight back in while the host's game stalls (a hitch, a slow
    load), so the host gets both travels at once: it ends up where the party went last (the inn), and the
    party settles there instead of bouncing between the two (a follower's stop on the way must not lead).
    Loads here take ~0.2 s, too short for the race without the stall."""
    try:
        ctx.both_go(INN)
        inn = ctx.player_cell("")
        with ctx.game_stalled(""):
            ctx.activate_on("client", INN_DOOR_INSIDE)
            ctx.wait_for("[client] out of the inn", lambda: ctx.player_cell("client") not in (None, inn), 30)
            ctx.wait_for("[client] the arrival's fade is over", lambda: ctx.takes_input("client"), 15)
            ctx.activate_on("client", INN_DOOR_OUTSIDE)
            ctx.wait_for("[client] back in the inn", lambda: ctx.player_cell("client") == inn, 30)
        ctx.players_together("back in the inn")
        ctx.wait_for("[host] the arrival's fade is over", lambda: ctx.takes_input(""), 15)
        # Settled: for 5 s nobody travels again.
        end = time.monotonic() + 5
        while time.monotonic() < end:
            cells = {name or "host": ctx.player_cell(name) for name in ("", "client")}
            if set(cells.values()) != {inn}:
                raise ctx.failure(f"the party moved on after settling in the inn ({inn}): {cells}")
            time.sleep(0.2)
        ctx.hold()
    finally:
        ctx.both_go(HOME_CELL)

def scenario_party_fast_travels(ctx):
    """The party fast travels from the river to Riverwood within one worldspace, twice: the host leads and
    the client follows (the party tether), then the client leads and the host follows. The host's trip
    passes time and the client keeps the host's time; each time both games end up showing the other
    player in Riverwood. Fast travel here is the console's FastTravel: it queues the same move, with the
    marker, as the map's."""
    def hour(instance):
        return ctx.game_state(instance).get("time", {}).get("hour")
    try:
        ctx.both_go("river", RIVER_CELL)
        before, client_before = hour(""), ctx.player_cell("client")
        ctx.travel("", lambda: ctx.console_on("", f"fasttravel {INN_DOOR_OUTSIDE:X}"), ctx.player_cell(""))
        passed = (hour("") - before) % 24
        if passed < 0.5:
            raise ctx.failure(f"the host's trip passed {passed:.2f} h: fast travel passes time")
        ctx.follows("client", client_before)
        ctx.players_together("in Riverwood, the host leading")
        ctx.wait_for("[client] keeps the host's time", lambda: is_close(hour("client"), hour(""), 0.1), 10)
        try:
            ctx.wait_for("[client] keeps the host's days passed and date", ctx.same_calendar, 10)
        except TestFailure:
            raise ctx.failure(ctx.calendars("[client] off the host's calendar after its trip"))

        ctx.both_go("river", RIVER_CELL)
        host_before = ctx.player_cell("")
        trip_start = hour("")
        ctx.travel("client", lambda: ctx.console_on("client", f"fasttravel {INN_DOOR_OUTSIDE:X}"), ctx.player_cell("client"))
        ctx.follows("", host_before)
        ctx.players_together("in Riverwood, the client leading")
        ctx.wait_for("the guest's trip advances the host clock", lambda: (hour("") - trip_start) % 24 > 0.5, 10)
        ctx.wait_for("the guest's trip keeps the party date", ctx.same_calendar, 10)
        ctx.wait_for("[client] keeps the host's time after its own trip", lambda: is_close(hour("client"), hour(""), 0.1), 10)
        ctx.hold()
    finally:
        ctx.both_go(HOME_CELL)

def scenario_party_loads_overlap(ctx):
    """Both leaders announce the destination before loading, so followers queue without waiting for arrival."""
    home = ctx.player_cell("")
    try:
        for leader, follower in (("", "client"), ("client", "")):
            ctx.both_go(INN)
            marks = {name: ctx.log_trail(name, 1 << 62)[1] for name in (leader, follower)}
            before = ctx.player_cell(follower)
            ctx.travel(leader, lambda: ctx.console_on(leader, f"coc {HOME_CELL}"), ctx.player_cell(leader))
            ctx.follows(follower, before)
            ctx.players_together("the overlapping loads arrive together")
            phases = {}
            announcement = None
            for name in (leader, follower):
                entries = ctx.log_trail(name, marks[name])[0]
                if name == leader:
                    announcement = next((e["time_ms"] for e in entries
                                         if "queued destination sent before loading" in e["text"]), None)
                phases[name] = [(int(e["text"].rsplit(" ", 1)[-1]), e["time_ms"])
                                for e in entries if "PositionPlayerEvent phase " in e["text"]]
            starts = {name: next((ms for phase, ms in phases[name] if phase == 0), None)
                      for name in (leader, follower)}
            finish = next((ms for phase, ms in phases[leader] if phase == 4), None)
            log(f"load timing: leader={leader or 'host'}, starts={starts}, leader_finish={finish}")
            if announcement is None or starts[leader] is None or announcement > starts[leader]:
                raise ctx.failure(f"the destination was not sent before the leader's load: sent={announcement}, phases={phases}")
            if starts[follower] is None:
                raise ctx.failure(f"the follower never began loading: {phases}")
    finally:
        if ctx.player_cell("") != home or ctx.player_cell("client") != home:
            ctx.both_go(HOME_CELL)

def scenario_guest_quest_teleport(ctx):
    """The quest MoveTo native on a guest's own body leads the party to its destination."""
    try:
        before = ctx.player_cell("")
        ctx.player_command("client", {"cmd": "papyrus_call", "class": "ObjectReference", "function": "MoveTo",
                                  "self": 0x14, "args": [{"form": INN_DOOR_INSIDE}, 0.0, 0.0, 0.0, True],
                                  "player_action": True})
        ctx.wait_for("the guest's quest-native teleport arrives", lambda: ctx.player_cell("client") != before, 30)
        ctx.follows("", before)
        ctx.players_together("the guest's quest teleport carries the party")
    finally:
        ctx.both_go(HOME_CELL)

def scenario_party_travel_with_menu(ctx):
    """A following player can be in the inventory menu when the leader travels."""
    home = ctx.player_cell("")
    try:
        for leader, follower in (("", "client"), ("client", "")):
            ctx.both_go(INN)
            before = ctx.player_cell(follower)
            ctx.tap_key(follower, 0x17)  # I: the native inventory shortcut
            ctx.wait_for("the follower opens InventoryMenu", lambda: "InventoryMenu" in ctx.open_menus(follower), 10)
            ctx.travel(leader, lambda: ctx.console_on(leader, f"coc {HOME_CELL}"), ctx.player_cell(leader))
            ctx.follows(follower, before)
            ctx.players_together("the player in the menu follows the leader")
            if "InventoryMenu" in ctx.open_menus(follower):
                ctx.tap_key(follower, 0x0F)
                ctx.wait_for("the follower closes its inventory", lambda: "InventoryMenu" not in ctx.open_menus(follower), 10)
    finally:
        for name in ("", "client"):
            if "InventoryMenu" in ctx.open_menus(name):
                ctx.tap_key(name, 0x0F)
        if ctx.player_cell("") != home or ctx.player_cell("client") != home:
            ctx.both_go(HOME_CELL)
