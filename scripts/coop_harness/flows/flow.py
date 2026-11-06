"""A playthrough flow (docs/design/playthrough-flows.md): an ordered chain of steps a party plays, each ending with the
party in the state the next one needs. A failed verb or check stops the flow and names the step; a copy difference a
player would see fails the step and the flow goes on; cosmetic ones are only reported."""
import json
import math
import time
from dataclasses import dataclass, field
from typing import Callable

from ..common import TEST_HOME
from ..comparisons import actor_of, classify_problem
from ..errors import TestFailure

TOGETHER = 1500.0  # units: a party further apart than this has split up
HANG_MS = 3000.0  # a frame longer than this is a hang a player notices
QUEST_RUNNING, QUEST_COMPLETED = 0x1, 0x2  # TESQuest flags (harness `quest`)
FLOWS = TEST_HOME / "flows"


@dataclass
class Step:
    name: str
    do: Callable  # do(ctx): the party's play, through the session's verbs
    checks: list = field(default_factory=list)  # check(ctx): raises ctx.failure when the state is wrong; stops the flow
    sees: list = field(default_factory=list)  # the same, for a difference players see that the next step survives
    checkpoint: bool = False  # the host saves here: `flows NAME --from <next step>` starts from it
    allow: frozenset = frozenset()  # health checks this step skips: "together", "menus", "alive"


@dataclass
class StepResult:
    name: str
    verdict: str  # PASS, FAIL (stopped the flow) or SEEN (passed, but players would see a copy difference)
    seconds: float
    error: str = ""
    evidence: str = ""
    visible: list = field(default_factory=list)
    cosmetic: list = field(default_factory=list)
    perf: dict = field(default_factory=dict)


def checkpoint_name(flow, step):
    return f"flow-{flow}-{step}"


def start_index(steps, start):
    """The index `--from STEP` starts at, and the checkpoint save it boots from (the step before's), or (0, None)."""
    if not start:
        return 0, None
    names = [step.name for step in steps]
    if start not in names:
        raise ValueError(f"no step {start!r}: {', '.join(names)}")
    index = names.index(start)
    earlier = [step for step in steps[:index] if step.checkpoint]
    if not earlier:
        raise ValueError(f"no checkpoint before {start!r}: run the flow from its start")
    if earlier[-1] is not steps[index - 1]:
        raise ValueError(f"{start!r} does not follow a checkpoint: start from the step after {earlier[-1].name!r}")
    return index, earlier[-1].name


def resumable(steps, name):
    """Whether `--from name` can start there (the step before it saved a checkpoint)."""
    try:
        return start_index(steps, name)[1] is not None
    except ValueError:
        return False


# --- Checks a step ends with ---

def together(ctx):
    """Both games show the other player, in the same cell, close together."""
    if not ctx.both_see_each_other():
        raise ctx.failure("the games don't show each other's player")
    where = {game: (ctx.player_snapshot(game).get("placement") or {}) for game in ("", "client")}
    worlds = {game: place.get("worldspace") or 0 for game, place in where.items()}
    cells = {game: place.get("cell") for game, place in where.items()}
    if worlds[""] != worlds["client"] or (not worlds[""] and cells[""] != cells["client"]):  # outdoors: neighbouring cells do
        raise ctx.failure(f"the players are in different places: host cell {cells['']} (worldspace {worlds['']:X}), "
                          f"guest cell {cells['client']} (worldspace {worlds['client']:X})")
    apart = math.dist(ctx.my_position("")[:2], ctx.my_position("client")[:2])
    if apart > TOGETHER:
        raise ctx.failure(f"the players are {apart:.0f} units apart (at most {TOGETHER:.0f})")


def quest_at(quest, stage, seconds=10):
    """Quest `quest` (form id) at `stage` in both games (the guest follows the host's)."""
    def check(ctx):
        stages = {}
        def there():
            stages.update({game or "host": ctx.quest_snapshot(game, quest)["stage"] for game in ("", "client")})
            return all(at == stage for at in stages.values())
        try:
            ctx.wait_for(f"quest {quest:08X} at stage {stage} in both games", there, seconds)
        except TestFailure:
            raise ctx.failure(f"quest {quest:08X} not at stage {stage} in both games: {stages}") from None
    check.__name__ = f"quest {quest:08X} at {stage}"
    return check


