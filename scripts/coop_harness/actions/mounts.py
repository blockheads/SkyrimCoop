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
            ctx.use(riding_game, ridden_horse)  # a player's Activate: the mount clip plays out
            ctx.wait_for("[client] its player rides the horse, now its game's to move",
                     lambda: ctx.actor_state(riding_game, 0x14).get("placement", {}).get("riding")
                     and ctx.actor_state(riding_game, ridden_horse).get("role") == "own", 15)
            # A player gets off once seated: until the mount animation ends the engine refuses (Actor::Dismount).
            ctx.wait_for("[client] its player is seated", lambda: ctx.seated_on_mount(riding_game), 10)
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
                     and ctx.actor_state("client", copy).get("role") == "copy", 15,
                     explain=lambda: ctx.mount_readout([(riding_game, 0x14), (riding_game, ridden_horse),
                                                        (observer, rider), (observer, observed_horse)]))
            ctx.wait_for("[host] the horse is its own again, the client's player off it",
                     lambda: ctx.actor_state("", horse).get("role") == "own"
                     and not ctx.actor_state(observer, rider).get("placement", {}).get("riding")
                     and not ctx.actor_state("", horse).get("placement", {}).get("ridden"), 10,
                     explain=lambda: ctx.mount_readout([(riding_game, 0x14), (riding_game, ridden_horse),
                                                        (observer, rider), (observer, observed_horse)]))
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


    def seated_on_mount(ctx, game):
        """Its player is up on its mount: riding, sit state 3, not getting on, and the horse's half of the pair over
        (bIsSynced off): sooner, a dismount drops it crouched (docs/re/systems/mounts.md)."""
        state = ctx.actor_state(game, 0x14)
        placement = state.get("placement", {})
        if not (placement.get("riding") and not placement.get("getting_on_off") and state.get("state", {}).get("sit_sleep") == 3):
            return False
        world = (ctx.game(game).read_status() or {}).get("world") or {}
        ridden = [int(form) for form, row in world.items() if (row.get("placement") or {}).get("ridden")]
        return bool(ridden) and not any(ctx.player_command(game, {"cmd": "graph_bool", "actor": form, "names": ["bIsSynced"]})
                                        .get("values", {}).get("bIsSynced") for form in ridden)

    def wait_seated(ctx, instance):
        """That game's player, having activated a horse, is up on it (seated_on_mount), else fails with the readout."""
        ctx.wait_for(f"[{instance or 'host'}] its player is up on its horse", lambda: ctx.seated_on_mount(instance), 10,
                     explain=lambda: ctx.mount_readout([(instance, 0x14)]))

    def get_off(ctx, instance):
        """That game's player looks clear of anything to activate and presses Activate: off its horse (wait_off). A
        rider's Activate takes the crosshair's ref first, its own mount only with none (ActivatePickRef 40548)."""
        world = (ctx.game(instance).read_status() or {}).get("world") or {}
        ridden = {int(form) for form, row in world.items() if (row.get("placement") or {}).get("ridden")}
        on = lambda: (crosshair := ctx.player_command(instance, {"cmd": "camera"}).get("crosshair") or 0) not in ridden and crosshair
        if on():
            # Up 20 degrees, its heading kept: a rider's turn is its horse's, its view's pitch held near 25 (negative up).
            ctx.turn_to(instance, pitch=-20, rate=180, seconds=3, name="looks up, clear of the crosshair's ref")
            ctx.wait_for(f"[{instance or 'host'}] the crosshair on nothing", lambda: not on(), 2,
                         explain=lambda: f"crosshair on {on():X}")
        ctx.tap_key(instance, ACTIVATE_KEY)
        ctx.wait_off(instance)

    def wait_off(ctx, instance):
        """That game's player is off any horse: not riding, not getting on or off, standing (sit state 0)."""
        def off():
            state = ctx.actor_state(instance, 0x14)
            placement = state.get("placement", {})
            return (not placement.get("riding") and not placement.get("getting_on_off")
                    and not state.get("state", {}).get("sit_sleep"))
        ctx.wait_for(f"[{instance or 'host'}] its player is off its horse", off, 15,
                     explain=lambda: ctx.mount_readout([(instance, 0x14)]))

    def wait_horse_free(ctx, instance, horse):
        """On that game nobody rides the horse: a copy of the last rider gets off by its owner's replayed exit clip,
        a moment after its owner, and until then that player's Activate on it does nothing (ActivateMount 37864)."""
        form = lambda: ctx.resolve(horse, instance)
        ctx.wait_for(f"[{instance or 'host'}] nobody rides the horse", lambda: not (ctx.actor_state(instance, form()).get("placement") or {}).get("ridden"),
                     10, explain=lambda: ctx.mount_readout([(instance, horse)]))

    def horse_owned_by(ctx, horse, owner):
        """The horse (a handle) is `owner`'s game's (host or guest) and a copy on the other, its rider off or on as the
        ride says; fails naming each game's role."""
        def roles():
            return {game: ctx.actor_state(game, ctx.resolve(horse, game)).get("role") for game in ("", "client")}
        want = {"": "own" if owner in ("", "host") else "copy", "client": "copy" if owner in ("", "host") else "own"}
        ctx.wait_for(f"the horse is the {owner or 'host'}'s", lambda: roles() == want, 10,
                     explain=lambda: f"roles {roles()}")

    def mount_readout(ctx, actors):
        """Each (game, form)'s role, riding/ridden/getting on or off, sit state and process level (a low-process mount
        gets a higher-process rider off by itself, UpdateLowMountRider 39174)."""
        parts = []
        for game, form in actors:
            form = ctx.resolve(form, game)
            state = ctx.actor_state(game, form)
            placement = state.get("placement", {})
            place = ctx.player_command(game, {"cmd": "actor_place", "actor": form})
            parts.append(f"[{game or 'host'} {form:X}] {state.get('role')} riding={placement.get('riding')} "
                         f"ridden={placement.get('ridden')} getting_on_off={placement.get('getting_on_off')} "
                         f"sit={state.get('state', {}).get('sit_sleep')} process={place.get('process_level')}")
        return "; ".join(parts)

    def ride_once(ctx, instance, horse, gap=130, settle=0.0):
        """That game's player walks up to the horse, gets on with Activate, and off again `settle` seconds after it is
        seated; returns its body (pose) on its own game and its copy's (with a session), read after getting off."""
        ctx.place_before(instance, horse, gap)
        ctx.use(instance, horse)
        ctx.wait_seated(instance)
        time.sleep(settle)  # a measurement: how soon a dismount plays its exit clip
        ctx.get_off(instance)
        other = "" if instance else "client"
        copy = ctx.player_copy(other) if ctx.game(other) else None
        bodies = {}
        for game, form in ((instance, 0x14), (other, copy)) if copy else ((instance, 0x14),):
            state = ctx.actor_state(game, form)
            pose = state.get("pose") or {}
            bodies[f"{game or 'host'} {form:X}"] = {"pelvis": round(pose.get("pelvis", 0) - pose.get("floor_gap", 0)),
                                                    "event": (state.get("animation") or {}).get("event"),
                                                    "sit": (state.get("state") or {}).get("sit_sleep")}
        return bodies

    def mount_timeline(ctx, instance, seconds=4.0, horse=None, press=False):
        """Samples that game's player every 0.1 s for `seconds`: riding, getting on/off, sit state and the graph's
        bIsSynced / bIsRiding (and the `horse` handle's role, last event and graph), one line per change."""
        lines, last, start = [], None, time.monotonic()
        if press:
            ctx.tap_key(instance, ACTIVATE_KEY)
        while time.monotonic() - start < seconds:
            state = ctx.actor_state(instance, 0x14)
            placement = state.get("placement", {})
            bools = ctx.graph_bools(instance, "bIsSynced", "bIsRiding")
            mount = ctx.actor_state(instance, ctx.resolve(horse, instance)) if horse else {}
            horse_bools = ctx.player_command(instance, {"cmd": "graph_bool", "actor": ctx.resolve(horse, instance),
                                                        "names": ["bIsSynced"]}).get("values", {}) if horse else {}
            now = (placement.get("riding"), placement.get("getting_on_off"), state.get("state", {}).get("sit_sleep"),
                   bools.get("bIsSynced"), bools.get("bIsRiding"), mount.get("role"),
                   (mount.get("animation") or {}).get("event"), f"synced={horse_bools.get('bIsSynced')}")
            if now != last:
                lines.append(f"{time.monotonic() - start:4.1f}s riding={now[0]} getting_on_off={now[1]} sit={now[2]} "
                             f"synced={now[3]} graph_riding={now[4]}" + (f" horse {now[5]} {now[6]} {now[7]}" if horse else ""))
                last = now
            time.sleep(0.1)
        return lines

    def copy_drawn(ctx, expected):
        return lambda actors: ctx.other_player(actors) is not None and actors[str(ctx.other_player(actors))].get("weapon_drawn") == expected

