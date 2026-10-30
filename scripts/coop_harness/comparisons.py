"""Comparisons for the co-op harness."""
import collections
import math
import re
from .common import (
    DIALOGUE_FIELDS,
    DIALOGUE_GRACE,
    LOG_LINE,
    REFUSED_REPLAY,
    log_time,
    ANGLE_TOLERANCE,
    BODY_TOLERANCE,
    BODY_TURN_TOLERANCE,
    CHANGES_FORM_FLAGS,
    CHANGES_HAVOK_MOVED,
    CHANGES_MOVED,
    CHANGES_OWN,
    CREATED_ID,
    DYNAMIC_MOTIONS,
    FLOAT_TOLERANCE,
    FORM_DELETED,
    FORM_NOT_SYNCED,
    FORM_TYPE_NAMES,
    LOW_PELVIS,
    POSE_TOLERANCE,
    POSITION_TOLERANCE,
    RAISED_PELVIS,
    ROTATE_TOLERANCE,
    VALUE_TOLERANCE,
    WORLD_FIELDS,
    WORLD_TOLERANCES,
)


def distance(a, b):
    return sum((x - y) ** 2 for x, y in zip(a, b)) ** 0.5


def near(a, b, tolerance, angle=False):
    a, b = (v if isinstance(v, list) else [v] for v in (a, b))
    def gap(x, y):
        d = abs(x - y) % (2 * math.pi) if angle else abs(x - y)
        return min(d, 2 * math.pi - d) if angle else d
    return len(a) == len(b) and all(gap(x, y) <= tolerance for x, y in zip(a, b))


def show_value(field, value):
    if value is None:
        return "-"
    if field in ("base", "cell", "owner"):
        return f"{value:X}"
    if field in ("flags", "changes"):
        return f"{value:#x}"
    if isinstance(value, list):
        return "(" + ", ".join(f"{v:.2f}" if field == "rotation" else f"{v:.0f}" for v in value) + ")"
    if isinstance(value, dict):
        return "{" + ", ".join(f"{k}: {v}" for k, v in value.items()) + "}"
    return str(value)


def pose_moved(a, b, name):
    return not near(a[name]["position"], b[name]["position"], POSE_TOLERANCE) or not near(a[name]["rotate"], b[name]["rotate"], ROTATE_TOLERANCE)


def pose_difference(a, b, a_again=(), b_again=()):
    """The nodes of two poses ({name: {position, rotate}}) that are not where the other game's are. With each pose
    sampled again, a node that moved between one game's samples animates on its own (an idle loop, which the engine
    also stops off-camera) and is skipped."""
    a, b = a or {}, b or {}
    moved = []
    for name in sorted(set(a) | set(b)):
        if any(name in a and name in again and pose_moved(a, again, name) for again in a_again) or \
                any(name in b and name in again and pose_moved(b, again, name) for again in b_again):
            continue
        if name not in a or name not in b:
            moved.append(f"{name} {'host only' if name in a else 'client only'}")
        elif not near(a[name]["position"], b[name]["position"], POSE_TOLERANCE):
            moved.append(f"{name} at {show_value('position', a[name]['position'])} vs {show_value('position', b[name]['position'])}")
        elif not near(a[name]["rotate"], b[name]["rotate"], ROTATE_TOLERANCE):
            moved.append(f"{name} turned differently")
    return "; ".join(moved[:6]) + (f" (+{len(moved) - 6} more)" if len(moved) > 6 else "")


def simulated(ref):
    """A physics object both games simulate: its first body dynamic (not keyframed by an animation, not fixed)."""
    return (first_body(ref) or {}).get("motion") in DYNAMIC_MOTIONS


def collision_difference(a, b):
    """Two lists of physics bodies ({node, in_world, position, rotation}) that differ, in order; a simulated body's
    turn too (an animated one turns on each game's clock)."""
    a, b = a or [], b or []
    if len(a) != len(b):
        return f"{len(a)} bodies vs {len(b)}"
    def turned(x, y):
        return x["motion"] in DYNAMIC_MOTIONS and y["motion"] in DYNAMIC_MOTIONS and not same_turn(x["rotation"], y["rotation"], BODY_TURN_TOLERANCE)
    different = [f"{x['node']}: {'in' if x['in_world'] else 'out of'} world at {show_value('position', x['position'])} vs "
                 f"{'in' if y['in_world'] else 'out of'} at {show_value('position', y['position'])}" + (", turned differently" if turned(x, y) else "")
                 for x, y in zip(a, b) if x["in_world"] != y["in_world"] or not near(x["position"], y["position"], BODY_TOLERANCE) or turned(x, y)]
    return "; ".join(different[:4])


