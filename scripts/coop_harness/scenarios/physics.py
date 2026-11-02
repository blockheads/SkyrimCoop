"""Scenarios physics for the co-op harness."""
import contextlib
import math
import time
from ..common import (
    BASE_PICKED,
    CREATED_ID,
    DYNAMIC_MOTIONS,
    FIREBALL,
    FORM_DISABLED,
    HOME_CELL,
    INN,
    ITEM_LISTS,
    KEYFRAMED_MOTION,
    LISTS_SHOWN,
    LYDIA,
    SINKHOLE,
    TAB_KEY,
    TAKE_ALL_KEY,
    WOLF,
    WORLD_CHANGES,
)
from ..errors import TestFailure
from ..comparisons import (
    describe_world_differences,
    distance,
    first_body,
    near,
    world_differences,
)
from ..utilities import log
from ..objects import (
    AnimTrace,
    RagdollTrace,
)


def scenario_physics_push_reaches_guest(ctx):
    """Physics sync (docs/design/physics-sync.md): the host's player knocks clutter with a fireball. The client's
    object it knocks hardest is held by the host's frames (keyframed, its own physics off), its 3D flies with the
    host's and lies where the host's lies; everything the blast woke rests before the world is compared."""
    ctx.knock_clutter_with_fireball("")

def scenario_guest_cast_moves_host(ctx):
    """Physics sync's step 5 (docs/design/physics-sync.md, "A guest's own actions"): the client's player knocks clutter
    with a fireball. Its copy's cast is replayed on the host, whose blast throws the host's clutter; the client's
    follows the host's, held keyframed, and lies where the host's lies."""
    ctx.knock_clutter_with_fireball("client")

def scenario_guest_bump_moves_host(ctx):
    """Physics sync's step 5 (docs/design/physics-sync.md, "A guest's own actions"): the client's player walks into a
    ladle on the floor. Its character pushes the client's ladle, and the host pushes its own with the same impulse
    (copies move by SetPosition and push nothing themselves); the client shows its own push until the host's ladle
    rests, then lies where the host's lies."""
    refs, guest_refs = ctx.world_state(""), ctx.world_state("client")
    target = ctx.pick_reference(refs, "MISC", 0, ctx.resting_clutter_on_both(refs, guest_refs))
    starts = ctx.where_now([target])
    start = ctx.stage_on_floor_before("client", target)
    ctx.place_before("client", target, 120)
    walked_from = ctx.my_position("client")
    seen = {"closest": float("inf")}
    ctx.press("client", "forward")
    try:
        def pushed():
            # How close the client's copy came to it on the host: a miss and a copy that pushes nothing read apart.
            if (copy := ctx.other_player_position("")) is not None:
                seen["closest"] = min(seen["closest"], math.dist(copy[:2], start[:2]))
            return not near(ctx.body_of("", target)["position"], start, 5)
        try:
            ctx.wait_for(f"[host] the client's character pushes {target:X}", pushed, 6)
        except TestFailure:
            raise ctx.failure(f"[host] the client's character does not push {target:X}: the client walked "
                              f"{math.dist(ctx.my_position('client')[:2], walked_from[:2]):.0f} units, its copy came "
                              f"within {seen['closest']:.0f} of it on the host")
    finally:
        ctx.release("client", "forward")
    ctx.guest_own_lies_as_host(target, start, "is pushed")
    ctx.put_back(starts)

