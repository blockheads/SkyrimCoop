"""Observations for the co-op harness."""
import math
import os
import sys
import time
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
from .comparisons import compare_actors, dialogue_differences, refused_replays, sliding_actors
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

    def check_desync(ctx, samples=int(DESYNC_WINDOW / DESYNC_INTERVAL) + 1, interval=DESYNC_INTERVAL):
        """Only differences seen in every sample count (copies legitimately lag for a moment)."""
        persistent = None
        for index in range(samples):
            current = dict(ctx.find_desyncs())
            persistent = current if persistent is None else {k: v for k, v in current.items() if k in persistent}
            if not persistent:
                return []
            if index + 1 < samples:
                time.sleep(interval)
        return list(persistent.values())

    def check_gait(ctx, seconds=2.0):
        """Actors that moved over the last seconds without walking (sliding, or in slow motion), on either game:
        ["[client] copy 0001348C Faendal moved 160 units in 2s, its feet 2", ...]."""
        problems = []
        for game in ("", "client"):
            world = (ctx.game(game).read_status() or {}).get("world") or {}
            gait = ctx.player_command(game, {"cmd": "gait", "seconds": seconds})["actors"]
            for actor, (travelled, feet) in sorted(sliding_actors(gait).items()):
                entry = world.get(str(actor)) or {}
                name = (entry.get("identity") or {}).get("name") or "?"
                problems.append(f"[{game or 'host'}] {entry.get('role') or 'actor'} {actor:08X} {name} moved {travelled:.0f} units "
                                f"in {seconds:.0f}s, its feet {feet:.0f}")
        return problems

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

    def save_both(ctx, name, noise_seconds, save_diff):
        """Both games saved as `name`, and with `noise_seconds` the host's noise (save_diff.noise_from)."""
        _game = ctx.game()
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
