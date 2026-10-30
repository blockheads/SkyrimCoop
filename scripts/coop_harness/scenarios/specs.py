"""Scenarios specs for the co-op harness."""
import time
from ..common import (
    ROOT,
    ALL_CONTROLS,
)
from ..errors import TestFailure

import json
import math
SPECS = ROOT / "Code/tests/specs"
GAMES = {"host": "", "guest": "client"}
WAIT = 5


def load(ctx, names=None):
    specs = [json.loads(path.read_text()) for path in sorted(SPECS.glob("*.json"))]
    return [spec for spec in specs if not names or spec["name"] in names]

def instance(ctx, step, key="game"):
    name = step.get(key, "host")
    if name not in GAMES:
        raise ctx.failure(f"no game {name!r} in the duo")
    return GAMES[name]

def ref(ctx, value):
    """A reference: a hex id, or a role this runner finds in the cell ("inner-door": coop_test.inner_door())."""
    return ctx.inner_door() if value == "inner-door" else int(value, 16)

def actor_state(ctx, value, game, actors):
    _game = ctx.game()
    actor = actors[value] if value in actors else {"host": ref(ctx, value)}
    form = actor["host"]
    if game and actor.get("spawned"):
        form = next((int(form) for form, state in ctx.actors_on(game).items()
                     if state.get("session_id") == actor["session_id"]), None)
    if form is None:
        return {"exists": False, "session_id": actor.get("session_id")}
    place = ctx.actor_place(game, form)
    _game = ctx.game(game)
    state = (((_game.read_status() or {}).get("world") or {}).get(str(form)) or {}).get("state", {})
    return {"form": form, "exists": bool(place), **place, **state}

def send_home(ctx, actor):
    form = actor["host"]
    ctx.player_command("", {"cmd": "move_actor", "actor": form, "home": True})
    ctx.wait_for(f"[host] {form:X} has gone home", lambda: not ctx.host_npc_state(form), WAIT)
    try:
        ctx.copy_follows_host(form, "goes home with its owner")
    finally:
        if actor.get("frozen"):
            ctx.freeze("", form, False)
            actor["frozen"] = False

def restore_actor(ctx, actor):
    form = actor["host"]
    place = ctx.actor_place("", form)
    if not place:
        return
    if actor.get("spawned"):
        ctx.remove_host_npc(form)
    else:
        if place["disabled"]:
            ctx.console_on("", f"{form:X}.enable")
            ctx.wait_for(f"[host] {form:X} enabled for cleanup", lambda: bool(ctx.host_npc_state(form)), WAIT)
        if ctx.host_npc_state(form) or actor.get("frozen"):
            send_home(ctx, actor)

def describe(ctx, spec, index, step, cleanup=False):
    return f"{spec['name']}, {'cleanup' if cleanup else 'step'} {index + 1}: {json.dumps(step)}"