def scenario_guest_throw_lands_on_host(ctx):
    """Physics sync's step 6 (docs/design/physics-sync.md, "A guest's own actions"): the client's player grabs a
    ladle (Activate held past fZKeyDelay; it stays held after the release), carries it and drops it (Activate again).
    While it holds it, the host's ladle is steered after the client's hold (simulated: the host's physics meets its
    actors); dropped, it flies on from where the host has it, and the client's own fall shows until the host's ladle
    rests, then lies as the host's."""
    refs, guest_refs = ctx.world_state(""), ctx.world_state("client")
    target = ctx.pick_reference(refs, "MISC", 0, ctx.resting_clutter_on_both(refs, guest_refs))
    start = first_body(refs[target])["position"]
    starts = ctx.where_now([target])
    ctx.place_before("client", target, 90)
    ctx.face("client", target)
    # A refused grab turns the release into an activation (ActivateHandler::ProcessButton 42420), which would pick
    # the ladle up: the player looks away from it first.
    ctx.press("client", "activate")
    try:
        ctx.wait_for(f"[client] grabs {target:X}", lambda: ctx.local_player("client").get("grabbed") == target, 3)
    except TestFailure:
        ctx.console_on("client", "player.setangle x -80")
        raise
    finally:
        ctx.release("client", "activate")
    try:
        # Carried off facing it (a turn drops it) where the floor is clear: back or to a side.
        me, at = ctx.my_position("client"), first_body(ctx.world_state("client", [target])[target])["position"]
        facing = math.degrees(math.atan2(at[0] - me[0], at[1] - me[1]))
        ways = {facing + 180: "back", facing - 90: "strafe_left", facing + 90: "strafe_right"}
        way = ways[ctx.clear_way("client", list(ways), 250)]
        walked_from = ctx.my_position("client")
        ctx.press("client", way)
        try:
            ctx.wait_for(f"[client] carries it off ({way})", lambda: math.dist(ctx.my_position("client")[:2], walked_from[:2]) > 100, 6)
        except TestFailure as failure:
            raise ctx.failure(f"{failure}: walked {math.dist(ctx.my_position('client')[:2], walked_from[:2]):.0f} units, "
                              f"{'still holding it' if ctx.local_player('client').get('grabbed') == target else 'no longer holding it'}") from None
        finally:
            ctx.release("client", way)
        def follows_hold():
            host, guest = ctx.body_of("", target), ctx.body_of("client", target)
            return (host["motion"] in DYNAMIC_MOTIONS and not near(host["position"], start, 20)
                    and near(host["position"], guest["position"], 10))
        ctx.wait_for(f"[host] {target:X} follows the client's hold, simulated (steered)", follows_hold, 5)
        held_at = ctx.body_of("client", target)["position"]
    finally:
        if ctx.local_player("client").get("grabbed"):
            ctx.tap("client", "activate")
    ctx.wait_for(f"[client] drops {target:X}", lambda: not ctx.local_player("client").get("grabbed"), 3)
    ctx.wait_for(f"[host] {target:X} is simulated again", lambda: ctx.body_of("", target)["motion"] in DYNAMIC_MOTIONS, 3)
    # Carried looking down at it, it hangs just off the floor: its drop is its own physics, however short.
    ctx.guest_own_lies_as_host(target, held_at, "falls", moved=0)
    ctx.put_back(starts)

BUCKET = "000747FB"
NPC_HEAD = 124  # a human's head top above its feet (the duo's player and copy read 124)


