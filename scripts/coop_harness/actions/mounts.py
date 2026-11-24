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
    HORSE_RACE,
    OUTDOOR_CELL,
    PLAYER_FACTION,
    PLAYER_HORSE_FACTION,
    RIVER_CELL,
)
from ..errors import TestFailure
from ..comparisons import distance
from ..utilities import log


def bad_clip_spans(name, clips, longest=0.1):
    """Each actor's span of samples with no clip playing or clips not loaded (a T-pose) lasting longest seconds or
    more, or still bad at the trace's end: one problem each."""
    problems, bad = [], {}  # actor -> (since, what), kept from its first bad sample until a good one
    for entry in clips:
        if entry.get("kind") != "clips":
            continue
        actor = entry["actor"]
        unloaded = [clip for clip, detail in (entry.get("detail") or {}).items() if not detail.get("loaded")]
        if unloaded or not entry.get("clips"):
            bad.setdefault(actor, (entry["t"], f"clips not loaded {unloaded}" if unloaded else "no clip playing"))
        elif (since := bad.pop(actor, None)) and (entry["t"] - since[0]) / 1e6 >= longest:
            problems.append(f"[{name} {actor:X}] {since[1]} for {(entry['t'] - since[0]) / 1e6:.2f} s")
    return problems + [f"[{name} {actor:X}] {what} to the end" for actor, (_, what) in bad.items()]


