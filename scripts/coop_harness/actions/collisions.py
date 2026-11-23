"""Collision trials: the same push in a game with no session and in the duo (docs/design/physics-sync.md, "The vanilla
baseline"). A trial stages objects ahead of a player on one lane, the player walks into them, and every game's bodies
are traced each frame; collision_metrics turns the traces into numbers, and a vanilla game's runs set their range."""
import json
import math
import time

from .. import collision_metrics
from ..common import ROOT, TRACES
from ..comparisons import first_body, near
from ..utilities import log

BASELINE = ROOT / "scripts/coop_harness/baselines/collisions.json"
# The lane every trial runs on, in QASmoke (the hall's floor from the coc spot, clear for 600 units): the same line in
# a game alone and in the duo, whoever walks it.
LANE = {"from": [-363.0, 2035.0, 6976.0], "along": [1.0, 0.0]}
# Two games' rests of one object agree within this (the send rule's limit is 2 units).
REST_APART = 2.0

CHEESE_WHEEL, CABBAGE, BUCKET, APPLE, BREAD, POTATO, TOMATO, SWEET_ROLL = "64B33", "64B3F", "747FB", "64B2F", "65C97", "64B41", "64B42", "64B3D"
NORD_MEAD, ALTO_WINE, CAST_IRON_POT, KETTLE = "34C5D", "3133B", "318FA", "318FB"


