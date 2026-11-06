"""Observations for the co-op harness."""
import copy
import math
import os
import sys
import threading
import time
from concurrent.futures import ThreadPoolExecutor
from .common import (
    CRASH_REGISTER,
    DESYNC_INTERVAL,
    DESYNC_WINDOW,
    FORM_TYPE_NAMES,
    KNOWN_LOCK_PATTERNS,
    LOCK_PATTERN,
    ROOT,
    SYNCED_QUEST_FLAGS,
)
from .errors import TestFailure
from .comparisons import (GraphApart, SLIDE_DISTANCE, active_states, compare_actors, graph_apart_line, dialogue_differences, drawn_walks, refused_replays,
                          sliding_actors, unwatched_games)
from .utilities import log


class Observations:
    def check_dialogue_trace(ctx):
        """Both games' conversations since the last take must agree (dialogue_differences): every menu in a
        conversation shows the party's list and one highlight, hidden alike while the NPC answers (shared-dialogue.md)."""
        records = {game: (ctx.player_command(game, {"cmd": "dialogue_trace"}) or {}).get("entries") or []
                   for game in ("", "client")}
        end = int(time.time() * 1e6)
        return [f"{field} at +{at:.1f}s: host {host!r} vs guest {guest!r}"
                for at, field, host, guest in dialogue_differences(records[""], records["client"], end)]

    def replay_refusals_line(ctx, since):
        """Each game's refused replays since `since`, one line (empty when none): animations copies did not play."""
        parts = []
        for game in ("", "client"):
            path = ctx.plugin_log(game)
            lines = []
            for file in [path, *(path.with_name(f"{path.stem}.{index}{path.suffix}") for index in range(1, 4))]:
                try:
                    lines.extend(file.read_text(errors="replace").splitlines()[-40000:])
                except OSError:
                    continue
            for (copy, event), count in sorted(refused_replays(lines, since).items()):
                parts.append(f"{game or 'host'} {copy:08X} {event} x{count}")
        return "; ".join(parts)

    @staticmethod
    def graph_runs(actor):
        """In 3D and alive: KillImpl stops a graph's updates, and an unloaded actor's graph stands still."""
        return (actor.get("placement") or {}).get("has_3d") and not (actor.get("state") or {}).get("dead")

    def copy_pairs(ctx, games=None):
        """Each living NPC in 3D's owner and copy by form id, from each game's own world: {session id: (owner, copy)}."""
        worlds = [((game.read_status() or {}).get("world") or {}) for game in (games or (ctx.game(""), ctx.game("client")))]
        pairs, copies = {}, {}
        for world in worlds:
            for form, actor in world.items():
                if actor.get("role") == "copy" and not actor.get("player") and ctx.graph_runs(actor):
                    copies[(actor.get("mod") or {}).get("session_id")] = int(form)
        for world in worlds:
            for form, actor in world.items():
                session = (actor.get("mod") or {}).get("session_id")
                if actor.get("role") == "own" and not actor.get("player") and session in copies and ctx.graph_runs(actor):
                    pairs[session] = (int(form), copies[session])
        return pairs

    def graph_watch_start(ctx, every=0.25):
        """Watches every NPC's graph, owner against copy, on connections of its own until graph_watch_stop."""
        games = [copy.copy(ctx.game("")), copy.copy(ctx.game("client"))]
        for game in games:
            game.rpc_connections = {}
        stop, apart, started = threading.Event(), GraphApart(), time.time()

        def read(game, form):
            return active_states((game.rpc({"cmd": "graph_state", "actor": form}) or {}).get("state", ""))

        def watch():
            pairs, refreshed = {}, 0.0
            with ThreadPoolExecutor(1) as other:
                while not stop.is_set():
                    if time.time() - refreshed > 3.0:
                        pairs, refreshed = ctx.copy_pairs(games), time.time()
                        apart.keep_only(owner for owner, _ in pairs.values())
                    for owner, copy_form in pairs.values():
                        if stop.is_set():
                            break
                        reading = other.submit(read, games[1], copy_form)
                        apart.observe(owner, read(games[0], owner), reading.result(), time.time())
                    stop.wait(every)

        def guarded():
            try:
                watch()
            except Exception as error:  # a check's own failure says so, it never fails the flow
                log(f"graph watch stopped: {error}")
                apart.keep_only(())  # the gap is no evidence: what it finished already stands
            finally:
                for game in games:
                    game.close()

        thread = threading.Thread(target=guarded, daemon=True, name="graph-watch")
        thread.start()
        return {"thread": thread, "stop": stop, "apart": apart, "started": started}

    def graph_watch_stop(ctx, watch):
        """The runs apart since graph_watch_start, as one log line ('' if it read nothing)."""
        watch["stop"].set()
        watch["thread"].join(10)
        return graph_apart_line(watch["apart"], watch["started"])

    def find_desyncs(ctx):
        """Compares both games once. Returns a list of (key, message)."""
        _game = ctx.game()
        worlds = {}
        for instance, game in (("", "host"), ("client", "client")):
            _game = ctx.game(instance)
            status = _game.read_status() or {}
            if "world" not in status:
                raise ctx.failure(f"{game} game reports no world snapshot (old DLL or not running)")
            worlds[game] = status["world"]
        return compare_actors(worlds)

    def lasting_desyncs(ctx, samples=int(DESYNC_WINDOW / DESYNC_INTERVAL) + 1, interval=DESYNC_INTERVAL, known=()):
        """{key: message}: only differences seen in every sample count (copies legitimately lag for a moment), without
        the `known` keys (a scene's own start: ReproActions.scene)."""
        persistent = None
        for index in range(samples):
            current = {k: v for k, v in ctx.find_desyncs() if k not in known}
            persistent = current if persistent is None else {k: v for k, v in current.items() if k in persistent}
            if not persistent:
                return {}
            if index + 1 < samples:
                time.sleep(interval)
        return persistent

    def check_desync(ctx, samples=int(DESYNC_WINDOW / DESYNC_INTERVAL) + 1, interval=DESYNC_INTERVAL, known=()):
        return list(ctx.lasting_desyncs(samples, interval, known).values())

    def check_gait(ctx, seconds=2.0, walked=None):
        """Actors that moved over the last seconds without walking (sliding, or in slow motion) while drawn, on either
        game: ["[client] copy 0001348C Faendal moved 160 units in 2s, its feet 2 (...)", ...]. A culled actor's skeleton
        keeps its last pose (docs/re/systems/actor-movement.md), so GaitWatch counts drawn frames only. `walked`
        ({game: units}) keeps the farthest drawn walk each game saw: the check's evidence it saw anything."""
        problems = []
        for game in ("", "client"):
            world = (ctx.game(game).read_status() or {}).get("world") or {}
            gait = ctx.player_command(game, {"cmd": "gait", "seconds": seconds})["actors"]
            if walked is not None:
                walked[game or "host"] = max([walked.get(game or "host", 0.0)] + list(drawn_walks(gait).values()))
            for actor, (travelled, feet) in sorted(sliding_actors(gait).items()):
                entry = world.get(str(actor)) or {}
                name = (entry.get("identity") or {}).get("name") or "?"
                mod = entry.get("mod") or {}
                steps = gait[str(actor)]
                problems.append(f"[{game or 'host'}] {entry.get('role') or 'actor'} {actor:08X} {name} moved {travelled:.0f} units "
                                f"in {seconds:.0f}s, its feet {feet:.0f} (drawn steps {steps['frames']}, culled "
                                f"{steps.get('hidden_frames', 0)}, process level {mod.get('process_level')}, "
                                f"package {mod.get('package_type')})")
        return problems

    def gait_after_arrival(ctx, cell, seconds=12.0, walked=None):
        """The party arrives in `cell`, then for `seconds` every 2 s check_gait on both games: the first problem of
        each actor seen (empty if every actor that moved walked). Without `walked`, a game where no actor walked in
        view is a problem too (an all-culled window shows nothing); with it ({game: units}), the caller judges that."""
        ctx.both_go(cell)
        seen, own = {}, walked is None
        walked = {} if own else walked
        started = time.monotonic()
        sampled = started
        def observed():
            nonlocal sampled
            now = time.monotonic()
            if now - sampled >= 2.0:
                for problem in ctx.check_gait(walked=walked):
                    seen.setdefault(problem.split(" moved ")[0], problem)
                sampled = now
            return now - started >= seconds
        ctx.wait_for(f"walking observations complete in {cell}", observed, seconds + 3)
        return list(seen.values()) + (ctx.unwatched(walked) if own else [])

    def unwatched(ctx, walked):
        """A problem for each game whose gait readouts never showed a drawn walk (check_gait's `walked`)."""
        return [f"[{game}] no actor walked {SLIDE_DISTANCE:.0f} units in view: nothing was checked"
                for game in unwatched_games(walked)]

    def known_lock_patterns(ctx):
        try:
            lines = KNOWN_LOCK_PATTERNS.read_text().splitlines()
        except OSError:
            return set()
        patterns = {line.split("#")[0].strip() for line in lines} - {""}
        # "A vs B" and "B vs A" are one pair: the checker names whichever thread it met first.
        return patterns | {" vs ".join(reversed(p.split(" vs "))) for p in patterns if " vs " in p}

    def lock_order_reports(ctx):
        """{sites: (game, report, ours)} over both games, without reviewed patterns."""
        _game = ctx.game()
        known = ctx.known_lock_patterns()
        found = {}
        for name in ("", "client"):
            _game = ctx.game(name)
            status = _game.read_status() or {}
            for report in status.get("lock_order", {}).get("reports", []):
                match = LOCK_PATTERN.match(report)
                sites = match.group(3) if match else report
                # A live overlap is never a reviewed pattern: its own key.
                if match and match.group(2).startswith("live"):
                    sites = "live: " + sites
                elif sites in known:
                    continue
                found.setdefault(sites, (name or "host", report, bool(match and match.group(1))))
        return found

    def save_file(ctx, instance, name):
        _game = ctx.game(instance)
        return _game.my_games / "Saves" / f"{name}.ess"

    def save_game(ctx, instance, name):
        """`save <name>` in that game; returns where its .ess lands (wait for it with saves_written)."""
        _game = ctx.game()
        path = ctx.save_file(instance, name)
        for stale in (path, path.with_suffix(".skse")):
            stale.unlink(missing_ok=True)
        ctx.console_on(instance, f"save {name}")
        return path

    def saves_written(ctx, *paths):
        """A wait_for predicate: every save exists, with its co-save, and is the same size twice 0.5s apart."""
        seen = {}
        def written():
            sizes = tuple(p.stat().st_size if p.exists() and p.with_suffix(".skse").exists() else -1 for p in paths)
            if -1 in sizes or seen.get("sizes") != sizes:
                seen.update(sizes=sizes, since=time.monotonic())
                return False
            return time.monotonic() - seen["since"] >= 0.5
        return written

    def save_both(ctx, name, noise_seconds, save_diff, guest_leaves=False):
        """Both games saved as `name`, and with `noise_seconds` the host's noise (save_diff.noise_from). A guest in a
        session saves nothing (SaveService): with guest_leaves it leaves first and saves its world as it left it."""
        _game = ctx.game()
        if ctx.game_state("client").get("online"):
            if not guest_leaves:
                raise ctx.failure("[client] a guest saves nothing in a session (SaveService): leave first to compare saves")
            ctx.player_command("client", {"cmd": "disconnect"})
            ctx.wait_for("[client] left the session, to save its world", lambda: not ctx.game_state("client").get("online"), 15)
        host, client = ctx.save_game("", name), ctx.save_game("client", name)
        ctx.wait_for("both saves written", ctx.saves_written(host, client), 60)
        noise = None
        if noise_seconds:
            # The same game again: what changes on its own in that time is noise, not drift.
            started = time.monotonic()
            ctx.wait_for(f"{noise_seconds:.0f}s of play", lambda: time.monotonic() - started >= noise_seconds, noise_seconds + 5)
            again = ctx.save_game("", name + "-again")
            ctx.wait_for("the second host save written", ctx.saves_written(again), 60)
            noise = save_diff.noise_from(host, again)
        return host, client, noise

    def quest_snapshot(ctx, instance, quest):
        """One quest as that game holds it now (harness `quest`): flags, stage, done stages, objectives, fills."""
        _game = ctx.game(instance)
        response = _game.rpc({"cmd": "quest", "quest": quest}) or {}
        if not response.get("ok"):
            raise ctx.failure(f"[{instance or 'host'}] quest {quest:08X}: {response.get('error', 'no answer')}")
        return {"flags": response["flags"], "stage": response["current_stage"],
                "done": sorted(stage for stage, done in response["stages"] if done),
                "objectives": {index: state for index, state in response["objectives"] if state},
                "aliases": {a["id"]: (a["ref"], a["name"]) for a in response["aliases"]},
                "bases": {a["id"]: a.get("base", 0) for a in response["aliases"]},
                "locations": {a["id"]: a["location"] for a in response["aliases"] if a.get("location")}}

    def snapshot_differences(ctx, host, client, names):
        """What differs between the host's and the client's snapshot of one quest, readable."""
        def ref(form_id):
            return names.get(form_id, f"{form_id:08X}") if form_id else "unfilled"

        out = []
        if host["flags"] & SYNCED_QUEST_FLAGS != client["flags"] & SYNCED_QUEST_FLAGS:
            out.append(f"flags {host['flags'] & SYNCED_QUEST_FLAGS:#06x} / {client['flags'] & SYNCED_QUEST_FLAGS:#06x}")
        if host["stage"] != client["stage"]:
            out.append(f"stage {host['stage']} / {client['stage']}")
        for label, a, b in (("done only on host", host["done"], client["done"]),
                            ("done only on client", client["done"], host["done"])):
            if missing := sorted(set(a) - set(b)):
                out.append(f"{label}: {missing}")
        for index in sorted(set(host["objectives"]) | set(client["objectives"])):
            a, b = host["objectives"].get(index, 0), client["objectives"].get(index, 0)
            if a != b:
                out.append(f"objective {index}: {a} / {b}")
        for alias in sorted(set(host["aliases"]) | set(client["aliases"])):
            (a, name), (b, _) = host["aliases"].get(alias, (0, "?")), client["aliases"].get(alias, (0, "?"))
            # A reference a game created has an id of its own: not synced yet (docs/progress.md, loose ends).
            if a != b and not (a >= 0xFF000000 or b >= 0xFF000000):
                out.append(f"alias {name}: {ref(a)} / {ref(b)}")
        return out

    def travel_on_host(ctx, command):
        """A console command that moves the host far (coc): the host arrives elsewhere, the client next to it."""
        def where(instance):
            return ctx.local_player(instance).get("position")
        before = where("")
        ctx.console_on("", command)
        ctx.wait_for(f"[host] arrives after '{command}'",
                 lambda: (here := where("")) and math.dist(here, before) > 2000, 90)
        ctx.wait_for("[client] arrives with the host (the party tether)",
                 lambda: (a := where("")) and (b := where("client")) and math.dist(a, b) < 1000, 90)

    def change_log(ctx, instance):
        _game = ctx.game(instance)
        response = _game.rpc({"cmd": "change_log"}, timeout=10) or {}
        if not response.get("ok"):
            raise ctx.failure(f"[{instance or 'host'}] change_log: {response.get('error', 'no answer (a test build?)')}")
        return response

    def print_change_rates(ctx, game, log_):
        """One game's change records per form type and flag, per second of the window."""
        sys.path.insert(0, str(ROOT / "scripts/re"))
        import save_diff
        seconds = max(log_["seconds"], 0.001)
        by_type = {}
        for bucket in log_["buckets"]:
            by_type.setdefault(FORM_TYPE_NAMES.get(bucket["type"], str(bucket["type"])), []).append(bucket)
        total = sum(b["calls"] for b in log_["buckets"])
        print(f"\n{game}: {total} change calls in {seconds:.1f}s ({total / seconds:.1f}/s)"
              + ("  [tables full: counts are partial]" if log_.get("full") else ""))
        for name, buckets in sorted(by_type.items(), key=lambda kv: -sum(b["calls"] for b in kv[1])):
            calls = sum(b["calls"] for b in buckets)
            distinct = log_["forms"].get(str(buckets[0]["type"]), 0)
            print(f"  {name}  {calls} calls ({calls / seconds:.2f}/s), {distinct} distinct forms recorded")
            for b in sorted(buckets, key=lambda b: -b["calls"])[:10]:
                flags = "+".join(save_diff.flag_names(name, b["flags"])) or "0"
                samples = " ".join(f"{s:08X}" for s in b["samples"])
                print(f"      {b['calls']:7d} {b['calls'] / seconds:8.2f}/s  recorded {b['recorded']:7d}  "
                      f"{'forced ' if b['forced'] else ''}{flags}  e.g. {samples}")

    def run_step(ctx, step, *, _game=None):
        """A repro step: a console command, or @show / @hide a menu."""
        _game = ctx.game(_game)
        if step.startswith(("@show ", "@hide ")):
            _game.rpc({"cmd": "menu", "name": step[6:], "show": step.startswith("@show")})
            log(f"Menu: {step}")
        else:
            ctx.run_console(step, _game=_game)

    def reproduce_once(ctx, args, cell, commands, *, _game=None):
        """Fresh game in `cell`, runs the commands, waits for a crash; its report, or None."""
        _game = ctx.game(_game)
        ctx.stop_game(_game=_game)
        if args.heap_history:
            os.environ["SKYRIMCOOP_HEAP_HISTORY"] = str(args.heap_history)
        if args.watch:
            os.environ["SKYRIMCOOP_HEAP_WATCH"] = args.watch
        if args.quarantine:
            os.environ["SKYRIMCOOP_HEAP_QUARANTINE"] = str(args.quarantine)
        if args.guard:
            os.environ["SKYRIMCOOP_HEAP_GUARD"] = args.guard
        if args.probe:
            os.environ["SKYRIMCOOP_PROBE"] = args.probe
        args.cell, args.save = cell, None
        run_dir = ctx.new_run_dir("repro")
        before = set(ctx.crash_logs(""))
        started = time.monotonic()
        try:
            ctx.launch(args, run_dir, _game=_game)
            try:
                for command in commands:
                    ctx.run_step(command, _game=_game)
                    time.sleep(3)
            except TestFailure:
                if not set(ctx.crash_logs("")) - before:  # a crash mid-command is the expected outcome
                    raise
            deadline = time.monotonic() + args.wait
            while time.monotonic() < deadline and not set(ctx.crash_logs("")) - before:
                time.sleep(0.5)
            new = sorted(set(ctx.crash_logs("")) - before)
            ctx.KNOWN_CRASHES[""].update(new)
            # The crash handler's part of the plugin log is complete once the game is gone.
            if new:
                ctx.wait_for("Waiting for the crashed game to exit", lambda: not ctx.prefix_processes(_game=_game), 30, _game=_game)
        finally:
            ctx.stop_game(_game=_game)
            ctx.collect_logs(run_dir, _game=_game)
            os.environ.pop("SKYRIMCOOP_HEAP_HISTORY", None)
            os.environ.pop("SKYRIMCOOP_HEAP_WATCH", None)
            os.environ.pop("SKYRIMCOOP_HEAP_QUARANTINE", None)
            os.environ.pop("SKYRIMCOOP_HEAP_GUARD", None)
            os.environ.pop("SKYRIMCOOP_PROBE", None)
        ctx.KNOWN_CRASHES[""].update(ctx.crash_logs(""))
        if not new:
            return None
        log(f"Crashed {time.monotonic() - started:.0f}s after launch (logs in {run_dir})")
        return new[-1]

    def crash_registers(ctx, crash_log):
        """The crash's registers that hold heap-like addresses (not the game's image, not small numbers)."""
        registers = {}
        for line in crash_log.read_text(errors="replace").splitlines():
            match = CRASH_REGISTER.match(line)
            value = int(match.group(2), 16) if match else 0
            if match and match.group(1) not in ("RSP", "R11") and 0x100000 < value < 0x7FF000000000 \
                    and not 0x140000000 <= value < 0x150000000:
                registers[match.group(1)] = value
        return registers
