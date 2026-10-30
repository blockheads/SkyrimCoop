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
    ALL_CONTROLS,
    CLAW_DOOR_CELL,
    CLAW_KEYHOLE,
    CLIENT_NETNS,
    CRAFTING_LIST,
    CRAFTING_LISTS,
    CREATED_ID,
    DOWN_KEY,
    DRAGON_SOULS,
    GOLD,
    KOLBJORN_PLATE,
    MESSAGE_BOX_OK,
    PLATE_SCRIPT,
    SHRINES,
    SOLJUND_FIRE,
    SOLJUND_SCRIPT,
    SOLJUND_TRIGGER,
    SYNCED_QUEST_FLAGS,
    TAB_KEY,
    UP_KEY,
)
from ..errors import TestFailure
from ..comparisons import reference_differences
from ..utilities import log


class StoryActions:
    def global_value(ctx, instance, global_id):
        _game = ctx.game(instance)
        response = _game.rpc({"cmd": "global", "global": global_id}) or {}
        if not response.get("ok"):
            raise ctx.failure(f"[{instance or 'host'}] global {global_id:08X}: {response.get('error', 'no answer')}")
        return response["value"]


    def synced_quest(ctx, instance, quest):
        """What quest sync carries of a quest on that game: flags, stage, done stages, objectives, reference fills by id
        (a created reference by its base: each game has its own id for it) and location fills."""
        state = ctx.quest_snapshot(instance, quest)
        def fill(alias, ref):
            return ("created", state["bases"].get(alias)) if ref >= CREATED_ID else ref
        return {"flags": state["flags"] & SYNCED_QUEST_FLAGS, "stage": state["stage"], "done": state["done"],
                "objectives": state["objectives"], "locations": state["locations"],
                "aliases": {alias: fill(alias, ref) for alias, (ref, _) in state["aliases"].items()}}


    def quest_fragments(ctx, instance, quest):
        _game = ctx.game(instance)
        return (_game.rpc({"cmd": "quest_fragments", "quest": quest}) or {}).get("runs")


    def reset_quest_fragments(ctx):
        _game = ctx.game()
        for instance in ("", "client"):
            _game = ctx.game(instance)
            _game.rpc({"cmd": "quest_fragments_reset"})


    def client_quest_matches(ctx, quest, label):
        def matches():
            return ctx.synced_quest("client", quest) == ctx.synced_quest("", quest)
        ctx.wait_for(f"[client] {label} as on the host", matches, 5)


    def arrival_story_events(ctx, instance, actor=0x14):
        """The location change story events raised in that game for an actor (this game's player by default) since the
        last call (harness story_trace), as (old location, new location, event id)."""
        _game = ctx.game(instance)
        return [event[:3] for event in (_game.rpc({"cmd": "story_trace"}) or {}).get("captured", []) if event[3] == actor]


    def story_event_outcome(ctx, instance, event_id, since):
        """Whether this game's story manager started a quest for that event; None until it has handled it."""
        for event in ctx.engine_events(instance, since)[0]:
            if event["kind"] == "story_event_processed" and event["event"] == event_id:
                return event["started_quest"]
        return None


    def story_trace_read(ctx, instance):
        """That game's story trace since the last read: the location changes raised there (old, new, event id, actor) and
        the story manager's outcomes (event id, code, node or quest; STORY_NODE_PASSED, STORY_QUEST_STARTED)."""
        _game = ctx.game(instance)
        response = _game.rpc({"cmd": "story_trace"}) or {}
        return response.get("captured", []), response.get("outcomes", [])


    def story_processing_order(ctx, instance, event_ids, since):
        """The order that game's story manager finished those events in, as [(event id, started a quest)]."""
        return [(e["event"], e["started_quest"]) for e in ctx.engine_events(instance, since)[0]
                if e["kind"] == "story_event_processed" and e["event"] in event_ids]


    def crafting_entries(ctx, instance, path):
        """A crafting menu list's field (CRAFTING_LIST): its entries, the selection."""
        return ctx.menu_var(instance, "Crafting Menu", f"{CRAFTING_LIST}.{path}")


    def open_crafting_station(ctx, instance, station, what):
        """That game's player uses a crafting station as a player does: from a side whose crosshair is on it, E opens its
        menu, E takes its item list (InventoryLists' state 2)."""
        tag = "[host]" if instance == "" else "[guest]"
        for bearing in (0, 90, 180, 270):
            ctx.place_before(instance, station, 90, bearing)
            ctx.face(instance, station)
            if ctx.camera(instance).get("crosshair") == station:
                break
        else:
            raise ctx.failure(f"{tag} no side of the {what} puts the crosshair on it: {ctx.camera_report(instance)}")
        ctx.tap_key(instance, ACTIVATE_KEY)
        ctx.wait_for(f"{tag} the {what}'s menu opens", lambda: "Crafting Menu" in ctx.open_menus(instance), 10)
        lists = lambda: ctx.menu_var(instance, "Crafting Menu", f"{CRAFTING_LISTS}.currentState")
        ctx.wait_for(f"{tag} the menu is in, its categories take the keys", lambda: lists() == 1, 5)
        ctx.tap_key(instance, ACTIVATE_KEY)
        ctx.wait_for(f"{tag} the item list takes the keys", lambda: lists() == 2, 5)


    def select_crafting_entry(ctx, instance, target):
        """Up or Down until the list's selection is at that index (the list does not wrap)."""
        tag = "[host]" if instance == "" else "[guest]"
        for _ in range(int(ctx.crafting_entries(instance, "entryList.length") or 0)):
            at = int(ctx.crafting_entries(instance, "selectedIndex"))
            if at == target:
                return
            ctx.tap_key(instance, DOWN_KEY if at < target else UP_KEY)
            ctx.wait_for(f"{tag} the selection moves", lambda: int(ctx.crafting_entries(instance, "selectedIndex")) != at, 3)


    def leave_crafting_station(ctx, instance, what):
        """Tab out of a crafting menu, answering its boxes with their first button (alchemy's "Created ... Discovered
        effects" note and "Quit alchemy?"), and stand up."""
        tag = "[host]" if instance == "" else "[guest]"
        for _ in range(6):
            menus = ctx.open_menus(instance)
            if "Crafting Menu" not in menus:
                break
            if "MessageBoxMenu" in menus:
                ctx.presses_box_button(instance, 0)
                ctx.wait_for(f"{tag} the box closes", lambda: "MessageBoxMenu" not in ctx.open_menus(instance), 3)
                continue
            ctx.tap_key(instance, TAB_KEY)
            ctx.wait_for(f"{tag} the menu closes or asks to quit",
                     lambda: "Crafting Menu" not in ctx.open_menus(instance) or "MessageBoxMenu" in ctx.open_menus(instance), 3)
        # An alchemy lab's exit animation runs past 10 s.
        ctx.wait_for(f"{tag} stands up from the {what}", lambda: ctx.sit_state(instance, 0x14) == 0, 30)


    def created_by_base(ctx, instance):
        """The references that game created, counted by base."""
        return collections.Counter(r["base"] for i, r in ctx.world_state(instance).items() if i >= CREATED_ID)


    def steam_status(ctx, instance):
        """The game's Steam account and session (harness status `steam`)."""
        _game = ctx.game(instance)
        return (_game.read_status() or {}).get("steam") or {}


    def client_steam_opens(ctx, url):
        """The client player's Steam opens a steam:// link, as clicking Join on an invite does: with the game running,
        Steam hands it the lobby (GameLobbyJoinRequested_t)."""
        _game = ctx.game('client')
        user = ctx.instance_user(_game=_game)
        if not user:
            raise ctx.failure("the client game shares the host's Steam account: no second account (COOP_CLIENT_USER)")
        result = ctx.sudo("ip", "netns", "exec", CLIENT_NETNS, "sudo", "-u", user, "-H", "env", f"DISPLAY={ctx.game_env(_game=_game)['DISPLAY']}",
                      f"DBUS_SESSION_BUS_ADDRESS=unix:path={ctx.user_bus(user)}", "/usr/games/steam", url)
        log(f"[client] Steam opened {url}" + (f" ({result.stderr.strip()[-200:]})" if result.returncode else ""))


    def steam_accounts(ctx):
        """(host's SteamID64, client's): two accounts, or the scenario cannot run."""
        host_id, guest_id = ctx.steam_status("").get("id"), ctx.steam_status("client").get("id")
        if not host_id or not guest_id:
            raise ctx.failure(f"a game reports no Steam account (host {host_id}, client {guest_id})")
        if host_id == guest_id:
            raise ctx.failure(f"both games signed into the same Steam account {host_id}: the client needs its own "
                              "(docs/design/testing.md, \"Two Steam accounts\")")
        return host_id, guest_id


    def plate_state(ctx, instance):
        _game = ctx.game(instance)
        return ((_game.rpc({"cmd": "script_state", "object": KOLBJORN_PLATE, "script": PLATE_SCRIPT}) or {}).get("state") or "").lower()


    def ref_enabled(ctx, instance, ref):
        _game = ctx.game(instance)
        return (_game.rpc({"cmd": "ref_enabled", "object": ref}) or {}).get("enabled")


    def item_count(ctx, instance, holder, item):
        _game = ctx.game(instance)
        return (_game.rpc({"cmd": "item_count", "object": holder, "item": item}) or {}).get("count")


    def client_on_host(ctx):
        return ctx.wait_on("", "shows the client's character", ctx.other_player)


    def carried_gold(ctx):
        """What each player carries: each game's own player and its copy of the other's, by name."""
        carried = {}
        for name in ("", "client"):
            for who, holder in (("player", 0x14), ("copy", ctx.player_copy(name))):
                if holder is not None:
                    carried[f"{name or 'host'} {who}"] = ctx.item_count(name, holder, GOLD)
        return carried


    def party_gold(ctx, timeout=10):
        """The party's gold, once every player carries the same (the purse's last change reached every game)."""
        agreed = lambda: (counts := set(ctx.carried_gold().values())) and len(counts) == 1 and [counts.pop()]
        try:
            return ctx.wait_for("every player carries the same gold", agreed, timeout)[0]
        except TestFailure as error:
            raise ctx.failure(f"{error}: {ctx.carried_gold()}") from error


    def wait_party_gold(ctx, description, count, timeout=10):
        """Every player carries count gold, the failure naming what each carries."""
        try:
            ctx.wait_for(description, lambda: set(ctx.carried_gold().values()) == {count}, timeout)
        except TestFailure as error:
            raise ctx.failure(f"{error}: {ctx.carried_gold()}") from error


    def set_party_gold(ctx, count, wait=True):
        """The party carries count gold: the host's console changes its player's, which is the purse."""
        have = ctx.item_count("", 0x14, GOLD)
        if have != count:
            ctx.console_on("", f"player.{'additem' if count > have else 'removeitem'} f {abs(count - have)}")
        if wait:
            ctx.wait_party_gold(f"every player carries the party's {count} gold", count)


    def copy_equips(ctx, instance, copy, since):
        """The items that game equipped on the copy itself since that trail number (EquipManager's "Copy X equip Y: ours")."""
        lines, _ = ctx.log_trail(instance, since)
        prefix = f"Copy {copy:X} equip "
        return {int(line["text"][len(prefix):].split(":")[0], 16) for line in lines
                if line["text"].startswith(prefix) and line["text"].endswith(": ours")}


    def boss_trigger_state(ctx, instance):
        _game = ctx.game(instance)
        return ((_game.rpc({"cmd": "script_state", "object": SOLJUND_TRIGGER, "script": SOLJUND_SCRIPT}) or {}).get("state") or "").lower()


    def fire_hits(ctx, since):
        """Whom the host's game saw the statue's fire hit (harness engine_events), from event `since` on."""
        events, _ = ctx.engine_events("", since)
        return {e["target"] for e in events if e.get("kind") == "hit" and e.get("source") == SOLJUND_FIRE}


    def claw_door_state(ctx, instance, ref):
        _game = ctx.game(instance)
        script = "HallofStoriesKeyholeScript" if ref == CLAW_KEYHOLE else "HallofStoriesDiskScript"
        return ((_game.rpc({"cmd": "script_state", "object": ref, "script": script}) or {}).get("state") or "").lower()


    def before_claw_door(ctx):
        ctx.both_go(CLAW_DOOR_CELL)
        ctx.place_before("", CLAW_KEYHOLE, 450, bearing=90)
        ctx.place_before("client", CLAW_KEYHOLE, 300, bearing=90)


    def ring_looks_alike(ctx, ring):
        """The ring's pose and record on the client are the host's (world_state's pose: its nodes' rotations)."""
        return not reference_differences(ctx.world_state("", [ring])[ring], ctx.world_state("client", [ring])[ring])


    def close_message_box(ctx, instance):
        """The player points at the box's one button and clicks it (vanilla message boxes take only the mouse, and a
        button takes a click once the cursor came onto it)."""
        ctx.move_cursor(instance, (MESSAGE_BOX_OK[0], MESSAGE_BOX_OK[1] + 80))
        ctx.move_cursor(instance, MESSAGE_BOX_OK)
        ctx.click(instance)
        ctx.wait_for(f"[{instance or 'host'}] the message box is closed", lambda: "MessageBoxMenu" not in ctx.menus(instance), 5)


    def knows_word(ctx, instance, word):
        return ctx.player_command(instance, {"cmd": "form", "form": word}).get("known")


    def word_state(ctx, instance, word):
        """Whether that game's player knows the word and has it unlocked."""
        form = ctx.player_command(instance, {"cmd": "form", "form": word})
        return bool(form.get("known")), bool(form.get("unlocked"))


    def forget_words(ctx, words):
        """Neither game's player knows or has unlocked these words (a forgotten word is not the party's to share)."""
        for name in ("", "client"):
            for word in words:
                ctx.player_command(name, {"cmd": "form_known", "form": word, "known": False, "unlocked": False})


    def dragon_souls(ctx, instance):
        return ctx.actor_value(instance, 0x14, DRAGON_SOULS)


    def own_race(ctx, instance):
        """The race of that game's own player, as its game has it."""
        _game = ctx.game(instance)
        return next(((s.get("identity") or {}).get("race") for s in ((_game.read_status() or {}).get("world") or {}).values()
                     if s.get("role") == "own" and (s.get("identity") or {}).get("base") == 7), None)


    def screen(ctx, instance):
        """What a cutscene does to that game's screen: its enabled controls, image-space modifiers and camera shake."""
        return ctx.player_command(instance, {"cmd": "screen"})


    def script_call(ctx, instance, klass, function, args=(), on=None):
        """That game queues the call as a quest's script would (no player's action behind it)."""
        request = {"cmd": "papyrus_call", "class": klass, "function": function, "args": list(args)}
        if on is not None:
            request["self"] = on
        ctx.player_command(instance, request)


    def player_script_call(ctx, instance, klass, function, args=(), on=None):
        """That game queues the call as a script its own player's action started (a lever, fishing, a picked line)."""
        request = {"cmd": "papyrus_call", "class": klass, "function": function, "args": list(args), "player_action": True}
        if on is not None:
            request["self"] = on
        ctx.player_command(instance, request)


    def screen_marker(ctx, instance, other):
        """A party call after the calls before it (a shake, from a script with no player behind it): once `other`
        shakes, any of the earlier calls sent to it has arrived and run too (one session, in order)."""
        ctx.wait_for(f"[{other or 'host'}] no shake before the marker", lambda: ctx.screen(other)["shake"]["seconds"] <= 0, 5)
        ctx.script_call(instance, "Game", "ShakeCamera", [None, 0.5, 2.0])
        ctx.wait_for(f"[{other or 'host'}] the marker's shake arrives", lambda: ctx.screen(other)["shake"]["seconds"] > 0, 5)


    def party_controls(ctx, state, what):
        """Both games' controls are `state` (a ControlMap mask; None: anything but all on)."""
        for name in ("", "client"):
            ctx.wait_for(f"[{name or 'host'}] controls {what}",
                     lambda name=name: (ctx.screen(name)["controls"] != ALL_CONTROLS) if state is None else ctx.screen(name)["controls"] == state, 5)


    def needs_quest(ctx, quest, label, save):
        """The duo was booted from the save the scenario needs: its quest is there to drive."""
        if not ctx.quest_snapshot("", quest).get("flags", 0) & 1 and not ctx.quest_snapshot("", quest).get("done"):
            raise ctx.failure(f"{label} is not under way here: boot the duo with `duo --save {save}`")


    def mirrored_here(ctx, instance, mark, call):
        """That game ran another game's party call `call` (Class.Function) on its own player since `mark`."""
        return any(f"another game's script runs {call} here" in line["text"] for line in ctx.log_trail(instance, mark)[0])


    def hud_messages(ctx, instance):
        """The HUD's latest notifications on that game, the newest last (the harness's hud_messages)."""
        return ctx.player_command(instance, {"cmd": "hud_messages"}).get("messages") or []


    def box_button(ctx, instance, index):
        """The open message box's button `index`, in window pixels: the movie centres the box on its stage (the test games'
        1280x720 window) and places each button by its centre (MessageBox.as, reference/plugins/skyrimui)."""
        def at(path):
            value = ctx.menu_var(instance, "MessageBoxMenu", f"_root.MessageMenu.{path}")
            if value is None:
                raise ctx.failure(f"[{instance or 'host'}] the message box has no {path}")
            return value
        return (round(at("_x") + at("Buttons._x") + at(f"Buttons.Button{index}._x")),
                round(at("_y") + at("Buttons._y") + at(f"Buttons.Button{index}._y")))


    def presses_box_button(ctx, instance, index):
        """The player points at the box's button and clicks it (vanilla boxes take the mouse)."""
        button = ctx.box_button(instance, index)
        ctx.move_cursor(instance, (button[0], button[1] + 40))
        ctx.move_cursor(instance, button)
        ctx.click(instance)


    def script_answer(ctx, instance, klass, function, args=(), on=None, player_action=False):
        """That game's script call, as a quest's (or its player's action's), and what it returned."""
        request = {"cmd": "papyrus_call", "class": klass, "function": function, "args": list(args), "player_action": player_action}
        if on is not None:
            request["self"] = on
        call = ctx.player_command(instance, request)["id"]
        return ctx.wait_for(f"[{instance or 'host'}] {klass}.{function} returns",
                        lambda: (r := ctx.player_command(instance, {"cmd": "papyrus_result", "id": call})).get("done") and [r["value"]], 5)[0]


    def blessings(ctx, instance):
        """The shrine blessings on that game's player."""
        _game = ctx.game(instance)
        effects = (((_game.read_status() or {}).get("world") or {}).get("20") or {}).get("effects") or []
        return {e.get("spell") for e in effects} & {blessing for _, blessing in SHRINES.values()}


    def host_quest(ctx, quest):
        _game = ctx.game('')
        response = _game.rpc({"cmd": "quest", "quest": quest}) or {}
        return response.get("current_stage"), {stage for stage, done in response.get("stages", []) if done}


    def is_current_package(ctx, instance, actor, package):
        """The condition GetIsCurrentPackage on the actor in that game, as a dialogue line reads it."""
        _game = ctx.game(instance)
        return (_game.rpc({"cmd": "is_current_package", "actor": actor, "package": package}) or {}).get("result")


    def ai_state(ctx, instance, actor):
        _game = ctx.game(instance)
        return (((_game.read_status() or {}).get("world") or {}).get(str(actor)) or {}).get("ai_state") or {}


    def running_package(ctx, instance, actor):
        _game = ctx.game(instance)
        return ((((_game.read_status() or {}).get("world") or {}).get(str(actor)) or {}).get("ai") or {}).get("running_package", 0)


    def faction_rank(ctx, instance, actor, faction):
        """The condition GetFactionRank on the actor in that game (-1: not a member), as a dialogue line reads it."""
        _game = ctx.game(instance)
        response = _game.rpc({"cmd": "faction_rank", "actor": actor, "faction": faction}) or {}
        if not response.get("ok"):
            raise ctx.failure(f"[{instance or 'host'}] faction_rank {actor:X} {faction:X}: {response.get('error')}")
        return response["result"]


    def container_items(ctx, instance, chest):
        """What the chest holds in that game: {item: count}, none of 0."""
        return {i["item"]: i["count"] for i in ctx.player_command(instance, {"cmd": "container_items", "object": chest})["items"] if i["count"]}

