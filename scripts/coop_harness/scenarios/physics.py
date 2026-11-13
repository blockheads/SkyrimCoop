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
    While it holds it, the ladle is the client's alone: the host's is keyframed on the client's hold (its grab refuses
    it: physics-sync.md, "Exclusive holds"); dropped, it flies on from where the host has it, and the client's own fall
    shows until the host's ladle rests, then lies as the host's."""
    refs, guest_refs = ctx.world_state(""), ctx.world_state("client")
    target = ctx.pick_reference(refs, "MISC", 0, ctx.resting_clutter_on_both(refs, guest_refs))
    start = first_body(refs[target])["position"]
    starts = ctx.where_now([target])
    ctx.place_before("client", target, 90)
    ctx.face("client", target)
    # Ready first: a ladle the host's frames still move is keyframed in the client's game, and the grab refuses it.
    # Placed by teleport, the client's copy reaches the host a moment later, while its ladle may still stir.
    ctx.wait_until_settled(target, "client")
    # A refused grab turns the release into an activation (ActivateHandler::ProcessButton 42420), which would pick
    # the ladle up: the player looks away from it first.
    try:
        ctx.grab("client", target)
    except TestFailure:
        ctx.console_on("client", "player.setangle x -80")
        raise
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
            return (host["motion"] not in DYNAMIC_MOTIONS and not near(host["position"], start, 20)
                    and near(host["position"], guest["position"], 10))
        ctx.wait_for(f"[host] {target:X} follows the client's hold, keyframed", follows_hold, 5)
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


def spawn_shared_bucket(ctx, base=BUCKET, upside_down=False):
    """The host spawns a bucket (a created reference, each game its own id) halfway to the stage and lets it rest;
    returns ({"": host id, "client": guest id}) once the client has it where it rests."""
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
    log(f"bucket: host {host_id:X}, client {guest_id:X}")
    return {"": host_id, "client": guest_id}


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
        ids = spawn_shared_bucket(ctx, base, upside_down)
        host_id = ids[""]
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

CAPSULE_TOP = 128  # the character controller's capsule over the feet (physics.md, "What stops a held body")
INTO_CAPSULE = 15  # a held bucket's centre this near an NPC's axis, under her capsule's top, is inside her


def controller_of(ctx, game, actor):
    """An actor's character controller readout (harness `controller`), with how far its capsule is from the actor."""
    read = ctx.player_command(game, {"cmd": "controller", "actor": actor})
    read["gap"] = math.dist(read["position"][:2], read["actor_position"][:2])
    read["rise"] = read["position"][2] - read["actor_position"][2]
    return read


def describe_controller(read):
    return (f"flags {read['flags']:08X}, state {read['state']}, capsule {read['gap']:.1f} off her axis and {read['rise']:.0f} up, velocity "
            f"{[round(v) for v in read['velocity']]}, last step {[round(v) for v in read['step_velocity']]}")