def bucket_on_npc_head(ctx, carrier, base=BUCKET, upside_down=False, stays=False):
    """`carrier`'s player grabs a bucket (spawned by the host: a created reference, each game its own id), looks up
    to lift it over head height, a restrained Lydia is stood under it, then the player looks down a degree at a time
    until it rests on her head, and lets go. It never sinks into her head while held, and it stays on her head
    afterwards, where the host's physics puts it, every game shows (an upright bucket rolls off her rounded head, as
    in a game without co-op). Upside down (the bucket trick) it may go over her head; `stays`: it must stay on her.
    Both players look at her; video and strips in the trace folder."""
    npc, _, copy = ctx.spawn_host_npc(LYDIA, restrained=True)
    host_id = None
    trace = None
    seen = {g: {"lowest": 1e9, "nearest": 1e9, "motions": set()} for g in ("", "client")}
    off, ended = {}, {}
    lowered_to = None
    try:
        before = set(ctx.world_state(""))
        ctx.console_on("", f"player.placeatme {base} 1")
        host_id = ctx.wait_for("[host] the bucket is in the world", lambda: next(
            (i for i, r in ctx.world_state("").items() if i not in before and r["base"] == int(base, 16) and first_body(r)), None), 5)
        # placeatme drops it at the host's feet, where its character controller keeps nudging it: set down halfway to
        # the stage instead, then found on the client where it rests.
        home, stage = ctx.load_home("")["position"], ctx.stage_spot()
        spot = [(home[0] + stage[0]) / 2, (home[1] + stage[1]) / 2, home[2]]
        ctx.place("", f"{host_id:X}", spot, ctx.world_state("", [host_id])[host_id]["position"])
        if upside_down:
            ctx.console_on("", f"{host_id:X}.setangle x 180")
        try:
            ctx.wait_for("[host] the bucket rests", lambda: not ctx.body_of("", host_id)["awake"], 15)
        except TestFailure:
            body = ctx.body_of("", host_id)
            raise ctx.failure(f"[host] the bucket does not rest at {[round(x) for x in spot]}: at "
                              f"{[round(x) for x in body['position']]}, awake {body['awake']}, velocity {body.get('velocity')}, "
                              f"motion {body['motion']}") from None
        def twin():
            at = ctx.world_state("", [host_id])[host_id]["position"]
            return next((i for i, r in ctx.world_state("client").items()
                         if r["base"] == int(base, 16) and first_body(r) and near(r["position"], at, 10)), None)
        guest_id = ctx.wait_for("[client] has the host's bucket where it rests", twin, 10)
        ids = {"": host_id, "client": guest_id}
        log(f"bucket: host {host_id:X}, client {guest_id:X}")
        target = ids[carrier]
        ctx.place_before(carrier, target, 90)
        ctx.face(carrier, target)
        ctx.press(carrier, "activate")
        try:
            ctx.wait_for(f"[{carrier or 'host'}] grabs the bucket", lambda: ctx.local_player(carrier).get("grabbed") == target, 3)
        finally:
            ctx.release(carrier, "activate")
        feet = ctx.my_position(carrier)[2]
        # Look up a little at a time (a sudden turn drops a held object) until it is well over a head.
        pitch = 20
        while ctx.body_of(carrier, target)["position"][2] < feet + NPC_HEAD + 50 and pitch > -60:
            pitch -= 3
            ctx.console_on(carrier, f"player.setangle x {pitch}")
            time.sleep(0.1)
        if ctx.local_player(carrier).get("grabbed") != target:
            raise ctx.failure(f"[{carrier or 'host'}] dropped the bucket lifting it (pitch {pitch})")
        if stays:
            # Set down as a careful player does: lowered to just over a head first, then she stands right under it.
            while (pitch < 45 and ctx.local_player(carrier).get("grabbed") == target
                   and ctx.body_of(carrier, target)["position"][2] - feet > NPC_HEAD + 45):
                pitch += 1
                ctx.console_on(carrier, f"player.setangle x {pitch}")
                time.sleep(0.1)
            time.sleep(0.5)
        held = ctx.body_of(carrier, target)["position"]
        log(f"bucket held at {[round(x) for x in held]}, {held[2] - feet:.0f} over the feet, pitch {pitch}")
        spot = [held[0], held[1], feet]
        under = 5 if stays else 20
        ctx.player_command("", {"cmd": "move_actor", "actor": npc, "position": spot})
        ctx.wait_for("[host] Lydia stands under it", lambda: distance(ctx.host_npc_state(npc)["position"][:2], spot[:2]) < under, 5)
        ctx.wait_on("client", "her copy stands under it",
                    lambda actors: distance((actors.get(str(copy), {}).get("actual") or [1e9, 1e9])[:2], spot[:2]) < under, 5)
        # The other game's player looks at her from her side (not past the carrier): both videos show the bucket.
        watcher = "client" if carrier == "" else ""
        me = ctx.my_position(carrier)
        side_bearing = math.degrees(math.atan2(me[0] - spot[0], me[1] - spot[1])) + 90
        ctx.place_before(watcher, copy if watcher == "client" else npc, 220, bearing=side_bearing)
        trace = AnimTrace(ctx, f"bucket-on-head-{carrier or 'host'}", {"": {npc: "lydia"}, "client": {copy: "lydia"}}).__enter__()
        def where(game):
            body = ctx.body_of(game, ids[game])
            at = (ctx.host_npc_state(npc).get("position") if game == ""
                  else ctx.actors_on("client").get(str(copy), {}).get("actual"))
            return body, at
        def sample():
            for game in ("", "client"):
                body, at = where(game)
                if not body or not at:
                    continue
                side = math.dist(body["position"][:2], at[:2])
                seen[game]["motions"].add(body["motion"])
                seen[game]["nearest"] = min(seen[game]["nearest"], side)
                if side < 20:
                    seen[game]["lowest"] = min(seen[game]["lowest"], body["position"][2] - at[2])
        # Lowered a degree at a time until it is just over her head, then let go (looking further down drags it toward
        # the player and off): where the host's own grab of it settles on her, 149 to 170 over her feet.
        while not stays and pitch < 45 and ctx.local_player(carrier).get("grabbed") == target:
            pitch += 1
            ctx.console_on(carrier, f"player.setangle x {pitch}")
            time.sleep(0.1)
            sample()
            body, at = where(carrier)
            if body and at and math.dist(body["position"][:2], at[:2]) < 20 and body["position"][2] - at[2] < NPC_HEAD + 45:
                break
        if stays:
            time.sleep(0.5)
            sample()
            body, at = where(carrier)
            log(f"let go {math.dist(body['position'][:2], at[:2]):.1f} off her axis, {body['position'][2] - at[2]:.0f} over her feet")
        lowered_to = pitch
        trace.mark("set on her head")
        if ctx.local_player(carrier).get("grabbed") == target:
            ctx.tap(carrier, "activate")
        ctx.wait_for(f"[{carrier or 'host'}] lets go of the bucket", lambda: not ctx.local_player(carrier).get("grabbed"), 3)
        time.sleep(1)
        # Where the host's physics puts it, every game shows: the host's rest, then the client's there too.
        deadline = time.monotonic() + 30
        while time.monotonic() < deadline and ctx.body_of("", ids[""])["awake"]:
            sample()
            if stays:
                for game in ("", "client"):
                    body, at = where(game)
                    if body and at and not (math.dist(body["position"][:2], at[:2]) < 30 and body["position"][2] - at[2] > 60):
                        off.setdefault(game, f"{math.dist(body['position'][:2], at[:2]):.0f} off her axis, "
                                             f"{body['position'][2] - at[2]:.0f} over her feet")
            time.sleep(0.2)
        if ctx.body_of("", ids[""])["awake"]:
            raise ctx.failure("[host] the bucket does not come to rest after the let go")
        if stays:
            time.sleep(1)
            for game in ("", "client"):
                body, at = where(game)
                if body and at and not (math.dist(body["position"][:2], at[:2]) < 30 and body["position"][2] - at[2] > 60):
                    off.setdefault(game, f"{math.dist(body['position'][:2], at[:2]):.0f} off her axis, "
                                         f"{body['position'][2] - at[2]:.0f} over her feet at rest")
        def lies_as_host():
            host, guest = ctx.body_of("", ids[""]), ctx.body_of("client", ids["client"])
            return near(guest["position"], host["position"], 2)
        try:
            ctx.wait_for("[client] the bucket lies where the host's rests", lies_as_host, 5)
        except TestFailure:
            host, guest = ctx.body_of("", ids[""]), ctx.body_of("client", ids["client"])
            raise ctx.failure(f"[client] the bucket lies at {[round(x) for x in guest['position']]}, the host's at "
                              f"{[round(x) for x in host['position']]}") from None
        for game in ("", "client"):
            body, at = where(game)
            if body and at:
                ended[game] = (f"{math.dist(body['position'][:2], at[:2]):.0f} off her axis, "
                               f"{body['position'][2] - at[2]:.0f} over her feet, awake {body['awake']}")
        trace.mark("after letting go")
    finally:
        if ctx.local_player(carrier).get("grabbed"):
            ctx.tap(carrier, "activate")
        ctx.console_on(carrier, "player.setangle x 0")
        if trace:
            trace.__exit__(None, None, None)
        if host_id:
            ctx.console_on("", f"{host_id:X}.disable")
            ctx.console_on("", f"{host_id:X}.markfordelete")
        ctx.remove_host_npc(npc)
    report = "; ".join(f"[{g or 'host'}] {'Lydia' if g == '' else 'her copy'}: lowest over her feet {seen[g]['lowest']:.0f} "
                       f"(head {NPC_HEAD}), nearest {seen[g]['nearest']:.0f}, ends {ended.get(g, '?')}"
                       for g in ("", "client"))
    log(f"bucket-on-head ({carrier or 'host'} carries, set down at pitch {lowered_to}): {report}")
    into = [] if upside_down else [g or "host" for g in ("", "client") if seen[g]["lowest"] < NPC_HEAD - 20]
    if into:
        raise ctx.failure(f"the bucket sank into Lydia's head in {', '.join(into)}'s game: {report}")
    if off:
        raise ctx.failure("the bucket did not stay on her head: " + "; ".join(
            f"[{g or 'host'}] first seen {text}" for g, text in off.items()) + f" ({report})")

