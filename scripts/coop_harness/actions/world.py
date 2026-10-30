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
    ESCAPE_KEY,
    HOME_CELL,
    INN,
    MENU_ACCEPT_KEY,
    MENU_RIGHT_KEY,
    STAGE_ON_DEATH,
    WAIT_KEY,
)


class WorldActions:
    def stage_script_state(ctx, instance, actor):
        _game = ctx.game(instance)
        return ((_game.rpc({"cmd": "script_state", "object": actor, "script": STAGE_ON_DEATH}) or {}).get("state") or "").lower()


    def in_memory(ctx, instance, form_id):
        _game = ctx.game(instance)
        return bool((_game.rpc({"cmd": "form", "form": form_id}) or {}).get("ok"))


    def calendar(ctx, instance):
        return ctx.game_state(instance).get("time", {})


    def wait_hours(ctx, instance, hours):
        """The player opens the Wait menu (T), moves its slider to `hours` (D) and waits (E); returns once
        the wait is over and the menu closed."""
        who = f"[{instance or 'host'}]"
        ctx.wait_for(f"{who} the game takes input", lambda: ctx.takes_input(instance), 10)
        ctx.tap_key(instance, WAIT_KEY)
        ctx.wait_for(f"{who} the Wait menu shows its hours", lambda: ctx.game_state(instance).get("wait_hours") is not None, 5)
        while (shown := ctx.game_state(instance).get("wait_hours")) < hours:
            ctx.tap_key(instance, MENU_RIGHT_KEY)
            ctx.wait_for(f"{who} the slider moves past {shown:g} h",
                     lambda: (ctx.game_state(instance).get("wait_hours") or 0) > shown, 3)
        if shown != hours:
            raise ctx.failure(f"{who} the slider shows {shown:g} h, not {hours}")
        ctx.tap_key(instance, MENU_ACCEPT_KEY)
        ctx.wait_for(f"{who} has waited and the menu closed", lambda: ctx.takes_input(instance), 10 + 2 * hours)


    def same_calendar(ctx, tolerance=1 / 1440):
        """Both games show the same date, whole days passed and GameDaysPassed (within a game minute)."""
        host, guest = ctx.calendar(""), ctx.calendar("client")
        return all(host.get(k) == guest.get(k) for k in ("day", "month", "year", "raw_days")) \
            and abs(host.get("days_passed", 0) - guest.get("days_passed", -1)) < tolerance


    def calendars(ctx, why):
        host, guest = ctx.calendar(""), ctx.calendar("client")
        fields = ("days_passed", "raw_days", "day", "month", "year", "hour")
        return f"{why}: host " + ", ".join(f"{k} {host.get(k)}" for k in fields) + \
            "; client " + ", ".join(f"{k} {guest.get(k)}" for k in fields)


    def shared_wait_menu(ctx, instance):
        return ctx.game_state(instance).get("wait_hours") is not None


    def begin_shared_wait(ctx, instance, hours, sleeping=False):
        """Confirm a native wait, then observe both games' menus before the first hour passes."""
        who = instance or "host"
        ctx.wait_for(f"[{who}] takes input", lambda: ctx.takes_input(instance), 10)
        if sleeping:
            # Skyrim.esm: the inn's CommonBed01L. The fixture gives the party access to this bed.
            ctx.place_before(instance, 0x5EDA7, 100)
            ctx.activate_on(instance, 0x5EDA7)
        else:
            ctx.tap_key(instance, WAIT_KEY)
        ctx.wait_for(f"[{who}] chooses hours", lambda: ctx.shared_wait_menu(instance), 5)
        while (shown := ctx.game_state(instance).get("wait_hours")) < hours:
            ctx.tap_key(instance, MENU_RIGHT_KEY)
            ctx.wait_for(f"[{who}] the slider moves past {shown:g}",
                     lambda: (ctx.game_state(instance).get("wait_hours") or 0) > shown, 3)
        if shown != hours:
            raise ctx.failure(f"[{who}] expected {hours} hours, got {shown}")
        ctx.tap_key(instance, MENU_ACCEPT_KEY)
        ctx.wait_for("both games show the shared Wait menu",
                 lambda: all(ctx.shared_wait_menu(name) for name in ("", "client")), 5)
        for name in ("", "client"):
            question = ctx.menu_var(name, "Sleep/Wait Menu", "_root.SleepWaitMenu_mc.QuestionInstance.text")
            expected = "rest" if sleeping and name in (instance, "") else "wait"
            if not isinstance(question, str) or expected not in question.lower():
                raise ctx.failure(f"[{name or 'host'}] expected {expected}, got {question!r}")
            if ctx.menu_var(name, "Sleep/Wait Menu", "_root.SleepWaitMenu_mc.disableControls") != 1:
                ctx.wait_for(f"[{name or 'host'}] its shared countdown starts",
                         lambda name=name: ctx.menu_var(name, "Sleep/Wait Menu", "_root.SleepWaitMenu_mc.disableControls") == 1, 5)


    def shared_wait_roundtrip(ctx, instance, sleeping=False):
        """The starter sleeps or waits; both native menus count down across midnight on the host's calendar."""
        bed_owner = None
        if sleeping:
            ctx.both_go(INN)
            bed_owner = ctx.player_command("", {"cmd": "form", "form": 0x5EDA7}).get("owner")
        try:
            if sleeping:
                ctx.console_on("", "0005EDA7.setownership 00000007")
                ctx.wait_for("the shared bed belongs to the player on both games",
                         lambda: all(ctx.player_command(name, {"cmd": "form", "form": 0x5EDA7}).get("owner") == 7
                                     for name in ("", "client")), 5)
            health_before = ctx.player_snapshot(instance).get("values", {}).get("health", 0)
            if health_before <= 30:
                raise ctx.failure(f"[{instance or 'host'}] not healthy enough for the rest fixture: {health_before}")
            ctx.console_on(instance, "player.damageav health 25")
            ctx.wait_for("the starter has lost health before resting",
                     lambda: ctx.player_snapshot(instance).get("values", {}).get("health", 0) < health_before - 20, 5)
            ctx.console_on("", "set gamehour to 22.5")
            ctx.wait_for("the guest is at the host's 22:30", lambda: is_close(ctx.calendar("client").get("hour"), 22.5, 0.1), 10)
            before = ctx.calendar("")
            ctx.begin_shared_wait(instance, 4, sleeping)
            ctx.wait_for("both games finish their shared countdown", lambda: all(ctx.takes_input(name) for name in ("", "client")), 20)
            after = ctx.calendar("")
            if after.get("raw_days") != before.get("raw_days") + 1 or not is_close(after.get("hour"), 2.5, 0.15):
                raise ctx.failure(ctx.calendars("the shared 4-hour countdown did not reach 02:30 the next day"))
            ctx.wait_for("both calendars agree after the shared wait", ctx.same_calendar, 5)
            ctx.wait_for("the starter's native rest restores its health",
                     lambda: ctx.player_snapshot(instance).get("values", {}).get("health", 0) >= health_before - 0.5, 5)
        finally:
            for name in ("", "client"):
                if ctx.shared_wait_menu(name):
                    ctx.tap_key(name, ESCAPE_KEY)
            if sleeping:
                # Restore the original bed owner, including no owner, before leaving the fixture.
                if bed_owner is not None:
                    ctx.console_on("", f"0005EDA7.setownership {bed_owner:08X}")
                    ctx.wait_for("the bed's original owner is restored on both games",
                             lambda: all(ctx.player_command(name, {"cmd": "form", "form": 0x5EDA7}).get("owner") == bed_owner
                                         for name in ("", "client")), 5)
                ctx.both_go(HOME_CELL)


    def system_line(ctx, instance, prefix):
        """The newest system line in the chat starting with prefix, or None."""
        lines = [l["text"] for l in ctx.chat(instance).get("lines", []) if l.get("system") and l["text"].startswith(prefix)]
        return lines[-1] if lines else None


    def clock_speed(ctx, instance):
        """How fast the game's clock runs (1 normal; a kill cam or Slow Time slows it)."""
        _game = ctx.game(instance)
        return (_game.read_status() or {}).get("clock_speed")


    def clocks_run_at(ctx, speed, why):
        for name in ("", "client"):
            ctx.wait_for(f"[{name or 'host'}] its clock runs at {speed} ({why})",
                     lambda name=name: abs((ctx.clock_speed(name) or 0) - speed) < 0.01, 3)

