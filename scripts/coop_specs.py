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

Actor steps: spawn_npc/stage_npc bind an actor name for kill/disable/enable/send_home and actor expectations.
go travels while away; deadly_hit damages the player's health. Actors agree, together and down wait for game
state; within overrides an expectation's wait. Staged and spawned actors are cleaned up after each run.
"""
import json
import math
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


def actor_state(value, game, actors):
    actor = actors[value] if value in actors else {"host": ref(value)}
    form = actor["host"]
    if game and actor.get("spawned"):
        form = next((int(form) for form, state in ct.actors_on(game).items()
                     if state.get("session_id") == actor["session_id"]), None)
    if form is None:
        return {"exists": False, "session_id": actor.get("session_id")}
    place = ct.actor_place(game, form)
    ct.select_instance(game)
    state = (((ct.read_status() or {}).get("world") or {}).get(str(form)) or {}).get("state", {})
    return {"form": form, "exists": bool(place), **place, **state}


def send_home(actor):
    form = actor["host"]
    ct.player_command("", {"cmd": "move_actor", "actor": form, "home": True})
    ct.wait_for(f"[host] {form:X} has gone home", lambda: not ct.host_npc_state(form), WAIT)
    try:
        ct.copy_follows_host(form, "goes home with its owner")
    finally:
        if actor.get("frozen"):
            ct.freeze("", form, False)
            actor["frozen"] = False


def restore_actors(actors):
    for actor in actors.values():
        form = actor["host"]
        place = ct.actor_place("", form)
        if not place:
            continue
        if actor.get("spawned"):
            ct.remove_host_npc(form)
        else:
            if place["disabled"]:
                ct.console_on("", f"{form:X}.enable")
                ct.wait_for(f"[host] {form:X} enabled for cleanup", lambda: bool(ct.host_npc_state(form)), WAIT)
            if ct.host_npc_state(form) or actor.get("frozen"):
                send_home(actor)


def describe(spec, index, step, cleanup=False):
    return f"{spec['name']}, {'cleanup' if cleanup else 'step'} {index + 1}: {json.dumps(step)}"


def run_step(spec, index, step, cleanup=False, actors=None):
    actors = actors if actors is not None else {}
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
        elif kind == "spawn_npc":
            form, session_id, copy = ct.spawn_host_npc(f"{ref(step['base']):08X}")
            actors[step["as"]] = {"host": form, "session_id": session_id, "copy": copy, "spawned": True}
        elif kind == "stage_npc":
            actor = actors[step["as"]] = {"host": ref(step["ref"]), "frozen": True}
            actor["copy"] = ct.stage_host_npc(actor["host"])
            actor["session_id"] = ct.host_npc_state(actor["host"])["session_id"]
        elif kind in ("kill", "disable", "enable", "send_home"):
            actor = actors[step["actor"]] if step["actor"] in actors else {"host": ref(step["actor"])}
            if kind == "send_home":
                send_home(actor)
            else:
                ct.console_on("", f"{actor['host']:X}.{kind}")
        elif kind == "deadly_hit":
            game = instance(step)
            health = ct.player_snapshot(game)["values"]["health"]
            ct.console_on(game, f"player.damageav health {math.ceil(max(health, 0) + 1000)}")
        elif kind == "go":
            game = instance(step)
            if ct.game_state(game).get("online") is not False:
                raise ct.TestFailure(f"{what}: go while away requires leaving first")
            before = ct.player_cell(game)
            cell = "Riverwood" if step["cell"] == "elsewhere" else step["cell"]
            ct.console_on(game, f"coc {cell}")
            ct.wait_for(what, lambda: ct.player_cell(game) not in (None, before) and ct.takes_input(game), 60)
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
    elif expect == "actors agree":
        holds = lambda: ct.check_desync() == []
    elif expect == "actor":
        observed = {}
        def holds():
            observed.clear()
            observed.update(actor_state(step["actor"], game, actors))
            return observed["exists"] and all(observed.get(key) == step[key]
                                               for key in ("dead", "disabled", "copy") if key in step)
    elif expect == "together":
        holds = lambda: ct.player_cell("") == ct.player_cell("client") and bool(ct.both_see_each_other())
    elif expect == "down":
        holds = lambda: ct.player_snapshot(game).get("state", {}).get("bleeding_out") == step["down"]
    else:
        raise ct.TestFailure(f"{what}: no such expectation")
    if step.get("now"):
        if not holds():
            if expect in ("controls", "shake"):
                state = ct.screen(game)
                raise ct.TestFailure(f"{what}: controls {state['controls']:X}, shake {state['shake']['seconds']:.2f}")
            state = actor_state(step["actor"], game, actors) if expect == "actor" else "not so"
            raise ct.TestFailure(f"{what}: {state}")
    else:
        try:
            ct.wait_for(what, holds, step.get("within", WAIT))
        except ct.TestFailure as error:
            state = None
            if expect == "actor":
                state = actor_state(step["actor"], game, actors)
            elif expect == "actors agree":
                state = ct.check_desync()
            raise ct.TestFailure(f"{error}" + (f"; observed {state}" if state is not None else "")) from None


def scenario(spec):
    def run():
        actors, failed = {}, False
        try:
            for index, step in enumerate(spec["steps"]):
                run_step(spec, index, step, actors=actors)
        except BaseException:
            failed = True
            raise
        finally:
            errors = []
            for index, step in enumerate(spec.get("cleanup", [])):
                try:
                    run_step(spec, index, step, cleanup=True, actors=actors)
                except ct.TestFailure as error:
                    errors.append(error)
            try:
                restore_actors(actors)
            except ct.TestFailure as error:
                errors.append(error)
            if errors:
                if not failed:
                    raise errors[0]
                ct.log("Spec cleanup: " + " | ".join(map(str, errors)))
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
             "lock": "lock_on", "add_items": "console_on additem", "remove_items": "console_on removeitem",
             "spawn_npc": "spawn_host_npc", "stage_npc": "stage_host_npc", "send_home": "move_actor home; thaw if frozen",
             "kill": "console_on host kill", "disable": "console_on host disable", "enable": "console_on host enable",
             "deadly_hit": "console_on damageav health", "go": "console_on coc; wait_for arrival"}
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
                    if step.get("expect") not in ("controls", "shake", "lock", "items", "actors agree", "actor", "together", "down"):
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