def scenario_guest_bucket_on_npc_head(ctx):
    """S1 (playtests/2026-10-31-fabien.md): the client's player puts a bucket on a host NPC's head."""
    bucket_on_npc_head(ctx, "client")

def scenario_host_bucket_on_npc_head(ctx):
    """S2's case and S1's control: the host's player puts a bucket on its own NPC's head."""
    bucket_on_npc_head(ctx, "")

WIDE_BUCKET = "00012FDF"  # the bucket that stays on a head upside down (000747FB and the iron pot roll off)


def scenario_guest_bucket_trick(ctx):
    """The bucket trick: the client's player sets a wide bucket upside down on a host NPC's head, and it stays there in
    both games."""
    bucket_on_npc_head(ctx, "client", base=WIDE_BUCKET, upside_down=True, stays=True)

def scenario_host_bucket_trick(ctx):
    """The bucket trick's vanilla reference: the host's player sets it on its own NPC's head."""
    bucket_on_npc_head(ctx, "", base=WIDE_BUCKET, upside_down=True, stays=True)

def scenario_ragdoll_lies_as_host(ctx):
    """Physics sync's ragdolls (docs/design/physics-sync.md): the host's player kills a wolf with fireballs. Its copy
    dies by the host's death action, then its ragdoll's bodies are the host's frames, and the body lies where the
    host's lies, bone by bone."""
    npc, _, copy = ctx.spawn_host_npc(WOLF, restrained=True)
    ctx.give_spell("", FIREBALL)
    ctx.place_before("", npc, 400)
    trace = RagdollTrace(ctx, "ragdoll-lies-as-host", {"": {npc: "the host's wolf"}, "client": {copy: "the client's copy"}})
    try:
        trace.__enter__()
        ctx.draw("")
        for _ in range(4):
            if ctx.host_npc_state(npc).get("dead"):
                break
            ctx.cast_spell("", npc, FIREBALL)
            with contextlib.suppress(TestFailure):
                ctx.wait_for("[host] the wolf dies", lambda: ctx.host_npc_state(npc).get("dead"), 3)
        ctx.wait_for("[host] the wolf is dead", lambda: ctx.host_npc_state(npc).get("dead"), 1)
        ctx.wait_on("client", f"its copy {copy:08X} dies", lambda actors: actors.get(str(copy), {}).get("dead"))
        ctx.ragdoll_lies_as_owner("", npc, "client", copy)
    finally:
        trace.__exit__(None, None, None)
        ctx.sheathe("")
    log("ragdoll: " + trace.report())
    trace.check(["client"])