def reference_differences(host, guest, host_again=(), guest_again=()):
    """Where the guest's reference differs from the host's: {field: 'host value vs guest value'}. host_again,
    guest_again: the reference sampled again in each game, to skip what animates on its own."""
    differences = {}
    for field in WORLD_FIELDS:
        a, b = host.get(field), guest.get(field)
        if field == "pose":
            # Untouched in both and without a graph: only its model's own loops, on each game's clock (frozen
            # off-camera at different phases).
            if not any(r.get(k) for r in (host, guest) for k in ("changes", "graph")):
                continue
            if text := pose_difference(a, b, [again.get("pose") or {} for again in host_again or ()],
                                       [again.get("pose") or {} for again in guest_again or ()]):
                differences[field] = text
            continue
        if field == "collision":
            if text := collision_difference(a, b):
                differences[field] = text
            continue
        # A physics object's turn is its bodies' (collision, as quaternions): its angles flip near a pitch of 90°.
        if field == "rotation" and simulated(host) and simulated(guest):
            continue
        if field == "lock":  # no lock reads as unlocked at level 0, as the lock row does
            a, b = (v or {"level": 0, "locked": False} for v in (a, b))
        if field == "flags" and a is not None and b is not None:
            a, b = a & ~FORM_NOT_SYNCED, b & ~FORM_NOT_SYNCED
        if field == "changes":
            # INVENTORY records deltas from each game's rolled base; the actual contents are compared above.
            if "items" in host and "items" in guest and host["items"] == guest["items"]:
                a, b = (a or 0) & ~0x20, (b or 0) & ~0x20
            a, b = (a or 0) & ~CHANGES_OWN or None, (b or 0) & ~CHANGES_OWN or None
            # A form-flags record for a flag each game keeps itself (a look-at target): equal flags otherwise.
            if ((host.get("flags") or 0) ^ (guest.get("flags") or 0)) & ~FORM_NOT_SYNCED == 0:
                a, b = (a or 0) & ~CHANGES_FORM_FLAGS or None, (b or 0) & ~CHANGES_FORM_FLAGS or None
            # Moved by a call or by physics: either way its position is what a save keeps.
            a, b = ((v | CHANGES_MOVED) & ~CHANGES_HAVOK_MOVED if v and v & CHANGES_HAVOK_MOVED else v for v in (a, b))
        if field in WORLD_TOLERANCES and a is not None and b is not None:
            same = near(a, b, WORLD_TOLERANCES[field], angle=field == "rotation")
        else:
            same = a == b
        if same:
            continue
        if field in ("items", "graph"):
            a, b = a or {}, b or {}
            differences[field] = ", ".join(f"{key}: {a.get(key, 0)} vs {b.get(key, 0)}"
                                           for key in sorted(set(a) | set(b)) if a.get(key, 0) != b.get(key, 0))
        else:
            differences[field] = f"{show_value(field, a)} vs {show_value(field, b)}"
            if field in ("flags", "changes"):
                differences[field] += f" (bits {(a or 0) ^ (b or 0):#x})"
    return differences


def reference_label(ref, game=None):
    kind = FORM_TYPE_NAMES.get(ref["type"], ref["type"])
    if ref["id"] < CREATED_ID:
        return f"{ref['id']:X} ({kind} {ref['base']:X})"
    return f"created {kind} {ref['base']:X} at {show_value('position', ref['position'])} ({ref['id']:X} on {game})"


