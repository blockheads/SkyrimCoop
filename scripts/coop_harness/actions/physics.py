"""Actions for the co-op harness."""
import argparse
import collections
import contextlib
import math
import os
import re
import socket
import subprocess
import time
import uuid
from ..common import (
    BODY_TURN_TOLERANCE,
    BONE_APART,
    BONE_ON_BODY,
    CREATED_ID,
    DOOR_CLOSING,
    DOOR_OPENING,
    DYNAMIC_MOTIONS,
    FIREBALL,
    FORM_DISABLED,
    FORM_TYPE_NAMES,
    IDLE_INTERVAL,
    IDLE_SAMPLES,
    KEYFRAMED_MOTION,
    OBJECT_EVENTS,
    PLAYER_WEREWOLF_QUEST,
    WORLD_WINDOW,
)
from ..errors import TestFailure
from ..comparisons import (
    distance,
    first_body,
    near,
    reference_differences,
    same_turn,
    show_value,
    world_differences,
)
from ..utilities import log
from ..objects import FlightTrace


class PhysicsActions:
    def world_state(ctx, instance, ids=None):
        """The references in that game's loaded cells (or only `ids`), actors aside (copies: `desync`): {id: fields}."""
        _game = ctx.game(instance)
        response = _game.rpc({"cmd": "world_state", "details": True, **({"ids": ids} if ids else {})}, timeout=20) or {}
        if not response.get("ok"):
            raise ctx.failure(f"[{instance or 'host'}] world_state refused: {response.get('error', 'no answer')}")
        return {r["id"]: r for r in response["references"] if not r.get("actor")}


    def sample_worlds(ctx, idle_samples=IDLE_SAMPLES):
        """Both games' worlds, and each again over a second: (host, guest, [host again], [guest again])."""
        host, guest = ctx.world_state(""), ctx.world_state("client")
        host_again, guest_again = [], []
        for _ in range(idle_samples):
            time.sleep(IDLE_INTERVAL)
            host_again.append(ctx.world_state(""))
            guest_again.append(ctx.world_state("client"))
        return host, guest, host_again, guest_again


    def shared_world_differences(ctx, idle_samples=IDLE_SAMPLES):
        """world_differences over the cells both games have loaded (the exterior grids around two players differ)."""
        worlds = ctx.sample_worlds(idle_samples)
        cells = {r["cell"] for r in worlds[0].values()} & {r["cell"] for r in worlds[1].values()}
        def cut(world):
            return {i: r for i, r in world.items() if r["cell"] in cells}
        return world_differences(cut(worlds[0]), cut(worlds[1]), [cut(w) for w in worlds[2]], [cut(w) for w in worlds[3]])


    def check_world(ctx, known=frozenset(), window=WORLD_WINDOW):
        """The world differences that last the whole window, without the (reference, field) pairs in `known` (already
        different before): {reference: {field: 'host vs guest'}}."""
        def new(differences):
            return any((label, f) not in known for label, fields in differences.items() for f in fields)
        # One quick look first: the idle samples only take differences away, so none here is none.
        if not new(ctx.shared_world_differences(idle_samples=0)):
            return {}
        deadline = time.monotonic() + window
        persistent = None
        while True:
            current = {label: {f: t for f, t in fields.items() if (label, f) not in known}
                       for label, fields in ctx.shared_world_differences().items()}
            if persistent is not None:
                current = {label: {f: t for f, t in fields.items() if f in persistent.get(label, {})}
                           for label, fields in current.items()}
            persistent = {label: fields for label, fields in current.items() if fields}
            if not persistent or time.monotonic() > deadline:
                return persistent


    def pick_reference(ctx, refs, type_name, nth=0, where=lambda r: True):
        """The nth reference of that base type the host shows (enabled, with 3D), lowest ids first."""
        type_id = next(i for i, name in FORM_TYPE_NAMES.items() if name == type_name)
        found = sorted(i for i, r in refs.items() if r["type"] == type_id and i < CREATED_ID and r["has_3d"]
                       and not r["flags"] & FORM_DISABLED and where(r))
        if len(found) <= nth:
            raise ctx.failure(f"no {type_name} #{nth} in the host's cell to change")
        return found[nth]


    def change_values(ctx, ref):
        z, angle = ref["position"][2], math.degrees(ref["rotation"][2]) % 360
        return {"z": f"{z:.2f}", "z_up": f"{z + 64:.2f}", "angle": f"{angle:.2f}", "turned": f"{(angle + 45) % 360:.2f}",
                "scale": f"{ref['scale']:.3f}", "level": (ref.get("lock") or {}).get("level", 0)}


    def at_rest(ctx, instance, ref, timeout=5):
        """Waits until the reference stops changing in that game (a door done swinging), as a player would before
        acting on it again."""
        last = {}
        time.sleep(0.1)  # a console line's engine call can run a frame later
        def still():
            now = ctx.world_state(instance, [ref]).get(ref) or {}
            settled = last and not reference_differences(last, now) and now.get("open_state") not in (DOOR_OPENING, DOOR_CLOSING)
            last.clear()
            last.update(now)
            return settled
        ctx.wait_for(f"[{instance or 'host'}] {ref:X} at rest", still, timeout)


    def guest_matches(ctx, ref, timeout=3):
        """Waits for the client's reference to match the host's; the fields that still differ, or none."""
        differences = {}
        def matches():
            host, guest = ctx.world_state("", [ref]).get(ref), ctx.world_state("client", [ref]).get(ref)
            differences.clear()
            differences.update(reference_differences(host, guest) if guest else {"present": "host only"})
            return not differences
        with contextlib.suppress(TestFailure):
            ctx.wait_for(f"[client] {ref:X} as the host's", matches, timeout)
        return differences


    def object_events(ctx, instance, since, ref):
        """The object events that game raised on the reference since that number, by kind in order."""
        events, _ = ctx.engine_events(instance, since)
        return [e["kind"] for e in events if (e["kind"] in OBJECT_EVENTS and e.get("object") == ref)
                or (e["kind"] == "container_changed" and ref in (e.get("from"), e.get("to")))]


    def put_back_left(ctx, before, ref):
        """What still differs from `before` in the host's reference, but its change record: a state put back can leave
        its flag (the engine's bookkeeping; the client must still match it)."""
        return {f: t for f, t in reference_differences(before, ctx.world_state("", [ref])[ref]).items() if f != "changes"}


    def world_change_reaches_guest(ctx, name, pick, stage, change, undo, refs):
        """Makes one change on the host and puts it back in both games: what the client got wrong, or none. The
        events each game raised on the reference must match too: they are the change's side effects on scripts."""
        ref = ctx.pick_reference(refs, *pick)
        values = {"r": f"{ref:X}", **ctx.change_values(refs[ref])}
        for line in stage:
            for game in ("", "client"):
                ctx.console_on(game, line.format(**values))
        before = ctx.world_state("", [ref])[ref]
        since = {game: ctx.engine_events(game)[1] for game in ("", "client")}
        differences = {}
        try:
            changes = [change] if isinstance(change, str) else change
            for line in changes:
                ctx.console_on("", line.format(**values))
            try:
                ctx.wait_for(f"[host] {name}: {ref:X} changed", lambda: reference_differences(before, ctx.world_state("", [ref])[ref]), 3)
            except TestFailure:
                return {"test": f"the host's `{'; '.join(changes).format(**values)}` changed nothing it reads"}
            differences = ctx.guest_matches(ref)
            host_events, guest_events = (ctx.object_events(game, since[game], ref) for game in ("", "client"))
            if host_events != guest_events:
                differences["events"] = f"{host_events} vs {guest_events}"
            return differences
        finally:
            # Put back on the host: the client must follow that too (a state back to its default is a change as well).
            # Repaired in the client only where it did not (its own change would be proposed).
            # A door ignores a SetOpenState while it swings: the host is put back at rest, and checked against how it was.
            for attempt in range(2 if undo else 0):
                ctx.at_rest("", ref)
                for line in undo:
                    ctx.console_on("", line.format(**values))
                with contextlib.suppress(TestFailure):
                    ctx.wait_for(f"[host] {ref:X} as before", lambda: not ctx.put_back_left(before, ref), 5)
                    break
            else:
                if undo:
                    differences["test"] = "the host's put-back left " + "; ".join(f"{f} {t}" for f, t in ctx.put_back_left(before, ref).items())
            if undo and (left := ctx.guest_matches(ref)):
                differences["put back"] = "; ".join(f"{f} {t}" for f, t in left.items())
                for line in undo:
                    ctx.console_on("client", line.format(**values))
            # The put-back's events, raised a frame apart in each game, stay in this kind's window, not the next's.
            with contextlib.suppress(TestFailure):
                ctx.wait_for(f"[both] {ref:X}'s events alike", lambda: ctx.object_events("", since[""], ref) == ctx.object_events("client", since["client"], ref), 3)


    def settled(ctx, ref, still_for=1.0, timeout=30):
        """Waits until the host's physics object stays within half a unit and 0.005 rad of one spot for still_for
        seconds (HostEngine's rest rule)."""
        anchor = {}
        def still():
            now = ctx.world_state("", [ref]).get(ref)
            if now is None:
                return False
            if not anchor or not near(anchor["position"], now["position"], 0.5) or not near(anchor["rotation"], now["rotation"], 0.005, angle=True):
                anchor.update(position=now["position"], rotation=now["rotation"], since=time.monotonic())
                return False
            return time.monotonic() - anchor["since"] >= still_for
        ctx.wait_for(f"[host] {ref:X} comes to rest", still, timeout)


    def created_reaches_guest(ctx, refs, base):
        """The host places a new object: the client shows one like it. Deleted again in both games."""
        before = {game: set(ctx.world_state(game)) for game in ("", "client")}
        ctx.console_on("", f"player.placeatme {base:X} 1")
        try:
            made = ctx.wait_for("[host] the new object", lambda: set(ctx.world_state("")) - before[""], 3)
            # It drops and rolls (a ladle rocks for 20s): compared where the host's comes to rest (each game simulates
            # the fall, the host's rest goes out).
            for ref in made:
                ctx.settled(ref)
            missing = {}
            def twin_shown():
                host, guest = ctx.world_state(""), ctx.world_state("client")
                missing.clear()
                missing.update({label: fields for label, fields in world_differences(host, guest).items() if label.startswith("created")})
                return not missing
            with contextlib.suppress(TestFailure):
                ctx.wait_for("[client] a twin of the host's new object", twin_shown, 3)
            return {label: "; ".join(f"{f} {t}" for f, t in fields.items()) for label, fields in missing.items()}
        finally:
            for game in ("", "client"):
                for ref in set(ctx.world_state(game)) - before[game]:
                    ctx.console_on(game, f"{ref:X}.disable")
                    ctx.console_on(game, f"{ref:X}.markfordelete")


    def body_of(ctx, instance, ref):
        return first_body(ctx.world_state(instance, [ref]).get(ref))


    def resting_clutter_on_both(ctx, refs, guest_refs):
        """A filter for clutter both games have, one body, in the physics world, at rest on the host."""
        def resting(ref):
            host, guest = first_body(ref), first_body(guest_refs.get(ref["id"]))
            return (host and guest and len(ref["collision"]) == 1 and host["in_world"] and guest["in_world"]
                    and host["motion"] in DYNAMIC_MOTIONS and not host["awake"])
        return resting


    def guest_follows_and_lies(ctx, target, start):
        """After the host's `target` moved off `start`: the client's 3D follows it held keyframed, and it lies where the
        host's lies."""
        seen = []
        def follows():
            # What the player sees: its 3D with its body (a keyframed body leaves its node behind, physics-sync.md).
            ref = ctx.world_state("client", [target]).get(target)
            body = first_body(ref)
            seen.append(f"motion {body['motion']}, 3D {math.dist(ref['position_3d'], start):.0f} off its start and "
                        f"{math.dist(ref['position_3d'], body['position']):.1f} from its body")
            return (body["motion"] == KEYFRAMED_MOTION and not near(ref["position_3d"], start, 5)
                    and near(ref["position_3d"], body["position"], 3))
        try:
            ctx.wait_for(f"[client] {target:X} and its 3D follow the host's, held keyframed", follows, 5)
        except TestFailure as failure:
            changes = [s for i, s in enumerate(seen) if i == 0 or s != seen[i - 1]]
            raise ctx.failure(f"{failure}: the client's was " + "; then ".join(changes[-6:])) from None
        ctx.wait_for(f"[host] {target:X} comes to rest", lambda: not ctx.body_of("", target)["awake"], 30)
        rest = ctx.body_of("", target)
        def lies_as_host():
            body = ctx.body_of("client", target)
            return near(body["position"], rest["position"], 1.0) and same_turn(body["rotation"], rest["rotation"])
        ctx.wait_for(f"[client] {target:X} lies where the host's does, at {show_value('position', rest['position'])}", lies_as_host, 5)


    def guest_own_lies_as_host(ctx, target, start, how, moved=5):
        """After the client's player moved `target` (`how`): the client's moved `moved` units off `start` by its own
        physics (the host's frames of it wait), and once the host's rests it lies where the host's lies, simulated again."""
        seen = []
        def own():
            body = ctx.body_of("client", target)
            seen.append(f"motion {body['motion']}, {math.dist(body['position'], start):.0f} from where it was")
            return body["motion"] in DYNAMIC_MOTIONS and not near(body["position"], start, moved)
        try:
            ctx.wait_for(f"[client] {target:X} {how} by its own physics", own, 5)
        except TestFailure as failure:
            changes = [s for i, s in enumerate(seen) if i == 0 or s != seen[i - 1]]
            raise ctx.failure(f"{failure}: the client's was " + "; then ".join(changes[-6:])) from None
        ctx.wait_for(f"[host] {target:X} comes to rest", lambda: not ctx.body_of("", target)["awake"], 30)
        rest = ctx.body_of("", target)
        def lies_as_host():
            body = ctx.body_of("client", target)
            return near(body["position"], rest["position"], 1.0) and same_turn(body["rotation"], rest["rotation"])
        ctx.wait_for(f"[client] {target:X} lies where the host's does, at {show_value('position', rest['position'])}", lies_as_host, 5)
        ctx.wait_for(f"[client] {target:X} is simulated again", lambda: ctx.body_of("client", target)["motion"] in DYNAMIC_MOTIONS, 2)


    def where_now(ctx, ids):
        """The host's references' positions and rotations, to put back after (put_back)."""
        return {i: (r["position"], r["rotation"]) for i, r in ctx.world_state("", list(ids)).items()}


    def put_back(ctx, starts):
        """The host puts each physics object back where `starts` had it (the console's SetAngle, then SetPos, which
        updates its 3D; both games get the move), so the next scenario finds the clutter where the cell has it."""
        for ref, (position, rotation) in starts.items():
            for axis, value in zip("xyz", rotation):
                ctx.console_on("", f"{ref:X}.setangle {axis} {math.degrees(value):.2f}")
            for axis, value in zip("xyz", position):
                ctx.console_on("", f"{ref:X}.setpos {axis} {value:.2f}")
        def back():
            now = ctx.world_state("", list(starts))
            return all(near(now[i]["position"], p, 3) and not first_body(now[i])["awake"] for i, (p, _) in starts.items() if i in now)
        ctx.wait_for(f"[host] {len(starts)} objects back where they were", back, 30)
        def client_too():
            host, guest = ctx.world_state("", list(starts)), ctx.world_state("client", list(starts))
            return all(i in guest and near(first_body(guest[i])["position"], first_body(host[i])["position"], 1.0)
                       and same_turn(first_body(guest[i])["rotation"], first_body(host[i])["rotation"], BODY_TURN_TOLERANCE) for i in host)
        ctx.wait_for(f"[client] shows them back", client_too, 5)


    def all_rest(ctx, ids):
        """Every one of `ids` the host has comes to rest: a slow roll on the floor can take half a minute, and the guest
        is 100 ms behind until its rest arrives, so the world is compared only after."""
        ctx.wait_for(f"[host] all {len(ids)} rest", lambda: not any(
            first_body(r)["awake"] for r in ctx.world_state("", list(ids)).values()), 60)


    def knock_clutter_with_fireball(ctx, caster):
        """The caster's player knocks clutter with a fireball; the object the blast throws furthest follows on the client
        and lies as the host's. A client's cast reaches the host's world through its copy's replayed cast."""
        refs, guest_refs = ctx.world_state(""), ctx.world_state("client")
        resting = ctx.resting_clutter_on_both(refs, guest_refs)
        clutter = {i: first_body(refs[i])["position"] for i in refs
                   if refs[i]["type"] == refs[ctx.pick_reference(refs, "MISC", 0, resting)]["type"] and resting(refs[i])}
        def around(spot):
            return {i: p for i, p in clutter.items() if math.dist(p, spot) < 200}
        # The blast throws whichever of the clutter around the aim it reaches best: aimed at the most of it (a table's,
        # not one an earlier scenario left alone on the floor).
        aim = max(sorted(clutter), key=lambda i: len(around(clutter[i])))
        nearby = around(clutter[aim])
        starts = ctx.where_now(nearby)
        ctx.give_spell(caster, FIREBALL)
        ctx.place_before(caster, aim, 300)
        trace = FlightTrace(ctx, f"fireball-{caster or 'host'}", nearby)
        try:
            trace.__enter__()
            ctx.draw(caster)
            ctx.cast_spell(caster, aim, FIREBALL)
            knocked = {}
            def flies_off():
                now = ctx.world_state("", list(nearby))
                moved = {i: math.dist(first_body(now[i])["position"], start) for i, start in nearby.items() if i in now}
                knocked["target"], distance = max(moved.items(), key=lambda m: m[1])
                return distance > 5
            ctx.wait_for(f"[host] the {caster or 'host'}'s blast throws one of {len(nearby)} clutter objects around {aim:X}", flies_off, 10)
            ctx.guest_follows_and_lies(knocked["target"], nearby[knocked["target"]])
            ctx.all_rest(nearby)
        finally:
            trace.__exit__(None, None, None)
            ctx.sheathe(caster)
        for ref in sorted(nearby, key=lambda i: i != knocked.get("target")):
            log("flight: " + trace.report(ref))
        ctx.put_back(starts)
        for ref in nearby:
            trace.check(ref)


    def stage_on_floor_before(ctx, instance, target, ahead=150):
        """The host puts `target` on the floor `ahead` units in front of that game's player (the console's setpos, a
        move both games get), away from the other player; returns where it rests once both show it there."""
        me, other = ctx.my_position(instance), ctx.other_player_position(instance)
        away = [me[0] - other[0], me[1] - other[1]]
        length = math.hypot(*away)
        away = [away[0] / length, away[1] / length] if length > 1 else [1.0, 0.0]
        spot = [me[0] + away[0] * ahead, me[1] + away[1] * ahead, me[2] + 20]
        for axis, value in zip("xyz", spot):
            ctx.console_on("", f"{target:X}.setpos {axis} {value:.0f}")
        def rests_there():
            body = ctx.body_of("", target)
            return not body["awake"] and math.dist(body["position"][:2], spot[:2]) < 30
        ctx.wait_for(f"[host] {target:X} rests on the floor at ({spot[0]:.0f}, {spot[1]:.0f})", rests_there, 10)
        rest = ctx.body_of("", target)["position"]
        ctx.wait_for(f"[client] shows {target:X} there", lambda: near(ctx.body_of("client", target)["position"], rest, 2), 5)
        return rest


    def ragdoll_of(ctx, instance, actor):
        """The actor's ragdoll bodies in order (harness `ragdoll`): motion, awake, position, rotation, velocity."""
        _game = ctx.game(instance)
        response = _game.rpc({"cmd": "ragdoll", "actor": actor}) or {}
        if not response.get("ok"):
            raise ctx.failure(f"[{instance or 'host'}] no ragdoll for {actor:X}: {response.get('error', 'no answer')}")
        return response["bodies"]


    def ragdoll_lies_as_owner(ctx, owner, actor, other, copy):
        """Once the owner's game's ragdoll of `actor` comes to rest, the other game's copy lies as it, bone by bone."""
        who, where = owner or "host", other or "host"
        def owner_rests():
            bodies = ctx.ragdoll_of(owner, actor)
            return any(b["motion"] in DYNAMIC_MOTIONS for b in bodies) and not any(b["awake"] for b in bodies)
        ctx.wait_for(f"[{who}] the ragdoll of {actor:X} comes to rest", owner_rests, 30)
        rest = ctx.ragdoll_of(owner, actor)
        apart = {}
        owner_dead = ctx.is_dead(ctx.actor_snapshot(owner, actor))
        def lies_as_owner():
            bodies = ctx.ragdoll_of(other, copy)
            if owner_dead and not ctx.is_dead(ctx.actor_snapshot(other, copy)):
                apart["now"] = "the owner's actor is dead and the copy lives"
                return False
            # What a person sees too: each bone on its body, and the bones where the owner's lie.
            apart["now"] = "; ".join(filter(None, [ctx.ragdoll_apart(rest, bodies), ctx.bones_off_bodies(bodies, "the copy's"),
                                                    ctx.bones_apart(rest, bodies)]))
            return not apart["now"]
        try:
            ctx.wait_for(f"[{where}] the copy {copy:X} lies as the {who}'s body ({len(rest)} bones)", lies_as_owner, 10)
        except TestFailure:
            raise ctx.failure(f"[{where}] the copy {copy:X} does not lie as the {who}'s body: {apart.get('now')}") from None


    def bones_off_bodies(ctx, bodies, whose):
        """The bones not on their bodies, as text: the skeleton a person sees is not where the physics lies."""
        off = [(i, math.dist(b["bone"], b["position"])) for i, b in enumerate(bodies) if b.get("bone")]
        off = [(i, d) for i, d in off if d > BONE_ON_BODY]
        return (f"{whose} visible body is not on its ragdoll: {len(off)} bones off their bodies, up to "
                f"{max(d for _, d in off):.0f} units (bone {max(off, key=lambda x: x[1])[0]})") if off else ""


    def bones_apart(ctx, owner, copy):
        """The copy's bones away from the owner's, as text."""
        pairs = [(i, math.dist(a["bone"], b["bone"])) for i, (a, b) in enumerate(zip(owner, copy)) if a.get("bone") and b.get("bone")]
        off = [(i, d) for i, d in pairs if d > BONE_APART]
        return (f"the copy's visible body lies {max(d for _, d in off):.0f} units from the owner's ({len(off)} of {len(pairs)} bones)"
                if off else "")


    def ragdoll_apart(ctx, host, guest, units=2.0, turn=0.05):
        """The bones where two ragdolls differ, as text (empty: alike within `units` and `turn` radians)."""
        if len(host) != len(guest):
            return f"{len(host)} bones vs {len(guest)}"
        apart = [f"bone {i} at {show_value('position', a['position'])} vs {show_value('position', b['position'])}"
                 for i, (a, b) in enumerate(zip(host, guest))
                 if not near(a["position"], b["position"], units) or not same_turn(a["rotation"], b["rotation"], turn)]
        return "; ".join(apart[:4]) + (f" (+{len(apart) - 4} more)" if len(apart) > 4 else "")


    def knocked_down_together(ctx, owner, actor, other, copy, knock_down):
        """Physics sync's owners, living knockdowns (docs/design/physics-sync.md): `knock_down()` knocks the owner's `actor`
        down. The other game's copy goes down with it, its ragdoll moved by the owner's frames, lies as the owner's does,
        and gets up with it."""
        who, where = owner or "host", other or "host"
        def knock(instance, form):
            return (ctx.actor_snapshot(instance, form).get("state") or {}).get("knock", 0)
        knock_down()
        ctx.wait_for(f"[{who}] {actor:X} is knocked down", lambda: knock(owner, actor), 3)
        ctx.wait_for(f"[{where}] its copy {copy:X} goes down with it", lambda: knock(other, copy), 3)
        # The owner's gets up once still, so the copy is compared with where the owner's lay a moment before (the copy
        # plays the owner's frames 100 ms behind, blended: within 4 units and 0.2 rad of one of them, not at a rest; a
        # copy left to its own physics lies meters off).
        owner_poses, closest = [], {"apart": "never compared while both lay"}
        def lies_as_owner():
            now = time.monotonic()
            if knock(owner, actor):
                owner_poses.append((now, ctx.ragdoll_of(owner, actor)))
            if knock(other, copy):
                lying = ctx.ragdoll_of(other, copy)
                for at, pose in owner_poses:
                    if now - 0.6 < at < now - 0.05:
                        if not (apart := ctx.ragdoll_apart(pose, lying, 4.0, 0.2)):
                            closest["alike"] = True
                        closest["apart"] = apart or closest["apart"]
            return not knock(owner, actor) and not knock(other, copy) or closest.get("alike")
        ctx.wait_for(f"[{where}] the copy lies as the {who}'s body lay a moment before", lies_as_owner, 20)
        if not closest.get("alike"):
            raise ctx.failure(f"[{where}] the copy never lay as the {who}'s body: {closest['apart']}")
        ctx.wait_for(f"[{who}] {actor:X} is up again", lambda: not knock(owner, actor), 20)
        ctx.wait_for(f"[{where}] the copy is up with it", lambda: not knock(other, copy), 3)


    def script_running(ctx, instance, script):
        """A stack in that game's VM runs a function of that script (harness script_stacks), waiting ones too."""
        prefix = script.lower() + "."
        return any(frame.lower().startswith(prefix) for stack in ctx.player_command(instance, {"cmd": "script_stacks"})["stacks"]
                   for frame in stack["frames"])


    def werewolf_running(ctx, instance):
        _game = ctx.game(instance)
        return bool((_game.rpc({"cmd": "quest", "quest": PLAYER_WEREWOLF_QUEST}) or {}).get("flags", 0) & 1)