def quest_completed(quest, seconds=10):
    def check(ctx):
        flags = {}
        def done():
            flags.update({game or "host": ctx.quest_snapshot(game, quest)["flags"] for game in ("", "client")})
            return all(value & QUEST_COMPLETED for value in flags.values())
        try:
            ctx.wait_for(f"quest {quest:08X} completed in both games", done, seconds)
        except TestFailure:
            raise ctx.failure(f"quest {quest:08X} not completed in both games: flags {flags}") from None
    check.__name__ = f"quest {quest:08X} completed"
    return check


def same_items(handle):
    """Container or corpse `handle` holds the same in both games (repro `expect items`)."""
    def check(ctx):
        ctx.refresh_handles()
        ctx.expect(handle, "items")
    check.__name__ = f"{handle} holds the same in both games"
    return check


def carries(game, item, count=1):
    """That game's player carries at least `count` of `item` (count 0: none at all)."""
    def check(ctx):
        held = ctx.item_count(game, 0x14, item) or 0
        if (held < count) if count else held:
            raise ctx.failure(f"[{game or 'host'}] carries {held} of {item:X}, expected {'none' if not count else f'{count}+'}")
    check.__name__ = f"{game or 'host'} carries {count} of {item:X}"
    return check


# --- After every step ---

def health(ctx, step):
    """What every step leaves right, unless the step allows otherwise: the party together, nobody stuck in a menu or
    down, the guest still connected."""
    if "together" not in step.allow:
        together(ctx)
    for game in ("", "client"):
        who = game or "host"
        if "menus" not in step.allow:
            try:
                ctx.wait_for(f"[{who}] takes input after {step.name}", lambda game=game: ctx.takes_input(game), 15)
            except TestFailure:
                raise ctx.failure(f"[{who}] stuck: menus {sorted(ctx.menus(game) - {'HUD Menu'})}") from None
        state = ctx.actor_row(game, 0x14).get("state") or {}
        if "alive" not in step.allow and (state.get("dead") or state.get("bleeding_out")):
            raise ctx.failure(f"[{who}] the player is down: {state}")
    if not ((ctx.game("").read_status() or {}).get("network") or {}).get("round_trips"):
        raise ctx.failure("the host has no connection: the guest dropped")


def perf_over(ctx, seconds):
    """Each game's frame times over the step's last `seconds` (at most a minute: the plugin's window)."""
    window = int(min(max(seconds, 1.0), 60.0) * 1000)
    out = {}
    for game in ("", "client"):
        frame = ((ctx.game(game).rpc({"cmd": "perf", "window_ms": window}, timeout=10) or {}).get("perf") or {}).get("frame_ms") or {}
        out[game or "host"] = {key: round(frame.get(key, 0.0), 1) for key in ("p99", "max")}
    return out


def hang_in(perf):
    """A game whose longest frame was a hang, or None."""
    return next((f"{game}: a {values['max']:.0f} ms frame" for game, values in perf.items() if values["max"] > HANG_MS), None)


def actor_distances(worlds, players):
    """{(game, form id): distance to the nearest player} for every actor in both games' snapshots."""
    out = {}
    for game, world in worlds.items():
        for form, row in world.items():
            place = (row.get("placement") or {}).get("position")
            if place and players:
                out[(game, int(form))] = min(math.dist(place[:2], p[:2]) for p in players)
    return out


def split_differences(problems, distances):
    """{key: message} -> (visible, cosmetic) message lists (comparisons.classify_problem)."""
    visible, cosmetic = [], []
    for key, message in problems.items():
        actor = actor_of(key)
        (visible if classify_problem(key, distances.get(actor) if actor else None) == "visible" else cosmetic).append(message)
    return visible, cosmetic


def copy_differences(ctx, known):
    """The owner-vs-copy differences new since `known`, split into what players see and what they don't."""
    problems = ctx.lasting_desyncs(known=known)
    host, guest = ctx.worlds()
    players = [p for p in (ctx.my_position(""), ctx.my_position("client")) if p]
    return problems, split_differences(problems, actor_distances({"host": host, "client": guest}, players))


def save_checkpoint(ctx, name):
    """The host saves the party (repro `checkpoint`); a refused save names the menus that held it (a game saves with none
    open)."""
    try:
        ctx.checkpoint(name)
    except TestFailure as error:
        menus = {game or "host": sorted(ctx.menus(game) - {"HUD Menu"}) for game in ("", "client")}
        raise ctx.failure(f"the checkpoint {name} was not saved ({error}); menus open: {menus}") from None