def run_step(ctx, spec, index, step, cleanup=False, actors=None):
    actors = actors if actors is not None else {}
    what = describe(ctx, spec, index, step, cleanup)
    ctx.CURRENT_STEP.update(description=what, time=time.time())
    ctx.observe("checkpoint", phase=ctx.phase, description=what)
    if "do" in step:
        kind = step["do"]
        if kind == "script":
            klass, function = step["call"].split(".", 1)
            call = ctx.player_script_call if step.get("by") == "player" else ctx.script_call
            call(instance(ctx, step), klass, function, step.get("args", []))
        elif kind == "leave":
            if instance(ctx, step) != "client":
                raise ctx.failure(f"{what}: only the guest leaves")
            if ctx.game_state("client").get("online") is not False:
                ctx.disconnect_client()
        elif kind == "join":
            if ctx.game_state("client").get("online") is False:
                ctx.rejoin_client()
        elif kind == "lock":
            ctx.lock_on(instance(ctx, step), ref(ctx, step["ref"]), step["locked"], step.get("level", 0))
        elif kind in ("add_items", "remove_items"):
            verb = "additem" if kind == "add_items" else "removeitem"
            ctx.console_on(instance(ctx, step), f"{ref(ctx, step['ref']):X}.{verb} {ref(ctx, step['item']):X} {step['count']}")
        elif kind == "marker":
            ctx.screen_marker(instance(ctx, step, "from"), instance(ctx, step, "to"))
        elif kind == "spawn_npc":
            form, session_id, copy = ctx.spawn_host_npc(f"{ref(ctx, step['base']):08X}")
            actors[step["as"]] = {"host": form, "session_id": session_id, "copy": copy, "spawned": True}
        elif kind == "stage_npc":
            actor = actors[step["as"]] = {"host": ref(ctx, step["ref"]), "frozen": True}
            actor["copy"] = ctx.stage_host_npc(actor["host"])
            actor["session_id"] = ctx.host_npc_state(actor["host"])["session_id"]
        elif kind in ("kill", "disable", "enable", "send_home"):
            actor = actors[step["actor"]] if step["actor"] in actors else {"host": ref(ctx, step["actor"])}
            if kind == "send_home":
                send_home(ctx, actor)
            else:
                ctx.console_on("", f"{actor['host']:X}.{kind}")
        elif kind == "deadly_hit":
            game = instance(ctx, step)
            health = ctx.player_snapshot(game)["values"]["health"]
            ctx.console_on(game, f"player.damageav health {math.ceil(max(health, 0) + 1000)}")
        elif kind == "go":
            game = instance(ctx, step)
            if ctx.game_state(game).get("online") is not False:
                raise ctx.failure(f"{what}: go while away requires leaving first")
            before = ctx.player_cell(game)
            cell = "Riverwood" if step["cell"] == "elsewhere" else step["cell"]
            ctx.console_on(game, f"coc {cell}")
            ctx.wait_for(what, lambda: ctx.player_cell(game) not in (None, before) and ctx.takes_input(game), 60)
        else:
            raise ctx.failure(f"{what}: no such step")
        return
    game = instance(ctx, step)
    expect = step.get("expect")
    if expect == "controls" and "equals" in step:
        mask = int(step["equals"], 16)
        holds = lambda: ctx.screen(game)["controls"] == mask
    elif expect == "controls" and "taken" in step:
        holds = lambda: (ctx.screen(game)["controls"] != ALL_CONTROLS) == step["taken"]
    elif expect == "shake":
        holds = lambda: (ctx.screen(game)["shake"]["seconds"] > 0) == step["shaking"]
    elif expect == "lock":
        door = ref(ctx, step["ref"])
        holds = lambda: ctx.door_lock(game, door)[0] == step["locked"] and ("level" not in step or ctx.door_lock(game, door)[1] == step["level"])
    elif expect == "items":
        holder, item = ref(ctx, step["ref"]), ref(ctx, step["item"])
        holds = lambda: (ctx.item_count(game, holder, item) or 0) == step["count"]
    elif expect == "actors agree":
        holds = lambda: ctx.check_desync() == []
    elif expect == "actor":
        observed = {}
        def holds():
            observed.clear()
            observed.update(actor_state(ctx, step["actor"], game, actors))
            return observed["exists"] and all(observed.get(key) == step[key]
                                               for key in ("dead", "disabled", "copy") if key in step)
    elif expect == "together":
        holds = lambda: ctx.player_cell("") == ctx.player_cell("client") and bool(ctx.both_see_each_other())
    elif expect == "down":
        holds = lambda: ctx.player_snapshot(game).get("state", {}).get("dead") == step["down"]
    else:
        raise ctx.failure(f"{what}: no such expectation")
    if step.get("now"):
        if not holds():
            if expect in ("controls", "shake"):
                state = ctx.screen(game)
                raise ctx.failure(f"{what}: controls {state['controls']:X}, shake {state['shake']['seconds']:.2f}")
            state = actor_state(ctx, step["actor"], game, actors) if expect == "actor" else "not so"
            raise ctx.failure(f"{what}: {state}")
    else:
        try:
            ctx.wait_for(what, holds, step.get("within", WAIT))
        except TestFailure as error:
            state = None
            if expect == "actor":
                state = actor_state(ctx, step["actor"], game, actors)
            elif expect == "actors agree":
                state = ctx.check_desync()
            raise ctx.failure(f"{error}" + (f"; observed {state}" if state is not None else "")) from None

def run(ctx, spec):
    actors = {}
    failure = None
    try:
        for index, step in enumerate(spec["steps"]):
            run_step(ctx, spec, index, step, actors=actors)
    except BaseException as error:
        failure = error
    previous_phase = ctx.phase
    ctx.phase = "cleanup"
    errors = []
    try:
        for index, step in enumerate(spec.get("cleanup", [])):
            try:
                run_step(ctx, spec, index, step, cleanup=True, actors=actors)
            except BaseException as error:
                errors.append(error)
        for actor in actors.values():
            try:
                restore_actor(ctx, actor)
            except BaseException as error:
                errors.append(error)
    finally:
        ctx.phase = previous_phase
    ctx.cleanup_errors.extend(errors)
    if failure is not None:
        raise failure
    if errors:
        raise errors[0]

def dry(ctx, names):
    """Each step as the harness call it makes, nothing run: checks the specs speak the runner's vocabulary."""
    calls = {"script": "script_call", "leave": "disconnect_client", "join": "rejoin_client", "marker": "screen_marker",
             "lock": "lock_on", "add_items": "console_on additem", "remove_items": "console_on removeitem",
             "spawn_npc": "spawn_host_npc", "stage_npc": "stage_host_npc", "send_home": "move_actor home; thaw if frozen",
             "kill": "console_on host kill", "disable": "console_on host disable", "enable": "console_on host enable",
             "deadly_hit": "console_on damageav health", "go": "console_on coc; wait_for arrival"}
    for spec in load(ctx, names):
        print(f"spec:{spec['name']}")
        for part in ("steps", "cleanup"):
            label = "step" if part == "steps" else "cleanup"
            for index, step in enumerate(spec.get(part, [])):
                if "do" in step:
                    if step["do"] not in calls:
                        raise SystemExit(f"{describe(ctx, spec, index, step, part == 'cleanup')}: no such step")
                    call = "player_script_call" if step.get("by") == "player" else calls[step["do"]]
                    print(f"  {label:7s} {index + 1:2d}  {call} {json.dumps({k: v for k, v in step.items() if k != 'do'})}")
                else:
                    if step.get("expect") not in ("controls", "shake", "lock", "items", "actors agree", "actor", "together", "down"):
                        raise SystemExit(f"{describe(ctx, spec, index, step, part == 'cleanup')}: no such expectation")
                    print(f"  {label:7s} {index + 1:2d}  {'check' if step.get('now') else 'wait_for'} {json.dumps(step)}")
