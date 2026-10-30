"""Runner for the co-op harness."""
from .source_tags import SOURCE_TAGS
import contextlib
import json
import random
import sys
import time
import uuid
from .common import (
    CLIENT_USER,
    ROOT,
    TEST_HOME,
    TEST_ITEMS,
)
from .errors import TestFailure
from .comparisons import (
    describe_world_differences,
    world_pairs,
)
from .utilities import log


from dataclasses import dataclass


@dataclass
class FlowResult:
    name: str
    seconds: float
    failures: list
    restored: bool
    checkpoint: dict

    @property
    def text(self):
        return "PASS" if not self.failures else "FAIL (" + " | ".join(f"{phase}: {error}" for phase, error in self.failures) + ")"


class Runner:
    def validate_source_tags(ctx):
        known = set().union(*(flow.tags for flow in ctx.registry.values())) | {'core'}
        for pattern, tags in SOURCE_TAGS:
            if unknown := tags - known:
                raise ctx.failure(f"SOURCE_TAGS {pattern}: unknown tag(s) {sorted(unknown)}")

    def with_accounts(ctx, names, requested=()):
        """Without a second Steam account (COOP_CLIENT_USER), the scenarios needing one only when named."""
        return names if CLIENT_USER else [n for n in names if "two-accounts" not in ctx.registry[n].tags or n in requested]

    def scenario_names(ctx, requested, tag):
        unknown = [name for name in requested if name not in ctx.registry]
        if unknown:
            raise ctx.failure(f"unknown scenario(s) {unknown}; known: {', '.join(ctx.registry)}")
        names = list(dict.fromkeys(requested)) if requested else [name for name in ctx.registry if "input" not in ctx.registry[name].tags or tag]
        names = ctx.with_accounts(names, requested or [])
        # A late save's scenario needs the duo booted from it: only when named, or asked for by `--tag late-save`.
        if not tag or "late-save" not in tag.split(","):
            names = [name for name in names if "late-save" not in ctx.registry[name].tags or name in (requested or [])]
        if tag:
            wanted = set(tag.split(","))
            # Restarting a game takes minutes: only when named, or asked for by its tag.
            if "restart" not in wanted:
                names = [name for name in names if "restart" not in ctx.registry[name].tags or name in (requested or [])]
            known = set().union(*(flow.tags for flow in ctx.registry.values()))
            if wanted - known:
                raise ctx.failure(f"unknown tag(s) {sorted(wanted - known)}; known: {', '.join(sorted(known))}")
            names = [name for name in names if ctx.registry[name].tags & wanted]
        unknown = [name for name in names if name not in ctx.registry]
        if unknown:
            raise ctx.failure(f"unknown scenario(s) {unknown}; known: {', '.join(ctx.registry)}")
        return names

    def reports_folder(ctx, name):
        """Where that game writes its bug report parts (Diagnostics::Folders::Reports)."""
        return TEST_HOME / f"prefix-{name or 'host'}/pfx/drive_c/users/steamuser/Documents/My Games/Skyrim Special Edition/SkyrimCoop/reports"

    def capture_failure(ctx, name, result, error):
        """A failed scenario as a player's /bug from the host: each game still up writes its part (log, messages,
        snapshot, the host's save) and its status as the failure was raised (at-failure.json, before the scenario's
        cleanup), then coop_report.py reads them as one timeline. Returns the report's id."""
        before = set(ctx.reports_folder("").glob("*"))
        ctx.player_command("", {"cmd": "bug_report", "text": f"{name}: {result}"})
        folder = ctx.wait_for("[host] the failure's bug report is written",
                          lambda: next((p for p in set(ctx.reports_folder("").glob("*")) - before if any(p.glob("host-*/note.txt"))), None), 30)
        with contextlib.suppress(TestFailure):
            ctx.wait_for("[client] its part of the report is written",
                     lambda: any((ctx.reports_folder("client") / folder.name).glob("guest-*/note.txt")), 15)
        for game, status in error.status.items():
            for part in (ctx.reports_folder(game) / folder.name).glob("*-*"):
                if status:
                    (part / "at-failure.json").write_text(json.dumps(status))
        sys.path.insert(0, str(ROOT / "scripts"))
        import coop_report
        coop_report.main([str(ctx.reports_folder("")), str(ctx.reports_folder("client")), "--id", folder.name, "--since", "30"])
        return folder.name

    def comparison_checks(ctx, known_world, known_locks):
        ctx.wait_for("the guest follows the host's weather and calendar", lambda: not ctx.world_sync_problems(), 5)
        if problems := ctx.check_desync():
            raise ctx.failure("desync afterwards: " + " | ".join(problems))
        if dialogue := ctx.check_dialogue_trace():
            raise ctx.failure("the menus differ in a conversation: " + " | ".join(dialogue[:6]))
        if refused := ctx.replay_refusals_line(ctx.flow_started):
            log(f"copies refused replayed animations: {refused}")
        if world := ctx.check_world(known_world):
            raise ctx.failure(f"world differs afterwards ({len(world)} references): " +
                              " | ".join(describe_world_differences(world, 20)))
        new_locks = [v for k, v in ctx.lock_order_reports().items() if k not in known_locks]
        for game, report, ours in new_locks:
            if not ours:
                log(f"warning: engine-only lock-order pattern on {game}: {report}")
        if ours_locks := [f"{game}: {report}" for game, report, ours in new_locks if ours]:
            raise ctx.failure("lock order (our code on the stack): " + " | ".join(ours_locks))

    def keep_failure(ctx, name, error):
        """Persist original evidence before the runner restores the fixture."""
        if not getattr(error, "status", None):
            error.status = {}
            for game in ctx.games:
                with contextlib.suppress(Exception):
                    error.status[game.name] = game.read_status()
        directory = TEST_HOME / "results" / f"{time.strftime('%Y%m%d-%H%M%S')}-{uuid.uuid4().hex[:6]}"
        directory.mkdir(parents=True)
        path = directory / "at-failure.json"
        path.write_text(json.dumps({"name": name, "phase": getattr(error, "phase", None) or ctx.phase,
                                    "checkpoint": getattr(error, "checkpoint", None), "error": str(error),
                                    "status": error.status}, indent=2) + "\n")
        log(f"Failure evidence: {path}")
        return path

    def run_flow(ctx, name, known_world, *, until_fail=False):
        """Own one flow's setup, actions, postchecks and restoration; retain every failure by phase."""
        flow = ctx.registry[name]
        started = time.monotonic()
        failures = []
        restored = True
        known_locks = {}
        original_step = {}
        ctx.CURRENT_STEP.update(description=None, time=time.time())
        ctx.FAILING = False
        ctx.cleanup_errors = []
        previous_capture = ctx.STATUS_AT_FAILURE
        ctx.STATUS_AT_FAILURE = True

        def setup():
            nonlocal known_locks
            if flow.fixture == "saved-story":
                homes = [ctx.load_home(game.name) for game in ctx.games]
                if not all(home and home.get("cell_name") is None and home.get("world", {}).get("live") for home in homes):
                    raise ctx.failure("this flow requires duo --save with its documented story save")
            known_locks = ctx.lock_order_reports()
            ctx.check_dialogue_trace()  # from here on: this flow's conversations
            ctx.flow_started = time.time()

        try:
            for phase, action in (("setup", setup), ("action", lambda: flow.run(ctx)),
                                  ("comparison", lambda: ctx.comparison_checks(known_world, known_locks))):
                ctx.phase = phase
                ctx.observe("phase", name=name, phase=phase)
                try:
                    action()
                except BaseException as error:
                    failures.append((getattr(error, "phase", None) or phase, error))
                    original_step = dict(ctx.CURRENT_STEP)
                    try:
                        ctx.keep_failure(name, error)
                    except Exception as capture_error:
                        log(f"Evidence capture failed: {capture_error}")
                    break
            failures.extend(("cleanup", error) for error in ctx.cleanup_errors
                            if not any(error is existing for _, existing in failures))
            if not (failures and until_fail):
                ctx.phase = "cleanup"
                ctx.observe("phase", name=name, phase="cleanup")
                try:
                    if leftovers := ctx.settle_baseline():
                        raise ctx.failure("left behind: " + " | ".join(leftovers))
                    if world := ctx.check_world(known_world):
                        raise ctx.failure("world left different: " + " | ".join(describe_world_differences(world, 20)))
                except BaseException as error:
                    restored = False
                    failures.append(("cleanup", error))
                    try:
                        ctx.keep_failure(name, error)
                    except Exception as capture_error:
                        log(f"Cleanup evidence capture failed: {capture_error}")
        finally:
            ctx.phase = None
            ctx.STATUS_AT_FAILURE = previous_capture
        if failures and not isinstance(failures[0][1], Exception):
            raise failures[0][1]
        return FlowResult(name, time.monotonic() - started, failures, restored, original_step or dict(ctx.CURRENT_STEP))

    def command_scenario(ctx, args):
        ctx.HOLD = args.hold
        names = ctx.scenario_names(args.names, args.tag)
        if getattr(args, "dry_run", False):
            print("Would run scenario" + (f" --tag {args.tag}" if args.tag else ":"))
            for name in names:
                print(f"  {name}")
            return
        for game in ctx.games:
            if not game.read_status():
                raise ctx.failure(f"{game.name or 'host'} game is not running: start `{sys.argv[0]} duo --keep-running` first")
        if not all(ctx.load_home(game.name) for game in ctx.games):
            log("warning: no baseline recorded by duo; using the players' current placement")
            for game in ctx.games:
                snapshot = ctx.player_snapshot(game.name)
                ctx.reset_player(game.name, {"position": snapshot["placement"]["position"],
                                            "heading": snapshot["placement"]["rotation"][2],
                                            "equipment": sorted(i for i in snapshot.get("equipment", []) if i not in TEST_ITEMS)})
            ctx.save_home()
        ctx.upgrade_home()
        until_fail = getattr(args, "until_fail", 0)
        order = [name for _ in range(until_fail or args.repeat) for name in names]
        seed = args.seed
        if args.shuffle:
            seed = args.seed if args.seed is not None else random.randrange(1 << 30)
            random.Random(seed).shuffle(order)
            log(f"Shuffled order (--seed {seed} repeats it): {' '.join(order)}")
        ctx.observe = getattr(args, "observe", lambda event, **data: None)
        ctx.observe("order", order=order, seed=seed)
        suite_start = time.monotonic()
        ctx.phase = "setup"
        try:
            if problems := ctx.settle_baseline():
                raise ctx.failure("cannot reach the baseline before the first scenario: " + " | ".join(problems))
            baseline_world = ctx.check_world()
        except Exception as error:
            ctx.keep_failure("baseline", error)
            raise
        finally:
            ctx.phase = None
        if baseline_world:
            log("warning: the worlds differ at the baseline: " + " | ".join(describe_world_differences(baseline_world, 20)))
        known_world = world_pairs(baseline_world)
        runs = []
        for index, name in enumerate(order):
            log(f"=== {name}: {ctx.registry[name].doc}")
            ctx.observe("start", name=name, index=index)
            marks, wall_start = ctx.trail_marks(), time.time()
            result = ctx.run_flow(name, known_world, until_fail=bool(until_fail))
            runs.append((name, result.seconds, result.text))
            if result.failures:
                ctx.CURRENT_STEP.update(result.checkpoint)
                error = result.failures[0][1]
                try:
                    log(f"=== {name}: what each game saw\n" + ctx.failure_digest(marks, wall_start, getattr(error, "time", time.time())))
                except Exception as digest_error:
                    log(f"Failure digest unavailable: {digest_error}")
                if until_fail:
                    try:
                        report = ctx.capture_failure(name, result.text, error)
                        log(f"=== --until-fail: bug report {report}; batch restoration skipped")
                    except Exception as capture_error:
                        log(f"Bug report capture failed: {capture_error}; original failure: {result.text}")
            ctx.observe("result", name=name, seconds=result.seconds, result=result.text)
            log(f"=== {name}: {result.text}")
            if (result.failures and (until_fail or args.fail_fast)) or not result.restored:
                reason = "the games could not be reset to the baseline" if not result.restored else "--until-fail" if until_fail else "--fail-fast"
                runs.extend((skipped, 0.0, f"NOT RUN ({reason})") for skipped in order[index + 1:])
                break
        print()
        for name, seconds, result in runs:
            print(f"{name:28s} {seconds:5.1f}s  {result}")
        if args.repeat > 1 or args.shuffle:
            print()
            for name in names:
                mine = [result for run, _, result in runs if run == name]
                print(f"{name:28s} {sum(r == 'PASS' for r in mine)}/{len(mine)} passed")
        print(f"{'total':28s} {time.monotonic() - suite_start:5.1f}s")
        if any(r != "PASS" for _, _, r in runs):
            raise SystemExit(1)
