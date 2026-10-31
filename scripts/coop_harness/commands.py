"""Commands for the co-op harness."""
from .source_tags import SOURCE_TAGS
from pathlib import Path
import argparse
import collections
import contextlib
import fnmatch
import json
import math
import random
import re
import shutil
import subprocess
import sys
import time
import uuid
from .common import (
    BOOT_FILE,
    CLI_HELP,
    GAME,
    HOME_CELL,
    LOCK_PATTERN,
    PLUGIN_LOADED,
    REPROS,
    ROOT,
    TEST_HOME,
    UNSYNCED_LINE,
)
from .errors import TestFailure
from .comparisons import (
    describe_world_differences,
    world_differences,
)
from .utilities import (
    atomic_write,
    log,
)


class Commands:
    def command_crash(ctx, args, *, _game=None):
        """The newest crash of each game (or --instance), as one report."""
        _game = ctx.game(_game)
        names = [_game.name] if args.instance else ["", "client"]
        for name in names:
            logs = ctx.crash_logs(name)
            if not logs:
                print(f"{name or 'host'}: no crash logs")
                continue
            print(f"===== {name or 'host'} ({len(logs)} crash logs) =====")
            print(ctx.crash_report(name, logs[-1]))
            print()

    def command_launch(ctx, args, *, _game=None):
        _game = ctx.game(_game)
        ctx.check_prerequisites()
        run_dir = ctx.new_run_dir("launch")
        try:
            ctx.launch(args, run_dir, _game=_game)
        finally:
            ctx.collect_logs(run_dir, _game=_game)
        log(f"Ready. Skyrim keeps running; stop it with: {sys.argv[0]} stop  (logs: {run_dir})")

    def command_reload(ctx, args):
        """Swaps the current build into both running games (the loader unloads the old client and
        loads the new one between two frames), then hosts and joins again. No game restart."""
        _game = ctx.game()
        started = time.monotonic()
        ctx.reload_games(args.seconds)
        print(f"\nRELOADED: both games run the new build and are connected again "
              f"({time.monotonic() - started:.1f}s)")
        ctx.report_game_memory()

    def command_duo(ctx, args):
        """Two real Skyrim instances: host + client on one machine, each must show the other."""
        _game = ctx.game()
        ctx.check_prerequisites()
        run_dir = getattr(args, "run_dir", None) or ctx.new_run_dir("duo")
        passed = False
        try:
            for name in ("", "client"):
                _game = ctx.game(name)
                log(f"--- {name or 'host'} ---")
                ctx.launch(args, run_dir, _game=_game)
            _game = ctx.game('')
            port = ctx.start_session("", _game=_game)["port"]
            log(f"Host is hosting on port {port}")
            _game = ctx.game('client')
            ctx.start_session(f"join {port}", _game=_game)
            log("Client joined the host")
            # Test players never run low on magicka: the "Magicka Low" help popup blocks all input
            # (bShowTutorials does not suppress it).
            for name in ("", "client"):
                _game = ctx.game(name)
                ctx.run_console("player.setav magicka 5000", _game=_game)
                if args.level:  # level-gated story manager starts (DA07: 20) need it
                    ctx.run_console(f"player.setlevel {args.level}", _game=_game)
            # Both players spawn on the same spot: step the client aside so each can see the other.
            _game = ctx.game('')
            host_position = ((_game.read_status() or {}).get("player") or {}).get("position")
            if args.save:
                ctx.wait_for("Late-save players see each other before placement", ctx.both_see_each_other, args.seconds, _game=_game)
                ctx.place_before("client", ctx.player_copy("client"), 100)
            elif host_position:
                _game = ctx.game('client')
                ctx.run_console(f"player.setpos x {host_position[0] + 200:.0f}", _game=_game)
                ctx.run_console(f"player.setpos y {host_position[1]:.0f}", _game=_game)

            seen = ctx.wait_for("Each game shows the other player", ctx.both_see_each_other, args.seconds, _game=_game)
            for name, players in seen.items():
                for actor, position in players.items():
                    log(f"{name} shows remote player {actor:08X} at ({position[0]:.0f}, {position[1]:.0f}, {position[2]:.0f})")
            if args.npc:
                # Host-owned NPC: spawned in the host's game, must be replicated to the client.
                _game = ctx.game('client')
                before = set(ctx.other_players(_game=_game))
                _game = ctx.game('')
                ctx.run_console(f"player.placeatme {args.npc} 1", _game=_game)
                _game = ctx.game('client')
                new_actors = ctx.wait_for(f"Client shows the host's new NPC (base {args.npc})",
                                      lambda: set(ctx.other_players(_game=_game)) - before, args.seconds, _game=_game)
                for actor in new_actors:
                    log(f"client shows host NPC {actor:08X}")
            passed = True
            print(f"\nPASS: host and client are connected and each shows the other's character"
                  f"{' and the host NPC' if args.npc else ''}. Logs: {run_dir}")
            if args.keep_running:
                for name in ("", "client"):
                    _game = ctx.game(name)
                    ctx.face_actor(next(iter(seen[name or "host"])), _game=_game)
                ctx.save_home(cell_name=args.cell if not args.save else None)
                boot = {key: getattr(args, key, None) for key in ("cell", "save", "level", "npc", "seconds", "boot_timeout", "world_timeout")}
                atomic_write(ctx.game("").control / BOOT_FILE, json.dumps(boot) + "\n")
        except TestFailure as error:
            print(f"\nFAIL: {error}\nLogs: {run_dir}", file=sys.stderr)
            raise SystemExit(1)
        finally:
            for name in ("", "client"):
                _game = ctx.game(name)
                ctx.collect_logs(run_dir, _game=_game)
            if args.keep_running:
                log(f"Both games keep running. Watch: `{sys.argv[0]} watch` (host) / `{sys.argv[0]} watch --instance client`; "
                    f"end with `{sys.argv[0]} stop`")
            else:
                for name in ("", "client"):
                    _game = ctx.game(name)
                    ctx.stop_game(_game=_game)
                log("Both games stopped" + ("" if passed else " (failed run)"))
        return run_dir

    def command_look(ctx, args):
        """What both screens show now: each camera (where it is, what it looks at, whether it sees whom it follows) and
        each player's body (on the floor, standing, lying), with what looks wrong in capitals."""
        while True:
            report = "\n".join(ctx.screen_lines("") + ctx.screen_lines("client"))
            if not args.follow:
                print(report)
                return
            print("\033[2J\033[H" + time.strftime("%H:%M:%S\n") + report + "\n\n(Ctrl+C to stop)", flush=True)
            time.sleep(0.5)

    def command_world_diff(ctx, args):
        differences = world_differences(*ctx.sample_worlds())
        if not differences:
            print("in sync: every reference in the loaded cells matches the host's")
            return
        fields = {}
        for changed in differences.values():
            for field in changed:
                fields[field] = fields.get(field, 0) + 1
        print(f"{len(differences)} references differ (" + ", ".join(f"{f} {n}" for f, n in fields.items()) + ")")
        for line in describe_world_differences(differences, args.details or None):
            print("  " + line)
        raise SystemExit(1)

    def command_desync(ctx, args):
        while True:
            problems = ctx.check_desync(args.samples, args.interval)
            report = "\n".join(problems) if problems else "in sync: every copy matches its owner"
            if not args.follow:
                print(report)
                if problems:
                    raise SystemExit(1)
                return
            print("\033[2J\033[H" + time.strftime("%H:%M:%S ") + report + "\n\n(Ctrl+C to stop)", flush=True)

    def command_status(ctx, args):
        while True:
            table = ctx.status_table()
            if not args.follow:
                print(table)
                return
            print("\033[2J\033[H" + table + "\n\n(refreshing every 0.5s, Ctrl+C to stop)", flush=True)
            time.sleep(0.5)

    def command_save_diff(ctx, args):
        """Saves both games at once and compares the two worlds (scripts/re/save_diff.py)."""
        sys.path.insert(0, str(ROOT / "scripts/re"))
        import save_diff
        name = f"coopdiff-{args.label}"
        with contextlib.redirect_stdout(sys.stderr if args.json else sys.stdout):  # --json: stdout is the report
            host, client, noise = ctx.save_both(name, args.noise, save_diff)
        host = save_diff.Save(host)
        report = save_diff.compare(host, save_diff.Save(client), noise, save_diff.editor_ids(host, GAME / "Data"))
        if args.json:
            print(json.dumps(report, indent=1, default=lambda o: sorted(o) if isinstance(o, set) else str(o)))
        else:
            save_diff.print_report(report, args.details)

    def command_quest_walk(ctx, args):
        """Walks the host through a quest's stages on a running duo (boot it with `duo --save`), the client
        leaving and rejoining on the way, and reports at each step how the client's quest compares."""
        _game = ctx.game()
        sys.path.insert(0, str(ROOT / "scripts/re"))
        import save_diff
        found = save_diff.plugin_quest(GAME / "Data" / args.plugin, args.quest)
        if not found:
            raise ctx.failure(f"no quest {args.quest} in {args.plugin}")
        local_id, plugin_stages = found
        _game = ctx.game('')
        order = [p.lower() for p in save_diff.Save(ctx.save_file("", args.names_from)).plugins]
        quest = order.index(args.plugin.lower()) << 24 | local_id & 0xFFFFFF
        names = save_diff.editor_ids(save_diff.Save(ctx.save_file("", args.names_from)), GAME / "Data")
        done_now = set(ctx.quest_snapshot("", quest)["done"])
        if args.stages:
            route = [int(s) for s in args.stages.split(",")]
        elif args.route_from:
            # The stages a later save of the same playthrough has done: the route its player took.
            later = save_diff.Save(ctx.save_file("", args.route_from))
            parsed = save_diff.parse_quest(later, later.forms[quest])
            route = sorted(stage for stage, done in parsed.get("stages", {}).items() if done and stage not in done_now)
        else:
            start = ctx.quest_snapshot("", quest)["stage"]
            route = [stage for stage in plugin_stages if stage > start and stage not in done_now]
        log(f"{args.quest} ({quest:08X}): route {route}")

        steps = []

        def compare(label, guest_here=True):
            host = ctx.quest_snapshot("", quest)
            if not guest_here:
                steps.append({"step": label, "host_stage": host["stage"], "guest": "away"})
                return
            # The client gets the time a sync would need; a report, so a miss is recorded, not raised.
            deadline, differences = time.monotonic() + args.settle, None
            while True:
                differences = ctx.snapshot_differences(host, ctx.quest_snapshot("client", quest), names)
                if not differences or time.monotonic() >= deadline:
                    break
                time.sleep(0.1)
                host = ctx.quest_snapshot("", quest)
            steps.append({"step": label, "host_stage": host["stage"], "differences": differences})
            log(f"{label}: " + ("matches" if not differences else "; ".join(differences)))

        compare("start (both loaded the save)")
        if not ctx.quest_snapshot("", quest)["flags"] & 1:  # not running: `setstage` needs it started (aliases filled)
            # --start-by: the trip that makes the game start it (a story manager event); else `startquest`.
            if args.start_by:
                for command in (c.strip() for c in args.start_by.split(";")):
                    if command.startswith("wait "):  # a start condition the host's game reaches itself
                        needed, needed_stage = command[5:].split(":")
                        needed_id = next(form for form, name in names.items() if name == needed)
                        ctx.wait_for(f"[host] {needed} reaches stage {needed_stage}",
                                 lambda: int(needed_stage) in ctx.quest_snapshot("", needed_id)["done"], 180, _game=_game)
                    else:
                        ctx.travel_on_host(command)
            else:
                ctx.console_on("", f"startquest {args.quest}")
            ctx.wait_for(f"[host] {args.quest} runs", lambda: ctx.quest_snapshot("", quest)["flags"] & 1, 60, _game=_game)
            compare("host's game starts the quest")
        away = False
        for index, stage in enumerate(route):
            if args.leave_after is not None and index == args.leave_after:
                ctx.disconnect_client()
                away = True
                steps.append({"step": "guest leaves", "host_stage": ctx.quest_snapshot("", quest)["stage"], "guest": "away"})
            if away and index == args.leave_after + args.away:
                ctx.rejoin_client()
                away = False
                compare("guest rejoins")
            ctx.console_on("", f"setstage {args.quest} {stage}")
            ctx.wait_for(f"[host] {args.quest} stage {stage} done", lambda: stage in ctx.quest_snapshot("", quest)["done"], 15, _game=_game)
            compare(f"host sets stage {stage}", guest_here=not away)
            if not ctx.quest_snapshot("", quest)["flags"] & 1:  # the stage's script ended the quest (a branch)
                log(f"{args.quest} ended at stage {ctx.quest_snapshot('', quest)['stage']}: the walk stops")
                break
        if away:
            ctx.rejoin_client()
            compare("guest rejoins")

        # The whole world at the end: every quest and global that differs, from both games' saves.
        host_path, client_path, _ = ctx.save_both(f"questwalk-{args.quest}", 0, save_diff)
        host_save, client_save = save_diff.Save(host_path), save_diff.Save(client_path)
        report = save_diff.compare(host_save, client_save, None, names)
        quests = [f for f in report["forms"] if f["type"] == "QUST"]
        globals_ = report["global_variables"]

        out_dir = TEST_HOME / "quest-walks"
        out_dir.mkdir(parents=True, exist_ok=True)
        out = out_dir / f"{time.strftime('%Y%m%d-%H%M%S')}-{args.quest}.json"
        out.write_text(json.dumps({"quest": args.quest, "route": route, "steps": steps,
                                   "quests": quests, "global_variables": globals_}, indent=1, default=str))

        print(f"\n{args.quest}: {sum(1 for s in steps if s.get('differences') == [])} of "
              f"{sum(1 for s in steps if 'differences' in s)} checked steps match")
        for s in steps:
            state = "guest away" if s.get("guest") == "away" else "matches" if not s["differences"] else \
                "; ".join(s["differences"])
            print(f"  {s['step']:<32} host at {s['host_stage']:<4} {state}")
        print(f"\nAt the end, {len(quests)} quests and {len(globals_)} globals differ between the games:")
        for f in quests[:args.details]:
            print(f"  {f['label']}: " + ("; ".join(f.get("fields", [])) or f.get("kind", "")))
        for g in globals_[:args.details]:
            print(f"  global {g['label']}: {g['host']} / {g['client']}")
        print(f"\nReport: {out}")

    def command_change_rate(ctx, args):
        """Counts the engine's change records (Code/client/TestHarness/ChangeLog.cpp) over a window: idle for
        --seconds, or while the named scenarios run."""
        _game = ctx.game()
        games = ("", "client") if args.instance in (None, "both") else ("client" if args.instance == "client" else "",)
        for name in games:
            _game = ctx.game(name)
            if not (_game.rpc({"cmd": "change_log_reset"}) or {}).get("ok"):
                raise ctx.failure(f"[{name or 'host'}] change_log_reset refused (a test build with ChangeLog?)")
        if args.scenarios:
            with contextlib.redirect_stdout(sys.stderr if args.json else sys.stdout):
                ctx.command_scenario(argparse.Namespace(names=args.scenarios, tag=None, hold=0, repeat=1, shuffle=False,
                                                    seed=None, fail_fast=False))
        else:
            started = time.monotonic()
            ctx.wait_for(f"{args.seconds:.0f}s of play", lambda: time.monotonic() - started >= args.seconds, args.seconds + 5, _game=_game)
        logs = {name or "host": ctx.change_log(name) for name in games}
        if args.json:
            print(json.dumps(logs, indent=1))
            return
        for game, log_ in logs.items():
            ctx.print_change_rates(game, log_)

    def command_unsynced(ctx, args):
        """The reference change flags the host's game had with no row to send them (ReferenceStates, logged the first
        time each since the plugin loaded): what vanilla still needs a row for."""
        sys.path.insert(0, str(ROOT / "scripts/re"))
        import save_diff
        lines = ctx.plugin_log("").read_text(errors="replace").splitlines()
        loads = [i for i, line in enumerate(lines) if PLUGIN_LOADED.search(line)]
        found = {}
        for line in lines[loads[-1] if loads else 0:]:
            if match := UNSYNCED_LINE.match(line):
                stamp, flags, ref = match.group(1), int(match.group(2), 16), int(match.group(3), 16)
                for bit in range(32):
                    if flags >> bit & 1:
                        found.setdefault(bit, (stamp, ref))
        if not found:
            print("every reference change the host had went out with a row")
            return
        for bit, (stamp, ref) in sorted(found.items()):
            print(f"  {1 << bit:#010x}  {save_diff.flag_names('REFR', 1 << bit)[0]:24s} first on {ref:X}  ({stamp})")

    def command_locks(ctx, args):
        _game = ctx.game()
        for name in ("", "client"):
            _game = ctx.game(name)
            status = _game.read_status()
            if not status:
                print(f"{name or 'host'}: not running")
                continue
            lock_order = status.get("lock_order", {})
            print(f"{name or 'host'}: " + ", ".join(f"{k} {v}" for k, v in lock_order.items() if k not in ("reports", "overlaps")))
            for report in lock_order.get("reports", []):
                print("  " + report)
            for overlap in lock_order.get("overlaps", []):
                print("  overlap: " + overlap)

    def command_repro(ctx, args, *, _game=None):
        """Runs a crash recipe (REPROS, or --cell and console commands) and prints each crash's report."""
        _game = ctx.game(_game)
        if args.name in REPROS:
            cell, commands = REPROS[args.name]
        else:  # console commands of its own
            cell, commands = args.cell, ([args.name] if args.name else []) + args.commands
        if not commands:
            raise ctx.failure("nothing to run: name a repro or give console commands")
        crashes = 0
        for run in range(1, args.runs + 1):
            log(f"Run {run}/{args.runs}: {cell}, then {' / '.join(commands)}")
            crash_log = ctx.reproduce_once(args, cell, commands, _game=_game)
            if not crash_log:
                log(f"No crash within {args.wait:.0f}s")
                continue
            crashes += 1
            print(ctx.crash_report("", crash_log, ours=not args.vanilla))
            if args.follow:
                # Heap addresses repeat when the game boots the same way: watch the crash's from boot.
                registers = ctx.crash_registers(crash_log)
                args.watch = ",".join(f"{value:#x}" for value in dict.fromkeys(registers.values()))
                log(f"Following: a second run watching {', '.join(f'{name} {value:#x}' for name, value in registers.items())}")
                follow_log = ctx.reproduce_once(args, cell, commands, _game=_game)
                if not follow_log:
                    log("The second run did not crash: nothing to follow")
                    continue
                again = ctx.crash_registers(follow_log)
                same = [name for name, value in registers.items() if again.get(name) == value]
                log(f"Registers the same as the first crash: {', '.join(same) or 'none'}"
                    + ("" if len(same) == len(registers) else " (the others' watches are about different memory)"))
                print(ctx.crash_report("", follow_log, ours=not args.vanilla))
        log(f"Crashed {crashes} of {args.runs} runs")

    def source_matches(ctx, path):
        return [(pattern, tags) for pattern, tags in SOURCE_TAGS if fnmatch.fnmatchcase(path, pattern)]

    def affected_source(ctx, path):
        if path.startswith("Code/tests/specs/"):
            return True
        if path.startswith(("docs/", "Code/tests/")):
            return False
        return path.startswith(("Code/", "scripts/")) or Path(path).suffix in {
            '.c', '.cc', '.cpp', '.cxx', '.h', '.hpp', '.inl', '.py', '.sh', '.ps1', '.lua',
        }

    def changed_sources(ctx, base):
        paths = set()
        for command in (["git", "diff", "--name-only", "-z", base, "--"],
                        ["git", "ls-files", "--others", "--exclude-standard", "-z"]):
            result = subprocess.run(command, cwd=ROOT, capture_output=True, text=True)
            if result.returncode:
                raise ctx.failure(f"{' '.join(command)}: {result.stderr.strip()}")
            paths.update(filter(None, result.stdout.split("\0")))
        return paths

    def command_affected(ctx, args):
        if args.check:
            sources = sorted(p.relative_to(ROOT).as_posix() for p in (ROOT / "Code").rglob("*")
                             if p.is_file() and ctx.affected_source(p.relative_to(ROOT).as_posix()))
            uncovered = [path for path in sources if not ctx.source_matches(path)]
            for path in uncovered:
                print(f"no tag for {path}")
            if uncovered:
                raise ctx.failure(f"{len(uncovered)} source file(s) have no SOURCE_TAGS pattern")
            print(f"SOURCE_TAGS covers all {len(sources)} files under Code/ (excluding other Code/tests/).")
            if not args.run:
                return

        paths = args.files if args.files is not None else ctx.changed_sources(args.base)
        reasons = collections.defaultdict(list)
        uncovered = []
        for path in sorted({Path(path).as_posix() for path in paths}):
            if not ctx.affected_source(path):
                continue
            matches = ctx.source_matches(path)
            if not matches:
                print(f"no tag for {path}")
                uncovered.append(path)
            for pattern, tags in matches:
                for tag in tags:
                    reasons[tag].append((path, pattern))

        print("tags: " + (", ".join(sorted(reasons)) or "none"))
        for tag in sorted(reasons):
            print(f"  {tag}:")
            for path, pattern in reasons[tag]:
                print(f"    {path} <- {pattern}")
        if 'core' in reasons:
            print("core changed: the full suite covers this, run it before a release")
            if args.run:
                print("The full suite was not run; --run selects only the other tags' scenarios.")
        tags = ",".join(sorted(set(reasons) - {'core'}))
        names = ctx.scenario_names([], tags) if tags else []
        print(f"scenarios ({len(names)}):")
        for name in names:
            print(f"  {name}")
        if uncovered:
            raise ctx.failure(f"{len(uncovered)} changed source file(s) have no SOURCE_TAGS pattern; no scenarios run")
        if args.run and names:
            if len(names) == len(ctx.registry):
                raise ctx.failure("affected selected the full suite; run it explicitly with scenario")
            ctx.command_scenario(argparse.Namespace(names=[], tag=tags, hold=0, repeat=1, shuffle=False,
                                                seed=None, fail_fast=False, dry_run=args.dry_run))
        elif args.run:
            print("No targeted scenarios to run.")

    def soak_snapshot(ctx):
        _game = ctx.game()
        games = {}
        for name, role in (("", "host"), ("client", "guest")):
            _game = ctx.game(name)
            status = _game.read_status()
            stale = status is None
            if stale:
                try:
                    status = json.loads((_game.control / "status.json").read_text())
                except (OSError, ValueError):
                    status = {}
            sample = {"frames": status.get("frames"), "locks": status.get("lock_order", {}).get("reports", []),
                      "heartbeat_ms": status.get("heartbeat_ms"), "memory": None, "stale_status": stale,
                      "build": status.get("game", {}).get("build"), "generation": status.get("reload", {}).get("generation")}
            try:
                pid = ctx.game_process(name)
                fields = dict(line.split(":", 1) for line in Path(f"/proc/{pid}/status").read_text().splitlines() if ":" in line)
                sample["memory"] = {"pid": pid, **{key: int(fields.get(field, "0 kB").split()[0]) * 1024
                                    for key, field in (("rss_bytes", "VmRSS"), ("swap_bytes", "VmSwap"), ("virtual_bytes", "VmSize"))}}
            except (TestFailure, OSError, ValueError) as error:
                sample["unavailable"] = str(error)
            games[role] = sample
        return games

    def soak_frame_deltas(ctx, before, after):
        result = {}
        for role in ("host", "guest"):
            first, last = before.get(role, {}), after.get(role, {})
            old, new = first.get("frames") or {}, last.get("frames") or {}
            pid = (first.get("memory") or {}).get("pid")
            same_process = pid is not None and pid == (last.get("memory") or {}).get("pid") \
                and first.get("generation") == last.get("generation")
            result[role] = {key: new[key] - old[key] for key in ("frames", "stalls")
                            if same_process and key in new and key in old and new[key] >= old[key]}
        return result

    def soak_facts(ctx, rounds):
        crashes, locks, failures = set(), set(), set()
        for round_ in rounds:
            crashes.update(crash["signature"] for crash in round_.get("crashes", []))
            for sample in [round_.get("before", {}), round_.get("after", {})] + [
                    entry.get(phase, {}) for entry in round_.get("scenarios", []) for phase in ("before", "after")]:
                for game in sample.values():
                    for report in game.get("locks", []):
                        match = LOCK_PATTERN.match(report)
                        sites = match.group(3) if match else report
                        locks.add(" vs ".join(sorted(sites.split(" vs "))))
            failures.update(entry["name"] for entry in round_.get("scenarios", []) if entry.get("result", "").startswith("FAIL"))
        return crashes, locks, failures

    def soak_summary(ctx, rounds, previous, memory_rounds=None):
        facts, old = ctx.soak_facts(rounds), ctx.soak_facts(previous)
        summary = {key: sorted(current - known) for key, current, known in zip(
            ("new_crash_signatures", "new_lock_patterns", "first_failures"), facts, old)}
        growth = []
        for role in ("host", "guest"):
            values = []
            for round_ in memory_rounds if memory_rounds is not None else rounds:
                # Fresh processes are compared at the same boundary: after the shuffled suite.
                memory = round_.get("after", {}).get(role, {}).get("memory")
                if memory and round_.get("complete"):
                    values.append({"round": round_["index"], "bytes": memory["rss_bytes"] + memory["swap_bytes"]})
            if len(values) >= 2 and all(b["bytes"] > a["bytes"] for a, b in zip(values, values[1:])) \
                    and values[-1]["bytes"] - values[0]["bytes"] >= 16 * 1024 * 1024:
                growth.append({"game": role, "samples": values, "growth_bytes": values[-1]["bytes"] - values[0]["bytes"]})
        summary["memory_growth"] = growth
        summary["round_errors"] = [{"round": r["index"], "message": r["error"]} for r in rounds
                                   if r.get("error") and not r.get("scenarios")]
        return summary

    def soak_summary_line(ctx, summary):
        findings = [f"{len(summary[key])} {label[:-1] if len(summary[key]) == 1 else label}" for key, label in (
            ("new_crash_signatures", "new crash signatures"), ("new_lock_patterns", "new lock patterns"),
            ("first_failures", "first failures"), ("memory_growth", "memory growth trends"),
            ("round_errors", "round errors")) if summary[key]]
        return ", ".join(findings) or "nothing new"

    def soak_crashes(ctx, before, directory):
        found = []
        for name, role in (("", "host"), ("client", "guest")):
            for path in ctx.crash_logs(name):
                if path in before[name]:
                    continue
                try:
                    report = ctx.crash_report(name, path)
                except (OSError, ValueError, subprocess.SubprocessError, ImportError, TestFailure) as error:
                    report = f"Could not symbolize crash: {error}"
                destination = directory / f"{role}-{path.name}"
                shutil.copy2(path, destination)
                report_file = destination.with_suffix(".report.txt")
                report_file.write_text(report)
                text = path.read_text(errors="replace")
                frames = re.findall(r"^\s*\[\d+\]\[P\].*?(\S+\.(?:exe|dll)\+[0-9A-Fa-f]+)", text, re.MULTILINE)[:5]
                exception = re.search(r'Unhandled exception "([^"]+)"', text)
                signature = (exception.group(1) if exception else "unknown exception") + ": " + (" > ".join(frames) or "no probable frames")
                found.append({"game": role, "log": str(destination), "report": str(report_file),
                              "signature": signature, "top_frames": frames})
                ctx.KNOWN_CRASHES[name].add(path)
        return found

    def stop_duo(ctx):
        _game = ctx.game()
        for name in ("", "client"):
            _game = ctx.game(name)
            if not ctx.stop_game(_game=_game):
                raise ctx.failure(f"{name or 'guest'}: could not stop the test game")
        for game in ctx.games:
            game.close()

    def command_soak(ctx, args):
        _game = ctx.game()
        if args.tag is not None and not args.tag.strip():
            raise ctx.failure("--tag must name a tag; omit it only for a full suite")
        if args.rounds is not None and args.rounds <= 0:
            raise ctx.failure("--rounds must be positive")
        hours = args.hours if args.hours is not None else (6 if args.rounds is None else None)
        if hours is not None and (not math.isfinite(hours) or hours <= 0):
            raise ctx.failure("--hours must be finite and positive")
        names = ctx.scenario_names([], args.tag) if args.tag else ctx.with_accounts(list(ctx.registry))
        if not names:
            raise ctx.failure("no scenarios selected")
        seed = args.seed if args.seed is not None else random.randrange(1 << 30)
        if args.dry_run:
            print(f"Would boot a fresh duo each round; {str(args.rounds) + ' rounds' if args.rounds else str(hours) + ' hours'}; "
                  f"seed {seed}; {len(names)} scenarios")
            return

        base = TEST_HOME / "soak"
        previous_files = sorted(base.glob("*/report.json"), key=lambda p: p.stat().st_mtime)
        previous, previous_file = {}, None
        for path in reversed(previous_files):
            try:
                previous = json.loads(path.read_text())
                previous_file = str(path)
                break
            except (OSError, ValueError):
                continue
        directory = base / (time.strftime("%Y%m%d-%H%M%S") + "-" + uuid.uuid4().hex[:6])
        directory.mkdir(parents=True)
        report_file = directory / "report.json"
        report = {"schema": 1, "started": time.time(), "seed": seed, "tag": args.tag, "hours": hours,
                  "requested_rounds": args.rounds, "previous_report": previous_file, "rounds": []}
        started = time.monotonic()
        deadline = started + hours * 3600 if hours is not None else math.inf

        def save_report():
            report["seconds"] = time.monotonic() - started
            report["summary"] = ctx.soak_summary(report["rounds"], previous.get("rounds", []))
            atomic_write(report_file, json.dumps(report, indent=2) + "\n")

        save_report()
        log(f"Soak {'smoke test' if args.tag else 'full suite'}; report: {report_file}")
        try:
            while time.monotonic() < deadline and (args.rounds is None or len(report["rounds"]) < args.rounds):
                index = len(report["rounds"]) + 1
                round_dir = directory / f"round-{index:04d}"
                round_dir.mkdir()
                round_ = {"index": index, "seed": seed + index - 1, "scenarios": [], "attempted_order": [], "crashes": [],
                          "runner_log": str(round_dir / "runner.log"), "phase": "setup"}
                round_["order"] = list(names)
                random.Random(round_["seed"]).shuffle(round_["order"])
                report["rounds"].append(round_)
                prior = previous.get("rounds", []) + report["rounds"][:-1]
                crash_before = {name: set(ctx.crash_logs(name)) for name in ("", "client")}
                round_started = time.monotonic()
                log(f"=== soak round {index}, seed {round_['seed']}: {len(names)} scenarios")
                save_report()

                def observe(event, **data):
                    if event in ("phase", "checkpoint"):
                        return
                    if event == "order":
                        round_["order"] = data["order"]
                    elif event == "start":
                        round_["attempted_order"].append(data["name"])
                        round_["scenarios"].append({**data, "seed": round_["seed"], "result": "RUNNING", "before": ctx.soak_snapshot()})
                    elif event == "result":
                        entry = round_["scenarios"][-1]
                        entry.update(data, after=ctx.soak_snapshot())
                        entry["frame_delta"] = ctx.soak_frame_deltas(entry["before"], entry["after"])
                        if data["result"] != "PASS":
                            entry["failed_step"] = dict(ctx.CURRENT_STEP)
                    save_report()

                try:
                    with (round_dir / "runner.log").open("w", buffering=1) as output, \
                            contextlib.redirect_stdout(output), contextlib.redirect_stderr(output):
                        ctx.stop_duo()
                        boot_start = time.monotonic()
                        round_["phase"] = "boot"
                        run_dir = ctx.new_run_dir(f"soak-round-{index:04d}")
                        round_["run_dir"] = str(run_dir)
                        save_report()
                        duo = argparse.Namespace(cell=HOME_CELL, save=None, boot_timeout=240, world_timeout=180,
                                                 seconds=60, keep_running=True, level=None, npc="", run_dir=run_dir)
                        ctx.command_duo(duo)
                        round_["boot_seconds"] = time.monotonic() - boot_start
                        round_["phase"] = "scenarios"
                        round_["before"] = ctx.soak_snapshot()
                        save_report()
                        ctx.command_scenario(argparse.Namespace(names=names if not args.tag else [], tag=args.tag, hold=0,
                                                           repeat=1, shuffle=True, seed=round_["seed"], fail_fast=False,
                                                           dry_run=False, observe=observe))
                except (TestFailure, SystemExit, OSError) as error:
                    round_["error"] = f"{round_['phase']}: {error}"
                finally:
                    round_["after"] = ctx.soak_snapshot()
                    for entry in round_["scenarios"]:
                        if entry["result"] == "RUNNING":
                            entry["result"] = "FAIL (round aborted: " + round_.get("error", "interrupted") + ")"
                    round_["not_run"] = round_["order"][len(round_["attempted_order"]):]
                    round_["complete"] = not round_["not_run"] and len(round_["scenarios"]) == len(names)
                    round_["crashes"] = ctx.soak_crashes(crash_before, round_dir)
                    for name in ("", "client"):
                        _game = ctx.game(name)
                        ctx.collect_logs(round_dir, _game=_game)
                    round_["seconds"] = time.monotonic() - round_started
                    round_["summary"] = ctx.soak_summary([round_], prior, memory_rounds=report["rounds"])
                    round_["comparison"] = f"round {index - 1}" if index > 1 else previous_file
                    save_report()
                passed = sum(entry["result"] == "PASS" for entry in round_["scenarios"])
                log(f"round {index}: {passed}/{len(names)} passed, {round_['seconds']:.1f}s; "
                    + ctx.soak_summary_line(round_["summary"]) + (f" (compared with {round_['comparison']})" if round_["comparison"] else ""))
        except KeyboardInterrupt:
            report["interrupted"] = True
            raise
        finally:
            report["finished"] = time.time()
            save_report()
            ctx.stop_duo()
        print(f"{len(report['rounds'])} rounds, {ctx.soak_summary_line(report['summary'])}; report: {report_file}")
        if any(round_.get("error") or round_["crashes"] or any(entry["result"] != "PASS" for entry in round_["scenarios"])
               for round_ in report["rounds"]):
            raise SystemExit(1)

    def cli(ctx):
        _game = ctx.game()
        try:
            ctx.validate_source_tags()
        except TestFailure as error:
            raise SystemExit(f"FAIL: {error}")
        parser = argparse.ArgumentParser(description=CLI_HELP, formatter_class=argparse.RawDescriptionHelpFormatter)
        commands = parser.add_subparsers(dest="command", required=True)
        for name in ("launch", "duo", "repro"):
            sub = commands.add_parser(name)
            sub.add_argument("--cell", default="QASmoke", help="cell for `coc` from the main menu (default QASmoke)")
            sub.add_argument("--save", help="load this save instead of coc")
            sub.add_argument("--boot-timeout", type=float, default=240)
            sub.add_argument("--world-timeout", type=float, default=180)
            sub.add_argument("--visible", action="store_true",
                             help="show the game on the desktop (default: off-screen Xvfb display; see `screenshot`)")
            if name in ("launch", "repro"):
                sub.add_argument("--vanilla", action="store_true",
                                 help="without our plugin (A/B baseline): the stand-in harness plugin "
                                      "(Code/vanilla_harness) runs the commands; the next normal launch redeploys ours")
            if name == "duo":
                sub.add_argument("--seconds", type=float, default=30, help="how long to wait for the games to show each other")
                sub.add_argument("--keep-running", action="store_true", help="leave Skyrim open afterwards")
            if name == "repro":
                sub.add_argument("name", nargs="?", help=f"a known crash: {', '.join(REPROS)}")
                sub.add_argument("commands", nargs="*", help="or: console commands after booting into --cell (3s apart)")
                sub.add_argument("--wait", type=float, default=25, help="seconds to wait for the crash after them")
                sub.add_argument("--runs", type=int, default=1, help="repeat N times (how reliably it crashes)")
                sub.add_argument("--heap-history", nargs="?", type=int, const=2, default=0, metavar="MILLIONS",
                                 help="record engine allocations and frees with stacks (Code/client/HeapHistory.h)")
                sub.add_argument("--watch", default="", metavar="ADDR,ADDR",
                                 help="keep every allocation and free of the blocks at these addresses since boot")
                sub.add_argument("--quarantine", nargs="?", type=int, const=256, default=0, metavar="MB",
                                 help="poison freed blocks and hold them back from reuse: a use after free faults at "
                                      "its first read (with --heap-history: on the freed block's story)")
                sub.add_argument("--guard", default="", metavar="SIZE,SIZE",
                                 help="page-heap these block sizes (hex, from a heap history): any access to one after "
                                      "its free faults right there, even a read that poison would not catch")
                sub.add_argument("--probe", default="", metavar="ID,ID",
                                 help="engine functions (Address Library IDs) whose calls go into the breadcrumbs: "
                                      "whether, when and on what the engine ran them before the crash")
                sub.add_argument("--follow", action="store_true",
                                 help="after a crash, run again watching the addresses in its registers from boot")
            if name == "duo":
                sub.add_argument("--npc", default="000A2C8E",
                                 help="NPC base form (8 hex digits) the host spawns; the client must show it (default Lydia, '' to skip)")
                sub.add_argument("--level", type=int,
                                 help="raise both players to this level after loading (late saves are level 1)")
        reload = commands.add_parser("reload", help="swap the current build into a running duo and reconnect")
        reload.add_argument("--seconds", type=float, default=30, help="how long to wait for the players to reappear")
        scenario = commands.add_parser("scenario", help="sync scenarios against a running `duo --keep-running`")
        scenario.add_argument("--hold", type=float, default=0,
                              help="pause N seconds after each synced step and face the camera at it (for watching)")
        scenario.add_argument("names", nargs="*", help=f"default: all of {', '.join(ctx.registry)}")
        scenario.add_argument("--shuffle", action="store_true", help="random order (scenarios must not depend on each other)")
        scenario.add_argument("--seed", type=int, help="with --shuffle: repeat a shuffled order")
        scenario.add_argument("--repeat", type=int, default=1, help="run the list N times")
        scenario.add_argument("--until-fail", type=int, default=0, metavar="N",
                              help="run the list up to N times, stopping at the first failure: both games write a bug report "
                                   "there (read back as one timeline) and stay as they failed")
        scenario.add_argument("--tag", help="only scenarios with one of these comma-separated tags (see the flow registry)")
        scenario.add_argument("--fail-fast", action="store_true", help="stop at the first scenario that fails")
        scenario.add_argument("--dry-run", action="store_true", help="print the selection without contacting Skyrim")
        scenario.add_argument("--no-restart", action="store_true",
                              help="stop the batch when a scenario leaves the baseline broken (default: boot the duo again and go on)")
        affected = commands.add_parser("affected", help="select scenarios from changed source files")
        affected.add_argument("--base", default="HEAD", metavar="REF", help="compare against REF (default HEAD), including untracked files")
        affected.add_argument("--run", action="store_true", help="run the selected scenarios; never expand core into the full suite")
        affected.add_argument("--check", action="store_true", help="check Code/ coverage, including shared specs but excluding other Code/tests/")
        affected.add_argument("--dry-run", action="store_true", help="with --run: print what scenario would run without contacting Skyrim")
        affected.add_argument("--files", nargs="*", help=argparse.SUPPRESS)
        soak = commands.add_parser("soak", help="fresh duo and shuffled scenarios each round; --tag limits a smoke test")
        budget = soak.add_mutually_exclusive_group()
        budget.add_argument("--hours", type=float, help="start rounds for H hours (default 6); finish the last round")
        budget.add_argument("--rounds", type=int, help="run N rounds instead of a time budget")
        soak.add_argument("--seed", type=int, help="first round's shuffle seed; each round adds one")
        soak.add_argument("--tag", help="smoke test only these comma-separated scenario tags")
        soak.add_argument("--dry-run", action="store_true", help="print the scope without booting or stopping games")
        stop = commands.add_parser("stop", help="stop the test game(s) (both instances unless --instance)")
        console = commands.add_parser("console", help="run a console command in the running test game")
        console.add_argument("text", nargs="+")
        status = commands.add_parser("status", help="both games side by side: health, death, weapon, casting")
        status.add_argument("--follow", "-f", action="store_true", help="keep refreshing")
        commands.add_parser("locks", help="both games' lock-order checker reports (deadlock patterns)")
        rate = commands.add_parser("change-rate", help="the engine's change records per form type and flag, idle or during scenarios")
        rate.add_argument("scenarios", nargs="*", help="run these scenarios as the window (default: --seconds idle)")
        rate.add_argument("--seconds", type=float, default=30, help="idle window without scenarios (default 30)")
        rate.add_argument("--instance", choices=("host", "client", "both"), help="which game (default: both)")
        rate.add_argument("--json", action="store_true", help="both games' raw counts as JSON")
        save_diff = commands.add_parser("save-diff", help="save both games at once and compare the two worlds block by block")
        save_diff.add_argument("--label", default="now", help="names the saves coopdiff-<label> (default now)")
        save_diff.add_argument("--noise", type=float, default=0, metavar="SECONDS",
                               help="save the host again after this long: what differs between those two is marked noise")
        save_diff.add_argument("--details", type=int, default=25, help="forms listed per class (0: summary only)")
        save_diff.add_argument("--json", action="store_true", help="the whole report as JSON")
        walk = commands.add_parser("quest-walk", help="walk the host through a quest on a running duo; compare the client's at each step")
        walk.add_argument("quest", help="the quest's editor id (MQ202)")
        walk.add_argument("--plugin", default="Skyrim.esm", help="the plugin defining it (default Skyrim.esm)")
        walk.add_argument("--names-from", required=True, metavar="SAVE",
                          help="the save the duo booted from: its plugin order and names")
        walk.add_argument("--route-from", metavar="SAVE", help="a later save of the playthrough: walk the stages it has done")
        walk.add_argument("--stages", help="walk these stages (comma-separated) instead")
        walk.add_argument("--start-by", metavar="COMMANDS",
                          help="the trip that makes the game start it, ;-separated: travel commands (coc X) and "
                               "`wait QUEST:STAGE` for a condition the host's game reaches itself (default startquest)")
        walk.add_argument("--leave-after", type=int, metavar="N", help="the client leaves after N stages")
        walk.add_argument("--away", type=int, default=2, help="stages the host sets while the client is away (default 2)")
        walk.add_argument("--settle", type=float, default=3, help="seconds the client gets to match each step (default 3)")
        walk.add_argument("--details", type=int, default=30, help="end-of-walk differences listed (default 30)")
        report = commands.add_parser("report", help="players' /export zips and bug reports: timeline, copy differences, crashes (coop_report.py)")
        report.add_argument("rest", nargs=argparse.REMAINDER, help="coop_report.py's arguments")
        crash = commands.add_parser("crash", help="the newest crash of each game: engine state (CrashLoggerSSE) and our last engine changes")
        crash.add_argument("--instance", choices=["client"], help="only this game (default: both)")
        commands.add_parser("unsynced", help="the reference change flags the host had with no row to send them")
        world_diff = commands.add_parser("world-diff", help="compare every reference in the loaded cells between the two games")
        world_diff.add_argument("--details", type=int, default=40, help="references listed (0: all)")
        look = commands.add_parser("look", help="what both screens show: each camera and each player's body")
        look.add_argument("--follow", "-f", action="store_true", help="keep looking")
        desync = commands.add_parser("desync", help="compare every actor's full state between the two games")
        desync.add_argument("--follow", "-f", action="store_true", help="keep checking")
        desync.add_argument("--samples", type=int, default=3, help="a difference must persist this many samples")
        desync.add_argument("--interval", type=float, default=1.0, help="seconds between samples")
        watch = commands.add_parser("watch", help="live view of the off-screen game display in a window (ffplay)")
        commands.add_parser("setup-display", help="one-time: install the off-screen dummy X server (sudo)")
        shot = commands.add_parser("screenshot", help="save a PNG of the off-screen game display")
        shot.add_argument("output", nargs="?", type=Path)
        for sub in (stop, console, watch, shot):
            sub.add_argument("--instance", choices=("host", "client", "both") if sub is watch else ("host", "client"),
                             help="which `duo` game (default: host)")
        args = parser.parse_args()
        ctx.VISIBLE = getattr(args, "visible", False)
        _game = ctx.game('client' if getattr(args, 'instance', None) == 'client' else '')
        try:
            if args.command == "launch":
                ctx.command_launch(args, _game=_game)
            elif args.command == "duo":
                ctx.command_duo(args)
            elif args.command == "scenario":
                ctx.command_scenario(args)
            elif args.command == "affected":
                ctx.command_affected(args)
            elif args.command == "soak":
                ctx.command_soak(args)
            elif args.command == "reload":
                ctx.command_reload(args)
            elif args.command == "console":
                ctx.run_console(" ".join(args.text), _game=_game)
                log("Done")
            elif args.command == "locks":
                ctx.command_locks(args)
            elif args.command == "save-diff":
                ctx.command_save_diff(args)
            elif args.command == "change-rate":
                ctx.command_change_rate(args)
            elif args.command == "quest-walk":
                ctx.command_quest_walk(args)
            elif args.command == "report":
                sys.path.insert(0, str(ROOT / "scripts"))
                import coop_report
                coop_report.main(args.rest)
            elif args.command == "crash":
                ctx.command_crash(args, _game=_game)
            elif args.command == "repro":
                ctx.command_repro(args, _game=_game)
            elif args.command == "unsynced":
                ctx.command_unsynced(args)
            elif args.command == "world-diff":
                ctx.command_world_diff(args)
            elif args.command == "desync":
                ctx.command_desync(args)
            elif args.command == "look":
                ctx.command_look(args)
            elif args.command == "status":
                ctx.command_status(args)
            elif args.command == "watch":
                names = ["", "client"] if args.instance == "both" else [_game.name]
                viewers = []
                for index, name in enumerate(names):
                    _game = ctx.game(name)
                    viewers.append(subprocess.Popen(
                        ["ffplay", "-loglevel", "error", "-window_title", f"Skyrim {name or 'host'} (coop test)",
                         "-left", str(40 + index * 660), "-top", "40", "-x", "640", "-y", "360",
                         "-f", "x11grab", "-framerate", "30", "-video_size", "1280x720", "-i", f"{_game.display}+4,30"]))
                for viewer in viewers:
                    viewer.wait()
            elif args.command == "setup-display":
                ctx.setup_display()
            elif args.command == "screenshot":
                args.output = args.output or ROOT / f"build/coop-test/screenshot-{_game.name or 'host'}.png"
                args.output.parent.mkdir(parents=True, exist_ok=True)
                subprocess.run(["import", "-display", _game.display, "-window", "root", str(args.output)], check=True)
                log(f"Saved {args.output}")
            else:
                names = [_game.name] if args.instance else ["", "client"]
                stopped = True
                for name in names:
                    _game = ctx.game(name)
                    stopped = ctx.stop_game(_game=_game) and stopped
                log("Stopped" if stopped else "Some processes would not exit")
        except TestFailure as error:
            print(f"FAIL: {error}", file=sys.stderr)
            sys.exit(1)
        except KeyboardInterrupt:
            sys.exit(130)
