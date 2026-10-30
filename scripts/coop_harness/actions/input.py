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
    CASTER_CHARGED,
    CONCENTRATION_SPELLS,
    COOKING_SPIT,
    FLAMES,
    IN_FURNITURE,
    IRON_SWORD,
    ON_GROUND,
    RIGHT_HAND,
    WEAPON_DRAWN,
    WEAPON_SHEATHED,
)
from ..comparisons import distance
from ..utilities import log


class InputActions:
    def actors_on(ctx, instance):
        _game = ctx.game(instance)
        return (_game.read_status() or {}).get("actors", {})


    def wait_on(ctx, instance, description, predicate, timeout=15):
        """Polls `predicate(actors)` against an instance's status until it returns a truthy value."""
        def check():
            return predicate(ctx.actors_on(instance))
        return ctx.wait_for(f"[{instance or 'host'}] {description}", check, timeout)


    def console_on(ctx, instance, command):
        _game = ctx.game(instance)
        return ctx.run_console(command, _game=_game)


    def local_player(ctx, instance):
        _game = ctx.game(instance)
        return (_game.read_status() or {}).get("player") or {}


    def player_snapshot(ctx, instance):
        _game = ctx.game(instance)
        return ((_game.read_status() or {}).get("world") or {}).get("20") or {}


    def other_player(ctx, actors):
        return next((int(a) for a, s in actors.items() if s.get("other_player") and s.get("has_3d")), None)


    def remote_npcs(ctx, actors):
        return {int(a) for a, s in actors.items() if not s.get("other_player") and s.get("has_3d")}


    def comes_to_rest(ctx, read, still=0.5, within=2.0):
        """A wait_for predicate: the position `read()` returns, once it has moved less than `within`
        units for `still` seconds."""
        seen = {"since": time.monotonic(), "at": None}
        def rested():
            at = read()
            if seen["at"] is None or distance(at, seen["at"]) > within:
                seen.update(since=time.monotonic(), at=at)
            return time.monotonic() - seen["since"] >= still and at
        return rested


    def show(ctx, observer, observed):
        """With --hold, turns the observer's camera toward the actor under test."""
        if ctx.HOLD and (target := observed(ctx.actors_on(observer))) is not None:
            ctx.face(observer, target)


    def hold(ctx):
        if ctx.HOLD:
            log(f"(holding {ctx.HOLD:.0f}s so you can look)")
            time.sleep(ctx.HOLD)


    def player_command(ctx, instance, request):
        """A plugin command in that game; fails the scenario if the game refuses it."""
        _game = ctx.game(instance)
        response = _game.rpc(request) or {}
        if not response.get("ok"):
            raise ctx.failure(f"[{instance or 'host'}] {request} refused: {response.get('error', 'no answer')}")
        return response


    def weapon_state(ctx, instance):
        return (ctx.player_snapshot(instance).get("state") or {}).get("weapon_state")


    def takes_input(ctx, instance):
        """No menu but the HUD is open and no kill cam plays: a fade (a cell change's Fader Menu), a loading
        screen, the console or a kill cam (the player's killing blow, by chance) swallow a player's buttons."""
        _game = ctx.game(instance)
        status = _game.read_status() or {}
        return not set(status.get("menus") or []) - {"HUD Menu"} and not status.get("kill_cam")


    def press(ctx, instance, button):
        """The player presses a button ("right", "left" or "ready") through the game's input dispatch,
        as its device would: everything that listens to input sees a player's press. Once the game takes
        input, as a player would wait for a fade to end."""
        ctx.wait_for(f"[{instance or 'host'}] the game takes input", lambda: ctx.takes_input(instance), 10)
        ctx.player_command(instance, {"cmd": "input", "button": button, "down": True})
        ctx.game(instance).held_buttons.add(button)


    def release(ctx, instance, button):
        """The player lets go of the button; the game is told how long it was held."""
        ctx.player_command(instance, {"cmd": "input", "button": button, "down": False})
        ctx.game(instance).held_buttons.discard(button)


    def tap(ctx, instance, button):
        ctx.press(instance, button)
        ctx.release(instance, button)


    def hands_settled(ctx, instance):
        """No draw or sheathe animation playing: until it ends, the game ignores Ready Weapon (a player's
        press as much as ours)."""
        me = ctx.local_player(instance)
        return not me.get("equipping") and not me.get("unequipping")


    def ready_weapon(ctx, instance, state, what):
        """Presses Ready Weapon and returns once its animation has played out: the weapon state alone
        is not enough (a spell's reads drawn on the press, while its animation still plays)."""
        who = f"[{instance or 'host'}]"
        ctx.wait_for(f"{who} the last draw or sheathe has played out", lambda: ctx.hands_settled(instance), 5)
        animations = ctx.local_player(instance).get("equip_animations", 0)
        ctx.tap(instance, "ready")
        ctx.wait_for(f"{who} {what}", lambda: ctx.local_player(instance).get("equip_animations", 0) > animations
                 and ctx.weapon_state(instance) == state and ctx.hands_settled(instance), 5)


    def draw(ctx, instance):
        """The player readies what it holds (the Ready Weapon button); returns once the hands are ready."""
        if ctx.weapon_state(instance) != WEAPON_DRAWN:
            ctx.ready_weapon(instance, WEAPON_DRAWN, "hands ready")
        ctx.wait_for(f"[{instance or 'host'}] hands settled", lambda: ctx.hands_settled(instance), 5)


    def sheathe(ctx, instance):
        """The player lowers its hands (the Ready Weapon button); returns once they are down."""
        if ctx.weapon_state(instance) != WEAPON_SHEATHED:
            ctx.ready_weapon(instance, WEAPON_SHEATHED, "hands lowered")
        ctx.wait_for(f"[{instance or 'host'}] hands settled", lambda: ctx.hands_settled(instance), 5)


    def equip_in_hand(ctx, instance, commands, item, what):
        """Runs the console commands that put `item` in the player's hand. With the hands up the game
        plays the equip animation (MagicForceEquip...), during which a press is cut short: waited for."""
        animations = ctx.local_player(instance).get("equip_animations", 0)
        hands = lambda: (ctx.player_snapshot(instance).get("state") or {}).get("hands", [])
        # A new item in raised hands plays the equip animation; the same item again plays nothing.
        animated = ctx.weapon_state(instance) == WEAPON_DRAWN and item not in hands()
        for command in commands:
            ctx.console_on(instance, command)
        who = f"[{instance or 'host'}]"
        ctx.wait_for(f"{who} {what}", lambda: item in hands(), 5)
        if animated:
            ctx.wait_for(f"{who} the equip animation has played out",
                     lambda: ctx.local_player(instance).get("equip_animations", 0) > animations and ctx.hands_settled(instance), 5)


    def attack(ctx, instance):
        """One right-hand attack: a click of the attack button, held until the swing starts (the game
        starts it from the held button's updates, so an instant click does nothing). A swing still going
        would pass for the new one, so it waits for that to end first."""
        ctx.wait_for(f"[{instance or 'host'}] the last swing is over",
                 lambda: not ctx.graph_bools(instance, "IsAttacking")["IsAttacking"], 3)
        ctx.press(instance, "right")
        try:
            # A killing blow can start a kill move instead (a paired animation: synced, not attacking).
            ctx.wait_for(f"[{instance or 'host'}] swings", lambda: any(ctx.graph_bools(instance, "IsAttacking", "bIsSynced").values()), 2)
        finally:
            ctx.release(instance, "right")


    def face(ctx, instance, target):
        ctx.player_command(instance, {"cmd": "face", "target": target})


    def clear_way(ctx, instance, bearings, distance):
        """The first of `bearings` (degrees, 0 north) the player can walk `distance` along: the harness's `surroundings`
        rays at knee and chest height clear the nearest way and either side of it."""
        _game = ctx.game(instance)
        response = _game.rpc({"cmd": "surroundings", "radius": distance}) or {}
        if not response.get("ok"):
            raise ctx.failure(f"[{instance or 'host'}] no surroundings: {response.get('error', 'no answer')}")
        knee, chest = response["rings"]["knee"], response["rings"]["chest"]
        clear = [not k["hit"] and not c["hit"] for k, c in zip(knee, chest)]
        step = 360 / len(knee)
        for bearing in bearings:
            i = round((bearing % 360) / step) % len(knee)
            if clear[i - 1] and clear[i] and clear[(i + 1) % len(clear)]:
                return bearing
        blocked = "; ".join(f"{k['bearing']}: {k['distance']:.0f}" for k in knee if k["hit"])
        raise ctx.failure(f"[{instance or 'host'}] none of {bearings} clear for {distance} units (knee hits by bearing: {blocked})")


    def game_frame(ctx, instance):
        _game = ctx.game(instance)
        return ((_game.read_status() or {}).get("frames") or {}).get("frames", 0)


    def place_before(ctx, instance, target, gap, bearing=None):
        """The player stands `gap` units from `target`, facing it (yaw and pitch, in the same frame), on a spot the plugin
        checked with rays: a floor under it, headroom, nothing between it and the target. A blocked bearing gives way to
        the nearest clear one (logged); none clear fails, naming what blocked each. Returns once it stands on the ground
        and has landed: the move leaves its character controller in
        the air for a moment, and a draw or a cast started then, or during the landing animation the
        graph plays after the controller is down (bInJumpState), goes wrong. The controller notices the
        move a frame later, so the ground is checked from a few frames on."""
        moved = ctx.game_frame(instance)
        request = {"cmd": "place", "target": target, "distance": gap}
        if bearing is not None:
            request["bearing"] = bearing  # its side of the target (0 north, 90 east): two players apart
        placed = ctx.player_command(instance, request)
        if bearing is not None and abs((placed.get("bearing", bearing) - bearing + 180) % 360 - 180) > 1:
            log(f"[{instance or 'host'}] bearing {bearing:.0f} from {target:X} is blocked: stands at {placed['bearing']:.0f} instead")
        ctx.wait_for(f"[{instance or 'host'}] stands on the ground", lambda: ctx.game_frame(instance) >= moved + 3
                 and ctx.local_player(instance).get("character_state") == ON_GROUND
                 and not ctx.graph_bools(instance, "bInJumpState")["bInJumpState"], 5)


    def give_sword(ctx, instance):
        ctx.equip_in_hand(instance, [f"player.additem {IRON_SWORD:08X} 1", f"player.equipitem {IRON_SWORD:08X}"],
                      IRON_SWORD, "holds the sword")


    def give_spell(ctx, instance, spell):
        ctx.equip_in_hand(instance, [f"player.addspell {spell:08X}", f"player.equipspell {spell:08X} right",
                                 "player.restoreav magicka 1000"], spell, "holds the spell")


    def graph_bools(ctx, instance, *names):
        """The player's behavior graph booleans by name."""
        return ctx.player_command(instance, {"cmd": "graph_bool", "names": list(names)})["values"]


    def cast_spell(ctx, instance, target, spell):
        """The player casts the right hand's `spell` with the attack button, facing `target` first (aimed
        spells fly where the camera looks). A charged spell fires once the game has charged it; a
        concentration spell streams until stop_casting."""
        if target:
            ctx.face(instance, target)
        # A press while the last cast is still going out (its release animation lasts about a second) is
        # ignored, as a player's would be.
        ctx.wait_for(f"[{instance or 'host'}] the last cast has gone out",
                 lambda: (ctx.local_player(instance).get("caster_states") or [0, 0])[RIGHT_HAND] == 0
                 and not ctx.graph_bools(instance, "IsCastingRight")["IsCastingRight"], 5)
        ctx.press(instance, "right")
        if spell in CONCENTRATION_SPELLS:
            return
        ctx.wait_for(f"[{instance or 'host'}] {spell:08X} charged",
                 lambda: (ctx.local_player(instance).get("caster_states") or [0, 0])[RIGHT_HAND] == CASTER_CHARGED, 5)
        ctx.release(instance, "right")


    def stop_casting(ctx, instance):
        """The player lets go of the attack button: a concentration spell stops."""
        ctx.release(instance, "right")


    def sneaking(ctx, instance):
        return bool((ctx.player_snapshot(instance).get("state") or {}).get("sneaking"))


    def copy_sneaking(ctx, observer):
        """The observer's copy of the other player: its sneaking flag (what detection reads), or None."""
        _game = ctx.game(observer)
        status = _game.read_status() or {}
        target = ctx.other_player(status.get("actors", {}))
        if target is None:
            return None
        return bool((((status.get("world") or {}).get(str(target)) or {}).get("state") or {}).get("sneaking"))


    def sneak(ctx, instance, down):
        """The player presses Sneak (a toggle) until it is sneaking (`down`) or standing."""
        if ctx.sneaking(instance) != down:
            ctx.tap(instance, "sneak")
        ctx.wait_for(f"[{instance or 'host'}] {'sneaks' if down else 'stands up'}", lambda: ctx.sneaking(instance) == down, 5)


    def sneak_roundtrip(ctx, actor_instance, observer, label):
        """Player sneaks then stands up; the other game's copy must follow and stay there. A copy runs
        Actor::Update, which decides whether its actor should sneak (package, combat, teammate rules) and
        toggles it if not: on a copy that toggle is an action our PerformAction hook refuses, so it keeps
        its owner's state (docs/re/systems/actor-ai-and-copies.md, "Actor::Update")."""
        ctx.show(observer, ctx.other_player)
        ctx.sneak(actor_instance, True)
        ctx.wait_for(f"[{observer or 'host'}] {label} sneaks", lambda: ctx.copy_sneaking(observer) is True, 5)
        # Held for a while: Actor::Update runs every frame on the copy.
        stable_until = time.time() + 1.0
        ctx.wait_for(f"[{observer or 'host'}] {label} still sneaks after 1s",
                 lambda: ctx.copy_sneaking(observer) is True and time.time() >= stable_until, 3)
        ctx.hold()
        ctx.sneak(actor_instance, False)
        ctx.wait_for(f"[{observer or 'host'}] {label} stands up", lambda: ctx.copy_sneaking(observer) is False, 5)


    def sit_state(ctx, instance, form_id):
        _game = ctx.game(instance)
        world = (_game.read_status() or {}).get("world") or {}
        return (((world.get(str(form_id)) or {}).get("state")) or {}).get("sit_sleep")


    def furniture_roundtrip(ctx, actor_instance, observer, label):
        """The player gets into furniture and out again; the other game's copy must follow both ways by
        replay, while loaded (a copy spawned later starts in its owner's graph state instead)."""
        ctx.show(observer, ctx.other_player)
        copy = ctx.other_player(ctx.actors_on(observer))
        ctx.console_on(actor_instance, f"player.moveto {COOKING_SPIT:08X}")
        ctx.wait_for(f"[{actor_instance or 'host'}] player is at the cooking spit", lambda: ctx.sit_state(actor_instance, 0x14) == IN_FURNITURE, 5)
        ctx.wait_for(f"[{observer or 'host'}] {label} is at the cooking spit", lambda: ctx.sit_state(observer, copy) == IN_FURNITURE, 5)
        ctx.hold()
        ctx.activate_on(actor_instance, COOKING_SPIT)
        ctx.wait_for(f"[{actor_instance or 'host'}] player leaves it", lambda: ctx.sit_state(actor_instance, 0x14) == 0, 5)
        ctx.wait_for(f"[{observer or 'host'}] {label} leaves it", lambda: ctx.sit_state(observer, copy) == 0, 5)
        if ctx.other_player(ctx.actors_on(observer)) != copy:
            raise ctx.failure(f"{label} was respawned on the way: the state was not replayed")


    def copy_casting(ctx, spell, expected):
        def check(actors):
            target = ctx.other_player(actors)
            return target is not None and (actors[str(target)]["casting"][RIGHT_HAND] == spell) == expected
        return check


    def cast_roundtrip(ctx, actor_instance, observer, label):
        """Player casts Flames (a held stream) by holding the attack button, and lets go; the other
        game's copy must cast, then stop."""
        ctx.give_spell(actor_instance, FLAMES)
        ctx.show(observer, ctx.other_player)
        ctx.draw(actor_instance)
        ctx.cast_spell(actor_instance, None, FLAMES)
        try:
            ctx.wait_for(f"[{actor_instance or 'host'}] player casts Flames",
                     lambda: ctx.local_player(actor_instance).get("casting", [0, 0])[RIGHT_HAND] == FLAMES, 5)
            ctx.wait_on(observer, f"{label} casts Flames", ctx.copy_casting(FLAMES, True), 5)
            ctx.hold()
        finally:
            ctx.stop_casting(actor_instance)
        ctx.wait_on(observer, f"{label} stops casting", ctx.copy_casting(FLAMES, False), 5)
        ctx.sheathe(actor_instance)


    def quick_sheathe(ctx, actor_instance, observer, label):
        """Player raises a spell and lowers it as soon as the game lets it, a few times. The owner's
        engine fires a late 'MRh_Equipped_Event' it refuses itself: sent anyway, the copy took it after
        the sheathe and kept showing the spell ready."""
        ctx.give_spell(actor_instance, FLAMES)
        for _ in range(3):
            ctx.draw(actor_instance)
            ctx.sheathe(actor_instance)
        ctx.wait_on(observer, f"{label}'s copy has the hands down", ctx.copy_drawn(False), 5)
        # The late event reaches the copy a few hundred ms after the sheathe: still down after it.
        end = time.monotonic() + 1.5
        while time.monotonic() < end:
            if not ctx.copy_drawn(False)(ctx.actors_on(observer)):
                raise ctx.failure(f"{label}'s copy raised the hands again after the sheathe")
            time.sleep(0.1)


    def hands_survive_reload(ctx, actor_instance, observer, label):
        """The player keeps its hands up through a hot reload, then lowers them and reloads again: each
        time the other game respawns its copy, which must show the hands as they are."""
        _game = ctx.game()
        ctx.give_spell(actor_instance, FLAMES)
        for up, verb in ((True, "raised"), (False, "lowered")):
            ctx.draw(actor_instance) if up else ctx.sheathe(actor_instance)
            ctx.wait_on(observer, f"{label}'s copy has the hands {verb}", ctx.copy_drawn(up), 5)
            ctx.reload_games()
            if bool(ctx.local_player(actor_instance).get("weapon_drawn")) != up:
                raise ctx.failure(f"the reload changed the {actor_instance or 'host'} player's hands")
            ctx.wait_on(observer, f"{label}'s respawned copy has the hands {verb}", ctx.copy_drawn(up))


    def health_roundtrip(ctx, actor_instance, ref, real_health, observer, observed, label):
        """Damages then restores health on `ref` in its owner's game; the other game's copy must show
        the owner's value each time."""
        def matches(actors):
            target = observed(actors)
            real = real_health()
            return target is not None and is_close(actors[str(target)].get("health"), real) and real
        ctx.show(observer, observed)
        for command, what in ((f"{ref}.damageav health 40", "damaged"), (f"{ref}.restoreav health 40", "restored")):
            before = real_health()
            ctx.console_on(actor_instance, command)
            ctx.wait_for(f"[{actor_instance or 'host'}] health {what}", lambda: not is_close(real_health(), before), 5)
            health = ctx.wait_on(observer, f"{label} health matches after being {what}", matches, 5)
            log(f"{label}: health {health:.0f} on both sides")
            ctx.hold()


    def game_window(ctx, instance):
        _game = ctx.game(instance)
        env = dict(os.environ, DISPLAY=_game.display)
        windows = subprocess.run(["xdotool", "search", "--name", "^Skyrim"], env=env, capture_output=True, text=True).stdout.split()
        if not windows:
            raise ctx.failure(f"[{instance or 'host'}] game window not found on {_game.display}")
        return env, windows[0]


    def hold_key(ctx, instance, key, seconds):
        """Holds a key in an instance's game for `seconds` (real XTEST input on the off-screen display:
        the game polls the keyboard once a frame, so a quick synthetic press can fall between two)."""
        env, window = ctx.game_window(instance)
        for command in (["windowfocus", window], ["keydown", key]):
            subprocess.run(["xdotool", *command], env=env, check=True, stderr=subprocess.DEVNULL)
        time.sleep(seconds)
        subprocess.run(["xdotool", "keyup", key], env=env, check=True, stderr=subprocess.DEVNULL)


    def key_on(ctx, instance, key):
        ctx.hold_key(instance, key, 0.1)


    def mouse_on(ctx, instance, action, button=1):
        """mousedown/mouseup in an instance's game window (button 1 = attack/cast with the right hand).
        Wine's DirectInput mouse ignores synthetic per-window events: real (XTEST) input."""
        env, window = ctx.game_window(instance)
        for command in (["windowfocus", window], ["mousemove", "--window", window, "640", "360"], [action, str(button)]):
            subprocess.run(["xdotool", *command], env=env, check=True, stderr=subprocess.DEVNULL)

