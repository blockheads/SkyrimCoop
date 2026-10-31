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
from ..comparisons import is_close
from ..common import (
    FIREBOLT,
    HOME_CELL,
    INN,
    KILL_MOVE_RANDOM,
    LYDIA,
    MELEE_GAP,
    POSITION_TOLERANCE,
    SPELL_GAP,
    UNRELENTING_FORCE,
    UNRELENTING_FORCE_THREE,
    UNRELENTING_FORCE_WORDS,
    WOLF,
)
from ..errors import TestFailure
from ..comparisons import distance
from ..utilities import log
from ..objects import GodMode


class CombatActions:
    def actor_snapshot(ctx, instance, actor):
        _game = ctx.game(instance)
        return ((_game.read_status() or {}).get("world") or {}).get(str(actor), {})


    def death_state(ctx, instance):
        return ctx.game_state(instance).get("death", {})


    def is_dead(ctx, snapshot):
        return bool((snapshot.get("state") or {}).get("dead"))


    def effect_spells(ctx, instance):
        return sorted(e["spell"] for e in (ctx.player_snapshot(instance).get("effects") or []))


    def death_spectate_respawn(ctx, victim, travel_down=False):
        """An enemy kills the player; they spectate through combat (their game never reloads) and are revived beside the
        standing party with every ability back."""
        survivor = "client" if victim == "" else ""
        for instance in ("", "client"):
            ctx.console_on(instance, "player.restoreav health 10000")
        ctx.give_sword(survivor)
        ctx.give_sword(victim)
        ctx.sheathe(survivor)
        # The host's enemy picks its own target among those it detects, and the host's player stands beside the stage: it
        # stays unseen (Invisibility) from before the enemy comes until its turn, or the enemy never reaches the guest.
        hidden_host = survivor == ""
        if hidden_host:
            ctx.console_on("", "player.setav invisibility 1")
        npc, _, copy = ctx.spawn_host_npc(WOLF)
        target = 0x14 if victim == "" else ctx.client_on_host()
        observer_target = ctx.client_on_host() if survivor == "client" else 0x14
        old_magicka = (ctx.player_snapshot(victim).get("values") or {}).get("magicka")
        abilities = ctx.effect_spells(victim)
        hands = (ctx.player_snapshot(victim).get("state") or {}).get("hands")
        try:
            ctx.console_on("", "set KillMoveRandom to 0")
            ctx.place_before(victim, npc if victim == "" else copy, MELEE_GAP)
            if victim:
                ctx.wait_for("the host sees the victim beside the staged enemy", lambda:
                         distance((ctx.actor_snapshot("", target).get("placement") or {}).get("position"), ctx.my_position(victim)) < 4, 10)
            health = (ctx.player_snapshot(victim).get("values") or {}).get("health", 100)
            if health > 5:
                ctx.console_on(victim, f"player.damageav health {health - 5:.6f}")
            ctx.console_on("", f"{npc:X}.startcombat {target:X}")
            def fight():
                wolf = ctx.actor_snapshot("", npc)
                seen = ctx.actor_snapshot(victim, npc if victim == "" else copy)
                seen_at = (seen.get("placement") or {}).get("position")
                gap = distance(seen_at, ctx.my_position(victim)) if seen_at else -1
                return (f"the host's enemy targets {(wolf.get('ai') or {}).get('combat_target', 0):X}, last event "
                        f"{(wolf.get('animation') or {}).get('event')!r}; on the victim's game it stands {gap:.0f} units off, "
                        f"last event {(seen.get('animation') or {}).get('event')!r}; the victim's health "
                        f"{(ctx.player_snapshot(victim).get('values') or {}).get('health')}")
            try:
                ctx.wait_for("the enemy's hit kills the player", lambda: ctx.death_state(victim).get("down"), 15)
            except TestFailure as failure:
                raise ctx.failure(f"{failure} ({fight()})") from None
            # It turns on the standing player before it is frozen: a frozen actor has no AI to take a new target (it
            # kept or dropped one by timing, B5 in docs/v1-open-issues-2026-10-29.md).
            if hidden_host:
                ctx.console_on("", "player.setav invisibility 0")
            ctx.console_on("", f"{npc:X}.startcombat {observer_target:X}")
            ctx.wait_for("the enemy turns on the standing player",
                     lambda: ctx.host_world(npc).get("ai", {}).get("combat_target") == observer_target, 5)
            ctx.freeze("", npc)
            if not ctx.is_dead(ctx.player_snapshot(victim)):
                raise ctx.failure("the downed player is not dead (essential, or bleeding out)")
            ctx.camera_sees_followed(victim, "the death camera")
            def copy_of_victim():
                return ctx.actor_snapshot(survivor, ctx.other_player(ctx.actors_on(survivor)))
            ctx.wait_for("the other game's copy dies with its owner", lambda: ctx.is_dead(copy_of_victim()), 5)
            # Its ragdoll is its game's (physics sync's owners): the copy lies as it does.
            if not travel_down:
                ctx.ragdoll_lies_as_owner(victim, 0x14, survivor, ctx.other_player(ctx.actors_on(survivor)))
            ctx.wait_for("spectator camera has a standing player once the player is dead",
                     lambda: ctx.death_state(victim).get("spectating"), 10)
            ctx.camera_sees_followed(victim, "spectating")
            ctx.spectating_stays_third_person(victim)
            try:
                ctx.wait_for("the host counts combat against the standing player",
                         lambda: ctx.death_state(victim).get("status") == "Party in combat", 5)
            except TestFailure as failure:
                raise ctx.failure(f"{failure} (status {ctx.death_state(victim).get('status')!r}, the wolf's target "
                                  f"{ctx.host_world(npc).get('ai', {}).get('combat_target')})") from None
            started = time.monotonic()
            def still_down_in_combat():
                if not ctx.game_state(victim).get("online") or not ctx.death_state(victim).get("down"):
                    raise ctx.failure("the player respawned, or its game reloaded, while the party was fighting")
                if not ctx.is_dead(ctx.player_snapshot(victim)):
                    raise ctx.failure("the dead player came back alive")
                if not ctx.is_dead(copy_of_victim()):
                    raise ctx.failure("the other game's copy came back alive")
                return time.monotonic() - started >= 6
            ctx.wait_for("the vanilla five-second death reload never fires", still_down_in_combat, 8)
            if travel_down:
                ctx.both_go(INN)
                if not ctx.is_dead(ctx.player_snapshot(victim)) or not ctx.death_state(victim).get("down"):
                    raise ctx.failure("party travel revived the dead follower")
                ctx.camera_sees_followed(victim, "the dead follower after party travel")
                ctx.both_go(HOME_CELL)
                # An unloaded NPC gets a new session and copy on return; its host reference survives.
                ctx.wait_for("the enemy is assigned after travel", lambda:
                             ctx.host_npc_state(npc).get("session_id", -1) >= 0, 10)
                copy = ctx.copy_of("client", ctx.host_npc_state(npc)["session_id"])
            # Drive spectator controls as player input; with a duo the sole standing target stays selected.
            watched = ctx.death_state(victim)["spectating"]
            ctx.tap_key(victim, 0xCD)
            ctx.player_command(victim, {"cmd": "mouse", "button": 0, "down": True})
            ctx.player_command(victim, {"cmd": "mouse", "button": 0, "down": False})
            if ctx.death_state(victim).get("spectating") != watched:
                raise ctx.failure("cycling lost the only standing player")
            # The surviving player ends the fight by actually swinging their weapon.
            ctx.console_on("", f"{npc:X}.forceav health 1")
            ctx.freeze("", npc, False)
            ctx.console_on("", f"{npc:X}.setrestrained 1")
            ctx.place_before(survivor, copy if survivor == "client" else npc, MELEE_GAP)
            # Drawn only now: a standing player with a weapon out draws the enemy off the victim while it is staged.
            ctx.draw(survivor)
            ctx.attack(survivor)
            ctx.wait_for("the standing player's blow kills the enemy", lambda: ctx.host_npc_state(npc).get("dead"), 8)
            ctx.wait_on("client", "the enemy's copy dies too", lambda actors: actors.get(str(copy), {}).get("dead"), 5)
            ctx.wait_for("the party leaves combat",
                     lambda: ctx.death_state(victim).get("status", "").startswith("Respawning in"), 10)
            clear_since = time.monotonic()
            ctx.wait_for("the player respawns beside the standing party", lambda: not ctx.death_state(victim).get("down"), 14)
            if not ctx.host_npc_state(npc).get("dead") or not ctx.actor_snapshot("client", copy).get("state", {}).get("dead"):
                raise ctx.failure(f"after the respawn the enemy is dead on the host: {bool(ctx.host_npc_state(npc).get('dead'))}, "
                                  f"its copy on the client: {bool(ctx.actor_snapshot('client', copy).get('state', {}).get('dead'))}")
            ctx.wait_for("the living player recovers engine state and controls",
                     lambda: not ctx.is_dead(ctx.player_snapshot(victim))
                     and (ctx.player_snapshot(victim).get("values") or {}).get("health", 0) > 0
                     and (ctx.player_snapshot(victim).get("state") or {}).get("knock") == 0
                     and ctx.takes_input(victim), 8)
            if not travel_down and time.monotonic() - clear_since < 9:
                raise ctx.failure("respawn did not wait ten continuous seconds out of combat")
            if distance(ctx.my_position(victim), ctx.my_position(survivor)) > 250:
                raise ctx.failure("the player did not respawn beside the standing party")
            if ctx.death_state(victim).get("spectating"):
                raise ctx.failure("the camera did not return to the respawned player")
            ctx.camera_sees_followed(victim, "after the respawn")
            start = ctx.my_position(victim)
            ctx.press(victim, "forward")
            try:
                ctx.wait_for("the respawned player can walk again", lambda: distance(ctx.my_position(victim)[:2], start[:2]) > 12, 5)
            finally:
                ctx.release(victim, "forward")
            ctx.wait_for("the revived player holds what it held", lambda: (ctx.player_snapshot(victim).get("state") or {}).get("hands") == hands, 3)
            # Its behavior graph runs again (KillImpl stops it: no draw or swing would ever play out).
            ctx.draw(victim)
            ctx.attack(victim)
            ctx.sheathe(victim)
            if ctx.effect_spells(victim) != abilities:
                raise ctx.failure(f"the revived player's effects {ctx.effect_spells(victim)} are not its abilities before {abilities}")
            observer = "client" if victim == "" else ""
            if ctx.is_dead(ctx.actor_snapshot(observer, ctx.other_player(ctx.actors_on(observer)))):
                raise ctx.failure("the other game still shows the revived player dead")
            ctx.wait_for("the copy retains its owner's magicka after respawn",
                     lambda: is_close((ctx.actor_snapshot(observer, ctx.other_player(ctx.actors_on(observer))).get("values") or {}).get("magicka"), old_magicka, 1), 5)
        finally:
            if hidden_host:
                ctx.console_on("", "player.setav invisibility 0")
            ctx.console_on("", f"set KillMoveRandom to {KILL_MOVE_RANDOM}")
            ctx.console_on(victim, "player.restoreav health 10000")
            ctx.remove_host_npc(npc)


    def player_health(ctx, instance):
        return (ctx.player_snapshot(instance).get("values") or {}).get("health")


    def friendly_fire(ctx, attacker, weapon):
        """The attacker's player hits the other player's copy with `weapon` ("sword" or "firebolt"): the victim's own
        player loses health on its game, the attacker's copy of it shows that value, and the victim's game runs no NPC's
        reaction to its player (no combat against the attacker)."""
        victim = "client" if attacker == "" else ""
        for instance in ("", "client"):
            ctx.console_on(instance, "player.restoreav health 10000")
        copy = ctx.other_player(ctx.actors_on(attacker))
        if copy is None:
            raise ctx.failure(f"[{attacker or 'host'}] no copy of the other player")
        if weapon == "sword":
            ctx.give_sword(attacker)
            ctx.place_before(attacker, copy, MELEE_GAP)
        else:
            ctx.give_spell(attacker, FIREBOLT)
            ctx.place_before(attacker, copy, SPELL_GAP)
        ctx.draw(attacker)
        before = ctx.player_health(victim)
        if weapon == "sword":
            ctx.attack(attacker)
        else:
            ctx.cast_spell(attacker, copy, FIREBOLT)
        after = ctx.wait_for(f"[{victim or 'host'}] the victim's own player takes the {weapon}",
                         lambda: (h := ctx.player_health(victim)) is not None and h < before - 0.5 and h, 3)
        log(f"[{victim or 'host'}] player health {before:.0f} -> {after:.0f}")
        ctx.wait_on(attacker, "the attacker's copy of the victim shows its health",
                lambda actors: is_close(actors.get(str(copy), {}).get("health"), ctx.player_health(victim)), 5)
        if (ctx.player_snapshot(victim).get("ai") or {}).get("combat_target"):
            raise ctx.failure(f"[{victim or 'host'}] the victim's player went into combat with its teammate")
        ctx.sheathe(attacker)
        ctx.console_on(victim, "player.restoreav health 10000")
        ctx.hold()


    def fus_ro_dah_player(ctx, attacker):
        """Use the real three-word shout on the other player; both games see the fall and recovery."""
        recovery = ctx.actor_value(attacker, 0x14, 86)  # kShoutRecoveryMult
        words = {(instance, word): ctx.word_state(instance, word)
                 for instance in ("", "client") for word in UNRELENTING_FORCE_WORDS}
        try:
            victim = "client" if attacker == "" else ""
            copy = ctx.other_player(ctx.actors_on(attacker))
            if copy is None:
                raise ctx.failure(f"[{attacker or 'host'}] no copy of the victim")
            for word in UNRELENTING_FORCE_WORDS:
                ctx.console_on(attacker, f"player.teachword {word:X}")
                ctx.console_on(attacker, f"player.unlockword {word:X}")
            ctx.console_on(attacker, f"player.addshout {UNRELENTING_FORCE:X}")
            ctx.console_on(attacker, f"player.equipshout {UNRELENTING_FORCE:X}")
            ctx.console_on(attacker, "player.setav shoutrecoverymult 0")
            ctx.console_on(attacker, "player.restoreav health 10000")
            ctx.console_on(victim, "player.restoreav health 10000")
            ctx.place_before(attacker, copy, 300, bearing=90)  # shout toward the clear west end, away from QA clutter
            ctx.wait_for("the three-word Unrelenting Force shout is equipped",
                     lambda: ctx.local_player(attacker).get("selected_power") == UNRELENTING_FORCE, 5)
            start = ctx.my_position(victim)
            _, since = ctx.engine_events(attacker)
            def knock(instance, actor):
                return (ctx.actor_snapshot(instance, actor).get("state") or {}).get("knock", 0)
            frame = ctx.game_frame(attacker)
            ctx.press(attacker, "shout")
            try:
                ctx.wait_for("the shout button is held through a full three-word charge",
                         lambda: ctx.game_frame(attacker) >= frame + 120, 5)
            finally:
                ctx.release(attacker, "shout")
            ctx.wait_for("the native three-word shout is cast, rather than its weaker versions",
                     lambda: any(e["kind"] == "spell_cast" and e.get("caster") == 0x14 and
                                 e.get("spell") == UNRELENTING_FORCE_THREE
                                 for e in ctx.engine_events(attacker, since)[0]), 5)
            ctx.wait_for(f"[{victim or 'host'}] the victim's own player is knocked down",
                     lambda: 0 < knock(victim, 0x14) <= 4, 5)
            ctx.wait_for(f"[{attacker or 'host'}] the victim's copy is knocked down",
                     lambda: 0 < knock(attacker, copy) <= 4, 5)
            ctx.wait_for(f"[{victim or 'host'}] the shout displaces the victim",
                     lambda: distance(ctx.my_position(victim)[:2], start[:2]) > 30, 5)
            ctx.wait_for(f"[{victim or 'host'}] the victim gets up alive",
                     lambda: not knock(victim, 0x14) and not ctx.is_dead(ctx.player_snapshot(victim)), 25)
            ctx.wait_for("the copy gets up with its owner", lambda: not knock(attacker, copy), 5)
            ctx.face(victim, ctx.other_player(ctx.actors_on(victim)))
            before_walk = ctx.my_position(victim)
            ctx.press(victim, "forward")
            try:
                ctx.wait_for("the recovered player can walk again",
                         lambda: distance(ctx.my_position(victim)[:2], before_walk[:2]) > 30, 5)
            finally:
                ctx.release(victim, "forward")
        finally:
            ctx.console_on(attacker, f"player.setav shoutrecoverymult {recovery:g}")
            for (instance, word), (known, unlocked) in words.items():
                ctx.player_command(instance, {"cmd": "form_known", "form": word, "known": known, "unlocked": unlocked})


    def spell_hits_host_npc(ctx, spell, name):
        """Client casts `spell` at the host's NPC. The host's real NPC must take the damage, and the
        client's copy must only ever show the host's value: a copy that drops below its owner applied
        the effect itself (two games simulating one actor)."""
        npc, _, copy = ctx.spawn_host_npc(LYDIA, restrained=True)
        ctx.give_spell("client", spell)
        ctx.place_before("client", copy, SPELL_GAP)
        ctx.draw("client")
        before = ctx.host_npc_health(npc)
        worst_gap = 0.0
        def landed():
            nonlocal worst_gap
            owner, mirror = ctx.host_npc_health(npc), ctx.actors_on("client").get(str(copy), {}).get("health")
            if owner is not None and mirror is not None:
                worst_gap = max(worst_gap, owner - mirror)
            return owner is not None and owner < before - 1 and owner
        try:
            ctx.cast_spell("client", copy, spell)
            after = ctx.wait_for(f"[host] the client's {name} lands", landed, 3)
        finally:
            ctx.stop_casting("client")
        log(f"host's NPC health {before:.0f} -> {after:.0f}")
        if worst_gap > 5:
            raise ctx.failure(f"the client's copy dropped {worst_gap:.0f} below the host's real NPC: the client applied the spell itself")
        ctx.copy_health_matches(npc, copy)
        ctx.health_bar_shows("client", copy)
        ctx.hold()


    def kill_credit(ctx, weapon):
        npc, _, copy = ctx.spawn_host_npc(WOLF, restrained=True)
        client_on_host = ctx.other_player(ctx.actors_on(""))
        dead = lambda: ctx.host_world(npc).get("state", {}).get("dead")
        # In front of its head, as a player would face it (its heading: 0 north, clockwise).
        front = math.degrees(ctx.actors_on("client")[str(copy)]["rotation"][2])
        with GodMode(ctx):
            if weapon == "spell":
                ctx.give_spell("client", FIREBOLT)
                ctx.place_before("client", copy, SPELL_GAP, front)
            else:
                ctx.give_sword("client")
                ctx.place_before("client", copy, MELEE_GAP, front)
            ctx.draw("client")
            # A killing blow can start a kill move instead (KillMoveRandom): the kill move then kills it.
            in_kill_move = lambda: ctx.player_command("", {"cmd": "kill_move_check", "attacker": npc, "victim": npc})["in_kill_move"]
            for hit in range(1, 16):
                before = ctx.host_npc_health(npc)
                if weapon == "spell":
                    ctx.cast_spell("client", copy, FIREBOLT)
                    ctx.hit_lands(npc, before, f"hit {hit}", 3)
                else:
                    ctx.attack("client")
                    ctx.wait_for(f"[host] hit {hit} lands or starts a kill move",
                             lambda: (h := ctx.host_npc_health(npc)) is not None and h < before - 0.5 or in_kill_move(), 3)
                if dead() or in_kill_move():
                    break
            ctx.wait_for("[host] the wolf dies", dead, 8)
        killer = ctx.wait_for("[host] killer recorded", lambda: ctx.host_world(npc).get("ai", {}).get("killer"), 5)
        if killer != client_on_host:
            raise ctx.failure(f"host records killer {killer:08X}, expected the client's character {client_on_host:08X}")
        log(f"host's wolf killed by the client's character {killer:08X} after {hit} hits")
        ctx.wait_on("client", "the wolf's copy dies too", lambda actors: actors.get(str(copy), {}).get("dead"), 5)
        # The death plays out: the copy's body comes to rest where its owner's lies.
        rest = ctx.wait_for("[host] the wolf's body comes to rest",
                        ctx.comes_to_rest(lambda: ctx.host_world(npc)["placement"]["position"]), 10)
        ctx.wait_on("client", "the copy's body lies with it",
                lambda actors: distance((actors.get(str(copy)) or {}).get("actual") or [1e9] * 3, rest) <= POSITION_TOLERANCE, 3)
        ctx.hold()


    def active_spells(ctx, instance, actor):
        """The spells of the actor's active effects in that game."""
        _game = ctx.game(instance)
        effects = (((_game.read_status() or {}).get("world") or {}).get(str(actor)) or {}).get("effects") or []
        return {e.get("spell") for e in effects}


    def shown_visuals(ctx, instance, form_id):
        """The shaders and art in that game's list on the actor, the finished ones still fading out too: a one-shot
        hit shader is finished from its start ("visuals" leaves those out)."""
        _game = ctx.game(instance)
        return set(((((_game.read_status() or {}).get("world") or {}).get(str(form_id)) or {}).get("mod") or {}).get("visuals_shown") or [])


    def cell_objects(ctx, instance):
        """The doors and containers in that game's cell, by reference id ({"cmd": "objects"})."""
        _game = ctx.game(instance)
        response = _game.rpc({"cmd": "objects"}) or {}
        return {o["id"]: o for o in response.get("objects", [])}


    def inner_door(ctx):
        """A door in the host's cell that is not a load door (QASmoke has two), closed."""
        doors = [o for o in ctx.cell_objects("").values() if o["door"] and not o["load_door"]]
        if not doors:
            raise ctx.failure("no door in the host's cell that stays in it")
        return min(doors, key=lambda o: o["id"])["id"]


    def object_command(ctx, instance, request):
        """{"cmd": "activate"/"lock", ...} in that game; the player does it, as by hand."""
        ctx.player_command(instance, request)
        log(f"[{instance or 'host'}] {request['cmd']} {request['object']:X}")


    def activate_on(ctx, instance, door):
        ctx.object_command(instance, {"cmd": "activate", "object": door})


    def lock_on(ctx, instance, door, locked, level=0):
        ctx.object_command(instance, {"cmd": "lock", "object": door, "locked": locked, "level": level})


    def door_state(ctx, instance, door):
        return ctx.cell_objects(instance).get(door, {}).get("open_state")


    def door_lock(ctx, instance, door):
        o = ctx.cell_objects(instance).get(door, {})
        return (o.get("locked"), o.get("lock_level")) if o.get("has_lock") else (False, None)