class MountsActions:
    def player_copy(ctx, instance):
        """That game's copy of the other player (its form id), or None."""
        _game = ctx.game(instance)
        world = (_game.read_status() or {}).get("world") or {}
        return next((int(k) for k, v in world.items() if v.get("player") and k != "20"), None)

    def spawn_horse(ctx, instance="", handle="horse"):
        """That game's player types /spawn horse: the host places a saddled horse beside it, the party's as a stable
        sells one (PlayerHorseFaction rank 1, owned by PlayerFaction). Names it `handle` in both games once it is
        ready to mount; returns the handle (dev-commands.md)."""
        before = set(ctx.local_player("").get("local_actors", {}))
        ctx.say(instance, "/spawn horse")

        def placed_horses():  # new placed references (FF...) of HorseRace: another NPC loading meanwhile is not it
            actors = ctx.local_player("").get("local_actors", {})
            return [int(k) for k, v in actors.items() if k not in before and int(k) >= 0xFF000000
                    and (v.get("identity") or {}).get("race") == HORSE_RACE]
        found = ctx.wait_for("[host] the horse spawns", placed_horses, 15)
        if len(found) != 1:
            raise ctx.failure(f"[host] {len(found)} new horses appeared, not one: {[f'{f:X}' for f in found]}")
        horse = found[0]
        ctx.staged().append(("npc", horse))  # unstage deletes it, whatever fails below
        ctx.wait_for(f"[host] the horse {horse:X} is the party's",
                     lambda: ctx.player_command("", {"cmd": "form", "form": horse}).get("owner") == PLAYER_FACTION
                     and ctx.faction_rank("", horse, PLAYER_HORSE_FACTION) >= 1, 10)
        sid = ctx.wait_for("[host] the horse has a session id",
                           lambda: (s := ctx.host_npc_state(horse).get("session_id")) is not None and s >= 0 and (s,), 10)[0]
        copy = ctx.copy_of("client", sid)
        # Inferred, not read: Activate on an actor still fading in did nothing (mount_roundtrip waits too).
        for game, form in (("", horse), ("client", copy)):
            ctx.wait_for(f"[{game or 'host'}] the horse has faded in",
                         lambda game=game, form=form: ctx.actor_state(game, form).get("mod", {}).get("fade_state") == 0, 10)
        ctx.name_handle(handle, {"host": horse, "guest": copy})
        return handle


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
        # Its horse standing still: a dismount asked for as it slows from a gallop was ignored (the duo, 2026-11-17,
        # once in several runs; Actor::Dismount 37906 refuses in midair, the rest not read).
        def still():
            place = lambda: ctx.my_position(instance)
            before = place()
            time.sleep(0.3)  # a measurement: its speed
            return distance(place(), before) < 2
        ctx.wait_for(f"[{instance or 'host'}] its horse stands still", still, 10)
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

    def passenger_seated(ctx, instance, horse):
        """That game's player rides along on the horse and is seated on both games (its copy too, seated at once:
        docs/re/systems/player-mount.md), else fails with the readout."""
        other = "" if instance else "client"
        def seated():
            copy = ctx.player_copy(other)
            states = [ctx.actor_state(instance, 0x14), ctx.actor_state(other, copy) if copy else {}]
            return all((s.get("placement") or {}).get("riding") and (s.get("state") or {}).get("sit_sleep") == 3 for s in states)
        ctx.wait_for(f"[{instance or 'host'}] its player rides along, seated on both games", seated, 10,
                     explain=lambda: ctx.mount_readout([(instance, 0x14), (other, ctx.player_copy(other) or 0), (instance, horse), (other, horse)]))

    def prompt_text(ctx, instance, target):
        """That game's crosshair prompt for `target` (TESNPC::GetActivateText 24716, through our hooks); fails when
        `target` is no actor with an NPC base there (a stale handle reads no prompt too)."""
        reply = ctx.player_command(instance, {"cmd": "activate_text", "ref": ctx.resolve(target, instance)}) or {}
        if not reply.get("ok"):
            raise ctx.failure(f"[{instance or 'host'}] no prompt to read for {target}: {reply}")
        return reply.get("text") or ""

    def prompt_offers(ctx, instance, target, word="Ride"):
        """That game's crosshair prompt for `target` (TESNPC::GetActivateText 24716) holds `word`: what the player
        reads before pressing E, else fails with the prompt it shows."""
        text = lambda: ctx.prompt_text(instance, target)
        ctx.wait_for(f"[{instance or 'host'}] the prompt for {target} offers {word}", lambda: word in text(), 5,
                     explain=lambda: f"prompt {text()!r}")

    def name_data(ctx, target):
        """Each game's display name for `target` and its ExtraTextDisplayData (cached text, message, quest, instance:
        names.md), the readout behind a crosshair that takes nothing."""
        return {game or "host": ctx.player_command(game, {"cmd": "name_data", "ref": ctx.resolve(target, game)})
                for game in ("", "client")}

    def expect_prompt_lacks(ctx, instance, target, word="Talk"):
        """That game's crosshair prompt for `target` (24716, through our hooks) does not hold `word`, read now: what
        the player would not see over it. Fails with the prompt it shows."""
        shown = ctx.prompt_text(instance, target)
        if word in shown:
            raise ctx.failure(f"[{instance or 'host'}] the prompt for {target} offers {word}: {shown!r}")
        return {"prompt": shown}

    def drives(ctx, instance, horse, seconds=2.5, least=300):
        """That game's player holds forward: the horse goes at least `least` units on both games and every rider
        stays on it (a horse nobody steers stays put)."""
        def place(game):
            return (ctx.actor_state(game, ctx.resolve(horse, game)).get("placement") or {}).get("position") or [0, 0, 0]
        before = {game: place(game) for game in ("", "client")}
        ctx.hold_key(instance, "w", seconds)
        def moved():
            return all(distance(place(game), before[game]) >= least for game in ("", "client"))
        ctx.wait_for(f"[{instance or 'host'}] drives the horse {least}+ units on both games", moved, 5,
                     explain=lambda: f"moved {[round(distance(place(g), before[g])) for g in ('', 'client')]}")

    def riders_with_horse(ctx, horse, within=80.0):
        """On both games every player's copy riding the horse has its reference at the horse's, not where it got on:
        its seat's REFRSyncController (20318) carries it (refused, it stayed behind, and its cell could unload under
        it). Fails naming each copy and how far off."""
        def apart():
            far = []
            for game in ("", "client"):
                copy = ctx.player_copy(game)
                state = ctx.actor_state(game, copy) if copy else {}
                if not (state.get("placement") or {}).get("riding"):
                    continue
                at = state["placement"].get("position")
                mount = (ctx.actor_state(game, ctx.resolve(horse, game)).get("placement") or {}).get("position")
                if at and mount and distance(at[:2], mount[:2]) > within:
                    far.append(f"[{game or 'host'} {copy:X}] {distance(at[:2], mount[:2]):.0f} units from its horse")
            return far
        ctx.wait_for("each riding copy's reference is at its horse", lambda: not apart(), 5, explain=lambda: "; ".join(apart()))

    def watch_riders(ctx):
        """Starts tracing, on both games, each game's player and its copy of the other: their bodies every frame
        (body_trace) and their graphs' clips (anim_trace), for riders_smooth to read."""
        # Each copy where its owner stands first: a staging move still arriving reads as a jump.
        def caught_up():
            for game, other in (("", "client"), ("client", "")):
                copy = ctx.player_copy(game)
                placement = (ctx.actor_state(game, copy).get("placement") or {}) if copy else {}
                if placement.get("riding"):
                    continue  # its seat carries it: its reference stays where it got on (player-mount.md)
                at = placement.get("position")
                if not at or distance(at, ctx.my_position(other)) > 20:
                    return False
            return True
        ctx.wait_for("each player's copy stands where its owner does", caught_up, 10)
        for game in ("", "client"):
            refs = [0x14] + ([copy] if (copy := ctx.player_copy(game)) else [])
            ctx.player_command(game, {"cmd": "body_trace", "refs": refs})
            ctx.player_command(game, {"cmd": "anim_trace", "actors": refs})

    def riders_smooth(ctx, step, most=40.0, snap=0.0, forbid=(), pop=0.0):
        """Since watch_riders, on both games: no player's pelvis, own or copy, moved more than `most` units in one frame
        (a warp; more on a slow frame, at up to 1,500 units a second), but once per body up to `snap` (a get-on moves
        its rider to its mark in one frame, vanilla: 76-83 units measured); no graph went 0.1 s with a clip not
        loaded or none playing (the skeleton's rest pose: a T-pose; one frame of it is a clip's file arriving); and no
        graph took an event in `forbid` (a passenger's get-on clip). A move within `pop` seconds after a body's snap is
        part of it, once (a quick get-on: the reference to the saddle, its riding pose a frame later). Fails naming each, per
        game and actor."""
        problems = []
        for game in ("", "client"):
            name = game or "host"
            bodies = ctx.player_command(game, {"cmd": "body_trace", "stop": True}).get("entries") or []
            clips = ctx.player_command(game, {"cmd": "anim_trace", "stop": True}).get("entries") or []
            last, snapped = {}, {}
            began = bodies[0]["t"] if bodies else 0
            for entry in bodies:
                # Its pelvis bone, what a person sees: the engine snaps a rider's root to the saddle in one frame of a
                # normal get-on, its clip hiding that.
                at = entry.get("pelvis") or entry.get("position_3d")
                if not at:
                    continue
                if (before := last.get(entry["id"])) is not None:
                    seconds = max((entry["t"] - before[0]) / 1e6, 0.0)
                    moved = distance(at, before[1])
                    if moved > max(most, 1500.0 * seconds):
                        first, count = snapped.get(entry["id"], (entry["t"], 0))
                        if moved <= snap and count < (2 if pop else 1) and entry["t"] - first <= pop * 1e6:
                            snapped[entry["id"]] = (first, count + 1)
                            log(f"  [{name} {entry['id']:X}] its one allowed snap: {moved:.0f} units at {(entry['t'] - began) / 1e6:.2f} s")
                        else:
                            problems.append(f"[{name} {entry['id']:X}] jumped {moved:.0f} units in {seconds * 1000:.0f} ms "
                                            f"at {(entry['t'] - began) / 1e6:.2f} s")
                last[entry["id"]] = (entry["t"], at)
            problems += bad_clip_spans(name, clips)
            problems += [f"[{name} {entry['actor']:X}] played {entry['event']}" for entry in clips
                         if entry.get("kind") in ("graph", "input") and entry.get("taken", True) and entry.get("event") in forbid]
        if problems:
            raise TestFailure(f"{step}: " + "; ".join(sorted(set(problems))[:12]))

    def mount_readout(ctx, actors):
        """Each (game, form)'s role, riding/ridden/getting on or off, sit state, process level (a low-process mount
        gets a higher-process rider off by itself, UpdateLowMountRider 39174), running package and, for a mount,
        kMountPointClear (unset: Activate gives it a ClearMountPosition package instead of a dismount, 37864)."""
        parts = []
        for game, form in actors:
            form = ctx.resolve(form, game)
            state = ctx.actor_state(game, form)
            placement = state.get("placement", {})
            place = ctx.player_command(game, {"cmd": "actor_place", "actor": form})
            parts.append(f"[{game or 'host'} {form:X}] {state.get('role')} riding={placement.get('riding')} "
                         f"ridden={placement.get('ridden')} getting_on_off={placement.get('getting_on_off')} "
                         f"sit={state.get('state', {}).get('sit_sleep')} process={place.get('process_level')} "
                         f"package={place.get('package', 0):X} point_clear={place.get('mount_point_clear')}")
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

    def bought_horse_ride(ctx, instance, seller="skulvar_sable_hilt", horse="horse"):
        """The host buys the stable's horse from `seller` (the scene's handles), then that game's player gets on and
        off it (ride_once); returns ride_once's bodies, or fails naming the step, the horse's readout with it."""
        ctx.set_party_gold(count=ctx.party_gold() + 1000)
        ctx.talk("", seller, ["Can I buy a horse?", "Here's my offer"])
        ctx.leave_dialogue(instance="")
        try:
            return ctx.ride_once(instance, horse)
        except TestFailure as failure:
            raise ctx.failure(f"{failure}; {ctx.mount_readout([[instance, 0x14], [instance, horse]])}") from None

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

