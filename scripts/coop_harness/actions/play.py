"""Checked actions (docs/design/checked-actions.md): the player acts frame by frame inside the game, which checks
the action's conditions every frame and reports how it ended. The verbs scenarios and the MCP layer play with."""
import bisect
import math
import time

from ..utilities import log


def _name(instance):
    return instance or "host"


class PlayActions:
    def act_start(ctx, instance, spec):
        """Starts a checked action in that game (the plugin's `act`); returns its id."""
        return ctx.player_command(instance, {"cmd": "act", **spec})["id"]

    def act_finish(ctx, instance, action_id, timeout):
        """Waits for that game's action to end: its outcome {status, reason, frame, seconds, watched, series?}."""
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            state = ctx.player_command(instance, {"cmd": "act_state"})
            if state.get("id") != action_id:
                raise ctx.failure(f"[{_name(instance)}] its action {action_id} was replaced by {state.get('id')}")
            if state.get("phase") == "ended":
                return state["outcome"]
            ctx.check_for_crash()
            time.sleep(0.05)
        ctx.player_command(instance, {"cmd": "act_cancel"})
        raise ctx.failure(f"[{_name(instance)}] its action {action_id} did not end in {timeout:.0f}s")

    def act(ctx, instance, spec, *, expect="done"):
        """One checked action, waited for. Fails the scenario unless it ends as `expect` (None: any), naming the
        frame and the condition that broke. Returns the outcome (its watched values under "watched")."""
        what = spec.get("name", "acts")
        ctx.CURRENT_STEP.update(description=f"[{_name(instance)}] {what}", time=time.time())
        log(f"[{_name(instance)}] {what}...")
        action_id = ctx.act_start(instance, spec)
        outcome = ctx.act_finish(instance, action_id, spec.get("ready_seconds", 5) + spec.get("seconds", 20) + 10)
        if expect and outcome["status"] != expect:
            raise ctx.failure(f"[{_name(instance)}] {what}: {outcome['status']}"
                              + (f", {outcome['reason']}" if outcome.get("reason") else "")
                              + (f" (watched {ctx.describe_watched(outcome)})" if outcome.get("watched") else ""))
        return outcome

    def act_both(ctx, specs, *, expect="done"):
        """Checked actions in several games at once ({instance: spec}), e.g. one game acting while another watches
        what it shows: {instance: outcome}, each with its series stamped by the wall clock (one machine)."""
        started = {instance: ctx.act_start(instance, spec) for instance, spec in specs.items()}
        outcomes = {}
        for instance, action_id in started.items():
            spec = specs[instance]
            outcomes[instance] = ctx.act_finish(instance, action_id, spec.get("ready_seconds", 5) + spec.get("seconds", 20) + 10)
        for instance, outcome in outcomes.items():
            if expect and outcome["status"] != expect:
                raise ctx.failure(f"[{_name(instance)}] {specs[instance].get('name', 'acts')}: {outcome['status']}"
                                  + (f", {outcome['reason']}" if outcome.get("reason") else ""))
        return outcomes

    def describe_watched(ctx, outcome):
        return ", ".join(f"{name} {value['min']:.1f} to {value['max']:.1f}" for name, value in outcome["watched"].items()
                         if value.get("min") is not None)

    def series_at(ctx, outcome, column, wall_ms):
        """A watched value (its column in the series, 0 the first watch) at that wall-clock time, the nearest sample."""
        rows = outcome.get("series") or []
        if not rows:
            return None
        times = [row[0] for row in rows]
        i = min(bisect.bisect_left(times, wall_ms), len(rows) - 1)
        if i and abs(times[i - 1] - wall_ms) < abs(times[i] - wall_ms):
            i -= 1
        return rows[i][1 + column]

    def bearing_of(ctx, instance, target):
        """Which side of `target` that game's player stands on (degrees, 0 north, 90 east), as `place_before` takes it."""
        me, at = ctx.my_position(instance), ctx.body_of(instance, target)["position"]
        return math.degrees(math.atan2(me[0] - at[0], me[1] - at[1])) % 360

    # --- Verbs ---

    def turn_to(ctx, instance, *, yaw=None, pitch=None, face=None, rate=60, keep=(), watch=(), seconds=10, name=None):
        """The player turns by mouse to a heading and pitch, or toward a reference, at most `rate` degrees a second,
        until it aims there within half a degree."""
        turn = {"rate": rate}
        for key, value in (("yaw", yaw), ("pitch", pitch), ("face", face)):
            if value is not None:
                turn[key] = value
        return ctx.act(instance, {"name": name or "turns", "ready": ["takes_input"], "turn": turn, "until": ["aimed"],
                                  "keep": list(keep), "watch": list(watch), "seconds": seconds})

    def grab(ctx, instance, target):
        """The player, standing and looking at it, holds Activate until it holds `target` (a grab), then lets go of
        the button: the object stays held."""
        return ctx.act(instance, {"name": f"grabs {target:X}", "ready": ["takes_input", "on_ground"], "buttons": ["activate"],
                                  "until": [{"holding": target}], "seconds": 3})

    def carry(ctx, instance, held, buttons, seconds, *, keep=(), watch=()):
        """The player walks with `held` for that long (`buttons`: forward, back, strafes), failing on the frame it is
        no longer held."""
        return ctx.act(instance, {"name": f"carries {held:X} ({' '.join(buttons)} {seconds:g} s)", "ready": ["takes_input"],
                                  "buttons": list(buttons), "until": [{"for": seconds}], "seconds": seconds + 2,
                                  "keep": [{"holding": held}, *keep], "watch": list(watch)})