def drags_into_npc(ctx, carrier, press_for=0):
    """`carrier`'s player grabs a bucket, a restrained Lydia stands beside it as far off as the bucket, and it turns
    toward her, swinging the bucket into her at chest height. Her capsule must stop the bucket, in the carrier's game,
    as in a game without co-op: it never comes within INTO_CAPSULE of her axis below her capsule's top. Logs her
    character controller in both games (a copy's capsule only pushes bodies when the engine integrates it,
    docs/re/systems/actor-movement.md). With `press_for` seconds, the player first carries the bucket about, then stops
    turning once it aims at her and holds it against her that long (the 2026-11-02 playtest: it sank in slowly), and the
    other game must show the bucket in the carrier's hand, never falling (it fell and was put back, over and over).
    Every step is a checked action: it fails on the frame the bucket leaves the player's hands."""
    npc, _, copy = ctx.spawn_host_npc(LYDIA, restrained=True)
    her = {"": npc, "client": copy}
    other = "client" if carrier == "" else ""
    ids = None
    nearest = {"side": 1e9, "over": None}
    try:
        ids = spawn_shared_bucket(ctx)
        target = ids[carrier]
        ctx.place_before(carrier, target, 90)
        ctx.face(carrier, target)
        ctx.grab(carrier, target)
        holding = {"holding": target}
        ctx.turn_to(carrier, pitch=10, rate=30, keep=[holding], name="lifts the bucket to chest height")
        if press_for:
            for button in ("forward", "back"):
                ctx.carry(carrier, target, [button], 1.5)
        # She stands as far off as the bucket is held, 50 degrees to the right: turning toward her swings the bucket
        # into her while the carrier stays clear of her.
        me, held = ctx.my_position(carrier), ctx.body_of(carrier, target)["position"]
        reach = math.dist(me[:2], held[:2])
        yaw = math.degrees(math.atan2(held[0] - me[0], held[1] - me[1]))
        side = math.radians(yaw + 50)
        spot = [me[0] + reach * math.sin(side), me[1] + reach * math.cos(side), me[2]]
        ctx.player_command("", {"cmd": "move_actor", "actor": npc, "position": spot})
        ctx.wait_for("[host] Lydia stands beside", lambda: distance(ctx.host_npc_state(npc)["position"][:2], spot[:2]) < 10, 5)
        ctx.wait_on("client", "her copy stands beside",
                    lambda actors: distance((actors.get(str(copy), {}).get("actual") or [1e9, 1e9])[:2], spot[:2]) < 10, 5)
        for game in ("", "client"):
            log(f"[{game or 'host'}] Lydia's controller before: {describe_controller(controller_of(ctx, game, her[game]))}")
        log(f"[{carrier or 'host'}] holds the bucket {reach:.0f} out, turns from {yaw:.0f} toward her at {yaw + 50:.0f}")
        # Off her axis and over her feet, every frame: the nearest while the bucket is level with her capsule.
        against = [{"name": "off", "distance_xy": [target, her[carrier]]}, {"name": "over", "z_between": [target, her[carrier]]}]

        def nearest_in(outcome):
            for row in outcome.get("series") or []:
                off, over = row[1], row[2]
                if off is not None and over is not None and 0 < over < CAPSULE_TOP and off < nearest["side"]:
                    nearest.update(side=off, over=over)

        # 30 degrees a second (a sudden turn drops a held object), up to her to press, else on past her.
        swing = ctx.act(carrier, {"name": "swings the bucket into her", "ready": ["takes_input"],
                                  "turn": {"yaw": yaw + (50 if press_for else 79.5), "rate": 30},
                                  "until": ["aimed"], "keep": [holding], "watch": against, "series": True, "seconds": 10})
        nearest_in(swing)
        for game in ("", "client"):
            log(f"[{game or 'host'}] Lydia's controller pushed: {describe_controller(controller_of(ctx, game, her[game]))}")
        if press_for:
            # Pressed against her; the other game watches its own bucket at the same time (the wall clock lines them up).
            shown = [{"name": "z", "z": ids[other]}]
            outcomes = ctx.act_both({
                carrier: {"name": f"holds the bucket against her for {press_for} s", "ready": ["takes_input"],
                          "until": [{"for": press_for}], "keep": [holding], "series": True, "seconds": press_for + 2,
                          "watch": [*against, {"name": "z", "z": target}]},
                other: {"name": "watches its bucket", "until": [{"for": press_for}], "series": True,
                        "seconds": press_for + 2, "watch": shown, "allow_menus": True}})
            pressed = outcomes[carrier]
            nearest_in(pressed)
            trail = [round(min(r[1] for r in pressed["series"][i:i + 60] if r[1] is not None), 1)
                     for i in range(0, len(pressed["series"]), 60)]
            log(f"[{carrier or 'host'}] pressed against her, nearest off her axis each second: {trail}")
            fell = {"most": 0.0, "times": 0, "falling": False}
            for row in pressed["series"]:
                shown_z = ctx.series_at(outcomes[other], 0, row[0])
                if row[3] is None or shown_z is None:
                    continue
                below = row[3] - shown_z
                fell["most"] = max(fell["most"], below)
                fell["times"] += below > 10 and not fell["falling"]
                fell["falling"] = below > 10
            log(f"[{other or 'host'}] its bucket fell below the held one {fell['times']} times, up to {fell['most']:.0f} units")
            if fell["times"]:
                raise ctx.failure(f"[{other or 'host'}] the bucket {carrier or 'host'} holds against her falls in its game "
                                  f"and is put back: {fell['times']} times in {press_for} s, up to {fell['most']:.0f} units below")
            for game in ("", "client"):
                log(f"[{game or 'host'}] Lydia's controller after pressing: {describe_controller(controller_of(ctx, game, her[game]))}")
    finally:
        if ctx.local_player(carrier).get("grabbed"):
            ctx.tap(carrier, "activate")
        ctx.console_on(carrier, "player.setangle x 0")
        if ids:
            ctx.console_on("", f"{ids['']:X}.disable")
            ctx.console_on("", f"{ids['']:X}.markfordelete")
        ctx.remove_host_npc(npc)
    report = f"nearest {nearest['side']:.1f} off her axis at {nearest['over'] or 0:.0f} over her feet, held throughout"
    log(f"drags-into-npc ({carrier or 'host'} carries): {report}")
    if nearest["side"] < INTO_CAPSULE:
        raise ctx.failure(f"[{carrier or 'host'}] the held bucket went into Lydia: {report}")


