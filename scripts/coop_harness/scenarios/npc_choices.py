"""Native NPC copy identity, fortified pools and equipment."""
import json

from ..utilities import log
from ..common import HOME_CELL


def compare_choice(ctx, npc, copy):
    ctx.wait_for("both leveled NPCs finish loading their bodies", lambda: all(
        ctx.actor_state(game, actor).get("mod", {}).get("fade_state") == 0
        for game, actor in (("", npc), ("client", copy))), 10)
    states = {game or "host": {
        "identity": ctx.actor_state(game, actor).get("identity", {}),
        "templates": ctx.player_command(game, {"cmd": "npc_templates", "actor": actor}),
        "recorded_name": ctx.player_command(game, {"cmd": "form_save", "form": actor, "base": True}),
        "forced_name": ctx.player_command(game, {"cmd": "form_save", "form": actor, "base": True, "flags": 32}),
    } for game, actor in (("", npc), ("client", copy))}
    log("leveled NPC choices: " + json.dumps(states, sort_keys=True))
    owner, duplicate = (states[game]["identity"] for game in ("host", "client"))
    if owner.get("name") != duplicate.get("name"):
        raise ctx.failure(f"leveled NPC {npc:X} name: host {owner.get('name')!r}, copy {duplicate.get('name')!r}; "
                          + json.dumps(states, sort_keys=True))


def spawned_choice(ctx):
    """A bandit boss the host encounters has the same resolved identity on its copy."""
    npc, _, copy = ctx.spawn_host_npc("0003DF17", restrained=True)
    try:
        compare_choice(ctx, npc, copy)
    finally:
        ctx.remove_host_npc(npc)


def placed_choice(ctx):
    """The guard's persistent reference keeps the host's choice when it reaches the party."""
    npc = 0xD8C56
    # Its reference is in the plains' loaded cell; it must be loaded before staging it in QASmoke.
    ctx.both_go("Whiterun plains", "cow Tamriel 0 -4")
    ctx.wait_for("the placed guard reference loads", lambda: ctx.actor_state("", npc).get("identity"), 10)
    ctx.both_go(HOME_CELL)
    original = ctx.actor_place("", npc)
    ctx.console_on("", f"{npc:X}.enable")
    try:
        copy = ctx.stage_host_npc(npc)
        compare_choice(ctx, npc, copy)
    finally:
        ctx.release_host_npc(npc)
        if original["disabled"]:
            ctx.console_on("", f"{npc:X}.disable")


def fortified_pools(ctx):
    """Onmund's enchanted outfit fortifies his magicka on his copy as well as his owner."""
    npc, _, copy = ctx.spawn_host_npc("0001C194", restrained=True)
    try:
        ctx.wait_for("Onmund and his copy finish loading", lambda: all(
            ctx.actor_state(game, actor).get("mod", {}).get("fade_state") == 0
            for game, actor in (("", npc), ("client", copy))), 10)
        pools = {game or "host": ctx.player_command(game, {"cmd": "actor_pools", "actor": actor})
                 for game, actor in (("", npc), ("client", copy))}
        log("Onmund native pools: " + json.dumps(pools, sort_keys=True))
        if problems := ctx.check_desync():
            raise ctx.failure("Onmund's enchanted outfit: " + " | ".join(problems))
    finally:
        ctx.remove_host_npc(npc)


def torch_repair(ctx):
    """Aela's copy equips a torch using its native slot and repairs the unchanged owner's worn set."""
    torch = 0x1D4EC
    npc, _, copy = ctx.spawn_host_npc("0001A696", restrained=True)
    try:
        ctx.console_on("", f"{npc:X}.additem {torch:X} 1")
        ctx.console_on("", f"{npc:X}.equipitem {torch:X}")
        def equipment(game, actor):
            return set(ctx.actor_state(game, actor).get("equipment") or [])
        ctx.wait_for("Aela wears her torch", lambda: torch in equipment("", npc), 10)
        ctx.wait_for("Aela's copy wears its owner's torch", lambda: torch in equipment("client", copy), 10)
        owner = equipment("", npc)
        ctx.player_command("client", {"cmd": "strip", "actor": copy})
        ctx.wait_for("the copy's equipment drifted", lambda: equipment("client", copy) != owner, 5)
        ctx.wait_for("the unchanged owner snapshot repairs every item including the torch",
                     lambda: equipment("client", copy) == equipment("", npc) and torch in equipment("client", copy), 10)
        if problems := ctx.check_desync():
            raise ctx.failure("Aela's repaired torch: " + " | ".join(problems))
    finally:
        ctx.remove_host_npc(npc)