def world_differences(host, guest, host_again=(), guest_again=()):
    """Where the guest's world differs from the host's: {reference: {field: 'host vs guest'}}. A reference one game
    lacks differs in "present"; created references pair up by base and position (their ids differ per game).
    host_again, guest_again: each game sampled again, so what animates on its own is skipped."""
    differences = {}
    for ref_id, ref in host.items():
        if ref_id < CREATED_ID:
            if ref_id not in guest:
                differences[reference_label(ref)] = {"present": "host only"}
            elif fields := reference_differences(ref, guest[ref_id], [again[ref_id] for again in host_again if ref_id in again],
                                                 [again[ref_id] for again in guest_again if ref_id in again]):
                differences[reference_label(ref)] = fields
    for ref_id, ref in guest.items():
        if ref_id < CREATED_ID and ref_id not in host:
            differences[reference_label(ref)] = {"present": "client only"}
    # A created reference deleted is gone (the engine frees it): only a living one is compared. A cast's projectiles
    # and hazards are each game's own, as the cast is (HostEngine's ReadCreate sends plain references only).
    def alive_created(world):
        return [r for r in world.values() if r["id"] >= CREATED_ID and not (r.get("flags") or 0) & FORM_DELETED
                and FORM_TYPE_NAMES.get(r["type"]) not in ("PROJ", "HAZD")]
    unpaired = alive_created(guest)
    for ref in alive_created(host):
        twin = next((r for r in unpaired if r["base"] == ref["base"] and near(r["position"], ref["position"], 2.0)), None)
        if twin is None:
            differences[reference_label(ref, "host")] = {"present": "host only"}
            continue
        unpaired.remove(twin)
        if fields := reference_differences(ref, twin):
            differences[reference_label(ref, "host")] = fields
    for ref in unpaired:
        differences[reference_label(ref, "client")] = {"present": "client only"}
    return differences


def world_pairs(differences):
    return {(label, field) for label, fields in differences.items() for field in fields}


def describe_world_differences(differences, limit=None):
    """One line per reference: 'label: field host vs guest; ...'."""
    return [f"{label}: " + "; ".join(f"{field} {text}" for field, text in fields.items())
            for label, fields in list(differences.items())[:limit]]


def first_body(ref):
    """A reference's first physics body as world_state reads it, or None."""
    bodies = (ref or {}).get("collision") or []
    return bodies[0] if bodies else None


def same_turn(a, b, tolerance=0.01):
    """Two quaternions (x, y, z, w) turn alike (q and -q are one rotation)."""
    return abs(sum(x * y for x, y in zip(a, b))) >= math.cos(tolerance / 2)


def flatten(tree, prefix=""):
    if isinstance(tree, dict):
        out = {}
        for key, value in tree.items():
            out.update(flatten(value, f"{prefix}{key}."))
        return out
    return {prefix[:-1]: tree}


def compare_field(field, owner, copy, owner_snapshot):
    """Returns None when the copy agrees with the owner, else a short reason."""
    if field.startswith(("mod.", "ai.", "graph")) or field in ("role", "identity.template_base", "identity.temporary_base"):
        return None  # bookkeeping (Local vs Remote) and AI (owner only) differ by design; the live
                     # graph changes every frame (compared over time by `coop_test.py graph`)
    if field == "ai_state.combat_target":
        return None  # each game's own form id for the target (a player is 14 on its own game): copy-in-owners-combat
    if field == "ai_state.package" and (owner or 0) >= 0xFF000000:
        return None  # a package the engine made has no id the other game shares: the copy holds do-nothing
    if field == "effects":
        return None  # copies never hold effects (their owner applies them); what shows is "visuals"
    if field == "state.knock":
        return None  # each game's ragdoll is its own physics: a knocked copy lies and gets up on its own frames
    if field in ("state.deferred_kill", "state.kill_queued"):
        return None  # a living copy is always in deferred kill (it dies by its owner's death action)
    if field == "visuals":
        missing, extra = set(owner or []) - set(copy or []), set(copy or []) - set(owner or [])
        if not missing and not extra:
            return None
        return " ".join(filter(None, [missing and "copy lacks " + ",".join(f"{v:08X}" for v in sorted(missing)),
                                      extra and "copy also shows " + ",".join(f"{v:08X}" for v in sorted(extra))]))
    if field == "identity.base" and (owner_snapshot["player"] or owner_snapshot["identity"]["temporary_base"]):
        return None  # players and leveled spawns are rebuilt on a new base: name and race are compared
    if field == "identity.name" and owner_snapshot["player"]:
        return None  # a player's copy shows the player's name, its own base is unnamed
    if field.startswith("ragdoll."):
        return None  # each game's physics, shown (`look`); how bodies move over time is RagdollTrace's
    if field.startswith("pose."):
        if field == "pose.floor" or owner is None or copy is None:
            return None  # what it stands on, or no reading (no 3D, no floor found)
        gap = abs(owner - copy)
        return None if gap <= POSE_TOLERANCE else f"{gap:.0f} units apart (how its body stands)"
    if field == "placement.position":
        gap = distance(owner, copy)
        return None if gap <= POSITION_TOLERANCE else f"{gap:.0f} units apart"
    if field == "placement.rotation":
        gap = abs((owner[2] - copy[2] + math.pi) % (2 * math.pi) - math.pi)
        return None if gap <= ANGLE_TOLERANCE else f"facing differs by {math.degrees(gap):.0f} deg"
    if field == "equipment":
        missing, extra = set(owner) - set(copy), set(copy) - set(owner)
        if not missing and not extra:
            return None
        return " ".join(filter(None, [missing and "copy lacks " + ",".join(f"{i:08X}" for i in sorted(missing)),
                                      extra and "copy also wears " + ",".join(f"{i:08X}" for i in sorted(extra))]))
    if isinstance(owner, (int, float)) and isinstance(copy, (int, float)) and not isinstance(owner, bool):
        return None if abs(owner - copy) <= VALUE_TOLERANCE else "differs"
    return None if owner == copy else "differs"