def scenario_knockdown_lies_as_host(ctx):
    """The host's player knocks a living NPC down: the client's copy lies and gets up with it."""
    npc, _, copy = ctx.spawn_host_npc(LYDIA)
    trace = RagdollTrace(ctx, "knockdown-lies-as-host", {"": {npc: "the host's Lydia"}, "client": {copy: "the client's copy"}})
    try:
        ctx.place_before("", npc, 150)
        with trace:
            ctx.knocked_down_together("", npc, "client", copy, lambda: ctx.console_on("", f"player.pushactoraway {npc:X} 5"))
    finally:
        ctx.remove_host_npc(npc)
    log("ragdoll: " + trace.report())
    log("copy vs owner: " + trace.compare_report("", npc, "client", copy))
    trace.check_copy("", npc, "client", copy)
    trace.check(["client"])

def scenario_knockdown_smoke(ctx):
    """Lydia stands between the two players, both facing her (watch both games: `watch`, `watch --instance client`);
    the host knocks her down and she gets up, three times. Her copy must not snap where she does not: a frame where
    its skeleton jumps and comes back (RagdollTrace.snaps)."""
    npc, _, copy = ctx.spawn_host_npc(LYDIA)
    def knock(instance, form):
        return (ctx.actor_snapshot(instance, form).get("state") or {}).get("knock", 0)
    name = "knockdown-smoke"
    trace = RagdollTrace(ctx, name, {"": {npc: "the host's Lydia"}, "client": {copy: "the client's copy"}})
    try:
        ctx.place_before("", npc, 250, bearing=0)
        ctx.place_before("client", copy, 300, bearing=90)
        trace.__enter__()
        anims = AnimTrace(ctx, name, {"": {npc: "lydia"}, "client": {copy: "lydia"}}, video=False).__enter__()
        for round in range(3):
            ctx.face("", npc)
            ctx.face("client", copy)
            ctx.console_on("", f"player.pushactoraway {npc:X} 5")
            ctx.wait_for(f"[host] Lydia goes down ({round + 1} of 3)", lambda: knock("", npc), 3)
            ctx.wait_for("[host] Lydia is up again", lambda: not knock("", npc), 20)
            ctx.wait_for("[client] her copy is up again", lambda: not knock("client", copy), 5)
    finally:
        trace.__exit__(None, None, None)
        if "anims" in locals():
            anims.__exit__(None, None, None)
        ctx.remove_host_npc(npc)
    log("ragdoll: " + trace.report())
    log("copy vs owner: " + trace.compare_report("", npc, "client", copy))
    log("snaps: " + trace.snaps_report("", npc, "client", copy))
    if trace.copy_snaps("", npc, "client", copy):
        raise ctx.failure("the copy snaps where its owner does not: " + trace.snaps_report("", npc, "client", copy) +
                          f" (samples: {trace.folder.name})")

