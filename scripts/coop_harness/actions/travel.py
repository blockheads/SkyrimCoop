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
from ..common import (
    DIVE_SECONDS,
    DO_NOTHING_PACKAGE,
    HOME_CELL,
    IRON_SWORD,
    RIVER_BANK,
    RIVER_CELL,
    RIVER_SPOT,
    SURFACE_PACKAGE,
)


class TravelActions:
    def player_cell(ctx, instance):
        """The cell the player is in, by that game's snapshot."""
        _game = ctx.game(instance)
        return ((((_game.read_status() or {}).get("world") or {}).get("20") or {}).get("placement") or {}).get("cell")


    def both_go(ctx, cell, command=None):
        """The party goes to `cell`: the host travels with coc (or `command`, e.g. "cow Tamriel 7 -8") and the
        client follows it (the party tether). Each game's arrival is waited for first: until then both still
        show the other player in the old cell, and that check alone passed at once."""
        before = {name: ctx.player_cell(name) for name in ("", "client")}
        ctx.console_on("", command or f"coc {cell}")
        for name in ("", "client"):
            ctx.wait_for(f"[{name or 'host'}] arrived in {cell}",
                     lambda name=name: ctx.player_cell(name) not in (None, before[name]), 60)
        ctx.wait_for(f"In {cell}, each game shows the other player", ctx.both_see_each_other, 60)
        # The arrival's fade: until it ends the game takes no input and resets the player's heading.
        for name in ("", "client"):
            ctx.wait_for(f"[{name or 'host'}] the arrival's fade is over", lambda name=name: ctx.takes_input(name), 10)


    def water(ctx, instance, form_id=None):
        """The actor's mod.water in that game (submerged, swimming, head_under, seconds_under); the player's
        own without `form_id`."""
        _game = ctx.game()
        if form_id is None:
            return (ctx.player_snapshot(instance).get("mod") or {}).get("water") or {}
        _game = ctx.game(instance)
        return ((((_game.read_status() or {}).get("world") or {}).get(str(form_id)) or {}).get("mod") or {}).get("water") or {}


    def dive_roundtrip(ctx, diver, observer, label):
        """The diver swims to the river bed, stays under, then surfaces: the owner gasps (Actor::Update
        queues a comment once a head was under for 4 s). The copy runs Actor::Update too; its own decisions
        there (gasp, Surface package, drowning damage) must not act. It plays the owner's line only, once,
        and keeps the do-nothing package and its owner's health."""
        _game = ctx.game()
        ctx.both_go("river", RIVER_CELL)
        # The river's wildlife (wolves within 3000 units) would join in: a fight, grunts on the voice line.
        # The host runs every NPC's AI: its combat AI off (tcai, a toggle) keeps them out until the end.
        ctx.console_on("", "tcai")
        try:
            for name, spot in ((diver, RIVER_SPOT), (observer, RIVER_BANK)):
                for axis, value in zip("xyz", spot):
                    ctx.console_on(name, f"player.setpos {axis} {value}")
            ctx.wait_for(f"[{diver or 'host'}] swims", lambda: ctx.water(diver).get("swimming"), 10, _game=_game)
            ctx.show(observer, ctx.other_player)
            _game = ctx.game(observer)
            copy = ctx.other_player((_game.read_status() or {}).get("actors", {}))
            lines = {"owner": [], "copy": []}
            packages, healths = set(), set()
            def watch():
                owner_line = (ctx.player_snapshot(diver).get("mod") or {}).get("voice") or {}
                _game = ctx.game(observer)
                snap = ((_game.read_status() or {}).get("world") or {}).get(str(copy)) or {}
                copy_line = (snap.get("mod") or {}).get("voice") or {}
                # A line counts once it plays (time left, UpdateSoundCallback started it): the voice slot can
                # still hold an old sound that never played.
                for who, line in (("owner", owner_line), ("copy", copy_line)):
                    if (line.get("seconds") or -1) > 0 and line.get("sound", -1) != -1:
                        if line["sound"] not in [l["sound"] for l in lines[who]]:
                            lines[who].append({"sound": line["sound"], "duration_ms": line.get("duration_ms", 0)})
                        else:
                            lines[who][-1]["duration_ms"] = max(lines[who][-1]["duration_ms"], line.get("duration_ms", 0))
                packages.add((snap.get("mod") or {}).get("package_type"))
                healths.add(round((snap.get("values") or {}).get("health", 0)))
            # Dive: look down and swim forward, as a player does; south, where the bed is deepest (the bank
            # where the observer stands is north).
            ctx.console_on(diver, "player.setangle z 180")
            ctx.console_on(diver, "player.setangle x 80")
            ctx.press(diver, "forward")
            try:
                ctx.wait_for(f"[{diver or 'host'}] head under water", lambda: ctx.water(diver).get("head_under"), 5, _game=_game)
                ctx.wait_for(f"[{observer or 'host'}] {label}'s head under water", lambda: ctx.water(observer, copy).get("head_under"), 5, _game=_game)
                ctx.wait_for(f"[{diver or 'host'}] under for {DIVE_SECONDS}s",
                         lambda: (watch(), ctx.water(diver).get("seconds_under", 0) >= DIVE_SECONDS)[1], DIVE_SECONDS + 5, _game=_game)
            finally:
                ctx.release(diver, "forward")
            ctx.console_on(diver, "player.setangle x -80")
            ctx.press(diver, "forward")
            try:
                ctx.wait_for(f"[{diver or 'host'}] surfaces and gasps",
                         lambda: (watch(), not ctx.water(diver).get("head_under") and lines["owner"])[1], 10, _game=_game)
            finally:
                ctx.release(diver, "forward")
            ctx.console_on(diver, "player.setangle x 0")
            # Until both lines are over: a gasp of the copy's own would start after it surfaced.
            ctx.wait_for(f"[{observer or 'host'}] {label} surfaced and its line is over",
                     lambda: (watch(), not ctx.water(observer, copy).get("head_under")
                              and ctx.line_over(observer, copy) and ctx.line_over(diver, 0x14))[1], 10, _game=_game)
            if len(lines["owner"]) != 1:
                raise ctx.failure(f"owner's lines {lines['owner']}: expected its one gasp")
            if len(lines["copy"]) != 1 or lines["copy"][0]["duration_ms"] != lines["owner"][0]["duration_ms"]:
                raise ctx.failure(f"copy's lines {lines['copy']}, owner's {lines['owner']}: the copy should play the owner's gasp only")
            if packages != {DO_NOTHING_PACKAGE}:
                raise ctx.failure(f"copy's packages while under water: {sorted(packages, key=str)} (a Surface package is {SURFACE_PACKAGE})")
            owner_health = round((ctx.player_snapshot(diver).get("values") or {}).get("health", 0))
            if healths != {owner_health}:
                raise ctx.failure(f"copy's health while under water {sorted(healths)}, owner's {owner_health}")
            ctx.hold()
        finally:
            ctx.console_on("", "tcai")
            ctx.both_go(HOME_CELL)


    def travel(ctx, instance, how, cell_before):
        """One player travels (`how` runs it) and arrives: its game is in another cell and takes input again."""
        how()
        ctx.wait_for(f"[{instance or 'host'}] arrived somewhere new",
                 lambda: ctx.player_cell(instance) not in (None, cell_before), 60)
        ctx.wait_for(f"[{instance or 'host'}] the arrival's fade is over", lambda: ctx.takes_input(instance), 15)


    def players_together(ctx, where):
        """Both games show the other player, with 3D, in the same cell (a copy left behind would not count)."""
        def check():
            cells = {ctx.player_cell(n) for n in ("", "client")}
            return len(cells) == 1 and all(ctx.other_player(ctx.actors_on(n)) for n in ("", "client"))
        ctx.wait_for(f"{where}: both in one cell, each game shows the other player", check, 30)


    def follows(ctx, instance, cell_before):
        """The party tether takes this player along: its game arrives somewhere new without its own travel."""
        ctx.wait_for(f"[{instance or 'host'}] follows the party", lambda: ctx.player_cell(instance) not in (None, cell_before), 60)
        ctx.wait_for(f"[{instance or 'host'}] the arrival's fade is over", lambda: ctx.takes_input(instance), 15)


    def open_menus(ctx, instance):
        _game = ctx.game(instance)
        return (_game.read_status() or {}).get("menus") or []


    def equip_roundtrip(ctx, actor_instance, ref, observer, observed, label, ready_weapon=False):
        """Equips then unequips an Iron Sword on `ref` in one game; the other game's copy must follow
        both ways."""
        def wears(expected):
            def check(actors):
                target = observed(actors)
                return target is not None and (IRON_SWORD in actors[str(target)]["equipped"]) == expected
            return check
        ctx.show(observer, observed)
        if ready_weapon:
            ctx.give_sword(actor_instance)
        else:
            ctx.console_on(actor_instance, f"{ref}.additem {IRON_SWORD:08X} 1")
            ctx.console_on(actor_instance, f"{ref}.equipitem {IRON_SWORD:08X}")
        owner = 0x14 if ref == "player" else int(ref, 16)
        ctx.wait_for(f"[{actor_instance or 'host'}] the owner wears the Iron Sword",
                     lambda: IRON_SWORD in ctx.actor_state(actor_instance, owner).get("equipment", []), 5)
        ctx.wait_on(observer, f"{label} wears the Iron Sword", wears(True))
        ctx.hold()
        if ready_weapon:
            # Same Ready Weapon input and own-state waits as the previous draw flow.
            ctx.draw(actor_instance)
            ctx.wait_on(observer, f"{label} draws the sword", ctx.copy_drawn(True), 5)
            ctx.hold()
            ctx.sheathe(actor_instance)
            ctx.wait_on(observer, f"{label} sheathes the sword", ctx.copy_drawn(False), 5)
        ctx.console_on(actor_instance, f"{ref}.unequipitem {IRON_SWORD:08X}")
        ctx.wait_for(f"[{actor_instance or 'host'}] the owner removes the Iron Sword",
                     lambda: IRON_SWORD not in ctx.actor_state(actor_instance, owner).get("equipment", []), 5)
        ctx.wait_on(observer, f"{label} no longer wears it", wears(False))
        ctx.hold()

