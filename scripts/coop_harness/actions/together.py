"""Both players acting at once (docs/design/repro-mcp.md, "together"), and the dialogue verbs its lanes use."""
import re
import threading
import time
from ..common import ACTIVATE_KEY, CLIENT_NETNS, DOWN_KEY, UP_KEY
from ..errors import TestFailure
from .. import game as game_module

# How far ahead the shared moment is: each lane reads its game's clock and queues its input before it.
LEAD_SECONDS = 0.4
FRAME_MS = 20.0  # inputs further apart than a frame did not land together


class TogetherActions:
    def together(ctx, lanes, sync="frame", lead=LEAD_SECONDS):
        """Runs every lane (a callable) in its own thread from one shared moment. sync "frame": a lane's first input
        command (key, mouse, input) runs in its game's first frame at that moment, by the game's own clock, so two
        games' presses land in the same frame; "start": the lanes only start together. Returns each lane's
        {result, landed: {game, frame, late_ms}, seconds}; a lane's failure fails the call once every lane ended."""
        if sync not in ("frame", "start"):
            raise ctx.failure(f"together: sync is 'frame' or 'start', not {sync!r}")
        # Each game's clock read before the moment: a lane then only queues its input.
        offsets = {game.name: game.clock_offset() for game in ctx.games} if sync == "frame" else {}
        moment = time.monotonic() + lead
        outcomes = [{} for _ in lanes]

        def run(index, lane):
            game_module.SCHEDULE.at = None
            game_module.SCHEDULE.landed = None
            game_module.SCHEDULE.offsets = offsets
            # Leave time to queue the scheduled input; a lane without one starts at the moment itself.
            time.sleep(max(0.0, moment - time.monotonic() - (0.15 if sync == "frame" else 0.0)))
            if sync == "frame":
                game_module.SCHEDULE.at = moment
            started = time.monotonic()
            try:
                outcomes[index]["result"] = lane()
            except TestFailure as error:
                outcomes[index]["failure"] = str(error)
            except Exception as error:  # a bug in a lane: reported with the others
                outcomes[index]["failure"] = f"{type(error).__name__}: {error}"
            finally:
                outcomes[index]["landed"] = game_module.SCHEDULE.landed
                outcomes[index]["seconds"] = round(time.monotonic() - started, 3)
                game_module.SCHEDULE.at = None

        threads = [threading.Thread(target=run, args=(i, lane), daemon=True, name=f"together-{i}") for i, lane in enumerate(lanes)]
        for thread in threads:
            thread.start()
        for thread in threads:
            thread.join()
        failed = [f"lane {i}: {o['failure']}" for i, o in enumerate(outcomes) if "failure" in o]
        if failed:
            raise ctx.failure("together: " + " | ".join(failed))
        if sync == "frame" and (apart := ctx.landed_apart(outcomes)) is not None and apart > FRAME_MS:
            raise ctx.failure(f"together: the inputs landed {apart} ms apart, not in one frame: {[o['landed'] for o in outcomes]}")
        return outcomes

    def landed_apart(ctx, outcomes):
        """How many ms apart the lanes' scheduled inputs ran (each game's own clock against the shared moment), None
        when a lane scheduled none. Under a frame (~16 ms) is the same frame for both games."""
        late = [(o.get("landed") or {}).get("late_ms") for o in outcomes]
        if any(value is None for value in late):
            return None
        return round(max(late) - min(late), 2)

    def line_delay(ctx, ms=0, jitter_ms=0):
        """The line between the two games delays each packet `ms` (each way, plus up to `jitter_ms`), as between two
        machines: netem on the guest's namespace link (runtime.py, ensure_client_netns). 0 takes it off; unstage and
        every duo boot do too. One machine's loopback-fast line hides races two players over the internet meet."""
        for device, prefix in (("coop2-host", []), ("coop2-ns", ["ip", "netns", "exec", CLIENT_NETNS])):
            ctx.sudo(*prefix, "tc", "qdisc", "del", "dev", device, "root")
            if ms or jitter_ms:
                if (result := ctx.sudo(*prefix, "tc", "qdisc", "add", "dev", device, "root", "netem", "delay", f"{ms}ms",
                                       *([f"{jitter_ms}ms"] if jitter_ms else []))).returncode != 0:
                    raise ctx.failure(f"line_delay: {device}: {result.stderr.strip()}")
        ctx.LINE_DELAY = ms or jitter_ms
        return {"delay_ms": ms, "jitter_ms": jitter_ms}

    # --- Dialogue lanes: real keys and mouse through the game's input dispatch ---

    def wait_list_shown(ctx, instance, seconds=10):
        """That game's menu shows its list (TOPIC_LIST_SHOWN): W and S do nothing while it slides in (DialogueMenu.as)."""
        ctx.wait_for(f"[{instance or 'host'}] its dialogue list is shown", lambda: ctx.list_shown(instance), seconds)

    def wait_takes_pick(ctx, instance, seconds=15):
        """That game's menu takes a pick: its list shown and the SWF's 750 ms after voice ready over."""
        ctx.wait_for(f"[{instance or 'host'}] its menu takes a pick", lambda: ctx.menu_takes_pick(instance), seconds)

    def wait_npc_quiet(ctx, seconds=90):
        """The host's NPC has said its line (no current info) and both menus take a pick: a line's end re-arms the SWF's
        750 ms gate (35285 sends voice ready), so a press then is dropped, in single player too."""
        ctx.wait_for("the NPC's line is over on the host", lambda: not ctx.dialogue("").get("current_info"), seconds)
        for game in ("", "client"):
            ctx.wait_takes_pick(game)

    def dialogue_highlight(ctx, instance, index):
        """That game's player moves its menu's highlight to option `index` with W and S."""
        ctx.wait_list_shown(instance)
        for _ in range(16):
            here = ctx.cursor_index(instance)
            if here == index:
                return {"highlight": here}
            ctx.tap_key(instance, DOWN_KEY if here < index else UP_KEY)
            ctx.wait_for(f"[{instance or 'host'}] the key moves the highlight", lambda: ctx.cursor_index(instance) != here, 5)
        raise ctx.failure(f"[{instance or 'host'}] the highlight never reached {index}: at {ctx.cursor_index(instance)}")

    def dialogue_hover(ctx, instance, index):
        """That game's player puts the mouse over option `index` (its row on screen), without clicking."""
        from ..scenarios import dialogue_mouse
        # Calibrating moves the mouse too: together's shared moment is for the hover itself.
        armed, game_module.SCHEDULE.at = getattr(game_module.SCHEDULE, "at", None), None
        scale = dialogue_mouse.calibrate(ctx, instance)
        game_module.SCHEDULE.at = armed
        return dialogue_mouse.hover(ctx, instance, index, scale, settle=False)

    def expect_same_highlight(ctx, seconds=2.0):
        """Both menus highlight the same option within `seconds` (the conversation's one highlight)."""
        def same():
            return ctx.cursor_index("") == ctx.cursor_index("client") >= 0
        try:
            ctx.wait_for("both menus highlight the same option", same, seconds)
        except TestFailure:
            raise ctx.failure(f"the menus highlight different options: host {ctx.cursor_index('')}, guest {ctx.cursor_index('client')}")
        return {"highlight": ctx.cursor_index("")}

    def dialogue_move(ctx, instance, direction, wait=True):
        """That game's player taps W ("up") or S ("down") once in its dialogue menu, not waiting for the highlight;
        `wait`: once its list shows (W and S do nothing before), else into whatever the menu shows."""
        if direction not in ("up", "down"):
            raise ctx.failure(f"dialogue_move: up or down, not {direction!r}")
        if wait:
            ctx.wait_list_shown(instance)
        ctx.tap_key(instance, UP_KEY if direction == "up" else DOWN_KEY)
        return {"moved": direction}

    def dialogue_move_press(ctx, instance, direction):
        """One W ("up") or S ("down"), then E at once: a player that moves and picks before anyone else's move arrives."""
        ctx.dialogue_move(instance, direction)
        ctx.tap_key(instance, ACTIVATE_KEY)
        return {"moved": direction, "pressed": True}

    def dialogue_sweep(ctx, instance, rounds=2):
        """That game's player runs the mouse over every option, top to bottom, `rounds` times, without clicking."""
        from ..scenarios import dialogue_mouse
        armed, game_module.SCHEDULE.at = getattr(game_module.SCHEDULE, "at", None), None
        scale = dialogue_mouse.calibrate(ctx, instance)
        game_module.SCHEDULE.at = armed
        count = len(ctx.dialogue(instance).get("options") or [])
        for _ in range(rounds):
            for index in range(count):
                if ctx.cursor_index(instance) < 0:  # the list went (a pick): nothing left to hover
                    return {"swept": index}
                dialogue_mouse.hover(ctx, instance, index, scale, settle=False)
        return {"swept": rounds * count}

    def press_activate_at(ctx, instance, target):
        """That game's player faces a handle and presses E (Activate) once, not waiting for what it starts."""
        ctx.face(instance, ctx.resolve(target, instance))
        ctx.tap_key(instance, ACTIVATE_KEY)
        return {"pressed": target}

    def dialogue_mark(ctx):
        """Where the host's log is now: expect_lines_said counts the picks said after it."""
        ctx.DIALOGUE_MARK = ctx.log_trail("", 1 << 62)[1]
        return {"mark": ctx.DIALOGUE_MARK}

    def lines_said(ctx):
        """The picks the host's game said since dialogue_mark: [{player, topic, info}]."""
        lines, _ = ctx.log_trail("", getattr(ctx, "DIALOGUE_MARK", 0))
        found = (re.search(r"Dialogue: player (\d+) picked topic ([0-9A-F]+) \(info ([0-9A-F]+)\)", row["text"]) for row in lines)
        return [{"player": int(m[1]), "topic": int(m[2], 16), "info": int(m[3], 16)} for m in found if m]

    def expect_lines_said(ctx, count=1, seconds=10.0):
        """The host's game says exactly `count` picks since dialogue_mark: within `seconds`, and no more after."""
        try:
            ctx.wait_for(f"the host's game says {count} line(s)", lambda: len(ctx.lines_said()) >= count, seconds)
        except TestFailure:
            raise ctx.failure(f"the host's game said {len(ctx.lines_said())} line(s), not {count}: {ctx.lines_said()}")
        # A long answer on the duo's silent device runs about twice its spoken length (dialogue.md, how long a line lasts).
        ctx.wait_for("both menus take a pick again or the conversation is over", lambda: all(
            ctx.menu_takes_pick(game) or not ctx.dialogue(game).get("menu") for game in ("", "client")), 90)
        if len(said := ctx.lines_said()) != count:
            raise ctx.failure(f"the host's game said {len(said)} line(s), not {count}: {said}")
        return {"said": said}

    def expect_one_conversation(ctx, seconds=3.0, settle=2.0):
        """Within `settle` seconds exactly one game's player is in a conversation, and it stays the only one for `seconds`.
        A guest's ask can open its menu a moment before the host's own talk ends it (the host turning to another NPC
        ends the old conversation first, shared-dialogue.md): that settles, two that stay do not."""
        def open_menus():
            return [game or "host" for game in ("", "client") if ctx.dialogue(game).get("menu")]
        try:
            ctx.wait_for("one conversation is left", lambda: len(open_menus()) == 1, settle)
        except TestFailure:
            raise ctx.failure(f"the conversations never settled to one: {open_menus()}")
        deadline = time.monotonic() + seconds
        while time.monotonic() < deadline:
            if len(menus := open_menus()) != 1:
                raise ctx.failure(f"{len(menus)} conversations at once after settling: {menus}")
        return {"talking": open_menus()[0]}

    def dialogue_walk(ctx, instance, target, watcher="", steps=1):
        """That game's player says every option of `target`'s first list in turn, each followed by `steps` - 1 more
        (the first option), then leaves (Tab) and talks again; `watcher` joins each conversation by talking and never
        picks. Every list on the way is the same on both menus (expect dialogue checks the rest). Returns each path.
        Talking again right after an NPC's goodbye finds it with nothing to say: keep paths short of a goodbye."""
        watcher = None if watcher is None else "" if watcher in ("", "host") else "client"
        paths = []
        first = None
        index = 0
        while first is None or index < len(first):
            ctx.talk(instance, target)
            if watcher is not None:
                ctx.talk(watcher, target)
            shown = [o.get("text") for o in ctx.dialogue(instance).get("options") or []]
            first = first or shown
            path, line = [], index
            for _ in range(steps):
                options = [o.get("text") for o in ctx.dialogue(instance).get("options") or []]
                if not options:
                    break
                ctx.client_shows_hosts_list(f"at {target}'s line {len(path)} of path {index}")
                said = ctx.pick(instance, min(line, len(options) - 1))
                path.append(said["picked"])
                if said.get("closed"):
                    break
                line = 0
            paths.append(path)
            ctx.close_dialogues()
            index += 1
        return {"paths": paths}

    def dialogue_press_after_moves(ctx, instance, watch, moves=2):
        """That game's player presses E once the `watch` game's menu highlight has moved `moves` times: a press in the
        middle of another player's highlighting."""
        watch = "" if watch in ("", "host") else "client"
        seen = {"last": ctx.cursor_index(watch), "moves": 0}
        def moved():
            here = ctx.cursor_index(watch)
            if here != seen["last"]:
                seen["last"], seen["moves"] = here, seen["moves"] + 1
            return seen["moves"] >= moves
        ctx.wait_for(f"[{watch or 'host'}] its highlight moves {moves} times", moved, 15)
        ctx.tap_key(instance, ACTIVATE_KEY)
        return {"pressed_at": seen["last"]}

    def dialogue_press(ctx, instance):
        """That game's player presses E once in its dialogue menu, whatever it shows: the highlighted option, a skip."""
        ctx.tap_key(instance, ACTIVATE_KEY)
        return {"pressed": True}