def scenario_guest_drags_into_npc(ctx):
    """The client's player swings a held bucket into a host NPC: her copy's capsule stops it."""
    drags_into_npc(ctx, "client")


def scenario_host_drags_into_npc(ctx):
    """The reference: the host's player swings a held bucket into its own NPC; her capsule stops it."""
    drags_into_npc(ctx, "")


def scenario_guest_holds_into_npc(ctx):
    """The client's player carries a bucket about, then holds it against a host NPC for 30 s: her copy's capsule
    keeps it out the whole time."""
    drags_into_npc(ctx, "client", press_for=30)


def scenario_host_holds_into_npc(ctx):
    """guest-holds-into-npc's reference: the host's player holds a bucket against its own NPC for 30 s."""
    drags_into_npc(ctx, "", press_for=30)



def sets_down_and_watches(ctx, placer):
    """`placer`'s player grabs a bucket, holds it beside itself and lets go: it rests in the placer's game, and every
    other game shows it resting there for 10 s, never falling on its own and snapped back (the user's 2026-11-02
    report: the host's item stayed put, the guest's kept falling, then came back where the host had it)."""
    other = "client" if placer == "" else ""
    ids = spawn_shared_bucket(ctx)
    try:
        mine, theirs = ids[placer], ids[other]
        ctx.place_before(placer, mine, 90)
        ctx.face(placer, mine)
        ctx.press(placer, "activate")
        try:
            ctx.wait_for(f"[{placer or 'host'}] grabs the bucket", lambda: ctx.local_player(placer).get("grabbed") == mine, 3)
        finally:
            ctx.release(placer, "activate")
        time.sleep(1.0)
        ctx.tap(placer, "activate")
        ctx.wait_for(f"[{placer or 'host'}] lets go", lambda: not ctx.local_player(placer).get("grabbed"), 3)
        ctx.wait_for(f"[{placer or 'host'}] the bucket rests", lambda: not ctx.body_of(placer, mine)["awake"], 10)
        rest = ctx.body_of(placer, mine)["position"]
        ctx.wait_for(f"[{other or 'host'}] shows it where it rests",
                     lambda: math.dist(ctx.body_of(other, theirs)["position"], rest) < 5, 5)
        log(f"[{placer or 'host'}] the bucket rests at {[round(x) for x in rest]}")
        # Watched: how far below its rest the other game's falls, and how often it wakes on its own.
        lowest, wakes, awake, worst = 0.0, 0, False, None
        start = time.monotonic()
        while time.monotonic() - start < 10:
            body = ctx.body_of(other, theirs)
            drop = rest[2] - body["position"][2]
            off = math.dist(body["position"], rest)
            if drop > lowest:
                lowest = drop
            if off > (worst or (0, 0))[0]:
                worst = (off, round(time.monotonic() - start, 1))
            if body["awake"] and not awake:
                wakes += 1
            awake = body["awake"]
            time.sleep(0.1)
        still = ctx.body_of(placer, mine)["position"]
        report = (f"{other or 'host'}'s fell up to {lowest:.1f} below its rest, woke {wakes} times, farthest "
                  f"{(worst or (0, 0))[0]:.1f} off at {(worst or (0, 0))[1]} s; the placer's moved {math.dist(still, rest):.1f}")
        log(f"sets-down ({placer or 'host'} places): {report}")
        if lowest > 5 or (worst and worst[0] > 5):
            raise ctx.failure(f"[{other or 'host'}] the bucket {placer or 'host'} set down does not stay put: {report}")
    finally:
        if ctx.local_player(placer).get("grabbed"):
            ctx.tap(placer, "activate")
        ctx.console_on("", f"{ids['']:X}.disable")
        ctx.console_on("", f"{ids['']:X}.markfordelete")


