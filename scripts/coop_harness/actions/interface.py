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
    ENTER_KEY,
    PLACE_MARKER_KEY,
    QUICK_MAP_KEY,
    REMOVE_IT_BUTTON,
    SHIFT_KEY,
    TWEEN_MENU_KEY,
    TYPED_KEYS,
)
from ..comparisons import distance


class InterfaceActions:
    def key(ctx, instance, code, down):
        ctx.player_command(instance, {"cmd": "key", "scan": code, "down": down})


    def held_a_frame(ctx, instance):
        """Waits until the game draws a frame: a key let go in the frame it went down is never taken by a menu's movie
        (the RaceSex menu's list missed such an Up)."""
        frame, end = ctx.game_frame(instance), time.monotonic() + 5
        while ctx.game_frame(instance) <= frame:  # quiet: one per keystroke
            if time.monotonic() > end:
                raise ctx.failure(f"[{instance or 'host'}] the game drew no frame while a key was held")
            time.sleep(0.005)


    def tap_key(ctx, instance, code, shift=False):
        """A key pressed and let go, as the keyboard sends it (through the game's input dispatch), held a frame."""
        if shift:
            ctx.key(instance, SHIFT_KEY, True)
        ctx.key(instance, code, True)
        ctx.held_a_frame(instance)
        ctx.key(instance, code, False)
        if shift:
            ctx.key(instance, SHIFT_KEY, False)


    def type_text(ctx, instance, text):
        for char in text:
            code, shift = TYPED_KEYS[char.lower()] if char.isalpha() else TYPED_KEYS[char]
            ctx.tap_key(instance, code, shift or char.isupper())


    def chat(ctx, instance):
        return ctx.game_state(instance).get("chat") or {}


    def open_chat(ctx, instance):
        """The player opens the chat line with Enter, once the game takes input (as a player waits)."""
        ctx.wait_for(f"[{instance or 'host'}] the game takes input", lambda: ctx.takes_input(instance), 10)
        ctx.tap_key(instance, ENTER_KEY)
        ctx.wait_for(f"[{instance or 'host'}] the chat line is open", lambda: ctx.chat(instance).get("open"), 5)


    def say(ctx, instance, text):
        """The player types a line into the chat and sends it with Enter."""
        ctx.open_chat(instance)
        ctx.type_text(instance, text)
        ctx.wait_for(f"[{instance or 'host'}] the chat line reads {text!r}", lambda: ctx.chat(instance).get("draft") == text, 5)
        ctx.tap_key(instance, ENTER_KEY)
        ctx.wait_for(f"[{instance or 'host'}] the chat line is closed", lambda: not ctx.chat(instance).get("open"), 5)


    def chat_has(ctx, instance, name, text):
        return any(l["name"] == name and l["text"] == text for l in ctx.chat(instance).get("lines", []))


    def map_marker(ctx, instance):
        return ctx.game_state(instance).get("map_marker")


    def map_takes_input(ctx, instance):
        """The map is open and ready (MapMenu's controlsReady), and nothing covers it (its fade-in)."""
        _game = ctx.game(instance)
        status = _game.read_status() or {}
        return (status.get("game") or {}).get("map_ready") and set(status.get("menus") or []) <= {"HUD Menu", "MapMenu", "Cursor Menu"}


    def open_world_map(ctx, instance):
        """M opens the remembered view; L returns a remembered local view to the world map."""
        ctx.wait_for(f"[{instance or 'host'}] the game takes input", lambda: ctx.takes_input(instance), 10)
        ctx.tap_key(instance, QUICK_MAP_KEY)
        def ready():
            if "MapMenu" not in ctx.menus(instance) or ctx.menus(instance) - {"HUD Menu", "MapMenu", "Cursor Menu"}:
                return False
            state = ctx.player_command(instance, {"cmd": "menu_flags", "name": "MapMenu"})
            return state["local_map_ready"] if state["local_map"] else ctx.map_takes_input(instance)
        ctx.wait_for(f"[{instance or 'host'}] the map finishes opening", ready, 10)
        if ctx.player_command(instance, {"cmd": "menu_flags", "name": "MapMenu"})["local_map"]:
            ctx.tap_key(instance, 0x26)
        ctx.wait_for(f"[{instance or 'host'}] the world map takes input", lambda: ctx.map_takes_input(instance), 10)


    def place_map_marker(ctx, instance):
        """The player opens the map (M), places a marker where it is centred (P) and closes it (Tab)."""
        who = f"[{instance or 'host'}]"
        ctx.open_world_map(instance)
        ctx.move_cursor(instance, (640, 360))
        ctx.tap_key(instance, PLACE_MARKER_KEY)
        ctx.wait_for(f"{who} its marker is on the map", lambda: ctx.map_marker(instance), 5)
        ctx.tap_key(instance, TWEEN_MENU_KEY)
        ctx.wait_for(f"{who} the map is closed", lambda: ctx.takes_input(instance), 10)
        return ctx.map_marker(instance)


    def menus(ctx, instance):
        _game = ctx.game(instance)
        return set((_game.read_status() or {}).get("menus") or [])


    def move_cursor(ctx, instance, target):
        """The player moves the mouse until the menu cursor is on target (window pixels)."""
        who = f"[{instance or 'host'}]"
        for _ in range(5):
            position = ctx.game_state(instance).get("cursor")
            if not position:
                raise ctx.failure(f"{who} no menu cursor")
            delta = (round(target[0] - position[0]), round(target[1] - position[1]))
            if max(abs(delta[0]), abs(delta[1])) <= 2:
                return
            ctx.player_command(instance, {"cmd": "mouse", "dx": delta[0], "dy": delta[1]})
            ctx.wait_for(f"{who} the cursor moves", lambda: ctx.game_state(instance).get("cursor") != position, 2)
        raise ctx.failure(f"{who} the cursor did not reach {target}: at {ctx.game_state(instance).get('cursor')}")


    def click(ctx, instance):
        ctx.player_command(instance, {"cmd": "mouse", "button": 0, "down": True})
        ctx.player_command(instance, {"cmd": "mouse", "button": 0, "down": False})


    def answer_marker_box(ctx, instance, button):
        """The player clicks a button of the "move or remove" box."""
        ctx.move_cursor(instance, button)
        ctx.click(instance)
        ctx.wait_for(f"[{instance or 'host'}] the box is closed", lambda: "MessageBoxMenu" not in ctx.menus(instance), 5)


    def remove_map_marker(ctx, instance):
        """The player opens the map (M), presses P on its marker and clicks "Remove It", then closes the map."""
        who = f"[{instance or 'host'}]"
        ctx.open_world_map(instance)
        ctx.tap_key(instance, PLACE_MARKER_KEY)
        ctx.wait_for(f"{who} asks to move or remove the marker", lambda: "MessageBoxMenu" in ctx.menus(instance), 5)
        ctx.answer_marker_box(instance, REMOVE_IT_BUTTON)
        ctx.wait_for(f"{who} its marker is gone", lambda: not ctx.map_marker(instance), 5)
        ctx.wait_for(f"{who} the map takes input", lambda: ctx.map_takes_input(instance), 5)
        ctx.tap_key(instance, TWEEN_MENU_KEY)
        ctx.wait_for(f"{who} the map is closed", lambda: ctx.takes_input(instance), 10)


    def same_marker(ctx, marker, placed):
        return bool(marker) and marker["world"] == placed["world"] and distance(marker["position"], placed["position"]) < 2

