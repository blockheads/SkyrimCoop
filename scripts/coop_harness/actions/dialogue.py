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
    CAMERA_STATES,
    CHARACTER_LAYER,
    DOWN_KEY,
    FOLLOWER_QUEST,
    TAB_KEY,
    UP_KEY,
)
from ..errors import TestFailure
from ..comparisons import (
    body_report,
    distance,
)
from ..utilities import log


class DialogueActions:
    def place(ctx, instance, ref, goal, current):
        """Moves `ref` to `goal` in an instance's game by lifting it 100 units, sliding it over and
        letting it drop onto the floor there. Separate setpos x/y/z at floor height can slide it into
        furniture or under the floor, and it then falls through the world."""
        ctx.console_on(instance, f"{ref}.setpos z {max(current[2], goal[2]) + 100:.0f}")
        ctx.console_on(instance, f"{ref}.setpos x {goal[0]:.0f}")
        ctx.console_on(instance, f"{ref}.setpos y {goal[1]:.0f}")


    def voice(ctx, instance, form_id):
        """The actor's voice line in that game (mod.voice): callback state, seconds left, sound id."""
        _game = ctx.game(instance)
        snapshot = (((_game.read_status() or {}).get("world") or {}).get(str(form_id)) or {})
        return (snapshot.get("mod") or {}).get("voice") or {}


    def package_type(ctx, instance, form_id):
        """The type of the actor's current package in that game (mod.package_type), -1 without one."""
        _game = ctx.game(instance)
        snapshot = (((_game.read_status() or {}).get("world") or {}).get(str(form_id)) or {})
        return (snapshot.get("mod") or {}).get("package_type")


    def subtitle(ctx, instance, form_id):
        """The subtitle that game shows over the actor (mod.subtitle), "" if none."""
        _game = ctx.game(instance)
        snapshot = (((_game.read_status() or {}).get("world") or {}).get(str(form_id)) or {})
        return (snapshot.get("mod") or {}).get("subtitle") or ""


    def line_playing(ctx, instance, form_id):
        """Started (UpdateSoundCallback set its timer) and not over."""
        line = ctx.voice(instance, form_id)
        return line.get("callback") == -1 and (line.get("seconds") or -1) > 0


    def line_over(ctx, instance, form_id):
        """No time left, and nothing queued with a sound behind it."""
        line = ctx.voice(instance, form_id)
        return (line.get("seconds") or -1) <= 0 and not (line.get("callback") == 3 and line.get("sound", -1) != -1)


    def dialogue(ctx, instance):
        """That game's conversation: {"menu": the Dialogue Menu shows, "speaker", "options": [...]}."""
        return ctx.player_command(instance, {"cmd": "dialogue"})


    def talking_to(ctx, instance, speaker):
        """That game's dialogue menu shows, with its player talking to `speaker`."""
        state = ctx.dialogue(instance)
        return state.get("menu") and state.get("speaker") == speaker


    def menu_closed(ctx, instance):
        return not ctx.dialogue(instance).get("menu")


    def list_shown(ctx, instance):
        """That game's dialogue menu shows its list: the arrow keys move the highlight (eMenuState TOPIC_LIST_SHOWN)."""
        return (ctx.dialogue(instance).get("swf") or {}).get("state") == 1


    def cursor(ctx, instance):
        """The topic highlighted in that game's dialogue menu (0: none)."""
        return (ctx.dialogue(instance).get("swf") or {}).get("cursor") or 0


    def cursor_index(ctx, instance):
        """The highlighted option's place in that game's menu list (-1: none)."""
        return (ctx.dialogue(instance).get("swf") or {}).get("cursor_index", -1)

    def lists_topic(ctx, instance, topic):
        """That game's conversation lists the topic (its own option, or another player's added to it)."""
        return topic in [o["topic"] for o in ctx.dialogue(instance).get("options", [])]


    def topics_shown(ctx, instance):
        """The topics that game's conversation lists, in order."""
        return [o["topic"] for o in ctx.dialogue(instance).get("options", [])]


    def options_shown(ctx, instance):
        """That game's dialogue list as (topic, line, text), in menu order."""
        return [(o["topic"], o["info"], o["text"]) for o in ctx.dialogue(instance).get("options", [])]


    def client_shows_hosts_list(ctx, what, timeout=10):
        """The client's menu shows exactly the host's list: its topics, lines and texts, in its order (every game's menu
        shows the host's game's list; docs/design/shared-dialogue.md). Returns it."""
        def same():
            host = ctx.options_shown("")
            return host and ctx.options_shown("client") == host and host
        try:
            return ctx.wait_for(f"[client] the menu shows the host's list {what}", same, timeout)
        except TestFailure:
            raise ctx.failure(f"[client] the menu does not show the host's list {what}: client {ctx.options_shown('client')}, "
                              f"host {ctx.options_shown('')}")


    def info_shown(ctx, instance, topic):
        """The line that game's conversation has for the topic (0: not listed)."""
        return next((o["info"] for o in ctx.dialogue(instance).get("options", []) if o["topic"] == topic), 0)


    def menu_takes_pick(ctx, instance):
        """That game's dialogue menu shows its list and takes a pick: the SWF's bAllowProgress, set 750 ms after the
        line's voice is ready, and its list shown (DialogueMenu.as eMenuState TOPIC_LIST_SHOWN)."""
        swf = ctx.dialogue(instance).get("swf") or {}
        return swf.get("allow_progress") == 1 and swf.get("state") == 1


    def close_dialogues(ctx):
        """Every open conversation ends as its players leave it (Tab): a menu hidden from outside skips the SWF's close,
        which gives the HUD its subtitles back (DialogueMenu 51511). The last one out ends the host's, which may run
        without its menu."""
        for instance in ("client", ""):
            if ctx.dialogue(instance).get("menu"):
                ctx.leave_dialogue(instance)
        ctx.wait_for("[host] its conversation ends", lambda: not ctx.dialogue("").get("speaker"), 10)

    def leave_dialogue(ctx, instance):
        """That game's player leaves its conversation (Tab) once its menu is past the greeting, where Tab only skips the
        line (DialogueMenu.as onCancelPress, SHOW_GREETING), and its menu closes."""
        def past_greeting():
            state = ctx.dialogue(instance)
            return not state.get("menu") or (state.get("swf") or {}).get("state", -1) > 0
        ctx.wait_for(f"[{instance or 'host'}] its menu is past the greeting", past_greeting, 10)
        # Ended meanwhile (the last one out, a goodbye): Tab outside the menu would open the Tween menu.
        if ctx.menu_closed(instance):
            return
        ctx.tap_key(instance, TAB_KEY)
        ctx.wait_for(f"[{instance or 'host'}] Tab closes its menu", lambda: ctx.menu_closed(instance), 10)


    def on_other_player(ctx, instance, within=30):
        """That game's player stands within `within` units of its copy of the other player."""
        _game = ctx.game(instance)
        status = _game.read_status() or {}
        actors = status.get("actors", {})
        other = ctx.other_player(actors)
        me = (status.get("player") or {}).get("position")
        return other is not None and me and distance(me[:2], actors[str(other)]["actual"][:2]) < within


    def look_apart(ctx):
        """Each player looks its own way, as players do before anything brings them together: the host up and
        north, the client down and south."""
        ctx.console_on("", "player.setangle x -30")
        ctx.console_on("", "player.setangle z 10")
        ctx.console_on("client", "player.setangle x 40")
        ctx.console_on("client", "player.setangle z 200")


    def camera_view(ctx, instance):
        """That game's camera: its mode, yaw, pitch and position."""
        v = ctx.player_command(instance, {"cmd": "camera", "target": 0})
        return v["state"], v["yaw"], v["pitch"], v["position"]


    def camera(ctx, instance):
        """That game's camera (harness `camera`): where it is and looks, what its view ray meets, whom it follows and
        whether a ray from their head reaches it (`sight`: a hit puts the camera behind or inside a wall or ceiling)."""
        return ctx.player_command(instance, {"cmd": "camera"})


    def hit_name(ctx, ray):
        """What a harness ray hit: its reference, or the cell's own collision."""
        return f"{ray['ref']:08X}" if ray.get("ref") else "the cell"


    def camera_report(ctx, instance, view=None):
        """One line on that game's camera, as a person would say what the screen shows."""
        v = view or ctx.camera(instance)
        state = v["state"]
        mode = CAMERA_STATES[state] if state < len(CAMERA_STATES) else f"state {state}"
        at = [round(x) for x in v["position"]]
        looking = v.get("looking") or {}
        if looking.get("hit"):
            meets = f"meets {ctx.hit_name(looking)} (layer {looking['layer']}) {looking['distance']:.0f} units ahead"
        else:
            meets = "meets nothing within 4096 units" if looking else "no ray"
        line = f"{mode} at {at}, yaw {v['yaw']:.0f} pitch {v['pitch']:.0f}, {meets}"
        if v.get("crosshair"):
            line += f", crosshair on {v['crosshair']:08X}"
        if v.get("following"):
            sight = v.get("sight")
            if sight is None:
                seen = "sight not cast (in its head, or no physics world)"
            elif sight.get("hit") and sight["layer"] == CHARACTER_LAYER:
                seen = f"sees its head past {ctx.hit_name(sight)} (an actor in the way, {sight['distance']:.0f} units from its head)"
            elif sight.get("hit"):
                seen = (f"BLOCKED by {ctx.hit_name(sight)} (layer {sight['layer']}) "
                        f"{sight['distance']:.0f} units from its head, {distance(v['followed_head'], v['position']):.0f} from the camera")
            else:
                seen = "sees its head"
            line += f"; follows {v['following']:08X}: {seen}"
        return line


    def spectating_stays_third_person(ctx, instance):
        """A spectating player switches to first person (the POV key): the view goes back to third person behind the
        watched player, not into its head."""
        ctx.player_command(instance, {"cmd": "first_person"})
        third_person = CAMERA_STATES.index("third person")
        ctx.wait_for(f"[{instance or 'host'}] the spectator's view goes back to third person",
                 lambda: ctx.camera(instance)["state"] == third_person, 3)
        ctx.camera_sees_followed(instance, "spectating, after the POV key")


    def screen_lines(ctx, instance):
        """What that game's screen shows of the party: its camera and every player's body there (own and copies)."""
        _game = ctx.game()
        who = instance or "host"
        try:
            lines = [f"  [{who}] camera: {ctx.camera_report(instance)}"]
        except (TestFailure, OSError, KeyError) as error:
            return [f"  [{who}] camera: none ({error})"]
        _game = ctx.game(instance)
        for form_id, snapshot in ((_game.read_status() or {}).get("world") or {}).items():
            if snapshot.get("player"):
                whose = "its player" if int(form_id) == 0x14 else f"copy {int(form_id):08X}"
                lines.append(f"  [{who}] {whose}: {body_report(snapshot)}")
        return lines


    def camera_sees_followed(ctx, instance, what):
        """The camera is in the open with whoever it follows: a ray from their head reaches it (not in a ceiling, wall or
        floor; an actor in between is fine). Fails naming what is in between."""
        view = ctx.camera(instance)
        sight = view.get("sight")
        if view.get("following") and sight and sight.get("hit") and sight["layer"] != CHARACTER_LAYER:
            raise ctx.failure(f"[{instance or 'host'}] {what}: the camera is cut off from {view['following']:08X}: "
                              f"{ctx.camera_report(instance, view)}")
        return view


    def same_view(ctx, what, degrees=2, units=15):
        """Both games show the same view: the camera in the same mode, at the same spot, turned the same way. The
        spot within 15 units: a follower goes where the leader arrived, and the leader settles a few units after."""
        seen = {}
        def check():
            (state_a, yaw_a, pitch_a, at_a), (state_b, yaw_b, pitch_b, at_b) = ctx.camera_view(""), ctx.camera_view("client")
            seen["views"] = (f"host {state_a} yaw {yaw_a:.1f} pitch {pitch_a:.1f} at {[round(a) for a in at_a]}, "
                             f"client {state_b} yaw {yaw_b:.1f} pitch {pitch_b:.1f} at {[round(a) for a in at_b]}")
            turn = abs((yaw_a - yaw_b + 180) % 360 - 180)
            return state_a == state_b and turn < degrees and abs(pitch_a - pitch_b) < degrees and distance(at_a, at_b) < units
        try:
            ctx.wait_for(what, check, 5)
        except TestFailure as e:
            raise ctx.failure(f"{e}: {seen.get('views')}") from None


    def stacked(ctx, what):
        """Both games show the two players on one spot: nobody pushed anybody off it."""
        for instance in ("client", ""):
            ctx.wait_for(f"[{instance or 'host'}] {what}", lambda instance=instance: ctx.on_other_player(instance), 5)


    def my_position(ctx, instance):
        _game = ctx.game(instance)
        return ((_game.read_status() or {}).get("player") or {}).get("position")


    def other_player_position(ctx, instance):
        """Where that game shows the other player (its copy)."""
        actors = ctx.actors_on(instance)
        other = ctx.other_player(actors)
        return actors[str(other)]["actual"] if other is not None else None


    def walk_off(ctx, walker, toward, what, *, _game=None):
        """The walker turns toward `toward` (x, y) and walks there until it is 100 units from the other player: off
        the stack (60), with a short clear way back, and stays off until its game takes it off the stack (half a second
        off the spot, TravelService::UpdateStack: one back on it sooner is still stacked). Nothing else follows from
        it: no menu opens, nobody travels."""
        _game = ctx.game(_game)
        _, mark = ctx.log_trail(walker, 1 << 62)
        start = ctx.my_position(walker)
        ctx.console_on(walker, f"player.setangle z {math.degrees(math.atan2(toward[0] - start[0], toward[1] - start[1])) % 360:.0f}")
        cells = {name: ctx.player_cell(name) for name in ("", "client")}
        ctx.press(walker, "forward")
        try:
            ctx.wait_for(f"[{walker or 'host'}] {what}",
                     lambda: distance(ctx.my_position(walker)[:2], ctx.other_player_position(walker)[:2]) > 100, 10, _game=_game)
        finally:
            ctx.release(walker, "forward")
        ctx.wait_for(f"[{walker or 'host'}] its game takes it off the stack",
                 lambda: any("Stack: 14 walked off" in line["text"] for line in ctx.log_trail(walker, mark)[0]), 5, _game=_game)
        for name in ("", "client"):
            if ctx.player_cell(name) != cells[name]:
                raise ctx.failure(f"[{name or 'host'}] walking off the stack took the party elsewhere")
            if not ctx.takes_input(name):
                raise ctx.failure(f"[{name or 'host'}] walking off the stack opened {sorted(set((_game.read_status() or {}).get('menus') or []) - {'HUD Menu'})}")


    def bumps_into_other(ctx, walker):
        """Off the stack, the walker collides again: walking into the other player (Forward held), it slides around
        it or is stopped by it, never through it: the two never come closer than their bodies allow."""
        ctx.face(walker, ctx.other_player(ctx.actors_on(walker)))
        seen = {"closest": float("inf")}
        rested = ctx.comes_to_rest(lambda: ctx.my_position(walker))
        def done():
            me, other = ctx.my_position(walker), ctx.other_player_position(walker)
            seen["gap"] = gap = distance(me[:2], other[:2])
            seen["closest"] = min(seen["closest"], gap)
            return seen["closest"] < 80 and (gap > 120 or rested())
        ctx.press(walker, "forward")
        try:
            try:
                ctx.wait_for(f"[{walker or 'host'}] walks into the other player, then past it or stopped by it", done, 10)
            except TestFailure as error:
                raise ctx.failure(f"{error} (closest {seen['closest']:.0f}, now {seen.get('gap', 0):.0f} units apart)")
        finally:
            ctx.release(walker, "forward")
        if seen["closest"] < 30:
            raise ctx.failure(f"[{walker or 'host'}] came within {seen['closest']:.0f} units of the other player: it walked "
                              "through it (collision should be back off the stack)")
        log(f"[{walker or 'host'}] came no closer than {seen['closest']:.0f} units to the other player")


    def subtitle_on_screen(ctx, instance):
        """Whose subtitle that game's HUD shows now (SubtitleManager's current speaker), 0 for none."""
        _game = ctx.game(instance)
        return (_game.read_status() or {}).get("subtitle_on_screen") or 0


    def in_conversation(ctx, instance, speaker):
        """That game's player talks to `speaker`, with its menu open or not (MenuTopicManager's speaker)."""
        return ctx.dialogue(instance).get("speaker") == speaker


    def follower_in_slot(ctx, slot=0):
        """The reference in the host's shared follower (0) or animal (1) slot (0: none)."""
        rows = ctx.player_command("", {"cmd": "quest", "quest": FOLLOWER_QUEST})["aliases"]
        return next((r["ref"] for r in rows if r["id"] == slot), 0)


    def dismiss_follower(ctx, npc, slot=0):
        """Dismisses the host's follower if it is that NPC (DialogueFollowerScript), emptying its slot."""
        if ctx.follower_in_slot(slot) == npc:
            ctx.player_command("", {"cmd": "papyrus_call", "class": "DialogueFollowerScript",
                                "function": "DismissAnimal" if slot else "DismissFollower",
                                "self": FOLLOWER_QUEST, "args": [] if slot else [0, 0]})
            ctx.wait_for(f"[host] {npc:08X} leaves the follower slot", lambda: ctx.follower_in_slot(slot) != npc, 15)


    def judged_line(ctx, instance, topic, speaker, player):
        """The line the speaker would say for the topic, judged for that player on that game (0: none)."""
        _game = ctx.game(instance)
        response = _game.rpc({"cmd": "judged_line", "topic": topic, "speaker": speaker, "player": player}) or {}
        if not response.get("ok"):
            raise ctx.failure(f"[{instance or 'host'}] judged_line: {response.get('error')}")
        return response["line"]


    def console_number(ctx, instance, command):
        """The number a console command answers with, its last ("HasSpell >> 1.00", "Player misc stat value 1.00")."""
        output = ctx.console_on(instance, command)
        numbers = re.findall(r"-?\d+(?:\.\d+)?", " ".join(output))
        if not numbers:
            raise ctx.failure(f"[{instance or 'host'}] no number answers `{command}`: {output}")
        return float(numbers[-1])


    def has_spell(ctx, instance, actor, spell):
        """Whether that game's actor knows the spell, as a condition reads it (the console's HasSpell)."""
        return ctx.console_number(instance, f"{actor:08X}.hasspell {spell:08X}") == 1.0


    def topic_info(ctx, instance, topic):
        """The line that game's menu lists first for the topic (None: not listed)."""
        return next((o["info"] for o in ctx.dialogue(instance).get("options", []) if o["topic"] == topic), None)

    def topic_infos(ctx, instance, topic):
        """Every line that game's menu lists for the topic, in order (a topic with a line per player lists each)."""
        return [o["info"] for o in ctx.dialogue(instance).get("options", []) if o["topic"] == topic]


    def quest_running(ctx, instance, quest_id):
        _game = ctx.game(instance)
        return bool((_game.rpc({"cmd": "quest", "quest": quest_id}) or {}).get("flags", 0) & 1)


    def quest_stage(ctx, instance, quest_id):
        _game = ctx.game(instance)
        return (_game.rpc({"cmd": "quest", "quest": quest_id}) or {}).get("current_stage")


    def select_entry(ctx, instance, menu, listing, wanted):
        """Walk a menu's actual highlight; fail if a named entry is absent."""
        value = lambda suffix: ctx.menu_var(instance, menu, listing + suffix)
        matches = lambda text: bool(text) and text.lstrip("$").split(" (")[0].casefold() == wanted.lstrip("$").casefold()
        count = value(".entryList.length")
        if not isinstance(count, (float, int)):
            raise ctx.failure(f"{menu}: no entries at {listing}")
        names = [value(f".entryList.{i}.text") for i in range(int(count))]
        target = next((i for i, text in enumerate(names) if matches(text)), None)
        if target is None:
            raise ctx.failure(f"{menu}: {wanted} is absent ({names})")
        for _ in range(int(count) + 1):
            selected = value(".selectedIndex")
            text = value(".selectedEntry.text")
            if matches(text):
                return
            ctx.tap_key(instance, DOWN_KEY if selected < target else UP_KEY)
            ctx.wait_for(f"{menu}: the highlight moves toward {wanted}",
                        lambda: value(".selectedIndex") != selected, 5)
        raise ctx.failure(f"{menu}: {wanted} is absent")


    def pick_with_keys(ctx, instance, topic, info=None):
        """That game's player moves the highlight to the topic (W, S) and picks it (E), once the menu takes a pick. With
        info: that line of the topic (a topic with a line per player lists each, shared-dialogue.md)."""
        ctx.wait_for(f"[{instance or 'host'}] the menu takes a pick", lambda: ctx.menu_takes_pick(instance), 10)
        # Mid-conversation, the speaker's copy must play what its owner does (only one player talks to it).
        speaker = ctx.dialogue("").get("speaker")
        if not ctx.FAILING and speaker and (problems := [p for p in ctx.check_desync() if f"[host {speaker:08X}," in p]):
            raise ctx.failure("desync in the conversation: " + " | ".join(problems))
        options = ctx.dialogue(instance).get("options", [])
        target = next((i for i, o in enumerate(options) if o["topic"] == topic and info in (None, o["info"])), None)
        if target is None:
            raise ctx.failure(f"[{instance or 'host'}] its menu does not list {topic:X}" + (f" with {info:X}" if info else ""))
        while (here := ctx.cursor_index(instance)) != target:
            ctx.tap_key(instance, DOWN_KEY if here < target else UP_KEY)
            ctx.wait_for(f"[{instance or 'host'}] the key moves the highlight", lambda: ctx.cursor_index(instance) != here, 5)
        log(f"[{instance or 'host'}] picks {topic:X}" + (f" ({info:X})" if info else ""))
        ctx.tap_key(instance, ACTIVATE_KEY)


    def skill(ctx, instance, index):
        """That game's player's skill: (level, experience toward the next level)."""
        s = ctx.player_command(instance, {"cmd": "skill", "index": index})
        return s["level"], s["experience"]


    def misc_stat(ctx, instance, name):
        """That game's player's misc stat (the console's GetPCMiscStat)."""
        return round(ctx.console_number(instance, f'getpcmiscstat "{name}"'))


    def set_misc_stat(ctx, instance, name, value):
        if change := value - ctx.misc_stat(instance, name):
            ctx.console_on(instance, f'modpcmiscstat "{name}" {change}')


    def actor_value(ctx, instance, actor, index):
        """That actor's value in that game (a copy's: what the host's scripts read of it)."""
        return ctx.player_command(instance, {"cmd": "skill", "index": index, "actor": actor})["level"]


    def natives_run_for_host(ctx, since, function):
        """The natives the host's script ran on that game's player since that trail number (GuestService's log)."""
        lines, _ = ctx.log_trail("client", since)
        return [line["text"] for line in lines if line["text"].startswith(f"GuestService: the host's script runs Game.{function}")]


    def force_greet(ctx, instance, actor):
        """The actor starts a conversation with that game's player, as its ForceGreet package does."""
        return ctx.player_command(instance, {"cmd": "force_greet", "actor": actor})["started"]


    def info_said(ctx, instance, info, said=None):
        """Whether that game marks the line said; with said, set back to it first (test setup only)."""
        return ctx.player_command(instance, {"cmd": "info_said", "info": info, **({} if said is None else {"said": said})})["said"]


    def player_race_on_host(ctx, instance):
        """The race of that game's player, as the host's copy of it shows it."""
        _game = ctx.game(instance)
        sid = next(s["mod"]["session_id"] for s in ((_game.read_status() or {}).get("world") or {}).values()
                   if s.get("role") == "own" and s.get("mod", {}).get("session_id") is not None and (s.get("identity") or {}).get("base") == 7)
        _game = ctx.game('')
        return next(((s.get("identity") or {}).get("race") for s in ((_game.read_status() or {}).get("world") or {}).values()
                     if s.get("role") == "copy" and s.get("mod", {}).get("session_id") == sid), None)


    def trainings(ctx, instance, count=None):
        """How many times that game's player trained this level (five allowed); with count, set back to it first."""
        return ctx.player_command(instance, {"cmd": "trainings", **({} if count is None else {"set": count})})["count"]