def scenario_knockdown_front_and_back(ctx):
    """Push a living NPC from the front and behind; each whole fall and get-up follows its owner."""
    npc, _, copy = ctx.spawn_host_npc(LYDIA)
    try:
        for index, offset in enumerate((math.pi, 0, math.pi)):
            ctx.place_before("", npc, 250, bearing=0)
            ctx.place_before("client", copy, 300, bearing=90)
            yaw = ctx.local_player("")["rotation"][2] + offset
            ctx.console_on("", f"{npc:X}.setangle z {math.degrees(yaw) % 360:.3f}")
            ctx.wait_for("the copy faces the same way before the push",
                     lambda: abs((ctx.actor_snapshot("client", copy)["placement"]["rotation"][2] - yaw + math.pi)
                                 % (2 * math.pi) - math.pi) < 0.1, 5)
            name = f"knockdown-front-and-back-{index + 1}"
            trace = RagdollTrace(ctx, name, {"": {npc: "the host's Lydia"}, "client": {copy: "the copy"}})
            with trace, AnimTrace(ctx, name, {"": {npc: "lydia"}, "client": {copy: "lydia"}}, video=False):
                ctx.knocked_down_together("", npc, "client", copy, lambda: ctx.console_on("", f"player.pushactoraway {npc:X} 5"))
            log("ragdoll: " + trace.report())
            log("copy vs owner: " + trace.compare_report("", npc, "client", copy))
            trace.check_copy("", npc, "client", copy)
            trace.check_copy_skeleton("", npc, "client", copy)
            if trace.copy_snaps("", npc, "client", copy):
                raise ctx.failure(trace.snaps_report("", npc, "client", copy) + f" (samples: {trace.folder.name})")
    finally:
        ctx.remove_host_npc(npc)

def scenario_guest_knockdown_lies_on_host(ctx):
    """The client's player is knocked down in its game: the host's copy of it lies and gets up with it."""
    copy = ctx.client_on_host()
    ctx.knocked_down_together("client", 0x14, "", copy, lambda: ctx.console_on("client", "player.pushactoraway player 5"))

