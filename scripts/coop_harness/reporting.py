"""Reporting for the co-op harness."""
import sys
from .common import (
    DLL,
    ROOT,
)
from .errors import TestFailure


class Reporting:
    def log_trail(ctx, instance, since=0):
        """That game's plugin log lines (info and up) numbered `since` and later, and the next number (harness log_trail)."""
        response = ctx.player_command(instance, {"cmd": "log_trail", "since": since})
        return response["lines"], response["next"]

    def trail_marks(ctx):
        """Where each game's log trail is now, to read from on a failure."""
        marks = {}
        for name in ("", "client"):
            try:
                marks[name] = ctx.log_trail(name, 1 << 62)[1]
            except (TestFailure, OSError, KeyError):
                marks[name] = None
        return marks

    def symbolized(ctx, text):
        """A log line's backtrace frames ("backtrace #NN module+0xOFF") with names (scripts/re/symbolize_crash.py); the
        lock-order checker's own frames left out."""
        if "backtrace #" not in text:
            return text
        if not ctx.SYMBOLIZE:
            sys.path.insert(0, str(ROOT / "scripts/re"))
            import symbolize_crash
            ctx.SYMBOLIZE.update(module=symbolize_crash, engine=symbolize_crash.engine_resolver())
        module = ctx.SYMBOLIZE["module"]
        out = []
        for line in text.split("\n"):
            match = module.FRAME.search(line)
            if not match:
                out.append(line)
                continue
            number, where, offset = match.group(1), match.group(2), int(match.group(3), 16)
            if where.lower() == "skyrimse.exe":
                name = ctx.SYMBOLIZE["engine"](offset)
            elif where.lower().startswith(DLL.stem.lower()):
                name = module.symbolize_ours(DLL, offset)
                if "LockOrder.cpp" in name:
                    continue
            else:
                name = ""
            out.append(f"      #{number} {where}+{offset:#x}  {name}")
        return "\n".join(out)

    def failure_digest(ctx, marks, started, failed_at, keep=60, context=10):
        """What each game saw during a failed scenario, up to the failure: its open menus, its log lines from the failing
        step on (marked '>', the last `keep`), the `context` before them and every warning and error of the scenario.
        Printed under the failure, so it explains itself."""
        step = ctx.CURRENT_STEP
        out = [f"  failing step: {step['description']}" if step["description"] else "  failing step: none waited on"]
        for name in ("", "client"):
            who = name or "host"
            try:
                lines, _ = ctx.log_trail(name, marks.get(name) or 0) if marks.get(name) is not None else ([], 0)
                open_menus = sorted(ctx.menus(name) - {"HUD Menu"})
            except (TestFailure, OSError, KeyError) as error:
                out.append(f"  [{who}] no trail: {error}")
                continue
            # A line logged within a moment of the failure is still its own; later ones are the clean-up's.
            before = [line for line in lines if line["time_ms"] / 1000 <= failed_at + 0.25]
            in_step = [line for line in before if line["time_ms"] / 1000 >= step["time"]][-keep:]
            earlier = [line for line in before if line["time_ms"] / 1000 < step["time"]]
            shown = {line["seq"] for line in earlier[-context:]}
            earlier = [line for line in earlier if line["seq"] in shown or line["level"] != "info"]
            left_out = len(lines) - len(in_step) - len(earlier)
            out.extend(ctx.screen_lines(name))
            out.append(f"  [{who}] menus open: {', '.join(open_menus) or 'none'}; log lines: {len(in_step)} in the failing step, "
                       f"{len(earlier)} before it" + (f" ({left_out} more left out)" if left_out else "") + ":")
            for line in earlier + in_step:
                at = line["time_ms"] / 1000
                mark = ">" if at >= step["time"] else " "
                level = "" if line["level"] == "info" else f"{line['level']}: "
                out.append(f"  {mark} {at - started:6.2f}s {level}{ctx.symbolized(line['text'])}")
        return "\n".join(out)

    def engine_events(ctx, instance, since=0):
        """That game's engine script events from number `since` on (harness EngineEvents), and the next number."""
        response = ctx.player_command(instance, {"cmd": "engine_events", "since": since})
        return response["events"], response["next"]

    def race_of(ctx, instance, actor):
        status = ctx.game(instance).read_status() or {}
        return (status.get("world") or {}).get(str(actor), {}).get("identity", {}).get("race")
