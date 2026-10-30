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
    ACTIVATE_KEY,
    HOME_CELL,
    HORSE,
    OUTDOOR_CELL,
    RIVER_CELL,
)
from ..errors import TestFailure
from ..comparisons import distance
from ..utilities import log


class MountsActions:
    def player_copy(ctx, instance):
        """That game's copy of the other player (its form id), or None."""
        _game = ctx.game(instance)
        world = (_game.read_status() or {}).get("world") or {}
        return next((int(k) for k, v in world.items() if v.get("player") and k != "20"), None)


    def mount_roundtrip(ctx, riding_game="client", cross_border=False, leave=False, follow=False):
        """A player rides the host's horse while the other game shows its rider and horse copies.
        Dismounting or disconnecting returns the horse to the host; travel carries it with the party."""
        _game = ctx.game()
        ctx.both_go("Whiterun plains", "cow Tamriel 0 -4") if cross_border else ctx.both_go(OUTDOOR_CELL)
        observer = "" if riding_game == "client" else "client"
        horse = None
        try:
            rider = ctx.player_copy(observer)
            before = set(ctx.local_player("").get("local_actors", {}))
            ctx.console_on("", f"player.placeatme {HORSE} 1")
            horse = int(next(iter(ctx.wait_for("[host] the horse loaded",
                                           lambda: set(ctx.local_player("").get("local_actors", {})) - before, 15))))
            sid = ctx.wait_for("[host] the horse has a session id",
                           lambda: (s := ctx.host_npc_state(horse).get("session_id")) is not None and s >= 0 and (s,), 10)[0]
            copy = ctx.copy_of("client", sid)
            ridden_horse = copy if riding_game == "client" else horse
            observed_horse = horse if observer == "" else copy
            # A placed actor fades in, and nothing activates it until it has (TESNPC::Activate).
            ctx.wait_for("[client] the horse has faded in", lambda: ctx.actor_state("client", copy).get("mod", {}).get("fade_state") == 0, 10)
            ctx.activate_on(riding_game, ridden_horse)
            ctx.wait_for("[client] its player rides the horse, now its game's to move",
                     lambda: ctx.actor_state(riding_game, 0x14).get("placement", {}).get("riding")
                     and ctx.actor_state(riding_game, ridden_horse).get("role") == "own", 15)
            # A player gets off once seated: until the mount animation ends the engine refuses (Actor::Dismount).
            ctx.wait_for("[client] its player is seated", lambda: not ctx.actor_state(riding_game, 0x14).get("placement", {}).get("getting_on_off"), 10)
            ctx.wait_for("[host] the client's player sits on the horse, a copy here",
                     lambda: ctx.actor_state(observer, rider).get("placement", {}).get("riding")
                     and ctx.actor_state(observer, observed_horse).get("role") == "copy"
                     and ctx.actor_state(observer, observed_horse).get("placement", {}).get("ridden"), 10)
            # While riding, the horse and the rider agree with their copies (the rest of Riverwood is not this scenario's).
            riding = [p for p in ctx.check_desync() if f", sid {sid}]" in p or "[client 00000014," in p]
            if riding:
                raise ctx.failure("while riding: " + " | ".join(riding))
            if follow:
                ctx.both_go("river", RIVER_CELL)
                for name, form in ((riding_game, 0x14), (riding_game, ridden_horse), (observer, observed_horse)):
                    state = ctx.actor_state(name, form)
                    log(f"mounted arrival [{name or 'host'}] {form:X}: role={state.get('role')}, placement={state.get('placement')}")
                if ctx.actor_state(riding_game, 0x14).get("placement", {}).get("riding"):
                    ctx.wait_for("the ridden horse arrives with its following player",
                             lambda: ctx.actor_state(riding_game, ridden_horse).get("placement", {}).get("cell") == ctx.player_cell(riding_game), 10)
            if cross_border:
                start = ctx.my_position(riding_game)
                # Select a straight path to a grid boundary using the game's collision rays.
                candidates = []
                for bearing in range(0, 360, 45):
                    dx, dy = math.sin(math.radians(bearing)), math.cos(math.radians(bearing))
                    limits = []
                    for coordinate, step in zip(start[:2], (dx, dy)):
                        if abs(step) > 0.001:
                            edge = (math.floor(coordinate / 4096) + (1 if step > 0 else 0)) * 4096
                            limits.append((edge - coordinate) / step)
                    candidates.append((min(limits) + 200, bearing))
                bearing = None
                for reach, direction in sorted(candidates):
                    try:
                        bearing = ctx.clear_way(riding_game, [direction], reach)
                        break
                    except TestFailure:
                        continue
                if bearing is None:
                    raise ctx.failure("no clear mounted route crosses this cell's border")
                # Stage a straight ride along the measured clear path; the border crossing below uses held input.
                # Strafe controls are camera-relative on horseback and do not reliably turn to an absolute heading.
                ctx.console_on(riding_game, f"{ridden_horse:08X}.setangle z {bearing}")
                ctx.console_on(riding_game, f"player.setangle z {bearing}")
                before_cell = tuple(math.floor(v / 4096) for v in ctx.my_position(riding_game)[:2])
                before_position = ctx.my_position(riding_game)
                ctx.press(riding_game, "forward")
                try:
                    ctx.wait_for("the mounted player rides across an exterior cell border",
                             lambda: tuple(math.floor(v / 4096) for v in ctx.my_position(riding_game)[:2]) != before_cell, 30)
                finally:
                    ctx.release(riding_game, "forward")
                ctx.wait_for("the observer's horse follows the ride across the border",
                         lambda: distance(ctx.actor_state(observer, observed_horse).get("placement", {}).get("position", [])[:2],
                                          ctx.my_position(riding_game)[:2]) < 200, 10)
                if distance(before_position[:2], ctx.my_position(riding_game)[:2]) < 100:
                    raise ctx.failure("the ride crossed no ground")
            if leave:
                ctx.disconnect_client()
                ctx.wait_for("the disconnected rider's horse returns to the host",
                         lambda: ctx.actor_state("", horse).get("role") == "own", 10)
                return
            if ctx.actor_state(riding_game, 0x14).get("placement", {}).get("riding"):
                ctx.tap_key(riding_game, ACTIVATE_KEY)
            ctx.wait_for("[client] its player got off, the horse a copy again",
                     lambda: not ctx.actor_state(riding_game, 0x14).get("placement", {}).get("riding")
                     and ctx.actor_state("client", copy).get("role") == "copy", 15)
            ctx.wait_for("[host] the horse is its own again, the client's player off it",
                     lambda: ctx.actor_state("", horse).get("role") == "own"
                     and not ctx.actor_state(observer, rider).get("placement", {}).get("riding")
                     and not ctx.actor_state("", horse).get("placement", {}).get("ridden"), 10)
        finally:
            if ctx.actor_state(riding_game, 0x14).get("placement", {}).get("riding"):
                ctx.tap_key(riding_game, ACTIVATE_KEY)
                ctx.wait_for("[client] its player got off for the cleanup",
                         lambda: not ctx.actor_state(riding_game, 0x14).get("placement", {}).get("riding"), 15)
            if leave and not ctx.game_state("client").get("online"):
                ctx.rejoin_client()
            if horse is not None:
                ctx.console_on("", f"{horse:08X}.disable")
                ctx.console_on("", f"{horse:08X}.markfordelete")
            ctx.both_go(HOME_CELL)
            saved = ctx.save_game("", "PartyMountCleanup")
            ctx.wait_for("the horse cleanup can be saved", ctx.saves_written(saved), 15)


    def copy_drawn(ctx, expected):
        return lambda actors: ctx.other_player(actors) is not None and actors[str(ctx.other_player(actors))].get("weapon_drawn") == expected