def scenario_world_changes_reach_guest(ctx):
    """World-state sync's check: every kind of change to a reference the host can make, one at a time, each put
    back after. Reports every kind the client's world does not follow, then what still differs in the whole
    loaded world, rather than stopping at the first."""
    refs = ctx.world_state("")
    problems = []
    if before := world_differences(*ctx.sample_worlds()):
        problems += ["before any change:"] + ["  " + line for line in describe_world_differences(before, 20)]
    for name, pick, stage, change, undo in WORLD_CHANGES:
        if differences := ctx.world_change_reaches_guest(name, pick, stage, change, undo, refs):
            problems.append(f"{name}: " + "; ".join(f"{f} {t}" for f, t in differences.items()))
            log(f"NOT SYNCED {problems[-1]}")
        else:
            log(f"synced: {name}")
    misc = ctx.pick_reference(refs, "MISC")
    if differences := ctx.created_reaches_guest(refs, refs[misc]["base"]):
        problems.append("create: " + "; ".join(f"{label} {text}" for label, text in differences.items()))
        log(f"NOT SYNCED {problems[-1]}")
    else:
        log("synced: create")
    if after := world_differences(*ctx.sample_worlds()):
        problems += ["after putting everything back:"] + ["  " + line for line in describe_world_differences(after, 20)]
    if problems:
        raise ctx.failure("the client's world does not follow the host's:\n  " + "\n  ".join(problems))

def scenario_late_join_gets_cell_state(ctx):
    """World sync's state at a cell load, for a game that missed changes: the host locks a door and disables a
    static while the client is away. The client rejoins standing in the same cell (no load to ask with: it asks
    for the cells it has loaded on joining), and its cell is the host's."""
    refs = ctx.world_state("")
    door = ctx.pick_reference(refs, "DOOR", 0, lambda r: not (r.get("lock") or {}).get("locked"))
    static = ctx.pick_reference(refs, "STAT", 3)
    level = (refs[door].get("lock") or {}).get("level", 0)
    def host_changed():
        return (ctx.world_state("", [door])[door].get("lock") or {}).get("locked") and ctx.world_state("", [static])[static]["flags"] & FORM_DISABLED
    ctx.disconnect_client()
    try:
        ctx.console_on("", f"{door:X}.lock 50")
        ctx.console_on("", f"{static:X}.disable")
        ctx.wait_for("[host] the door locked and the static gone", host_changed, 5)
        if not ctx.guest_matches(door, 1) or not ctx.guest_matches(static, 1):
            raise ctx.failure("[client] has the host's changes while away: the test shows nothing")
        ctx.rejoin_client()
        differences = {f"{ref:X}": text for ref in (door, static) if (text := ctx.guest_matches(ref, 10))}
        if differences:
            raise ctx.failure("[client] rejoined with its own cell, not the host's: " +
                              "; ".join(f"{ref} {fields}" for ref, fields in differences.items()))
    finally:
        if ctx.game_state("client").get("online") is False:
            ctx.rejoin_client()
        ctx.lock_on("", door, False, level)
        ctx.console_on("", f"{static:X}.enable")
    for ref in (door, static):
        if left := ctx.guest_matches(ref, 5):
            raise ctx.failure(f"[client] {ref:X} not put back as the host's: {left}")

