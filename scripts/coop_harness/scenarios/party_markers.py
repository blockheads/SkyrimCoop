"""Players find each other in the native compass and map."""
from ..common import OUTDOOR_CELL, HOME_CELL, TAB_KEY, ROOT, WINDOWS
import json
import subprocess


def value(ctx, game, menu, path):
    response = (ctx.game(game).rpc({"cmd": "menu_var", "menu": menu, "path": path}) or {})
    return response.get("value")


def evidence(ctx, game, label, menu, paths):
    folder = ROOT / "build/marker-evidence/native"
    folder.mkdir(parents=True, exist_ok=True)
    (folder / f"{label}-{game or 'host'}.json").write_text(json.dumps(
        {path: value(ctx, game, menu, path) for path in paths}, indent=2))
    if not WINDOWS:
        subprocess.run(["import", "-display", ctx.game(game).display, "-window", "root",
                        "-crop", "1280x720+4+30", "+repage", str(folder / f"{label}-{game or 'host'}.png")], check=True, timeout=10)


def compass(ctx):
    for game in ("", "client"):
        name = ctx.game_state(game)["players"][0]["name"]
        root = "_root.HUDMovieBaseInstance"
        ctx.wait_for(f"[{game or 'host'}] compass tracks {name}",
                     lambda: value(ctx, game, "HUD Menu", root + ".SkyrimCoopPartyCount") == 1, 5)
        index = int(value(ctx, game, "HUD Menu", root + ".SkyrimCoopPartyStart"))
        marker = root + f".CompassMarkerList.{index}.movie"
        ctx.wait_for(f"[{game or 'host'}] {name}'s compass icon is drawn",
                     lambda: value(ctx, game, "HUD Menu", marker + ".partyName") == name and
                     (value(ctx, game, "HUD Menu", marker + "._alpha") or 0) > 0 and
                     (value(ctx, game, "HUD Menu", marker + "._xscale") or 0) > 0, 5)
        evidence(ctx, game, "compass", "HUD Menu", [root + ".SkyrimCoopPartyCount", marker + ".partyName",
                 marker + "._x", marker + "._alpha", marker + "._xscale"])


def world_map(ctx, cell=OUTDOOR_CELL, movement=True):
    suffix = "" if movement else "-interior"
    try:
        ctx.both_go(cell)
        if movement:
            ctx.hold_key("client", "d", 0.8)
        for game in ("", "client"):
            player = ctx.game_state(game)["players"][0]
            marker = f"_root.WorldMap.MarkerContainer.SkyrimCoopPartyMarkers.Player{player['id']}"
            ctx.open_world_map(game)
            ctx.move_cursor(game, (1050, 100))
            try:
                ctx.wait_for(f"[{game or 'host'}] map shows {player['name']}",
                             lambda: value(ctx, game, "MapMenu", marker + ".textField.text") == player["name"] and
                             value(ctx, game, "MapMenu", marker + "._visible") == 1 and
                             value(ctx, game, "MapMenu", marker + ".TextClip._visible") == 1 and
                             value(ctx, game, "MapMenu", marker + ".TextClip._alpha") == 100, 5)
            finally:
                evidence(ctx, game, "world-map" + suffix, "MapMenu", [marker + path for path in
                         (".textField.text", "._visible", "._x", "._y", ".TextClip._visible", ".TextClip._alpha", ".textField._alpha")] +
                         ["_root.WorldMap.MapWidth", "_root.WorldMap.MapHeight", "_root.WorldMap.MarkerContainer._visible"])
            if movement:
                before = [value(ctx, game, "MapMenu", marker + axis) for axis in ("._x", "._y")]
                other = "" if game else "client"
                ctx.hold_key(other, "d", 0.6)
                ctx.wait_for("the party pin follows its moving player while the map stays open",
                             lambda: any(abs((value(ctx, game, "MapMenu", marker + axis) or 0) - at) > 0.01
                                         for axis, at in zip(("._x", "._y"), before)), 5)
            ctx.tap_key(game, 0x26)
            ctx.wait_for("the local map finishes fading in",
                         lambda: ctx.player_command(game, {"cmd": "menu_flags", "name": "MapMenu"})["local_map_ready"] and
                         all(value(ctx, game, "MapMenu", "_root.WorldMap.LocalMapMenu" + path) == 100
                             for path in ("._alpha", "._parent._alpha")), 5)
            local = f"_root.WorldMap.LocalMapMenu.IconDisplay.MarkerContainer.SkyrimCoopPartyMarkers.Player{player['id']}"
            try:
                ctx.wait_for(f"[{game or 'host'}] local map shows {player['name']}",
                             lambda: value(ctx, game, "MapMenu", local + ".textField.text") == player["name"] and
                             value(ctx, game, "MapMenu", local + "._visible") == 1 and
                             value(ctx, game, "MapMenu", local + ".TextClip._visible") == 1 and
                             value(ctx, game, "MapMenu", local + ".TextClip._alpha") == 100, 5)
            finally:
                evidence(ctx, game, "local-map" + suffix, "MapMenu", [local + path for path in
                         (".textField.text", "._visible", "._x", "._y", ".TextClip._visible", ".TextClip._alpha", ".textField._alpha")] +
                         ["_root.WorldMap.LocalMapMenu.IconDisplay.MapWidth", "_root.WorldMap.LocalMapMenu.IconDisplay.MapHeight",
                          "_root.WorldMap.LocalMapMenu._alpha", "_root.WorldMap.LocalMapMenu._parent._alpha"])
            ctx.tap_key(game, 0x26)
            ctx.tap_key(game, TAB_KEY)
            ctx.wait_for("the map closes", lambda: "MapMenu" not in ctx.menus(game), 5)
    finally:
        for game in ("", "client"):
            if "MapMenu" in ctx.menus(game):
                ctx.tap_key(game, TAB_KEY)
                ctx.wait_for("the map closes", lambda: "MapMenu" not in ctx.menus(game), 5)
        ctx.both_go(HOME_CELL)


def rejoin(ctx):
    try:
        compass(ctx)
        ctx.disconnect_client()
        ctx.wait_for("the host's compass removes the departed player", lambda: value(ctx, "", "HUD Menu",
                     "_root.HUDMovieBaseInstance.SkyrimCoopPartyCount") == 0, 5)
    finally:
        ctx.rejoin_client()
    compass(ctx)
