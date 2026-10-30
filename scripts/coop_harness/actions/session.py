"""Actions for the co-op harness."""
import argparse
import collections
import contextlib
import math
import os
import re
import socket
import subprocess
import time
import uuid
from ..common import PEER
from ..utilities import atomic_write


class SessionActions:
    def rejoin_client(ctx):
        """The client joins the host's running session again."""
        _game = ctx.game()
        if ctx.game_state("client").get("main_menu"):
            ctx.joins_with_world("rejoins from the main menu")
            return
        _game = ctx.game('')
        port = ((_game.read_status() or {}).get("session") or {}).get("port")
        if not port:
            raise ctx.failure("[host] no session port to rejoin")
        _game = ctx.game('client')
        ctx.start_session(f"join {port}", _game=_game)
        ctx.wait_for("Each game shows the other player again", ctx.both_see_each_other, 30, _game=_game)


    def party_panel_lists_the_other(ctx, when):
        """Each game's party panel shows the other player, with its character's health: the client's
        lists the host (as the host), the host's lists the client."""
        def panel(instance):
            return ctx.game_state(instance).get("party_panel")
        for instance, host in (("client", True), ("", False)):
            ctx.wait_for(f"[{instance or 'host'}] the party panel lists the {'host' if host else 'client'} {when}",
                     lambda: (rows := panel(instance)) is not None and len(rows) == 1 and rows[0]["host"] == host
                     and rows[0]["health"] >= 0, 10)


    def party_panel_shows_level(ctx, instance, level, when):
        """The game's party panel shows the other player at aLevel."""
        ctx.wait_for(f"[{instance or 'host'}] the party panel shows the other player at level {level} {when}",
                 lambda: (rows := ctx.game_state(instance).get("party_panel")) and len(rows) == 1 and rows[0]["level"] == level, 5)


    def join_status(ctx, instance):
        """The game's last attempt to join: {"state", "reason", "message", "details"}."""
        return ctx.game_state(instance).get("join") or {}


    def system_chat(ctx, instance):
        """The system lines (session events) the game's chat received."""
        return [line["text"] for line in (ctx.game_state(instance).get("chat") or {}).get("lines", []) if line["system"]]


    def free_port(ctx):
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as probe:
            probe.bind(("127.0.0.1", 0))
            return probe.getsockname()[1]


    def run_peer(ctx, *args):
        if not PEER.exists():
            raise ctx.failure(f"{PEER} missing: scripts/xmake-linux.sh build -j16 coop_peer")
        return [str(PEER), *map(str, args)]


    def client_joins(ctx, port):
        """The client asks the game on port to join (its answer decides what follows)."""
        _game = ctx.game('client')
        atomic_write(_game.control / "session.txt", f"{uuid.uuid4().hex} join {port}\n")


    def ui_element(ctx, instance, element_id):
        """Our UI's element of that id: {"shown", "text", "box": [x, y, width, height] in window pixels}."""
        return ctx.player_command(instance, {"cmd": "ui_element", "id": element_id})


    def clicks_ui(ctx, what, element_id):
        """The client's player moves the menu cursor onto our UI's element and clicks it."""
        element = ctx.ui_element("client", element_id)
        if not element.get("shown"):
            raise ctx.failure(f"[client] {what}: {element_id} is not shown")
        x, y, width, height = element["box"]
        ctx.move_cursor("client", (x + width / 2, y + height / 2))
        ctx.click("client")


    def empty_plugin(ctx):
        import struct
        fields = (b"HEDR" + struct.pack("<HfiI", 12, 1.71, 0, 0x800)
                  + b"CNAM" + struct.pack("<H", 11) + b"SkyrimCoop\0")
        return b"TES4" + struct.pack("<IIIHHHH", len(fields), 0, 0, 0, 0, 44, 0) + fields


    def load_order_file(ctx, *, _game=None):
        """The selected game's Plugins.txt (in its own prefix: each test game has its own load order)."""
        _game = ctx.game(_game)
        return _game.prefix / "pfx/drive_c/users/steamuser/AppData/Local/Skyrim Special Edition/Plugins.txt"


    def restart_client(ctx, plugins, dropped=True):
        """Starts the client game again with `plugins` as its Plugins.txt; it joins nothing yet. A game
        killed in a session says no goodbye: the host must notice it stopped answering (enet's timeout,
        up to 30 s) and drop the player, or the next game on that connection is taken for it."""
        _game = ctx.game('client')
        ctx.stop_game(_game=_game)
        if dropped:
            ctx.wait_for("[host] drops the client whose game ended", lambda: not ctx.game_state("").get("players"), 40, _game=_game)
            _game = ctx.game('client')
        ctx.load_order_file(_game=_game).write_bytes(plugins)
        ctx.launch(argparse.Namespace(cell="QASmoke", save=None, boot_timeout=240, world_timeout=180), ctx.new_run_dir("restart"), _game=_game)
        ctx.run_console("player.setav magicka 5000", _game=_game)


    def difficulty(ctx, instance):
        return ctx.game_state(instance)["difficulty"]


    def set_difficulty(ctx, instance, value):
        """Sets the game's difficulty as its settings menu does; the difficulty right after."""
        return ctx.player_command(instance, {"cmd": "difficulty", "value": value})["difficulty"]