def _grid(bases, columns, first=120, spacing=32):
    """Items in rows across the lane, `columns` wide, the first row `first` units ahead."""
    return [(base, first + (n // columns) * spacing, (n % columns - (columns - 1) / 2) * spacing) for n, base in enumerate(bases)]


# What a trial stages: items as (base to spawn or "ref:<id>" of the cell's own, units along the lane, units across),
# the height they drop from (above the tallest item's centre: a cabbage set 6 units up was in the floor and shot out
# sideways), and how long the player walks. `group`: held to vanilla as a whole (a pile's objects
# trade places run to run), not object by object.
TRIALS = {
    "push_line": {"items": [("ref:32AFE", 120, 0), ("ref:32AFF", 170, 0)], "lift": 20, "walk": 1.5},
    "push_mixed": {"items": [(CHEESE_WHEEL, 120, 0), (CABBAGE, 165, 0), (BUCKET, 215, 0)], "lift": 8, "walk": 2.0},
    # A tavern table's worth on the floor: bottles standing, food, pots.
    "tavern": {"items": _grid([CHEESE_WHEEL, NORD_MEAD, CABBAGE, ALTO_WINE, BREAD, BUCKET, APPLE, KETTLE, NORD_MEAD, POTATO,
                                CAST_IRON_POT, SWEET_ROLL, TOMATO, ALTO_WINE, CHEESE_WHEEL, APPLE], columns=4),
               "lift": 14, "walk": 3.0, "group": True},
    # The player picks up a bucket (Activate held), carries it into a pile of clutter and lets go in it.
    "carry_into_pile": {"items": [(BUCKET, 90, 0)] + _grid([CHEESE_WHEEL, NORD_MEAD, CABBAGE, ALTO_WINE, BREAD, KETTLE, APPLE, CAST_IRON_POT,
                                                            NORD_MEAD, POTATO, SWEET_ROLL, TOMATO], columns=4, first=210, spacing=26),
                        "lift": 14, "walk": 3.0, "group": True, "carry": True},
    # A cheese wheel stacked on another (the fourth number: dropped that much higher), the top one grabbed, held still
    # for `hold` seconds against the one under it and let go: it falls back (docs/bugs.md, "A released wheel freezes").
    "stack_release": {"items": [(CHEESE_WHEEL, 100, 0), (CHEESE_WHEEL, 100, 0, 16)], "lift": 8, "walk": 0, "grab": 1, "hold": 1.5},
    # As the playtest's (2026-11-16): the top wheel of a stack in a pile of wheels, carried forward through the pile and
    # back (`moves`), lifted, held still over the stack and let go.
    "pile_carry_release": {"items": [(CHEESE_WHEEL, 100, 0), (CHEESE_WHEEL, 100, 0, 16)] + _grid([CHEESE_WHEEL] * 4, columns=2, first=135, spacing=34),
                           "lift": 8, "walk": 0, "grab": 1, "moves": [(["forward"], 0.5), (["back"], 0.4)], "hold": 1.5, "group": True},
    # Picked up and dropped on a pile: the first item grabbed, lifted (`raise` degrees, before the walk: `lift_first`),
    # carried over the pile, held still there and let go, so it falls onto a pile asleep (docs/re/systems/physics.md,
    # "A body landing on a sleeping pile"). A bucket onto a tavern pile, a cheese wheel onto wheels, a cabbage onto
    # standing bottles.
    "drop_on_pile": {"items": [(BUCKET, 90, 0)] + _grid([CHEESE_WHEEL, NORD_MEAD, CABBAGE, ALTO_WINE, BREAD, KETTLE, APPLE, CAST_IRON_POT,
                                                         POTATO], columns=3, first=140, spacing=24),
                     "lift": 14, "walk": 0, "grab": 0, "raise": 28, "lift_first": True, "moves": [(["forward"], 0.3)], "hold": 0.8, "group": True},
    "drop_wheel_on_wheels": {"items": [(CHEESE_WHEEL, 90, 0)] + _grid([CHEESE_WHEEL] * 6, columns=3, first=140, spacing=30),
                             "lift": 8, "walk": 0, "grab": 0, "raise": 28, "lift_first": True, "moves": [(["forward"], 0.3)], "hold": 0.8,
                             "group": True},
    "drop_on_bottles": {"items": [(CABBAGE, 90, 0)] + _grid([NORD_MEAD, ALTO_WINE, NORD_MEAD, ALTO_WINE, NORD_MEAD, ALTO_WINE], columns=3,
                                                            first=140, spacing=20),
                        "lift": 14, "walk": 0, "grab": 0, "raise": 28, "lift_first": True, "moves": [(["forward"], 0.3)], "hold": 0.8, "group": True},
}


class CollisionActions:

    def collision_objects(ctx, kind, alone=False):
        """The trial's objects, spawned by the host once a session (staged: unstage removes them) or the cell's own:
        [{"": host id, "client": guest id}] in the trial's order."""
        made = ctx.__dict__.setdefault("COLLISION_OBJECTS", {})
        # Made in these games: ids start over in games booted again, where they name other objects.
        games_now = []
        for game in ([""] if alone else ["", "client"]):
            status = ctx.game(game).read_status() or {}
            games_now.append(((status.get("session") or {}).get("request"), status.get("pid"), (status.get("frames") or {}).get("frames", 0)))
        made_in = ctx.__dict__.get("COLLISION_GAMES")
        if made_in is not None and ([g[:2] for g in made_in] != [g[:2] for g in games_now] or any(now[2] < then[2] for now, then in zip(games_now, made_in))):
            made.clear()
            ctx.staged().clear()  # staged in games that are gone
        ctx.COLLISION_GAMES = games_now
        # Still there in every game (a guest that joined again has not got the earlier session's).
        games = [""] if alone else ["", "client"]
        if kind in made and all(game in o and (o[game] in (state := ctx.world_state(game, [o[game]]))) and first_body(state[o[game]])
                                for o in made[kind] for game in games):
            return made[kind]
        # One trial's objects on the lane at a time: another's left lying there would be staged into.
        if made:
            from ..errors import TestFailure
            try:
                ctx.unstage()
            except TestFailure:  # staged in games that are gone (an earlier duo): nothing left to remove
                ctx.staged().clear()
            made.clear()
        objects = []
        for base, *_ in TRIALS[kind]["items"]:
            if base.startswith("ref:"):
                ref = int(base[4:], 16)
                ctx.staged().append(("put_back", ctx.where_now([ref])))  # the cell's own: off the lane again after
                objects.append({"": ref, "client": ref})
            elif alone:
                before = set(ctx.world_state(""))
                ctx.console_on("", f"player.placeatme {base} 1")
                host = next(iter(ctx.wait_for(f"[host] the new {base}", lambda: set(ctx.world_state("")) - before, 5)))
                ctx.staged().append(("object", host))
                objects.append({"": host})
            else:
                placed = ctx.place_object(base)
                objects.append({"": placed["host"], "client": placed["guest"]})
        made[kind] = objects
        return objects

    def stage_objects(ctx, kind, objects, games):
        """The host sets the trial's objects upright over their spots on the lane and lets them drop; returns once each
        rests there in every game."""
        trial, me, away = TRIALS[kind], LANE["from"], LANE["along"]
        across = [-away[1], away[0]]
        spots = []
        for ids, (_, along, side, *above) in zip(objects, trial["items"]):
            ref = ids[""]
            spot = [me[0] + away[0] * along + across[0] * side, me[1] + away[1] * along + across[1] * side,
                    me[2] + trial["lift"] + sum(above)]
            spots.append(spot)
            for axis in "xyz":
                ctx.console_on("", f"{ref:X}.setangle {axis} 0")
            for axis, value in zip("xyz", spot):
                ctx.console_on("", f"{ref:X}.setpos {axis} {value:.0f}")
        # The console leaves each asleep in the air, so all are in place before any drops: woken, they fall onto the
        # floor and settle.
        for ids in objects:
            ctx.player_command("", {"cmd": "wake", "ref": ids[""]})
        for ids, spot in zip(objects, spots):
            ref = ids[""]
            def rests_there(ref=ref, spot=spot):
                # Asleep where it fell to, not in the air exactly where setpos left it.
                body = ctx.body_of("", ref)
                return not body["awake"] and math.dist(body["position"][:2], spot[:2]) < 120 and math.dist(body["position"], spot) > 0.05
            ctx.wait_for(f"[host] {ref:X} rests on the lane", rests_there, 20, explain=lambda ref=ref: f"its body: {ctx.body_of('', ref)}")
            for game in games[1:]:
                rest = ctx.body_of("", ref)["position"]
                ctx.wait_for(f"[client] shows {ids[game]:X} there",
                             lambda ids=ids, rest=rest, game=game: (body := ctx.body_of(game, ids[game])) and near(body["position"], rest, 2), 8,
                             explain=lambda ids=ids, rest=rest, game=game: f"the host's {ids[''] :X} rests at {[round(x, 1) for x in rest]}; "
                                                                           f"the guest's: {ctx.body_of(game, ids[game])}")

    def collision_trial(ctx, walker="", kind="push_line", alone=False):
        """One trial: `walker`'s player walks along the lane into the trial's objects (TRIALS[kind]). Returns each
        game's metrics ({"host": {"0": {...}, ...}, "client": ...}; a group trial's as one, "all"), and the trace's
        folder. `alone`: one game, no guest (the vanilla baseline)."""
        trial = TRIALS[kind]
        games = [""] if alone else ["", "client"]
        mine = "client" if walker in ("client", "guest") else ""
        # Both players off the objects' spots while they drop: the walker at the lane's start, the other behind it.
        # Before new objects are made (at the host's feet), so the guest never stands among them.
        start, away = LANE["from"], LANE["along"]
        ctx.player_command(walker, {"cmd": "place", "position": start})
        if not alone:
            ctx.player_command("" if mine else "client", {"cmd": "place", "position": [start[0] - away[0] * 150, start[1] - away[1] * 150, start[2]]})
        objects = ctx.collision_objects(kind, alone)
        ctx.stage_objects(kind, objects, games)
        # Behind the first row, looking along the lane.
        away, first = LANE["along"], objects[0]
        ctx.place_before(walker, first[mine], trial["items"][0][1], bearing=math.degrees(math.atan2(-away[0], -away[1])) % 360)
        me, at = ctx.my_position(walker), ctx.body_of(walker, first[mine])["position"]
        along = ((at[0] - me[0]) * away[0] + (at[1] - me[1]) * away[1]) / (math.dist(me[:2], at[:2]) or 1.0)
        if along < 0.9 and not trial.get("carry") and "grab" not in trial:
            raise ctx.failure(f"[{walker or 'host'}] no clear spot behind the lane's objects: the player stands "
                              f"{math.degrees(math.acos(max(-1.0, min(1.0, along)))):.0f} degrees off it")
        for game in games:
            # Its own player too (0x14): where the walker was when an object first moved.
            if not (ctx.game(game).rpc({"cmd": "body_trace", "refs": sorted(o[game] for o in objects) + [0x14]}) or {}).get("ok"):
                raise ctx.failure(f"[{game or 'host'}] the body trace did not start")
        samples = {game: [] for game in games}
        try:
            if trial.get("carry"):
                # Looking at it from the placement: held, it hangs before the player and meets the pile first.
                ctx.grab(walker, first[mine])
            if "grab" in trial:
                held_id = objects[trial["grab"]][mine]
                ctx.grab(walker, held_id)
                # Lifted by looking up (the hand holds it on the crosshair's line), before or after the walk, then held still.
                def lifts():
                    pitch = ctx.camera_view(walker)[2]
                    ctx.turn_to(walker, pitch=pitch - trial.get("raise", 12), rate=30, keep=[{"holding": held_id}], name="lifts it")
                if trial.get("lift_first"):
                    lifts()
                for buttons, seconds in trial.get("moves", []):
                    ctx.carry(walker, held_id, buttons, seconds)
                if not trial.get("lift_first"):
                    lifts()
                ctx.carry(walker, held_id, [], trial["hold"])
                let_go = ctx.body_of(walker, held_id)["position"]
                ctx.tap(walker, "activate")
                ctx.wait_for(f"[{walker or 'host'}] lets go of it", lambda: not ctx.local_player(walker).get("grabbed"), 4)
            if trial["walk"]:
                ctx.act(walker, {"name": f"walks into the {kind} objects", "ready": ["takes_input"] if trial.get("carry") else ["takes_input", "on_ground"],
                                 "buttons": ["forward"], "until": [{"for": trial["walk"]}], "seconds": trial["walk"] + 2})
            if trial.get("carry"):
                held = ctx.local_player(walker).get("grabbed") == first[mine]
                if held:
                    ctx.tap(walker, "activate")
                    ctx.wait_for(f"[{walker or 'host'}] lets go of it", lambda: not ctx.local_player(walker).get("grabbed"), 4)
            if probe := ctx.__dict__.get("COLLISION_PROBE"):
                probe(walker, [o[mine] for o in objects])
            still = {}
            def all_rest():
                for game in games:
                    samples[game].extend((ctx.game(game).rpc({"cmd": "body_trace"}, timeout=10) or {}).get("entries", []))
                # In one place for a second in every game (a rest placed from another game has landed too). Not
                # "asleep": what leans on the player's body stays awake without moving.
                bodies = [ctx.body_of(game, o[game]) for game in games for o in objects]
                # A body not readable for a moment (its 3D being moved between nodes) is not at rest.
                now = None if any(b is None for b in bodies) else [[round(x * 2) for x in b["position"]] for b in bodies]
                if now is None or still.get("at") != now:
                    still.update(at=now, since=time.monotonic())
                    return False
                return time.monotonic() - still["since"] >= 1.0
            ctx.wait_for("every object rests in every game", all_rest, 45)
        finally:
            for game in games:
                samples[game].extend((ctx.game(game).rpc({"cmd": "body_trace", "stop": True}, timeout=10) or {}).get("entries", []))
        folder = TRACES / f"{time.strftime('%Y%m%d-%H%M%S')}-collision-{kind}"
        folder.mkdir(parents=True, exist_ok=True)
        (folder / "bodies.json").write_text(json.dumps({"objects": objects, "items": trial["items"], "samples": samples}))
        result = {"kind": kind, "walker": walker or "host", "trace": str(folder), "rests": {},
                  "objects": [f"{base} {o['']:X}" for o, (base, *_) in zip(objects, trial["items"])]}
        if "grab" in trial:
            # How far the let go one fell: frozen in the air, about nothing.
            rest = ctx.body_of(walker, objects[trial["grab"]][mine])["position"]
            result["fell"] = round(let_go[2] - rest[2], 1)
        for game in games:
            each = {str(n): collision_metrics.measure(collision_metrics.path_of(samples[game], o[game])) for n, o in enumerate(objects)}
            result[game or "host"] = {"all": collision_metrics.measure_group(list(each.values()))} if trial.get("group") else dict(each)
            if trial.get("carry"):
                result[game or "host"]["carried"] = {k: v for k, v in each["0"].items() if k != "rest"}
                result["held_to_the_end"] = held
            if "grab" in trial and (released := each[str(trial["grab"])]):
                # Each game's let go object, and how far below where it was let go it rests in that game.
                result[game or "host"]["released"] = {**{k: v for k, v in released.items() if k != "rest"},
                                                      "fell": let_go[2] - released["rest"][2]}
                result.setdefault("slept_moving", {})[game or "host"] = collision_metrics.slept_moving(samples[game], objects[trial["grab"]][game])
            result["rests"][game or "host"] = [m["rest"] if m else None for m in each.values()]
            if not any(m and m["moved"] >= 1 for m in each.values()):
                raise ctx.failure(f"[{game or 'host'}] the walker moved none of the {kind} objects")
        return result

    def body_watch(ctx, instance="", refs=(), seconds=3.0, limit=60):
        """Watches these references' bodies in that game for `seconds` (body_trace): for each, the frames where it
        moved, woke, slept or changed its motion type, as short lines (the newest `limit`): what moves a body that
        should lie still, and how often."""
        game = ctx.game(instance)
        ids = [ctx.resolve(ref, instance) for ref in refs]
        game.rpc({"cmd": "body_trace", "stop": True}, timeout=10)
        if not (game.rpc({"cmd": "body_trace", "refs": ids}) or {}).get("ok"):
            raise ctx.failure(f"[{instance or 'host'}] the body trace did not start")
        entries, end = [], time.monotonic() + seconds
        try:
            while time.monotonic() < end:
                entries.extend((game.rpc({"cmd": "body_trace"}, timeout=10) or {}).get("entries", []))
        finally:
            entries.extend((game.rpc({"cmd": "body_trace", "stop": True}, timeout=10) or {}).get("entries", []))
        watched = {}
        for ref in ids:
            mine = [e for e in entries if e.get("id") == ref and not e.get("rendered") and e.get("bodies")]
            lines = []
            for before, now in zip(mine, mine[1:]):
                a, b = before["bodies"][0], now["bodies"][0]
                step = math.dist(a["position"], b["position"])
                if step > collision_metrics.MOVE or a["awake"] != b["awake"] or a["motion"] != b["motion"]:
                    lines.append(f"{(now['t'] - mine[0]['t']) / 1e6:.3f}s step {step:.2f} to ({b['position'][0]:.1f}, {b['position'][1]:.1f}, "
                                 f"{b['position'][2]:.1f}) speed {math.hypot(*b['velocity']):.0f} motion {b['motion']} {'awake' if b['awake'] else 'asleep'}")
            watched[f"{ref:X}"] = {"frames": len(mine), "changes": len(lines), "lines": lines[-limit:]}
        return watched

    def lies_on_npc(ctx, npc, ref, seconds=20.0, above=135.0, ahead=0.0, wakes=False):
        """The host's console sets reference `ref` (the same id in both games) `above` `npc`'s feet (and `ahead` along
        x) and lets it fall onto it; then both games' bodies of it are traced for `seconds` while the NPC goes about. The guest's must move as
        the host's does: no frame's jump over 2 units, no more turn-backs than the host's and 3, no further travelled
        than twice the host's and 10 units. Returns each game's motion (collision_metrics.measure) and how far apart
        the two ended. `wakes`: first who_wakes on it in each game, half of `seconds` each (what it goes through there)."""
        ref = ref if isinstance(ref, int) else int(ref, 16)
        actor = ctx.resolve(npc, "")
        at = ((ctx.actor_snapshot("", actor) or {}).get("placement") or {}).get("position")
        if not at:
            raise ctx.failure(f"[host] {npc} is not in the world")
        ctx.staged().append(("put_back", ctx.where_now([ref])))
        # `ahead`: units along the cell's x from its feet (an NPC leaning over a counter has its back there).
        for axis, value in zip("xyz", (at[0] + ahead, at[1], at[2] + above)):
            ctx.console_on("", f"{ref:X}.setpos {axis} {value:.0f}")
        ctx.player_command("", {"cmd": "wake", "ref": ref})
        time.sleep(2.0)
        woken = {name: ctx.who_wakes(game, [ref], seconds / 2) for name, game in (("host", ""), ("guest", "client"))} if wakes else None
        games = {"host": ctx.game(""), "guest": ctx.game("client")}
        entries = {name: [] for name in games}
        for name, game in games.items():
            game.rpc({"cmd": "body_trace", "stop": True}, timeout=10)
            if not (game.rpc({"cmd": "body_trace", "refs": [ref]}) or {}).get("ok"):
                raise ctx.failure(f"[{name}] the body trace did not start")
        end = time.monotonic() + seconds
        try:
            while time.monotonic() < end:
                for name, game in games.items():
                    entries[name].extend((game.rpc({"cmd": "body_trace"}, timeout=10) or {}).get("entries", []))
        finally:
            for name, game in games.items():
                entries[name].extend((game.rpc({"cmd": "body_trace", "stop": True}, timeout=10) or {}).get("entries", []))
        seen = {name: collision_metrics.measure(collision_metrics.path_of(entries[name], ref)) for name in games}
        if not seen["host"] or not seen["guest"]:
            raise ctx.failure(f"no trace of {ref:X}: host {len(entries['host'])} entries, guest {len(entries['guest'])}")
        result = {name: {k: (round(v, 2) if isinstance(v, float) else v) for k, v in m.items() if k != "rest"} for name, m in seen.items()}
        result["apart"] = round(math.dist(seen["host"]["rest"], seen["guest"]["rest"]), 2)
        if woken:
            result["woken"] = woken
        host, guest = seen["host"], seen["guest"]
        wrong = []
        if guest["jump"] > 2.0:
            wrong.append(f"jumped {guest['jump']:.1f} units in a frame (the host's {host['jump']:.1f})")
        if guest["turns"] > host["turns"] + 3:
            wrong.append(f"turned back {guest['turns']} times (the host's {host['turns']})")
        if guest["path"] > 2 * host["path"] + 10:
            wrong.append(f"travelled {guest['path']:.0f} units (the host's {host['path']:.0f})")
        if wrong:
            raise ctx.failure(f"[guest] {ref:X} on {npc} does not lie as the host's: " + "; ".join(wrong) + f" ({result})")
        return result

    def collision_restless(ctx, walker="client", kind="drop_wheel_on_wheels", rounds=6, seconds=4.0, after=4.5):
        """Trials until one leaves `walker`'s game creeping: `after` seconds past the let go its objects still move a
        little (0.1 to 3 units in a second, no rest). Then, still inside that trial, who_wakes on them there for
        `seconds`, with where the walker's player stands: what keeps a pile from its rest. None found: the rounds run."""
        game = "client" if walker in ("client", "guest") else ""
        found = {}
        def probe(_, refs):
            time.sleep(after)
            before = [ctx.body_of(game, ref) for ref in refs]
            time.sleep(1.0)
            now = [ctx.body_of(game, ref) for ref in refs]
            crept = {f"{ref:X}": round(math.dist(a["position"], b["position"]), 2) for ref, a, b in zip(refs, before, now)
                     if a and b and 0.1 < math.dist(a["position"], b["position"]) < 3.0}
            if len(crept) >= 2:
                found.update(crept=crept, player=[round(x, 1) for x in ctx.my_position(walker)],
                             bodies={f"{ref:X}": [round(x, 1) for x in b["position"]] for ref, b in zip(refs, now) if b},
                             wakers=ctx.who_wakes(game, refs, seconds))
        ctx.COLLISION_PROBE = probe
        try:
            for n in range(rounds):
                run = ctx.collision_trial(walker, kind)
                if found:
                    return {"round": n + 1, "trace": run["trace"], **found}
        finally:
            ctx.COLLISION_PROBE = None
        return {"rounds": rounds, "crept": {}}

    def collision_apart(ctx, apart=0.5):
        """Every trial object made this session whose body the two games have at different places (more than `apart`
        units), with both places and whether each is awake: what a pile that will not settle disagrees about."""
        off = []
        for kind, objects in ctx.__dict__.get("COLLISION_OBJECTS", {}).items():
            for (base, *_), ids in zip(TRIALS[kind]["items"], objects):
                host, guest = ctx.body_of("", ids[""]), ctx.body_of("client", ids["client"])
                if host and guest and (gap := math.dist(host["position"], guest["position"])) > apart:
                    where = lambda b: f"({b['position'][0]:.1f}, {b['position'][1]:.1f}, {b['position'][2]:.1f}) {'awake' if b['awake'] else 'asleep'}"
                    off.append(f"{kind} {base} {ids['']:X}/{ids['client']:X}: {gap:.1f} apart, host {where(host)}, guest {where(guest)}")
        return off

    def collision_baseline(ctx, kind="push_line", rounds=20, sample="session_idle"):
        """In a game with no session: `rounds` trials kept as one sample of the baseline, whose ranges are every
        sample's rounds together. The samples: "vanilla" (vanilla_start plugin=true with HavokStepFix, PlayerHands,
        PlayerCollision and CopyRagdollPhysics left out) and "session_idle" (vanilla_start plugin=true: our frame-step
        fix on). Returns the ranges and how many of these rounds a single one would have failed before."""
        runs = []
        for n in range(rounds):
            run = ctx.collision_trial("", kind, alone=True)
            runs.append({i: {k: v for k, v in m.items() if k != "rest"} if m else None for i, m in run["host"].items()})
            log(f"baseline {kind} {n + 1}/{rounds}: " + "; ".join(
                f"#{i} moved {m['moved']:.0f} in {m['seconds']:.1f} s, peak {m['peak']:.0f}, jump {m['jump']:.1f}"
                for i, m in run["host"].items() if m))
        baseline = json.loads(BASELINE.read_text()) if BASELINE.exists() else {}
        before = baseline.get(kind, {})
        # What a single round of these would have failed against the ranges before: the test's own noise.
        noise = sum(1 for run in runs if any(collision_metrics.outside(run[i], r) for i, r in before.get("ranges", {}).items() if run.get(i)))
        samples = {**before.get("runs", {}), sample: runs}
        every = [run for each in samples.values() for run in each]
        ranges = {i: collision_metrics.envelope([run[i] for run in every]) for i in runs[0]}
        baseline[kind] = {"items": [base for base, *_ in TRIALS[kind]["items"]], "rounds": len(every),
                          "measured": time.strftime("%Y-%m-%d"), "ranges": ranges, "runs": samples}
        BASELINE.parent.mkdir(parents=True, exist_ok=True)
        BASELINE.write_text(json.dumps(baseline, indent=1, sort_keys=True) + "\n")
        return {"ranges": ranges, "outside_before": f"{noise}/{rounds}"}

    def expect_collision_like_vanilla(ctx, walker="", kind="push_line", rounds=3):
        """`rounds` trials in the duo, held to the vanilla baseline (collision_metrics.compare): in each game no object
        ever jumps or shakes beyond vanilla's, the middle round moves within vanilla's ranges (how far, how long, how
        fast), and both games rest each object at one place every round. Fails naming every game, object and metric
        outside."""
        if not BASELINE.exists() or kind not in (baseline := json.loads(BASELINE.read_text())):
            raise ctx.failure(f"no vanilla baseline of {kind}: collision_baseline in a game with no session first")
        ranges = baseline[kind]["ranges"]
        limit = collision_metrics.limits(ranges, baseline)
        runs, wrong = [], []
        for n in range(rounds):
            run = ctx.collision_trial(walker, kind)
            runs.append(run)
            for i, (host, guest) in enumerate(zip(run["rests"]["host"], run["rests"]["client"])):
                if host and guest and (apart := math.dist(host, guest)) > REST_APART:
                    wrong.append(f"round {n + 1}: object {i} ({run['objects'][i]}) rests {apart:.1f} units apart on the two games")
            for game, count in run.get("slept_moving", {}).items():
                if count:
                    wrong.append(f"round {n + 1}: [{game}] the let go object was put to sleep {count} times while it moved")
        for game in ("host", "client"):
            for i, r in ranges.items():
                if off := collision_metrics.compare([run[game].get(i) for run in runs], r, limit):
                    what = collision_metrics.object_name(i, runs[0]["objects"])
                    wrong.append(f"[{game}] {what}: " + ", ".join(off))
        traces = ", ".join(run["trace"].rsplit("/", 1)[-1] for run in runs)
        log(f"collision {kind}, {walker or 'host'} walks, {rounds} rounds: " + ("as vanilla" if not wrong else "; ".join(wrong)) + f" ({traces})")
        if wrong:
            raise ctx.failure(f"{walker or 'host'}'s {kind} is not as vanilla's: " + "; ".join(wrong) + f" ({traces})")
        return {"rounds": [{g: run[g] for g in ("host", "client")} for run in runs], "traces": traces}

    def collision_check_alone(ctx, kind="push_line", rounds=4):
        """In one game with no session: `rounds` trials held to the vanilla baseline, one line each (what is outside,
        or "as vanilla"). With the plugin's hooks on, it shows what the plugin alone changes."""
        baseline = json.loads(BASELINE.read_text())[kind]
        lines = []
        for _ in range(rounds):
            run = ctx.collision_trial("", kind, alone=True)
            off = [f"{'the objects' if i == 'all' else i}: " + ", ".join(found)
                   for i, ranges in baseline["ranges"].items() if (found := collision_metrics.outside(run["host"][i], ranges))]
            lines.append("; ".join(off) or "as vanilla")
            log(f"alone {kind}: {lines[-1]}")
        return lines

    def collision_baselines(ctx, rounds=20, kinds=None, sample="session_idle"):
        """collision_baseline for every trial (or `kinds`), one after the other: how many rounds were outside before."""
        return {kind: ctx.collision_baseline(kind, rounds, sample)["outside_before"] for kind in (kinds or list(TRIALS))}

    def collision_suite(ctx, kinds=None, walkers=("guest", "host")):
        """Every trial with each walker against the vanilla baseline: one line each, "as vanilla" or what is outside."""
        from ..errors import TestFailure
        lines = {}
        for kind in kinds or list(TRIALS):
            for walker in walkers:
                try:
                    ctx.expect_collision_like_vanilla("client" if walker == "guest" else "", kind)
                    lines[f"{walker} {kind}"] = "as vanilla"
                except TestFailure as failure:
                    lines[f"{walker} {kind}"] = str(failure).rsplit(" (", 1)[0]
                    ctx.FAILING = False
        return lines
