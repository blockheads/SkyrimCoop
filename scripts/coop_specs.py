#!/usr/bin/env python3
"""The shared specs (Code/tests/specs/*.json) as Skyrim scenarios: the same files GameTests runs against the fake
engine (docs/design/testing.md, "Shared specs"). Each spec is the scenario "spec:<name>", tagged `spec` and its own.

  coop_specs.py [names] [scenario options]   run them against the running duo (as `coop_test.py scenario`)
  coop_specs.py --dry [names]                print each spec's steps as the harness calls they make, run nothing

Steps: {"do": "script", "game", "call": "Class.Function", "args", "by": "player"?}, {"do": "leave"|"join", "game"},
{"do": "marker", "from", "to"}; {"expect": "controls", "game", "equals": "0x..."|"taken": bool, "now"?},
{"expect": "shake", "game", "shaking": bool, "now"?}; {"do": "lock", "game", "ref", "locked", "level"?},
{"do": "add_items"|"remove_items", "game", "ref", "item", "count"} (the console's additem / removeitem);
{"expect": "lock", "game", "ref", "locked", "level"?}, {"expect": "items", "game", "ref", "item", "count"}. A ref is a
hex id or a role ("inner-door"). An expectation waits (5 s) unless "now". "cleanup" always runs. "tags" (default
scripts) are the scenario's tags besides `spec`.
"""
import json
import sys
from pathlib import Path

import coop_test as ct

SPECS = Path(__file__).resolve().parents[1] / "Code/tests/specs"
GAMES = {"host": "", "guest": "client"}
WAIT = 5


def load(names=None):
    specs = [json.loads(path.read_text()) for path in sorted(SPECS.glob("*.json"))]
    return [spec for spec in specs if not names or spec["name"] in names]


def instance(step, key="game"):
    name = step.get(key, "host")
    if name not in GAMES:
        raise ct.TestFailure(f"no game {name!r} in the duo")
    return GAMES[name]


def ref(value):
    """A reference: a hex id, or a role this runner finds in the cell ("inner-door": coop_test.inner_door())."""
    return ct.inner_door() if value == "inner-door" else int(value, 16)


def describe(spec, index, step, cleanup=False):
    return f"{spec['name']}, {'cleanup' if cleanup else 'step'} {index + 1}: {json.dumps(step)}"


def run_step(spec, index, step, cleanup=False):
    what = describe(spec, index, step, cleanup)
    if "do" in step:
        kind = step["do"]
        if kind == "script":
            klass, function = step["call"].split(".", 1)
            call = ct.player_script_call if step.get("by") == "player" else ct.script_call
            call(instance(step), klass, function, step.get("args", []))
        elif kind == "leave":
            if instance(step) != "client":
                raise ct.TestFailure(f"{what}: only the guest leaves")
            if ct.game_state("client").get("online") is not False:
                ct.disconnect_client()
        elif kind == "join":
            if ct.game_state("client").get("online") is False:
                ct.rejoin_client()
        elif kind == "lock":
            ct.lock_on(instance(step), ref(step["ref"]), step["locked"], step.get("level", 0))
        elif kind in ("add_items", "remove_items"):
            verb = "additem" if kind == "add_items" else "removeitem"
            ct.console_on(instance(step), f"{ref(step['ref']):X}.{verb} {ref(step['item']):X} {step['count']}")
        elif kind == "marker":
            ct.screen_marker(instance(step, "from"), instance(step, "to"))
        else:
            raise ct.TestFailure(f"{what}: no such step")
        return
    game = instance(step)
    expect = step.get("expect")
    if expect == "controls" and "equals" in step:
        mask = int(step["equals"], 16)
        holds = lambda: ct.screen(game)["controls"] == mask
    elif expect == "controls" and "taken" in step:
        holds = lambda: (ct.screen(game)["controls"] != ct.ALL_CONTROLS) == step["taken"]
    elif expect == "shake":
        holds = lambda: (ct.screen(game)["shake"]["seconds"] > 0) == step["shaking"]
    elif expect == "lock":
        door = ref(step["ref"])
        holds = lambda: ct.door_lock(game, door)[0] == step["locked"] and ("level" not in step or ct.door_lock(game, door)[1] == step["level"])
    elif expect == "items":
        holder, item = ref(step["ref"]), ref(step["item"])
        holds = lambda: (ct.item_count(game, holder, item) or 0) == step["count"]
    else:
        raise ct.TestFailure(f"{what}: no such expectation")
    if step.get("now"):
        if not holds():
            if expect in ("controls", "shake"):
                state = ct.screen(game)
                raise ct.TestFailure(f"{what}: controls {state['controls']:X}, shake {state['shake']['seconds']:.2f}")
            raise ct.TestFailure(f"{what}: not so")
    else:
        ct.wait_for(what, holds, WAIT)


def scenario(spec):
    def run():
        try:
            for index, step in enumerate(spec["steps"]):
                run_step(spec, index, step)
        finally:
            for index, step in enumerate(spec.get("cleanup", [])):
                run_step(spec, index, step, cleanup=True)
    run.__doc__ = spec.get("doc", spec["name"])
    return run


def register(scenarios, tags, names=None):
    """Adds each spec to coop_test's scenarios and tags; returns their scenario names."""
    added = []
    for spec in load(names):
        name = f"spec:{spec['name']}"
        scenarios[name] = scenario(spec)
        tags[name] = {"spec", *spec.get("tags", ["scripts"])}
        added.append(name)
    return added


def dry(names):
    """Each step as the harness call it makes, nothing run: checks the specs speak the runner's vocabulary."""
    calls = {"script": "script_call", "leave": "disconnect_client", "join": "rejoin_client", "marker": "screen_marker",
             "lock": "lock_on", "add_items": "console_on additem", "remove_items": "console_on removeitem"}
    for spec in load(names):
        print(f"spec:{spec['name']}")
        for part in ("steps", "cleanup"):
            label = "step" if part == "steps" else "cleanup"
            for index, step in enumerate(spec.get(part, [])):
                if "do" in step:
                    if step["do"] not in calls:
                        raise SystemExit(f"{describe(spec, index, step, part == 'cleanup')}: no such step")
                    call = "player_script_call" if step.get("by") == "player" else calls[step["do"]]
                    print(f"  {label:7s} {index + 1:2d}  {call} {json.dumps({k: v for k, v in step.items() if k != 'do'})}")
                else:
                    if step.get("expect") not in ("controls", "shake", "lock", "items"):
                        raise SystemExit(f"{describe(spec, index, step, part == 'cleanup')}: no such expectation")
                    print(f"  {label:7s} {index + 1:2d}  {'check' if step.get('now') else 'wait_for'} {json.dumps(step)}")


def main():
    arguments = sys.argv[1:]
    if "--dry" in arguments:
        dry([a for a in arguments if a != "--dry"])
        return
    names = [a for a in arguments if not a.startswith("-") and not a.isdigit()]
    options = [a for a in arguments if a not in names]
    added = register(ct.SCENARIOS, ct.TAGS, [n.removeprefix("spec:") for n in names] or None)
    sys.argv = [str(Path(ct.__file__)), "scenario", *added, *options]
    ct.main()


if __name__ == "__main__":
    main()