def scenario_host_sets_down_item(ctx):
    """The host's player sets a bucket down beside itself: the client shows it resting there, not falling."""
    sets_down_and_watches(ctx, "")


def scenario_guest_sets_down_item(ctx):
    """The client's player sets a bucket down beside itself: the host shows it resting there, not falling."""
    sets_down_and_watches(ctx, "client")


def drops_and_watches(ctx, dropper):
    """`dropper`'s player drops an iron sword from its inventory: once it rests in the dropper's game, every other
    game shows it resting there for 10 s, never falling on its own and snapped back (2026-11-02 report)."""
    other = "client" if dropper == "" else ""
    sword = 0x00012EB7
    before = {g: set(ctx.loose_swords(g)) for g in ("", "client")}
    ctx.console_on(dropper, f"player.additem {sword:X} 1")
    ctx.wait_for(f"[{dropper or 'host'}] the player carries a sword", lambda: (ctx.item_count(dropper, 0x14, sword) or 0) > 0, 5)
    dropped = ctx.player_command(dropper, {"cmd": "drop", "item": sword, "plain": True})["object"]
    try:
        ctx.wait_for(f"[{dropper or 'host'}] the sword rests", lambda: (b := ctx.body_of(dropper, dropped)) and not b["awake"], 10)
        rest = ctx.body_of(dropper, dropped)["position"]
        twin = ctx.wait_for(f"[{other or 'host'}] the dropped sword lies in its world",
                            lambda: ctx.new_loose_sword(other, before[other]), 10)
        try:
            ctx.wait_for(f"[{other or 'host'}] shows it where it rests",
                         lambda: (b := ctx.body_of(other, twin)) and math.dist(b["position"], rest) < 5, 10)
        except TestFailure:
            body = ctx.body_of(other, twin)
            raise ctx.failure(f"[{other or 'host'}] its sword is not where {dropper or 'host'}'s rests at "
                              f"{[round(x) for x in rest]}: at {[round(x) for x in body['position']] if body else None}, "
                              f"{math.dist(body['position'], rest):.1f} off, awake {body and body['awake']}") from None
        log(f"[{dropper or 'host'}] the sword rests at {[round(x) for x in rest]}")
        lowest, wakes, awake, worst = 0.0, 0, False, (0.0, 0.0)
        start = time.monotonic()
        while time.monotonic() - start < 10:
            body = ctx.body_of(other, twin)
            lowest = max(lowest, rest[2] - body["position"][2])
            off = math.dist(body["position"], rest)
            if off > worst[0]:
                worst = (off, round(time.monotonic() - start, 1))
            wakes += body["awake"] and not awake
            awake = body["awake"]
            time.sleep(0.1)
        report = (f"{other or 'host'}'s fell up to {lowest:.1f} below its rest, woke {wakes} times, farthest "
                  f"{worst[0]:.1f} off at {worst[1]} s")
        log(f"drops ({dropper or 'host'} drops): {report}")
        if lowest > 5 or worst[0] > 5:
            raise ctx.failure(f"[{other or 'host'}] the sword {dropper or 'host'} dropped does not stay put: {report}")
    finally:
        # The host's world decides: removed there, it goes from every game.
        for ref in set(ctx.loose_swords("")) - before[""]:
            ctx.console_on("", f"{ref:X}.disable")
            ctx.console_on("", f"{ref:X}.markfordelete")


def scenario_host_drops_item_stays(ctx):
    """The host's player drops a sword: the client shows it resting where the host's rests."""
    drops_and_watches(ctx, "")


def scenario_guest_drops_item_stays(ctx):
    """The client's player drops a sword: the host shows it resting where the client's rests."""
    drops_and_watches(ctx, "client")