def mod_problems(snapshot, role):
    """The mod's bookkeeping must match the actor's role."""
    components = set((snapshot.get("mod") or {}).get("components") or [])
    need = {"Local"} if role == "own" else {"Remote", "Interpolation"}
    problems = [f"mod: missing {name}Component" for name in sorted(need - components)]
    if role == "copy" and "Local" in components:
        problems.append("mod: copy also has LocalComponent (two owners)")
    return problems


def body_problems(snapshot):
    """What looks wrong with a player's body as a person would see it (harness `pose`): floating or sunk feet, a
    skeleton lifted off its feet, or lying while it should stand. Nothing for NPCs, the dead, the knocked down,
    the seated, riders and swimmers."""
    pose, state, placement = snapshot.get("pose"), snapshot.get("state") or {}, snapshot.get("placement") or {}
    water = (snapshot.get("mod") or {}).get("water") or {}
    if not snapshot.get("player") or not pose or state.get("dead") or state.get("knock") or state.get("sit_sleep"):
        return []
    if placement.get("riding") or placement.get("getting_on_off") or water.get("swimming"):
        return []
    problems = []
    if pose["floor_gap"] > FLOAT_TOLERANCE:
        problems.append(f"body: floats {pose['floor_gap']:.0f} units above the floor")
    elif pose["floor_gap"] < -FLOAT_TOLERANCE:
        problems.append(f"body: sunk {-pose['floor_gap']:.0f} units into the floor")
    pelvis = pose["pelvis"] - pose["floor_gap"]  # above its own feet
    if pelvis > RAISED_PELVIS:
        problems.append(f"body: skeleton lifted off its feet (pelvis {pelvis:.0f} above them, ~71 standing)")
    elif pelvis < LOW_PELVIS and not state.get("sneaking"):
        problems.append(f"body: lies or kneels while it should stand (pelvis {pelvis:.0f} above its feet)")
    return problems


def problem_kind(problem):
    """A problem's words without its measurements: the same problem in every sample of a desync check."""
    return re.sub(r"[-+]?\d+(\.\d+)?", "#", problem)


def body_report(snapshot):
    """One line on how an actor's body looks (harness `pose`)."""
    pose, state = snapshot.get("pose"), snapshot.get("state") or {}
    if not pose:
        return "no body reading (no 3D, no floor under it, or not a humanoid skeleton)"
    how = ", ".join(filter(None, ["dead" if state.get("dead") else "", f"knock {state['knock']}" if state.get("knock") else "",
                                  "sneaking" if state.get("sneaking") else "", f"seated {state['sit_sleep']}" if state.get("sit_sleep") else ""]))
    line = (f"feet {pose['floor_gap']:+.0f} from the floor, pelvis {pose['pelvis']:.0f}, head {pose['head']:.0f} above it"
            + (f" ({how})" if how else ""))
    if (ragdoll := snapshot.get("ragdoll")) and ragdoll.get("awake"):
        line += f", {ragdoll['awake']} of {ragdoll['bodies']} ragdoll bodies awake (fastest {ragdoll['fastest']:.0f} units/s)"
    problems = body_problems(snapshot)
    return line + ("; " + "; ".join(p.removeprefix("body: ") for p in problems).upper() if problems else "")