# --- The run ---

def run_flow(ctx, flow, start=None):
    """Plays the flow's steps from `start` (a step name; its checkpoint was booted), and returns their results."""
    index, _ = start_index(flow.STEPS, start)
    known = set(ctx.lasting_desyncs())  # what differs where the flow starts is the save's own, not this run's
    results = []
    for step in flow.STEPS[index:]:
        began = time.monotonic()
        ctx.CURRENT_STEP.update(description=f"{flow.NAME}/{step.name}", time=time.time())
        try:
            step.do(ctx)
            for check in step.checks:
                check(ctx)
            health(ctx, step)
            perf = perf_over(ctx, time.monotonic() - began)
            if hang := hang_in(perf):
                raise ctx.failure(f"a hang during {step.name}: {hang}")
            if step.checkpoint:
                save_checkpoint(ctx, checkpoint_name(flow.NAME, step.name))
            problems, (visible, cosmetic) = copy_differences(ctx, known)  # a game gone here fails the step too
        except TestFailure as error:
            evidence = ""
            try:
                evidence = str(ctx.keep_failure(f"{flow.NAME}/{step.name}", error))
            except Exception as capture:
                evidence = f"(evidence capture failed: {capture})"
            results.append(StepResult(step.name, "FAIL", round(time.monotonic() - began, 1), str(error), evidence))
            break
        known |= set(problems)  # each difference is reported at the step it appeared
        for check in step.sees:
            try:
                check(ctx)
            except TestFailure as error:
                visible.append(str(error))
        results.append(StepResult(step.name, "SEEN" if visible else "PASS", round(time.monotonic() - began, 1),
                                  visible=visible, cosmetic=cosmetic, perf=perf))
    return results


def verdict(results, steps):
    """PASS only when every step ran and none failed or showed players a difference."""
    if len(results) < len(steps) or any(result.verdict != "PASS" for result in results):
        return "FAIL"
    return "PASS"


def summary(flow, results, start=None, seconds=0.0):
    """The run as markdown: a line per step, then each step's failure, evidence and copy differences."""
    index, _ = start_index(flow.STEPS, start)
    steps = flow.STEPS[index:]
    lines = [f"# {flow.NAME}: {verdict(results, steps)}", "", flow.DOC, "",
             f"{len(results)} of {len(steps)} steps ran" + (f" (from {start})" if start else "") + f", {seconds / 60:.1f} min.", ""]
    ran = {result.name: result for result in results}
    for step in steps:
        result = ran.get(step.name)
        if not result:
            lines.append(f"- {step.name}: NOT RUN")
            continue
        extra = f": {result.error}" if result.error else f" ({len(result.visible)} seen by players)" if result.visible else ""
        lines.append(f"- {step.name}: {result.verdict} {result.seconds:.0f} s{extra}")
    for result in results:
        if not (result.error or result.visible or result.cosmetic):
            continue
        lines += ["", f"## {result.name}", ""]
        if result.error:
            again = f" --from {result.name}" if resumable(flow.STEPS, result.name) else ""
            lines += [f"Failed: {result.error}", f"Evidence: {result.evidence}",
                      f"Again from here: `coop_test.py flows {flow.NAME}{again}`", ""]
        if result.visible:
            lines += ["Copy differences players see:", *[f"- {line}" for line in result.visible], ""]
        if result.cosmetic:
            lines += ["Cosmetic copy differences (not the verdict):", *[f"- {line}" for line in result.cosmetic], ""]
        if result.perf:
            lines.append("Frames: " + ", ".join(f"{game} p99 {v['p99']} ms, max {v['max']} ms" for game, v in result.perf.items()))
    return "\n".join(lines) + "\n"


def write_report(flow, results, start=None, seconds=0.0, folder=None):
    folder = folder or FLOWS / f"{time.strftime('%Y%m%d-%H%M%S')}-{flow.NAME}"
    folder.mkdir(parents=True, exist_ok=True)
    (folder / "summary.md").write_text(summary(flow, results, start, seconds))
    (folder / "summary.json").write_text(json.dumps([result.__dict__ for result in results], indent=2) + "\n")
    return folder