def cannot_take_hold(ctx, holder):
    """`holder`'s player grabs a bucket and holds it up; the other player stands by it, looks at it and holds
    Activate. An object is its mover's alone (2026-11-02 decision): the other's grab is refused, the holder still holds
    it, and both games show it in the holder's hands."""
    taker = "client" if holder == "" else ""
    ids = spawn_shared_bucket(ctx)
    try:
        mine, theirs = ids[holder], ids[taker]
        ctx.place_before(holder, mine, 90)
        ctx.face(holder, mine)
        ctx.grab(holder, mine)
        ctx.turn_to(holder, pitch=-10, rate=30, keep=[{"holding": mine}], name="lifts the bucket")
        # The taker stands beside the bucket, 90 degrees round from the holder, and looks at it.
        ctx.place_before(taker, theirs, 80, bearing=(ctx.bearing_of(holder, mine) + 90) % 360)
        ctx.turn_to(taker, face=theirs, rate=60, name="looks at the held bucket")
        both = ctx.act_both({
            holder: {"name": "keeps holding it", "until": [{"for": 3}], "keep": [{"holding": mine}], "seconds": 4,
                     "allow_menus": True},
            taker: {"name": "tries to grab it", "ready": ["takes_input"], "buttons": ["activate"],
                    "until": [{"for": 3}], "keep": [{"holding": 0}], "seconds": 4}}, expect=None)
        for game, outcome in both.items():
            if outcome["status"] != "done":
                raise ctx.failure(f"[{game or 'host'}] the bucket {holder or 'host'} holds is not its alone: "
                                  f"{outcome['status']}, {outcome.get('reason')}")
    finally:
        for game in ("", "client"):
            if ctx.local_player(game).get("grabbed"):
                ctx.tap(game, "activate")
        ctx.console_on(holder, "player.setangle x 0")
        ctx.console_on("", f"{ids['']:X}.disable")
        ctx.console_on("", f"{ids['']:X}.markfordelete")


def scenario_host_cannot_take_guest_hold(ctx):
    """The client's player holds a bucket: the host's player cannot grab it from its hands."""
    cannot_take_hold(ctx, "client")


def scenario_guest_cannot_take_host_hold(ctx):
    """The host's player holds a bucket: the client's player cannot grab it from its hands."""
    cannot_take_hold(ctx, "")

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
    # Against its owner's: a wolf blown about by a second fireball shakes and flickers on the host too.
    trace.check_as_owner("", npc, "client", copy)

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
    trace.check_as_owner("", npc, "client", copy)

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
    # Its graphs' events and clips on both games (traces/<time>-guest-knockdown-lies-on-host/timeline.txt).
    bodies = RagdollTrace(ctx, "guest-knockdown-lies-on-host", {"client": {0x14: "the guest's player"}, "": {copy: "the host's copy"}})
    with bodies, AnimTrace(ctx, "guest-knockdown-lies-on-host", {"client": {0x14: "player"}, "": {copy: "player"}}, video=False):
        ctx.knocked_down_together("client", 0x14, "", copy, lambda: ctx.console_on("client", "player.pushactoraway player 5"))
    log("ragdoll: " + bodies.report())

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


def scenario_clutter_rests_at_any_frame_rate(ctx, *, window=20, _game=None):
    """S4 (playtests/2026-10-31-fabien.md): Havok's step set 2.4 times the frame (fMaxTime 0.04 at 60 fps, as the
    default 1/60 is at 144 fps) wakes the inn's clutter, which then creeps. Each game steps physics with its frames:
    the step reads back as the frame's whatever the INI says."""
    _game = ctx.game(_game)

    def awake(step):
        ctx.run_console(f'setini "fMaxTime:HAVOK" {step}')
        counts, end = {}, time.monotonic() + window
        while time.monotonic() < end:
            time.sleep(2)
            for ref, r in ctx.world_state("").items():
                if any(body.get("awake") for body in r.get("collision") or []):
                    counts[ref] = counts.get(ref, 0) + 1
        # Awake in the second half of the window: woken and not settling.
        return {ref for ref, n in counts.items() if n > window // 4}

    try:
        ctx.both_go(INN)
        matched = awake(0.0166)
        mismatched = awake(0.04)
        # Which clutter wakes also depends on who walks by: the step itself is the check, the counts the evidence.
        woken = sorted(mismatched - matched)
        log(f"[host] awake at a matched step: {len(matched)}, at 2.4 frames: {len(mismatched)}; woken: "
            + ", ".join(f"{ref:X}" for ref in woken))
        step = float(ctx.run_console('getini "fMaxTime:HAVOK"')[0].split(">>")[-1])
        if step > 0.03:
            raise ctx.failure(f"[host] Havok steps {step} s at 60 fps (2.4 frames), woke {len(woken)} objects that rest "
                              "at a matched step")
    finally:
        ctx.run_console('setini "fMaxTime:HAVOK" 0.0166')
        ctx.both_go(HOME_CELL)