def describe_actor(snapshot, form_id, game):
    name = snapshot["identity"].get("name") or ("player" if snapshot["player"] else "?")
    return f"{name} [{game} {int(form_id):08X}, sid {snapshot['mod']['session_id']}]"


def compare_actors(worlds):
    """Compare already sampled actor worlds, without I/O or waits."""
    problems = []
    copies = {game: {s["mod"]["session_id"]: (fid, s) for fid, s in world.items() if s["role"] == "copy"}
              for game, world in worlds.items()}
    paired = {game: set() for game in worlds}
    for game, world in worlds.items():
        other = "client" if game == "host" else "host"
        for form_id, owned in world.items():
            if owned["role"] != "own":
                continue
            who = describe_actor(owned, form_id, game)
            for problem in mod_problems(owned, "own") + body_problems(owned):
                problems.append((f"{who} {problem_kind(problem)}", f"{who} (owned by {game}): {problem}"))
            sid = owned["mod"]["session_id"]
            if sid < 0:
                problems.append((f"{who} unlinked", f"{who}: owned by {game} but never registered with the server"))
                continue
            if sid not in copies[other]:
                problems.append((f"{who} missing", f"{who}: owned by {game}, but {other} has no copy"))
                continue
            copy_id, copy = copies[other][sid]
            paired[other].add(sid)
            for problem in mod_problems(copy, "copy") + body_problems(copy):
                problems.append((f"{who} copy {problem_kind(problem)}", f"{who}: {other}'s copy {int(copy_id):08X} {problem}"))
            mine, theirs = flatten(owned), flatten(copy)
            for field in sorted(set(mine) | set(theirs)):
                reason = compare_field(field, mine.get(field), theirs.get(field), owned)
                if reason:
                    problems.append((f"{who} {field}",
                                     f"{who} {field}: {game} (owner) {mine.get(field)!r} vs {other} copy "
                                     f"{theirs.get(field)!r} - {reason}"))
    for game, by_sid in copies.items():
        for sid, (form_id, copy) in by_sid.items():
            if sid not in paired[game]:
                who = describe_actor(copy, form_id, game)
                problems.append((f"{who} orphan", f"{who}: copy in {game} with no owner in the other game"))
    return problems


def is_close( a, b, tolerance=1.0):
    return a is not None and b is not None and abs(a - b) <= tolerance


def refused_replays(lines, since):
    """The copies' replayed actions their graph refused (AnimationSystem's line) at or after `since`:
    {(copy form id, event or action id): count}."""
    counts = collections.Counter()
    for line in lines:
        if (match := LOG_LINE.match(line)) and (refused := REFUSED_REPLAY.search(match.group(3))) \
                and log_time(match.group(1)) >= since:
            counts[(int(refused.group(1), 16), refused.group(3) or refused.group(2))] += 1
    return counts


def dialogue_differences(host, guest, end, grace=DIALOGUE_GRACE):
    """Where two games' conversation records (DialogueTrace entries, "t" in microseconds) disagree while both menus
    are on the same speaker, for longer than `grace` seconds: [(seconds after the first entry, field, host's,
    guest's)], the first moment of each lasting difference."""
    times = sorted({e["t"] for e in host + guest} | {end})
    def state(entries, at):
        current = {"menu": False}
        for entry in entries:
            if entry["t"] > at:
                break
            current = entry
        return current
    found, since = [], {}
    for at, until in zip(times, times[1:]):
        h, g = state(host, at), state(guest, at)
        together = h.get("menu") and g.get("menu") and h.get("speaker", -1) >= 0 and h.get("speaker") == g.get("speaker")
        for field in DIALOGUE_FIELDS:
            # What a list holds matters while players see it: a hidden one (the NPC answering) only by being hidden.
            seen = field == "shown" or (h.get("shown") and g.get("shown"))
            if not together or not seen or h.get(field) == g.get(field):
                since.pop(field, None)
                continue
            start = since.setdefault(field, [at, h.get(field), g.get(field), False])
            if until - start[0] > grace * 1e6 and not start[3]:
                start[3] = True
                found.append(((start[0] - times[0]) / 1e6, field, start[1], start[2]))
    return found
