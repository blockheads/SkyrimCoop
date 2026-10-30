"""Fixtures for the co-op harness."""
import json
import math
import time
from .comparisons import is_close
from .common import (
    CLEAR_WEATHER,
    CREATED_ID,
    HOME_CELL,
    HOME_FILE,
    HOME_TOLERANCE,
    PLAYER_VALUES,
    TEST_ITEMS,
)
from .errors import TestFailure
from .comparisons import distance
from .utilities import (
    atomic_write,
    log,
)


class Fixtures:
    def world_sync_problems(ctx):
        host, guest = ctx.game_state(""), ctx.game_state("client")
        problems = []
        if host.get("weather") != guest.get("weather"):
            problems.append(f"weather: host {host.get('weather')}, guest {guest.get('weather')}")
        if not ctx.same_calendar() or not is_close(host.get("time", {}).get("hour"), guest.get("time", {}).get("hour"), 0.05):
            problems.append(ctx.calendars("calendar differs"))
        return problems

    def reset_world(ctx, home):
        world = home["world"]
        if world.get("live"):
            ctx.wait_for("baseline: the guest follows the live story world", lambda: not ctx.world_sync_problems(), 10)
            return
        ctx.console_on("", f"fw {world['weather']:X}")
        for field, global_name in (("year", "gameyear"), ("month", "gamemonth"), ("day", "gameday"),
                                   ("hour", "gamehour"), ("scale", "timescale")):
            ctx.console_on("", f"set {global_name} to {world['time'][field]:.8f}")

    def record_world_home(ctx, preserve_current=False):
        # Saved stories keep their weather and hour; empty test worlds use clear noon. The guest follows normal sync.
        time_ = ctx.calendar("")
        weather = ctx.game_state("").get("weather") if preserve_current else CLEAR_WEATHER
        world = {"weather": weather, "time": {key: time_[key] for key in ("year", "month", "day", "hour", "scale")},
                 "raw_days_at_record": time_.get("raw_days")}
        if preserve_current:
            world["live"] = True
        else:
            world["time"]["hour"] = 12.0
        ctx.reset_world({"world": world})
        ctx.wait_for("baseline: the guest follows the host's weather and calendar", lambda: not ctx.world_sync_problems(), 10)
        return world

    def save_home(ctx, cell_name=HOME_CELL):
        """Records the world fixture and each player's placement, equipment, level and maximum values."""
        populated = cell_name is None
        world = ctx.record_world_home(preserve_current=populated)
        for name in ("", "client"):
            me = ctx.player_snapshot(name)
            placement = me.get("placement") or {}
            if not placement.get("position"):
                raise ctx.failure(f"{name or 'host'}: no player snapshot to record the baseline from")
            home = {"position": placement["position"], "heading": placement["rotation"][2],
                    "equipment": sorted(i for i in me.get("equipment", []) if i not in TEST_ITEMS),
                    "level": ctx.game_state(name).get("level"), "cell": placement.get("cell"), "cell_name": cell_name,
                    "values": {value: me.get("values", {}).get(f"max_{value}") for value in PLAYER_VALUES},
                    "world": world, "npcs": sorted(ctx.owned_npcs(name)) if populated else [],
                    "copies": sorted(ctx.remote_npcs(ctx.actors_on(name))) if populated else []}
            atomic_write(ctx.game(name).control / HOME_FILE, json.dumps(home) + "\n")
        log("Recorded both players' baseline (home.json)")

    def load_home(ctx, instance):
        _game = ctx.game(instance)
        try:
            return json.loads((_game.control / HOME_FILE).read_text())
        except (OSError, ValueError):
            return None

    def upgrade_home(ctx):
        homes = {name: ctx.load_home(name) for name in ("", "client")}
        if all(home and home.get("world") and home.get("values") for home in homes.values()):
            return
        if not all(homes.values()):
            raise ctx.failure("no player baseline: start duo --keep-running first")
        world = ctx.record_world_home()
        for name, home in homes.items():
            me = ctx.player_snapshot(name)
            home.update(world=world, cell=me.get("placement", {}).get("cell"), cell_name=HOME_CELL,
                        values={value: me.get("values", {}).get(f"max_{value}") for value in PLAYER_VALUES})
            atomic_write(ctx.game(name).control / HOME_FILE, json.dumps(home) + "\n")
        log("Upgraded home.json with the world fixture and player values")

    def owned_npcs(ctx, instance):
        return [int(a) for a in ctx.local_player(instance).get("local_actors", {})]

    def baseline_npc_additions(ctx, actors, home, field):
        """Story saves can load more placed actors; new test-created actors still need cleanup."""
        return sorted(actor for actor in set(actors) - set(home.get(field, []))
                      if not home["world"].get("live") or actor >= CREATED_ID)

    def foreign_in_hands(ctx, state, home):
        """Something in the hands that the baseline does not wear (a test's sword or spell)."""
        return any(item and item not in home["equipment"] for item in state.get("hands") or [])

    def reset_player(ctx, instance, home):
        _game = ctx.game()
        me = ctx.player_snapshot(instance)
        state = me.get("state") or {}
        _game = ctx.game(instance)
        # First person, and no idle camera orbiting the player: what `watch` shows is the player's view.
        ctx.run_console('setini "fAutoVanityModeDelay:Camera" 1000000', _game=_game)
        # Subtitles on in a game started before the prefs had them (prepare_prefix).
        ctx.run_console('setini "bDialogueSubtitles:Interface" 1', _game=_game)
        ctx.run_console('setini "bGeneralSubtitles:Interface" 1', _game=_game)
        _game.rpc({"cmd": "first_person"})
        for button in sorted(ctx.game(instance).held_buttons):
            ctx.release(instance, button)
        for source in (0, 1):
            _game.rpc({"cmd": "interrupt", "caster": 0x14, "source": source})
        if state.get("sneaking"):
            ctx.sneak(instance, False)
        if state.get("weapon_drawn") or state.get("weapon_state"):
            # The sheathe action, as a player's: that is what copies replay (setting the engine flag
            # directly leaves them drawn).
            ctx.sheathe(instance)
        if sorted(me.get("equipment", [])) != home["equipment"] or ctx.foreign_in_hands(state, home):
            ctx.run_console("player.unequipall", _game=_game)
            for item in home["equipment"]:
                ctx.run_console(f"player.equipitem {item:08X}", _game=_game)
        if home.get("level") and ctx.game_state(instance).get("level") != home["level"]:
            ctx.run_console(f"player.setlevel {home['level']}", _game=_game)
        values = me.get("values") or {}
        for value in PLAYER_VALUES:
            target = home.get("values", {}).get(value)
            if target is not None and not is_close(values.get(f"max_{value}"), target, 0.5):
                ctx.run_console(f"player.setav {value} {target:g}", _game=_game)
            if values.get(value, 0) < (target if target is not None else values.get(f"max_{value}", 0)) - 0.5:
                ctx.run_console(f"player.restoreav {value} 100000", _game=_game)
        if (me.get("ai") or {}).get("combat_target"):
            ctx.run_console("player.stopcombat", _game=_game)
        position = (me.get("placement") or {}).get("position")
        goal = home["position"]
        if not position or distance(position, goal) > HOME_TOLERANCE:
            # Lifted a little and dropped onto the floor there (see place()).
            ctx.run_console(f"player.setpos z {goal[2] + 20:.0f}", _game=_game)
            ctx.run_console(f"player.setpos x {goal[0]:.0f}", _game=_game)
            ctx.run_console(f"player.setpos y {goal[1]:.0f}", _game=_game)
        ctx.run_console(f"player.setangle z {math.degrees(home['heading']) % 360:.0f}", _game=_game)
        ctx.run_console("player.setangle x 0", _game=_game)

    def reset_baseline(ctx):
        if ctx.game_state("client").get("online") is False:
            ctx.rejoin_client()
        home = ctx.load_home("")
        # A conversation still running on an NPC about to go would be removed under it (the guest crash in 37541,
        # 2026-10-29): the play path for that is dialogue-npc-removed-mid-conversation, not the baseline.
        if any(ctx.dialogue(name).get("speaker") for name in ("", "client")):
            ctx.close_dialogues()
        # Home first: the NPCs around wherever a scenario left the party are the world's, not the scenario's.
        for name in ("", "client"):
            place = ctx.load_home(name)
            if place.get("cell") and ctx.player_cell(name) != place["cell"]:
                if not place.get("cell_name"):
                    raise ctx.failure("baseline cell differs and has no coc name; boot duo with --cell")
                ctx.console_on(name, f"coc {place['cell_name']}")
                ctx.wait_for(f"[{name or 'guest'}] back in the baseline cell",
                         lambda name=name, place=place: ctx.player_cell(name) == place["cell"], 60)
        for name in ("", "client"):
            for npc in ctx.baseline_npc_additions(ctx.owned_npcs(name), ctx.load_home(name), "npcs"):
                if npc >= CREATED_ID:
                    ctx.console_on(name, f"{npc:08X}.disable")
                    ctx.console_on(name, f"{npc:08X}.markfordelete")
                else:  # a plugin's NPC (staged, or one that walked in) goes back where the game placed it, never deleted
                    ctx.player_command(name, {"cmd": "move_actor", "actor": npc, "home": True})
        for name in ("", "client"):
            ctx.reset_player(name, ctx.load_home(name))
        ctx.reset_world(home)

    def check_baseline(ctx):
        """What differs from the baseline in either game (empty: a scenario can start)."""
        problems = []
        world = ctx.load_home("")["world"]
        if world.get("live"):
            problems.extend(ctx.world_sync_problems())
        for name in ("", "client"):
            game = name or "host"
            home = ctx.load_home(name)
            me = ctx.player_snapshot(name)
            state = me.get("state") or {}
            values = me.get("values") or {}
            current = ctx.game_state(name)
            if not world.get("live") and current.get("weather") != world["weather"]:
                problems.append(f"{game}: weather {current.get('weather')}, baseline {world['weather']}")
            for field in ("year", "month", "day", "hour", "scale"):
                if not world.get("live") and not is_close(current.get("time", {}).get(field), world["time"][field], 0.05):
                    problems.append(f"{game}: calendar {field} {current.get('time', {}).get(field)}, baseline {world['time'][field]}")
            if home.get("cell") and ctx.player_cell(name) != home["cell"]:
                problems.append(f"{game}: not in the baseline cell {home['cell']:X}")
            if any(state.get("casting") or []):
                problems.append(f"{game}: player still casting {state['casting']}")
            if ctx.foreign_in_hands(state, home):
                problems.append(f"{game}: player holds {state['hands']} in the hands")
            if state.get("weapon_drawn"):
                problems.append(f"{game}: player's hands are raised")
            if sorted(me.get("equipment", [])) != home["equipment"]:
                problems.append(f"{game}: player wears {sorted(me.get('equipment', []))}, baseline {home['equipment']}")
            for value in PLAYER_VALUES:
                if (target := home.get("values", {}).get(value)) is not None and not is_close(values.get(f"max_{value}"), target, 0.5):
                    problems.append(f"{game}: maximum {value} {values.get(f'max_{value}')}, baseline {target}")
                if values.get(value, 0) < values.get(f"max_{value}", 0) - 1:
                    problems.append(f"{game}: player {value} {values.get(value):.0f} of {values.get(f'max_{value}'):.0f}")
            if (me.get("ai") or {}).get("combat_target"):
                problems.append(f"{game}: player in combat with {me['ai']['combat_target']:08X}")
            if home.get("level") and ctx.game_state(name).get("level") != home["level"]:
                problems.append(f"{game}: player level {ctx.game_state(name).get('level')}, baseline {home['level']}")
            position = (me.get("placement") or {}).get("position")
            if not position or distance(position, home["position"]) > HOME_TOLERANCE:
                problems.append(f"{game}: player at {[round(v) for v in position or []]}, "
                                f"baseline {[round(v) for v in home['position']]}")
            if npcs := ctx.baseline_npc_additions(ctx.owned_npcs(name), home, "npcs"):
                problems.append(f"{game}: still owns NPCs {[f'{n:08X}' for n in npcs]}")
            actors = ctx.actors_on(name)
            if ctx.other_player(actors) is None:
                problems.append(f"{game}: does not show the other player")
            if copies := ctx.baseline_npc_additions(ctx.remote_npcs(actors), home, "copies"):
                problems.append(f"{game}: still shows NPC copies {[f'{n:08X}' for n in copies]}")
        if not problems:
            problems = ctx.check_desync(samples=1)
        return problems

    def settle_baseline(ctx, timeout=8.0):
        """Apply the baseline once, then wait for the commanded state; report anything left different."""
        try:
            ctx.reset_baseline()
            deadline = time.monotonic() + timeout
            while True:
                problems = ctx.check_baseline()
                if not problems or time.monotonic() >= deadline:
                    return problems
                ctx.check_for_crash()
                time.sleep(0.1)
        except TestFailure as error:  # e.g. a game that crashed: the scenario before it is to blame
            return [f"reset failed: {error}"]
