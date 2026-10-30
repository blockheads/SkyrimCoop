"""Scenarios movement for the co-op harness."""
import math
from ..common import LYDIA
from ..errors import TestFailure
from ..comparisons import distance


def scenario_steady_state(ctx):
    """Desync checker: every actor's full state matches between owner and copy."""
    if problems := ctx.check_desync():
        raise ctx.failure("desync: " + " | ".join(problems))

def scenario_move_host_npc(ctx):
    """Host moves its NPC to a player; the client's copy must end up where the host's NPC is."""
    npc, _, copy = ctx.spawn_host_npc(LYDIA, restrained=True)
    ctx.show("client", lambda actors: copy)
    host_actors = ctx.actors_on("")
    players = {"player": ctx.local_player("")["position"]}
    if client_on_host := ctx.other_player(host_actors):
        players[f"{client_on_host:08X}"] = host_actors[str(client_on_host)]["actual"]
    start = ctx.host_npc_state(npc)["position"]
    target = max(players, key=lambda ref: distance(players[ref], start))  # the farther player
    goal = players[target]
    ctx.place("", f"{npc:08X}", goal, start)
    ctx.wait_for("[host] NPC moved and landed", lambda: distance(ctx.host_npc_state(npc)["position"], start) > 100
             and abs(ctx.host_npc_state(npc)["position"][2] - goal[2]) < 60, 10)
    last = {}
    def copy_matches(actors):
        last.update(copy=actors.get(str(copy), {}).get("actual"), owned=ctx.host_npc_state(npc)["position"])
        return last["copy"] and distance(last["copy"], last["owned"]) < 30
    try:
        ctx.wait_on("client", f"NPC copy {copy:08X} at the host NPC's position", copy_matches, 10)
    except TestFailure:
        raise ctx.failure(f"copy at {[round(v) for v in last['copy'] or []]}, host's NPC at {[round(v) for v in last['owned']]}")
    ctx.hold()

def scenario_copy_jumps_with_owner(ctx):
    """The host puts its NPC 900 units away at once (the console's setpos, as a script's MoveTo does): the client's
    copy jumps there too, never sliding across in between (Movement::Teleported)."""
    npc, _, copy = ctx.spawn_host_npc(LYDIA, restrained=True)
    ctx.show("client", lambda actors: copy)
    start = ctx.host_npc_state(npc)["position"]
    def flat(a, b):
        return math.hypot(a[0] - b[0], a[1] - b[1])
    ctx.wait_on("client", f"copy {copy:08X} at the host's NPC", lambda actors: (p := actors.get(str(copy), {}).get("actual")) and flat(p, start) < 30, 10)
    goal = [start[0] + 900, start[1], start[2]]
    ctx.console_on("", f"{npc:08X}.setpos x {goal[0]:.0f}")
    seen = []
    def arrived(actors):
        if position := actors.get(str(copy), {}).get("actual"):
            seen.append(position)
        return position and flat(position, goal) < 30
    ctx.wait_on("client", f"copy {copy:08X} at the NPC's new place", arrived, 5)
    if between := [[round(v) for v in p] for p in seen if flat(p, start) > 30 and flat(p, goal) > 30]:
        raise ctx.failure(f"[client] the copy slid across: {len(between)} of {len(seen)} reads between, {between[:3]}")
    ctx.hold()


def scenario_furniture_host_player(ctx):
    """Host uses the cooking spit and leaves it; the client's copy must follow both ways."""
    ctx.furniture_roundtrip("", "client", "host's character")

def scenario_furniture_client_player(ctx):
    """Client uses the cooking spit and leaves it; the host's copy must follow both ways."""
    ctx.furniture_roundtrip("client", "", "client's character")

def scenario_sneak_host_player(ctx):
    """Host sneaks and stands up; the client's copy of the host must follow."""
    ctx.sneak_roundtrip("", "client", "host's character")

def scenario_sneak_client_player(ctx):
    """Client sneaks and stands up; the host's copy of the client must follow."""
    ctx.sneak_roundtrip("client", "", "client's character")


def scenario_cast_host_player(ctx):
    """Host casts Flames (held) and stops; the client's copy of the host must follow."""
    ctx.cast_roundtrip("", "client", "host's character")

def scenario_cast_client_player(ctx):
    """Client casts Flames (held) and stops; the host's copy of the client must follow."""
    ctx.cast_roundtrip("client", "", "client's character")

def scenario_quick_sheathe_host_player(ctx):
    """Host raises and at once lowers a spell; the client's copy must end with the hands down."""
    ctx.quick_sheathe("", "client", "host's character")

def scenario_quick_sheathe_client_player(ctx):
    """Client raises and at once lowers a spell; the host's copy must end with the hands down."""
    ctx.quick_sheathe("client", "", "client's character")

def scenario_hands_survive_reload_host_player(ctx):
    """Host's hands up/down across hot reloads; the client's respawned copy must match."""
    ctx.hands_survive_reload("", "client", "host's character")

def scenario_hands_survive_reload_client_player(ctx):
    """Client's hands up/down across hot reloads; the host's respawned copy must match."""
    ctx.hands_survive_reload("client", "", "client's character")

def scenario_health_host_player(ctx):
    """Host player loses/regains health; the client's copy of the host must match."""
    ctx.health_roundtrip("", "player", lambda: ctx.local_player("").get("health"), "client", ctx.other_player, "host's character")

def scenario_health_client_player(ctx):
    """Client player loses/regains health; the host's copy of the client must match."""
    ctx.health_roundtrip("client", "player", lambda: ctx.local_player("client").get("health"), "", ctx.other_player,
                     "client's character")