def scenario_guest_opens_hosts_roll(ctx, *, take=False, _game=None):
    """A container's leveled entries are the host's roll (world sync step 2b): in the inn, the client opens the
    container with the most leveled entries that a player can reach (the merchant's stock chest under the floor is
    not) as its player would (an open is what rolls them), and it holds what the host's does, which the host's game
    rolled when the client's inn loaded."""
    _game = ctx.game(_game)
    try:
        ctx.both_go(INN)
        refs = ctx.world_state("")
        found = [r for r in refs.values() if r.get("leveled") and r["has_3d"] and not r["flags"] & FORM_DISABLED and r.get("items")]
        if not found:
            raise ctx.failure("[host] no container in the inn holds a leveled roll: the test shows nothing")
        chest, unreachable = None, []
        for candidate in sorted(found, key=lambda r: (-r["leveled"], r["id"])):
            try:
                ctx.place_before("client", candidate["id"], 100)
            except TestFailure as error:
                unreachable.append(f"{candidate['id']:X} ({error})")
                continue
            chest = candidate["id"]
            break
        if chest is None:
            raise ctx.failure("[client] no container with a leveled roll it can stand before: " + "; ".join(unreachable))
        if unreachable:
            log(f"[client] not reachable: {', '.join(u.split(' ')[0] for u in unreachable)}")
        host = ctx.container_items("", chest)
        ctx.activate_on("client", chest)
        ctx.wait_for(f"[client] {chest:X}'s menu shows its lists",
                 lambda: ctx.menu_var("client", "ContainerMenu", ITEM_LISTS + ".currentState") in LISTS_SHOWN, 5, _game=_game)
        if (theirs := ctx.container_items("client", chest)) != host:
            raise ctx.failure(f"[client] opened {chest:X} holding its own roll: " + ctx.item_differences(host, theirs, "host", "client"))
        if not take:
            ctx.tap_key("client", TAB_KEY)
            ctx.wait_for("[client] the menu closed", lambda: ctx.takes_input("client"), 5, _game=_game)
            return
        # Taking it all (playtests/2026-10-31-fabien.md S5): the guest gets the host's roll, the host's chest empties.
        carried = {item: ctx.item_count("client", 0x14, item) or 0 for item in host}
        ctx.tap_key("client", TAKE_ALL_KEY)
        ctx.wait_for("[client] the menu closed", lambda: ctx.takes_input("client"), 5, _game=_game)
        got = lambda: {item: (ctx.item_count("client", 0x14, item) or 0) - carried[item] for item in host}
        try:
            ctx.wait_for(f"[client] the guest gets the host's roll {host}", lambda: got() == host, 10, _game=_game)
            ctx.wait_for(f"[host] {chest:X} is empty", lambda: not ctx.container_items("", chest), 10, _game=_game)
        finally:
            log(f"  [client] got {got()}, its {chest:X} holds {ctx.container_items('client', chest)}; "
                f"[host] {chest:X} holds {ctx.container_items('', chest)}")
    finally:
        ctx.both_go(HOME_CELL)

def scenario_placed_leveled_items_are_hosts(ctx):
    """A reference placed with a leveled base picks it when its cell first loads (Soljund's Sinkhole's clutter book:
    `DummyBook` with XLIB `LItemBookClutter`, 124 books): the client's is the host's pick, its 3D the new base's.
    Where it rests is step 5's (physics), not checked here."""
    try:
        ctx.both_go(SINKHOLE)
        refs = ctx.world_state("")
        picked = sorted(i for i, r in refs.items() if (r.get("changes") or 0) & BASE_PICKED and i < CREATED_ID)
        if not picked:
            raise ctx.failure(f"[host] nothing in {SINKHOLE} picked its base: the test shows nothing")
        def differences():
            host, guest = ctx.world_state("", picked), ctx.world_state("client", picked)
            found = {}
            for ref in picked:
                a, b = host[ref], guest.get(ref)
                if b is None:
                    found[ref] = "client lacks it"
                elif a["base"] != b["base"]:
                    found[ref] = f"base {a['base']:X} vs {b['base']:X}"
                elif (a["has_3d"], a.get("hidden")) != (b["has_3d"], b.get("hidden")):
                    found[ref] = f"3D {a['has_3d']}/{a.get('hidden')} vs {b['has_3d']}/{b.get('hidden')}"
                elif sorted(a.get("pose") or {}) != sorted(b.get("pose") or {}):
                    found[ref] = f"model nodes {sorted(a.get('pose') or {})} vs {sorted(b.get('pose') or {})}"
            return found
        left = {}
        def matches():
            left.clear()
            left.update(differences())
            return not left
        with contextlib.suppress(TestFailure):
            ctx.wait_for(f"[client] {len(picked)} picked bases as the host's", matches, 5)
        if left:
            raise ctx.failure("[client] picked its own: " + "; ".join(f"{ref:X} {text}" for ref, text in left.items()))
    finally:
        ctx.both_go(HOME_CELL)
