"""Actions for the co-op harness."""
import argparse
import collections
import contextlib
import json
import math
import os
import re
import socket
import subprocess
import time
import uuid
from pathlib import Path
from ..comparisons import is_close
from ..common import (
    FIREBOLT,
    HOME_CELL,
    INN,
    IRON_SWORD,
    KILL_MOVE_ALIGN_S,
    KILL_MOVE_APART,
    KILL_MOVE_BLOW_WINDOW,
    KILL_MOVE_DROP,
    KILL_MOVE_RANDOM,
    KILL_MOVE_SETTLE_S,
    LYDIA,
    MELEE_GAP,
    POSITION_TOLERANCE,
    SPELL_GAP,
    TEST_HOME,
    UNRELENTING_FORCE,
    UNRELENTING_FORCE_THREE,
    UNRELENTING_FORCE_WORDS,
    TURN_UN_DISINTEGRATE,
    WOLF,
)
from ..errors import TestFailure
from ..comparisons import distance
from ..utilities import log
from ..objects import AnimTrace, DYNAMIC_MOTIONS, GodMode, RagdollTrace

# The kill move table (scripts/re/kill_moves.py --rows) and the weapons its rows name; dual wield is left out:
# kill_move_on equips one hand.
KILL_MOVE_TABLE = json.loads((Path(__file__).resolve().parents[1] / "kill_moves.json").read_text())
KILL_MOVE_LOG = TEST_HOME / "kill-moves.jsonl"
KILL_MOVE_WEAPONS = {"hands": 0, "dagger": 0x1397E, "sword": IRON_SWORD, "war axe": 0x13790, "mace": 0x13982,
                     "greatsword": 0x1359D, "battleaxe": 0x13980, "warhammer": 0x13981}


def ragdoll_fall(bodies, game, actor, blow):
    """kill_move_ragdoll's numbers for one game: the traced ragdoll from its clip's KillActor (`blow`)."""
    frames = bodies.frames(game, actor)
    if not blow or not frames:
        return {"blow": bool(blow), "frames": len(frames)}
    t0 = blow["time"]
    after = [(f["t"] / 1e6 - t0, f) for f in frames if f["t"] / 1e6 >= t0]
    first = lambda test: next((round(t, 2) for t, f in after if test(f)), None)
    pelvis = [(t, f["pelvis"]) for t, f in after if f.get("pelvis")]
    out = {"dead_s": first(lambda f: f.get("dead")),
           "dynamic_s": first(lambda f: any((b or {}).get("motion") in DYNAMIC_MOTIONS for b in f["bodies"]))}
    if pelvis:
        out["still_s"] = next((round(t, 2) for t, p in pelvis if math.dist(p, pelvis[0][1]) > 1.0), None)
        early = [math.dist(a[1], b[1]) for a, b in zip(pelvis, pelvis[1:]) if b[0] < 1.0]
        out["jump"] = round(max(early, default=0.0), 1)
        out["travel"] = round(math.dist(pelvis[0][1], pelvis[-1][1]), 1)
    out["speed"] = round(max((math.hypot(*b["velocity"]) for _, f in after for b in f["bodies"] if b and b.get("velocity")), default=0.0))
    measure = bodies.measure(game, actor)
    if measure:
        out["flickers"] = sum(len(v["flickers"]) for v in measure["series"].values())
        out["turns"] = max(v["worst"] for v in measure["series"].values())
        out["stray"] = round(measure["stray"], 1)
    asleep = [not any((b or {}).get("awake") for b in f["bodies"]) for _, f in after]
    out["rest_s"] = next((round(after[i][0], 1) for i in range(len(after)) if after[i][1].get("dead") and all(asleep[i:])), None)
    return out


def kill_move_apart(trace, attacker, other):
    """How differently the other game shows the pair than the attacker's: through each game's own clip (from its
    attacker's kill move start, past the slide to the mark, to its victim's KillActor), the largest difference in the attacker-to-victim gap,
    in the victim's height above the attacker and in the victim's turn from the attacker's (units, degrees)."""
    def start(game):
        return next((e["time"] for e in trace.entries if e["game"] == game and e["who"] == "attacker" and e["kind"] == "state"
                     and e["state"].get("kill_move")), None)
    def samples(game):
        t0 = start(game)
        if t0 is None:
            return []
        blow = trace.find(game, "victim", "KillActor")
        end = min(blow["time"] if blow else t0 + KILL_MOVE_ALIGN_S, t0 + KILL_MOVE_ALIGN_S)
        out = []
        poses = {who: [e for e in trace.entries if e["game"] == game and e["who"] == who and e["kind"] == "pos" and e["time"] >= t0]
                 for who in ("attacker", "victim")}
        for mine in poses["attacker"]:
            if mine["time"] > end:
                break
            theirs = min(poses["victim"], key=lambda e: abs(e["time"] - mine["time"]), default=None)
            if theirs is None or abs(theirs["time"] - mine["time"]) > 0.05:
                continue
            gap = math.hypot(theirs["pos"][0] - mine["pos"][0], theirs["pos"][1] - mine["pos"][1])
            turn = (math.degrees(theirs["yaw"] - mine["yaw"]) + 180) % 360 - 180
            out.append((mine["time"] - t0, gap, theirs["pos"][2] - mine["pos"][2], turn))
        return out
    own, seen = samples(attacker), samples(other)
    worst = {"gap": 0.0, "height": 0.0, "turn": 0.0, "at": {}}  # at: when each was worst
    for t, gap, height, turn in seen:
        if t < KILL_MOVE_SETTLE_S:
            continue
        near = min(own, key=lambda s: abs(s[0] - t), default=None)
        if near is None or abs(near[0] - t) > 0.05:
            continue
        for key, value in (("gap", abs(gap - near[1])), ("height", abs(height - near[2])), ("turn", abs((turn - near[3] + 180) % 360 - 180))):
            if value > worst[key]:
                worst[key] = round(value, 1)
                worst["at"][key] = round(t, 2)
    return worst


class CombatActions:
    def combat_targets_of(ctx, npc, instance="client"):
        """An actor's combat in one game (harness combat_targets): its controller's target, the behaviour tree's
        running nodes, its group's candidates. On a guest, for an NPC's copy."""
        game = "guest" if instance in ("client", "guest") else "host"
        ctx.refresh_handles()
        return ctx.player_command(instance, {"cmd": "combat_targets", "actor": ctx.resolve(npc, game)})

    def equip_weapon(ctx, instance, item):
        """That game's player gets `item` (a form id) and holds it in its hand; 0 is bare hands."""
        if item:
            ctx.equip_in_hand(instance, [f"player.additem {item:08X} 1", f"player.equipitem {item:08X}"], item, "holds the weapon")
            return
        hands = lambda: (ctx.player_snapshot(instance).get("state") or {}).get("hands", [])
        for held in hands():
            if held:
                ctx.console_on(instance, f"player.unequipitem {held:08X}")
        # What is still held is a spell: the console refuses unequipspell for a weapon.
        for slot, held in zip(("left", "right"), hands()):
            if held:
                ctx.console_on(instance, f"player.unequipspell {held:08X} {slot}")
        ctx.wait_for(f"[{instance or 'host'}] both hands are empty", lambda: not any(hands()), 5)


    def kill_move_exchange(ctx, copies):
        """Both players, one hit from death and moving, swing at each other at once. Returns whether the two games' pairs
        crossed; crossed, the first swing must win: one player down, the other standing. `copies`: each game's copy of the other player."""
        games = ("", "client")
        ctx.wait_for("both players are up", lambda: not any(ctx.death_state(game).get("down") for game in games), 40)
        for game in games:
            ctx.console_on(game, "player.restoreav health 10000")
            ctx.draw(game)
        ctx.place_before("", copies[""], MELEE_GAP + 40)
        for game in games:
            ctx.console_on(game, f"player.damageav health {(ctx.player_health(game) or 100) - 3:.3f}")
        for game in games:
            other = copies[game]
            ctx.wait_on(game, "the other player shows one hit from death", lambda a, c=other: (a.get(str(c), {}).get("health") or 100) <= 10, 5)
        trace = AnimTrace(ctx, "kill-moves-cross", {game: {0x14: "player", copies[game]: "other"} for game in games}, video=False)
        with trace:
            def own_pairs():
                trace.take()
                return all(any(e["game"] == game and e["who"] == "player" and e["kind"] == "action" and e["event"].startswith("pa_")
                               for e in trace.entries) for game in games)
            held = []
            def hold(game, button):
                ctx.player_command(game, {"cmd": "input", "button": button, "down": True})
                held.append((game, button))
            try:
                hold("", "forward")
                hold("client", "strafe_left")
                for game in games:
                    ctx.face(game, copies[game])
                    hold(game, "right")
                ctx.wait_for("each game started its own pair", own_pairs, 1)
                crossed = True
            except TestFailure:
                crossed = False
            finally:
                for game, button in held:
                    with contextlib.suppress(TestFailure):
                        ctx.player_command(game, {"cmd": "input", "button": button, "down": False})
            if crossed:
                # The first swing wins: one player dies, the other stands through the whole window.
                down = {game: False for game in games}
                watched = time.monotonic()
                while time.monotonic() - watched < KILL_MOVE_BLOW_WINDOW:
                    down = {game: bool(ctx.death_state(game).get("down")) for game in games}
                    if all(down.values()):
                        raise ctx.failure(f"both players died {time.monotonic() - watched:.1f} s after the kill moves crossed")
                if not any(down.values()):
                    raise ctx.failure("nobody died: the first swing should have won")
                for game in games:
                    if down[game]:
                        trace.wait_state(game, "player", "kill_move", True, 1, after=trace.start)
        return crossed


    def kill_move_on(ctx, attacker, base, weapon, behind=False, watch=6.0, strict=False, rest=True, at=None, bearing=None, perks=(), fighting=False):
        """`attacker`'s player ("" the host, "client") strikes a restrained host NPC of `base`, one hit from death, with
        `weapon` (a form id; 0 bare hands) from its front or from `behind`. Returns what played (`action`; None: a plain
        swing), each game's time from the action to the death, and `problems`: what a player would see wrong `watch`
        seconds after it (someone still in a kill move or its pair, the victim alive or its kill held, its ragdoll
        missing, keyframed or awake, the two games' bodies apart, an attacker that cannot walk). `strict`: fails on any
        problem, or when no kill move played. `rest` False leaves the lying bodies unread (bugs.md, "A kill move's dead
        copy awake or lying apart"). `at`: the victim's spot (default the stage); `bearing`: the attacker's side of the
        victim (0 north, 90 east) instead of its front or back. `perks`: form ids the attacker holds for the swing (the
        decapitations need SavageStrike or DevastatingBlow). `fighting`: the victim is not restrained but fights the
        attacker (its AI on, the attacker invulnerable), as in a real fight. `height`: the attacker's feet above the victim's, what
        the kill move idles' GetTargetHeight window reads (kill-moves.md, "Slopes")."""
        games = ("", "client")
        other = "client" if attacker == "" else ""
        said = lambda game: game or "host"
        npc, _, copy = ctx.spawn_host_npc(base, restrained=not fighting, goal=at)
        if fighting:
            ctx.god_mode(attacker, True)
            ctx.console_on("", f"{npc:08X}.startcombat {0x14 if attacker == '' else ctx.client_on_host():X}")
        victim = {"": npc, "client": copy}
        killer = {attacker: 0x14, other: ctx.other_player(ctx.actors_on(other))}
        report = {"attacker": said(attacker), "weapon": f"{weapon:X}", "behind": behind, "action": None, "problems": []}
        ctx.console_on(attacker, "set KillMoveRandom to 100")
        for perk in perks:
            ctx.console_on(attacker, f"player.addperk {perk}")
        try:
            ctx.sheathe(attacker)  # an equip into hands left raised by the last kill move plays no counted animation
            ctx.equip_weapon(attacker, weapon)
            ctx.draw(attacker)
            ctx.console_on("", f"{npc:08X}.damageav health {ctx.host_world(npc)['values']['health'] - 3:.0f}")
            ctx.wait_on("client", "the victim's copy shows it one hit from death",
                        lambda actors: (actors.get(str(copy), {}).get("health") or 100) < 5, 3)
            front = math.degrees(ctx.actors_on("client")[str(copy)]["rotation"][2])
            side = bearing if bearing is not None else front + (180 if behind else 0)
            ctx.place_before(attacker, victim[attacker], MELEE_GAP, side)
            ctx.place_before(other, victim[other], 300, side + 90)
            report["height"] = round(ctx.player_snapshot(attacker)["placement"]["position"][2]
                                     - ctx.actor_snapshot(attacker, victim[attacker])["placement"]["position"][2], 1)
            check = {}
            def will_kill():
                check.update(ctx.player_command(attacker, {"cmd": "kill_move_check", "victim": victim[attacker]}))
                return check.get("should_attack_kill") and check.get("attack_target") == victim[attacker]
            try:
                ctx.wait_for(f"[{said(attacker)}] a blow now is a kill move", will_kill, 3)
            except TestFailure:
                pass
            report["check"] = {key: check.get(key) for key in ("should_attack_kill", "killable", "attack_target")}
            report["check"]["victim"] = victim[attacker]
            trace = AnimTrace(ctx, "kill-move-on", {game: {killer[game]: "attacker", victim[game]: "victim"} for game in games}, video=False)
            with trace:
                ctx.press(attacker, "right")
                try:
                    started = trace.wait_state(attacker, "attacker", "kill_move", True, 3)
                except TestFailure:
                    started = None
                finally:
                    ctx.release(attacker, "right")
                if started:
                    action = trace.action_before(started)
                    report["action"] = action and action["event"]
                dead = lambda game: ctx.is_dead(ctx.actor_snapshot(game, victim[game]))
                try:
                    ctx.wait_for("the victim dies on both games", lambda: all(dead(game) for game in games), 10)
                except TestFailure:
                    report["problems"] += [f"[{said(game)}] the victim is alive 10 s after the blow" for game in games if not dead(game)]
                def last_states():
                    trace.take()
                    return {(game, who): next((e["state"] for e in reversed(trace.entries) if e["game"] == game and e["who"] == who
                                               and e["kind"] == "state"), {}) for game in games for who in ("attacker", "victim")}
                try:
                    ctx.wait_for("everyone has left the pair on both games",
                                 lambda: not any(state.get("kill_move") or state.get("bIsSynced") for state in last_states().values()), watch)
                except TestFailure:
                    pass  # said below: who, on which game, in what state
                trace.take()
                for game in games:
                    for who in ("attacker", "victim"):
                        states = [e for e in trace.entries if e["game"] == game and e["who"] == who and e["kind"] == "state"]
                        last = states[-1]["state"] if states else {}
                        if last.get("kill_move") or last.get("bIsSynced"):
                            report["problems"].append(f"[{said(game)}] the {who} is still in the kill move (kill_move {last.get('kill_move')}, bIsSynced {last.get('bIsSynced')})")
                        # One pair on every game: the other game's copies are in the kill move too (bugs.md, "The
                        # attacker's copy not in its kill move").
                        if started and not any(e["state"].get("kill_move") for e in states):
                            report["problems"].append(f"[{said(game)}] the {who} never entered the kill move")
                        died = next((e for e in states if e["state"].get("life") in (1, 2)), None)  # dying, dead (6: restrained)
                        blow = trace.find(game, who, "KillActor")
                        if who == "victim" and died and started:
                            report[f"{said(game)}_death_s"] = round(died["time"] - started["time"], 2)
                        # A pair that played is measured on each game: its blow, then its death.
                        if who == "victim" and started and not (died and blow):
                            report["problems"].append(f"[{said(game)}] the victim's {'KillActor' if not blow else 'death'} was never seen in the trace")
                        if who == "victim" and died and blow and died["time"] < blow["time"] - 0.1:
                            report["problems"].append(f"[{said(game)}] the victim died {blow['time'] - died['time']:.2f} s before its clip's KillActor")
                        if who == "victim" and died and blow:
                            report[f"{said(game)}_blow_to_death_s"] = round(died["time"] - blow["time"], 2)
                            # Its clip's KillActor is the blow: it drops then, not a pose hold later (kill-moves.md).
                            if died["time"] - blow["time"] > KILL_MOVE_DROP:
                                report["problems"].append(f"[{said(game)}] the victim stood {died['time'] - blow['time']:.2f} s after its clip's KillActor before it died")
                    state = ctx.actor_snapshot(game, victim[game]).get("state") or {}
                    if state.get("deferred_kill") or state.get("kill_queued"):
                        report["problems"].append(f"[{said(game)}] the victim's kill is still held (deferred {state.get('deferred_kill')}, queued {state.get('kill_queued')})")
            if started:
                report["apart"] = kill_move_apart(trace, attacker, other)
                if report["apart"]["gap"] > KILL_MOVE_APART or report["apart"]["height"] > KILL_MOVE_APART:
                    report["problems"].append(f"[{said(other)}] the pair stands apart from the {said(attacker)}'s: {report['apart']}")
            def lying():
                return {game: ctx.ragdoll_of(game, victim[game]) if rest and dead(game) else [] for game in games}
            try:
                # The host's body settles in its own time; its copy is put at that rest and sleeps (physics-sync.md).
                ctx.wait_for("both games' bodies of the victim come to rest",
                             lambda: not any(body["awake"] for bodies in lying().values() for body in bodies), 30)
            except TestFailure:
                pass  # said below, body by body
            bodies = lying()
            for game in games:
                out = [i for i, body in enumerate(bodies[game]) if not body["in_world"]]
                held = [i for i, body in enumerate(bodies[game]) if body["motion"] == 4]
                awake = [i for i, body in enumerate(bodies[game]) if body["awake"]]
                for what, which in (("out of the world", out), ("keyframed", held), ("awake", awake)):
                    if which:
                        report["problems"].append(f"[{said(game)}] the dead victim's ragdoll: bodies {which} {what}")
            if apart := ctx.ragdoll_apart(bodies[""], bodies["client"], units=8.0, turn=0.25):
                report["problems"].append(f"the victim's body lies differently: {apart}")
            # The attacker walks: a kill move or a held pair leaves its controls dead.
            at = ctx.player_snapshot(attacker)["placement"]["position"]
            ctx.player_command(attacker, {"cmd": "input", "button": "back", "down": True})
            try:
                ctx.wait_for(f"[{said(attacker)}] the attacker walks", lambda: distance(ctx.player_snapshot(attacker)["placement"]["position"], at) > 30, 3)
            except TestFailure:
                report["problems"].append(f"[{said(attacker)}] the attacker cannot walk after its kill move")
            finally:
                ctx.player_command(attacker, {"cmd": "input", "button": "back", "down": False})
        finally:
            ctx.console_on(attacker, f"set KillMoveRandom to {KILL_MOVE_RANDOM}")
            if fighting:
                ctx.god_mode(attacker, False)
            for perk in perks:
                ctx.console_on(attacker, f"player.removeperk {perk}")
            if weapon:  # the one it was given: a second of its kind would take the hand when one is unequipped
                ctx.console_on(attacker, f"player.removeitem {weapon:08X} 1")
            ctx.console_on("", f"{npc:08X}.disable")
            ctx.console_on("", f"{npc:08X}.markfordelete")
        # strict: a scenario's check. A kill move must have played, with nothing wrong after it.
        if strict and (not report["action"] or report["problems"]):
            raise ctx.failure(f"kill move {report['action'] or '(none: a plain swing)'} with {weapon:X}: " + ("; ".join(report["problems"]) or "no pair started"))
        return report


    def kill_move_ragdoll(ctx, attacker, base, weapon, behind=False, alone=False, seconds=12.0):
        """A kill move on a restrained NPC (`attacker` "" the host, "client" the guest), its victim's ragdoll traced every
        frame on each game: how it falls, against the same in one game with no session (`alone`, after
        vanilla_start(plugin=true): our hooks load but no session runs). Per game, from the victim's clip's KillActor:
        dead_s, dynamic_s (first frame a ragdoll body is dynamic), still_s (how long the pelvis then stayed within a unit),
        jump (its most in one frame in the first second), speed (body peak, units/s), flickers, turns (worst second),
        stray, rest_s and travel (pelvis, blow to rest)."""
        games = ("",) if alone else ("", "client")
        if alone:
            before = set((ctx.game("").read_status() or {}).get("world") or {})
            ctx.console_on("", f"player.placeatme {base} 1")
            npc = int(next(iter(ctx.wait_for("[host] the NPC loads", lambda: {key for key in (ctx.game("").read_status() or {}).get("world") or {}
                                                                              if key not in before}, 10))))
            ctx.console_on("", f"{npc:08X}.setrestrained 1")
            victim = {"": npc}
        else:
            npc, _, copy = ctx.spawn_host_npc(base, restrained=True)
            victim = {"": npc, "client": copy}
        report = {"attacker": "alone" if alone else attacker or "host", "weapon": f"{weapon:X}", "action": None}
        ctx.console_on(attacker, "set KillMoveRandom to 100")
        try:
            ctx.sheathe(attacker)
            ctx.equip_weapon(attacker, weapon)
            ctx.draw(attacker)
            health = ctx.actor_snapshot("", npc)["values"]["health"]
            ctx.console_on("", f"{npc:08X}.damageav health {health - 3:.0f}")
            ctx.wait_for("the victim is one hit from death on the attacker's game",
                         lambda: ((ctx.actor_snapshot(attacker, victim[attacker]).get("values") or {}).get("health") or 100) < 5, 5)
            facing = math.degrees(ctx.actor_snapshot(attacker, victim[attacker])["placement"]["rotation"][2])
            ctx.place_before(attacker, victim[attacker], MELEE_GAP, facing + (180 if behind else 0))
            if not alone:
                ctx.place_before("client" if attacker == "" else "", victim["client" if attacker == "" else ""], 300, 90)
            ctx.wait_for("a blow now is a kill move", lambda: (check := ctx.player_command(
                attacker, {"cmd": "kill_move_check", "victim": victim[attacker]})).get("should_attack_kill")
                and check.get("attack_target") == victim[attacker], 5)
            watched = {game: {victim[game]: "victim"} for game in games}
            watched[attacker][0x14] = "attacker"
            with AnimTrace(ctx, "kill-move-ragdoll", watched, video=False) as trace, \
                    RagdollTrace(ctx, "kill-move-ragdoll", {game: {victim[game]: "victim"} for game in games}) as bodies:
                ctx.press(attacker, "right")
                try:
                    started = trace.wait_state(attacker, "attacker", "kill_move", True, 3)
                finally:
                    ctx.release(attacker, "right")
                action = trace.action_before(started)
                report["action"] = action and action["event"]
                ctx.wait_for("the victim dies on every game", lambda: all(ctx.is_dead(ctx.actor_snapshot(g, victim[g])) for g in games), 12)
                end = time.monotonic() + seconds
                while time.monotonic() < end:
                    bodies.take()
                    trace.take()
                    if all((f := bodies.frames(g, victim[g])) and f[-1].get("dead") and not any((b or {}).get("awake") for b in f[-1]["bodies"])
                           for g in games):
                        break
                    time.sleep(0.2)
                trace.take()
            for game in games:
                report[game or "host"] = ragdoll_fall(bodies, game, victim[game], trace.find(game, "victim", "KillActor"))
            return report
        finally:
            ctx.console_on(attacker, f"set KillMoveRandom to {KILL_MOVE_RANDOM}")
            if weapon:
                ctx.console_on(attacker, f"player.removeitem {weapon:08X} 1")
            ctx.console_on("", f"{npc:08X}.disable")
            ctx.console_on("", f"{npc:08X}.markfordelete")

    def kill_move_table(ctx, attacker, victims=(), tries=2):
        """`attacker`'s player ("" the host, "client") swings through the kill move table (kill_moves.json, made by
        scripts/re/kill_moves.py --rows: the kill moves a normal swing can pick per victim, weapon and side) with
        kill_move_on, `victims` (names) or all. A row is swung until each of its kill moves played, `tries` swings per
        kill move at most; each swing's report is appended to build/coop-test/kill-moves.jsonl. Returns {played: {event: [each time's problems]}, unpaired: rows that never paired, missed:
        kill moves never picked}."""
        played, unpaired = {}, []
        KILL_MOVE_LOG.parent.mkdir(parents=True, exist_ok=True)
        rows = [row for row in KILL_MOVE_TABLE if row["weapon"] in KILL_MOVE_WEAPONS and (not victims or row["victim"] in victims)]
        rows.sort(key=lambda row: -len(row["events"]))  # the rows with most kill moves first: fewer rows left to swing
        for row in rows:
            what = f"{row['victim']}, {row['weapon']}{' from behind' if row['behind'] else ''}{' with perks' if row['perks'] else ''}"
            swung = 0
            while any(event not in played for event in row["events"]) and swung < tries * len(row["events"]):
                swung += 1
                try:
                    report = ctx.kill_move_on(attacker=attacker, base=row["base"], weapon=KILL_MOVE_WEAPONS[row["weapon"]],
                                              behind=row["behind"], perks=row["perks"])
                except TestFailure as failure:  # one row's stop is said with the rest, not the table's end
                    unpaired.append(f"{what}: {failure}")
                    ctx.unstage()
                    break
                log(f"[{report['attacker']}] {what}: {report['action'] or 'no kill move'}, apart {report.get('apart')}, "
                    f"problems {report['problems']}")
                with KILL_MOVE_LOG.open("a") as out:  # each swing kept, also when the run is cut short
                    out.write(json.dumps({"row": what, **report}) + "\n")
                if report["action"]:
                    played.setdefault(report["action"], []).append([f"{what}: {problem}" for problem in report["problems"]])
                elif swung >= len(row["events"]) and not any(event in played for event in row["events"]):
                    unpaired.append(f"{what} (check {report.get('check')})")
                    break
        missed = sorted({event for row in rows for event in row["events"]} - set(played))
        return {"played": played, "unpaired": unpaired, "missed": missed}

    def combat_watch(ctx, npc, seconds=5.0, every=0.33, sight=None):
        """An NPC of the host's in a fight, over time: its gap to its target, how far the target's predicted place (what its
        close movement reads) is off the target's real one, the target's path follower and the close movement branch.
        `sight` (an actor): also its sight of that one each row (harness `sight`): angle off its facing, LOS location,
        level."""
        actor = ctx.resolve(npc, "host")
        rows = []
        end = time.monotonic() + seconds
        while time.monotonic() < end:
            read = ctx.player_command("", {"cmd": "combat_targets", "actor": actor})
            place = ctx.actor_snapshot("", actor).get("placement", {}).get("position") or [0, 0, 0]
            at = read.get("target_position") or [0, 0, 0]
            close = next((node for node in read.get("behavior", []) if "CloseMovement" in node), "")
            rows.append({"t": round(seconds - (end - time.monotonic()), 1), "target": f"{read.get('controller_target', 0):X}",
                         "gap": round(math.dist(place[:2], at[:2])),
                         "predicted_off": round(math.dist(at[:2], (read.get("target_predicted") or at)[:2])),
                         "range": round(read.get("close_range", 0) + read.get("margin", 0)),
                         "path_state": read.get("target_path_state"), "path_points": read.get("target_path_points"),
                         "branch": close.rsplit(" > ", 2)[-2:], "state": read.get("combat_state"),
                         "lost": read.get("target_lost"), "detected": read.get("detected"),
                         "candidates": [(f"{c['actor']:X}", c["detection"], c["flags"], round(c.get("light", 0)),
                                         round(c.get("raw_light", 0)), c.get("line_of_sight")) for c in read.get("candidates", [])]})
            if sight is not None:
                seen = ctx.player_command("", {"cmd": "sight", "actor": actor, "target": ctx.resolve(sight, "host")})
                rows[-1]["sight"] = (round(seen.get("angle", 0)), seen.get("los"), seen.get("level"))
            time.sleep(every)
        return rows

    def sight_watch(ctx, npc, target=0x14, seconds=5.0, every=0.5):
        """An NPC of the host's sight of a target over time (harness `sight`, stealth.md "Line of sight"): its combat
        target, the angle off its facing, CalculateLOS's location (0 none), CalculateDetection's level and terms, and per
        probe ray (torso, head, feet; 37770, not the engine's own) what it meets: '-' nothing, 'T' the target, else the
        reference:node and cast layer."""
        actor, aim = ctx.resolve(npc, "host"), ctx.resolve(target, "host")
        rows = []
        end = time.monotonic() + seconds
        while time.monotonic() < end:
            read = ctx.player_command("", {"cmd": "sight", "actor": actor, "target": aim})
            rays = []
            for ray in read.get("rays", []):
                if not ray["hit"]:
                    rays.append("-")
                elif ray["sees"]:
                    rays.append("T")
                else:
                    rays.append(f"{ray['hit_ref']}:{ray['hit_node']}/L{ray['cast_layer']}")
            rows.append({"t": round(seconds - (end - time.monotonic()), 1), "combat_target": read.get("combat_target"),
                         "distance": round(read.get("distance", 0)), "angle": round(read.get("angle", 0)),
                         "los": read.get("los"), "level": read.get("level"), "in_view": read.get("in_view"),
                         "sound": round(read.get("sound", 0), 1), "sight": round(read.get("sight", 0), 1),
                         "noise": (read.get("target_noise"), round(read.get("target_noise_timer", 0), 1)), "rays": rays})
            time.sleep(every)
        return rows

    def host_death_watch(ctx, npc=0x13486, seconds=20.0, guest_offset=-80, dies="host"):
        """The half-stop's setup: the NPC fights both players, the host dies, the guest stands still; then combat_watch.
        Returns the guest's place at the death, the longest stall (same gap past close range, in combat) and the rows'
        gaps. Use after a checkpoint in the inn; retry between rounds (the NPC stays hostile to the copy). dies="guest" is
        the comparison: the host's player is the one left standing."""
        npc = ctx.resolve(npc, "host")
        copy = ctx.client_on_host()
        combat = lambda: ctx.player_command("", {"cmd": "combat_targets", "actor": npc})
        ctx.console_on("", f"{npc:X}.resurrect")
        ctx.god_mode("client", True)
        ctx.god_mode("", True)
        ctx.console_on("", f"player.moveto {npc:X} 80 0 0")
        ctx.console_on("client", f"player.moveto {npc:X} {guest_offset} 0 0")
        ctx.console_on("", f"{npc:X}.startcombat 14")
        ctx.wait_for("the NPC fights the host", lambda: combat().get("controller_target") == 0x14, 15)
        ctx.console_on("", f"{npc:X}.startcombat {copy:X}")
        ctx.wait_for("both players are the NPC's targets",
                     lambda: {0x14, copy} <= {c["actor"] for c in combat().get("candidates", [])}, 15)
        dying, left = ("", "client") if dies == "host" else ("client", "")
        ctx.god_mode(dying, False)
        ctx.console_on(dying, "player.kill")
        ctx.wait_for(f"the {dies} is down", lambda: ctx.death_state(dying).get("down"), 15)
        guest_at = [round(v) for v in (ctx.actor_snapshot("", copy).get("placement", {}).get("position") or [])]
        rows = ctx.combat_watch(npc, seconds=seconds, every=0.5, sight=copy if dies == "host" else 0x14)
        ctx.god_mode(left, False)
        stall, run, last = 0, 0, None
        for row in rows:
            held = row["state"] == 1 and row["gap"] > row["range"] and row["gap"] == last
            run = run + 1 if held else 0
            stall = max(stall, run)
            last = row["gap"]
        return {"guest_at": guest_at, "stall_s": round(stall * 0.5, 1),
                "search_s": round(sum(r["state"] == 2 for r in rows) * 0.5, 1),
                "gaps": " ".join(f"{r['gap']}{'' if r['state'] == 1 else 's'}" for r in rows),
                "sight": " ".join(f"{a}/{l}/{v}" for a, l, v in (r["sight"] for r in rows))}

    def console_watch(ctx, reads, seconds=10.0, every=1.0, instance=""):
        """Console reads over time ({name: line}, e.g. {"state": "13486.getcombatstate"}): the numbers each line prints, also in
        the vanilla stand-in (no harness readouts there), to compare a fight with the bare game."""
        rows, start = [], time.monotonic()
        while time.monotonic() - start < seconds:
            row = {"t": round(time.monotonic() - start, 1)}
            for name, line in reads.items():
                printed = (ctx.console_on(instance, line) or [""])[-1]
                numbers = [float(n) for n in re.findall(r"-?\d+(?:\.\d+)?", printed.split(">>")[-1].split(":")[-1])]
                row[name] = numbers[0] if len(numbers) == 1 else numbers or None
            rows.append(row)
            time.sleep(every)
        return rows

    def counter(ctx, instance, name):
        """How many times that game's named counter (Diagnostics/Counters.h) was bumped; 0 before its first use."""
        counters = (ctx.player_command(instance, {"cmd": "counters"}) or {}).get("counters") or {}
        return (counters.get(name) or {}).get("count", 0)

    def closes_in(ctx, npc, back_seconds=6.0, within=300.0, seconds=15.0):
        """The guest's player walks back and the host's `npc` must come to within `within` of the guest's copy while
        fighting that copy: fails with what combat_watch saw (a half-stop drifts at range, or fights someone else)."""
        guest = ctx.client_on_host()
        ctx.act("client", {"name": f"walks back from {npc} {back_seconds:g} s", "ready": ["takes_input"], "buttons": ["back"],
                           "until": [{"for": back_seconds}], "seconds": back_seconds + 2})
        seen = []
        end = time.monotonic() + seconds
        while time.monotonic() < end:
            seen += ctx.combat_watch(npc, seconds=0.1)
            here = ctx.actor_snapshot("", ctx.resolve(npc, "host")).get("placement", {}).get("position")
            there = ctx.actor_snapshot("", guest).get("placement", {}).get("position")
            if seen[-1]["target"] == f"{guest:X}" and here and there and math.dist(here[:2], there[:2]) <= within:
                return seen[-1]
        raise ctx.failure(f"{npc} never came within {within:g} of the guest's copy {guest:X} while fighting it in "
                          f"{seconds:g} s: {seen[:3]} ... {seen[-2:]}")

    def actor_snapshot(ctx, instance, actor):
        _game = ctx.game(instance)
        return ((_game.read_status() or {}).get("world") or {}).get(str(actor), {})


    def death_state(ctx, instance):
        return ctx.game_state(instance).get("death", {})


    def is_dead(ctx, snapshot):
        return bool((snapshot.get("state") or {}).get("dead"))


    def effect_spells(ctx, instance):
        return sorted(e["spell"] for e in (ctx.player_snapshot(instance).get("effects") or []))


    def death_spectate_respawn(ctx, victim, travel_down=False):
        """An enemy kills the player; they spectate through combat (their game never reloads) and are revived beside the
        standing party with every ability back."""
        survivor = "client" if victim == "" else ""
        for instance in ("", "client"):
            ctx.console_on(instance, "player.restoreav health 10000")
        ctx.give_sword(survivor)
        ctx.give_sword(victim)
        ctx.sheathe(survivor)
        # The host's enemy picks its own target among those it detects, and the host's player stands beside the stage: it
        # stays unseen (Invisibility) from before the enemy comes until its turn, or the enemy never reaches the guest.
        hidden_host = survivor == ""
        if hidden_host:
            ctx.console_on("", "player.setav invisibility 1")
        npc, _, copy = ctx.spawn_host_npc(WOLF)
        target = 0x14 if victim == "" else ctx.client_on_host()
        observer_target = ctx.client_on_host() if survivor == "client" else 0x14
        old_magicka = (ctx.player_snapshot(victim).get("values") or {}).get("magicka")
        abilities = ctx.effect_spells(victim)
        hands = (ctx.player_snapshot(victim).get("state") or {}).get("hands")
        try:
            ctx.console_on("", "set KillMoveRandom to 0")
            ctx.place_before(victim, npc if victim == "" else copy, MELEE_GAP)
            if victim:
                ctx.wait_for("the host sees the victim beside the staged enemy", lambda:
                         distance((ctx.actor_snapshot("", target).get("placement") or {}).get("position"), ctx.my_position(victim)) < 4, 10)
            health = (ctx.player_snapshot(victim).get("values") or {}).get("health", 100)
            if health > 5:
                ctx.console_on(victim, f"player.damageav health {health - 5:.6f}")
            ctx.console_on("", f"{npc:X}.startcombat {target:X}")
            def fight():
                wolf = ctx.actor_snapshot("", npc)
                seen = ctx.actor_snapshot(victim, npc if victim == "" else copy)
                seen_at = (seen.get("placement") or {}).get("position")
                gap = distance(seen_at, ctx.my_position(victim)) if seen_at else -1
                return (f"the host's enemy targets {(wolf.get('ai') or {}).get('combat_target', 0):X}, last event "
                        f"{(wolf.get('animation') or {}).get('event')!r}; on the victim's game it stands {gap:.0f} units off, "
                        f"last event {(seen.get('animation') or {}).get('event')!r}; the victim's health "
                        f"{(ctx.player_snapshot(victim).get('values') or {}).get('health')}")
            try:
                ctx.wait_for("the enemy's hit kills the player", lambda: ctx.death_state(victim).get("down"), 15)
            except TestFailure as failure:
                raise ctx.failure(f"{failure} ({fight()})") from None
            # It turns on the standing player before it is frozen: a frozen actor has no AI to take a new target (it
            # kept or dropped one by timing, B5 in docs/v1-open-issues-2026-10-29.md).
            if hidden_host:
                ctx.console_on("", "player.setav invisibility 0")
            ctx.console_on("", f"{npc:X}.startcombat {observer_target:X}")
            ctx.wait_for("the enemy turns on the standing player",
                     lambda: ctx.host_world(npc).get("ai", {}).get("combat_target") == observer_target, 5)
            ctx.freeze("", npc)
            if not ctx.is_dead(ctx.player_snapshot(victim)):
                raise ctx.failure("the downed player is not dead (essential, or bleeding out)")
            ctx.camera_sees_followed(victim, "the death camera")
            def copy_of_victim():
                return ctx.actor_snapshot(survivor, ctx.other_player(ctx.actors_on(survivor)))
            ctx.wait_for("the other game's copy dies with its owner", lambda: ctx.is_dead(copy_of_victim()), 5)
            # Its ragdoll is its game's (physics sync's owners): the copy lies as it does. Traced: what moves the copy's
            # bodies after they are put at the owner's rest (bugs.md, "A dead player's copy a little off its owner's body").
            if not travel_down:
                victim_copy = ctx.other_player(ctx.actors_on(survivor))
                trace = RagdollTrace(ctx, "death-spectate", {victim: {0x14: "the dead player"}, survivor: {victim_copy: "its copy"}})
                try:
                    with trace:
                        ctx.ragdoll_lies_as_owner(victim, 0x14, survivor, victim_copy, trace)
                except TestFailure:
                    log(f"the dead player's bodies: {trace.report()}; samples {trace.folder}")
                    raise
            ctx.wait_for("spectator camera has a standing player once the player is dead",
                     lambda: ctx.death_state(victim).get("spectating"), 10)
            ctx.camera_sees_followed(victim, "spectating")
            ctx.spectating_stays_third_person(victim)
            try:
                ctx.wait_for("the host counts combat against the standing player",
                         lambda: ctx.death_state(victim).get("status") == "Party in combat", 5)
            except TestFailure as failure:
                raise ctx.failure(f"{failure} (status {ctx.death_state(victim).get('status')!r}, the wolf's target "
                                  f"{ctx.host_world(npc).get('ai', {}).get('combat_target')})") from None
            started = time.monotonic()
            def still_down_in_combat():
                if not ctx.game_state(victim).get("online") or not ctx.death_state(victim).get("down"):
                    raise ctx.failure("the player respawned, or its game reloaded, while the party was fighting")
                if not ctx.is_dead(ctx.player_snapshot(victim)):
                    raise ctx.failure("the dead player came back alive")
                if not ctx.is_dead(copy_of_victim()):
                    raise ctx.failure("the other game's copy came back alive")
                return time.monotonic() - started >= 6
            ctx.wait_for("the vanilla five-second death reload never fires", still_down_in_combat, 8)
            if travel_down:
                ctx.both_go(INN)
                if not ctx.is_dead(ctx.player_snapshot(victim)) or not ctx.death_state(victim).get("down"):
                    raise ctx.failure("party travel revived the dead follower")
                ctx.camera_sees_followed(victim, "the dead follower after party travel")
                ctx.both_go(HOME_CELL)
                # An unloaded NPC gets a new session and copy on return; its host reference survives.
                ctx.wait_for("the enemy is assigned after travel", lambda:
                             ctx.host_npc_state(npc).get("session_id", -1) >= 0, 10)
                copy = ctx.copy_of("client", ctx.host_npc_state(npc)["session_id"])
            # Drive spectator controls as player input; with a duo the sole standing target stays selected.
            watched = ctx.death_state(victim)["spectating"]
            ctx.tap_key(victim, 0xCD)
            ctx.player_command(victim, {"cmd": "mouse", "button": 0, "down": True})
            ctx.player_command(victim, {"cmd": "mouse", "button": 0, "down": False})
            if ctx.death_state(victim).get("spectating") != watched:
                raise ctx.failure("cycling lost the only standing player")
            # The surviving player ends the fight by actually swinging their weapon.
            ctx.console_on("", f"{npc:X}.forceav health 1")
            ctx.freeze("", npc, False)
            ctx.console_on("", f"{npc:X}.setrestrained 1")
            ctx.place_before(survivor, copy if survivor == "client" else npc, MELEE_GAP)
            # Drawn only now: a standing player with a weapon out draws the enemy off the victim while it is staged.
            ctx.draw(survivor)
            ctx.attack(survivor)
            ctx.wait_for("the standing player's blow kills the enemy", lambda: ctx.host_npc_state(npc).get("dead"), 8)
            ctx.wait_on("client", "the enemy's copy dies too", lambda actors: actors.get(str(copy), {}).get("dead"), 5)
            ctx.wait_for("the party leaves combat",
                     lambda: ctx.death_state(victim).get("status", "").startswith("Respawning in"), 10)
            clear_since = time.monotonic()
            ctx.wait_for("the player respawns beside the standing party", lambda: not ctx.death_state(victim).get("down"), 14)
            if not ctx.host_npc_state(npc).get("dead") or not ctx.actor_snapshot("client", copy).get("state", {}).get("dead"):
                raise ctx.failure(f"after the respawn the enemy is dead on the host: {bool(ctx.host_npc_state(npc).get('dead'))}, "
                                  f"its copy on the client: {bool(ctx.actor_snapshot('client', copy).get('state', {}).get('dead'))}")
            ctx.wait_for("the living player recovers engine state and controls",
                     lambda: not ctx.is_dead(ctx.player_snapshot(victim))
                     and (ctx.player_snapshot(victim).get("values") or {}).get("health", 0) > 0
                     and (ctx.player_snapshot(victim).get("state") or {}).get("knock") == 0
                     and ctx.takes_input(victim), 8)
            if not travel_down and time.monotonic() - clear_since < 9:
                raise ctx.failure("respawn did not wait ten continuous seconds out of combat")
            if distance(ctx.my_position(victim), ctx.my_position(survivor)) > 250:
                raise ctx.failure("the player did not respawn beside the standing party")
            if ctx.death_state(victim).get("spectating"):
                raise ctx.failure("the camera did not return to the respawned player")
            ctx.camera_sees_followed(victim, "after the respawn")
            start = ctx.my_position(victim)
            ctx.press(victim, "forward")
            try:
                ctx.wait_for("the respawned player can walk again", lambda: distance(ctx.my_position(victim)[:2], start[:2]) > 12, 5)
            finally:
                ctx.release(victim, "forward")
            ctx.wait_for("the revived player holds what it held", lambda: (ctx.player_snapshot(victim).get("state") or {}).get("hands") == hands, 3)
            # Its behavior graph runs again (KillImpl stops it: no draw or swing would ever play out).
            ctx.draw(victim)
            ctx.attack(victim)
            ctx.sheathe(victim)
            if ctx.effect_spells(victim) != abilities:
                raise ctx.failure(f"the revived player's effects {ctx.effect_spells(victim)} are not its abilities before {abilities}")
            observer = "client" if victim == "" else ""
            if ctx.is_dead(ctx.actor_snapshot(observer, ctx.other_player(ctx.actors_on(observer)))):
                raise ctx.failure("the other game still shows the revived player dead")
            ctx.wait_for("the copy retains its owner's magicka after respawn",
                     lambda: is_close((ctx.actor_snapshot(observer, ctx.other_player(ctx.actors_on(observer))).get("values") or {}).get("magicka"), old_magicka, 1), 5)
        finally:
            if hidden_host:
                ctx.console_on("", "player.setav invisibility 0")
            ctx.console_on("", f"set KillMoveRandom to {KILL_MOVE_RANDOM}")
            ctx.console_on(victim, "player.restoreav health 10000")
            ctx.remove_host_npc(npc)


    def player_health(ctx, instance):
        return (ctx.player_snapshot(instance).get("values") or {}).get("health")


    def friendly_fire(ctx, attacker, weapon):
        """The attacker's player hits the other player's copy with `weapon` ("sword" or "firebolt"): the victim's own
        player loses health on its game, the attacker's copy of it shows that value, and the victim's game runs no NPC's
        reaction to its player (no combat against the attacker)."""
        victim = "client" if attacker == "" else ""
        for instance in ("", "client"):
            ctx.console_on(instance, "player.restoreav health 10000")
        copy = ctx.other_player(ctx.actors_on(attacker))
        if copy is None:
            raise ctx.failure(f"[{attacker or 'host'}] no copy of the other player")
        if weapon == "sword":
            ctx.give_sword(attacker)
            ctx.place_before(attacker, copy, MELEE_GAP)
        else:
            ctx.give_spell(attacker, FIREBOLT)
            ctx.place_before(attacker, copy, SPELL_GAP)
        ctx.draw(attacker)
        before = ctx.player_health(victim)
        if weapon == "sword":
            ctx.attack(attacker)
        else:
            ctx.cast_spell(attacker, copy, FIREBOLT)
        after = ctx.wait_for(f"[{victim or 'host'}] the victim's own player takes the {weapon}",
                         lambda: (h := ctx.player_health(victim)) is not None and h < before - 0.5 and h, 3)
        log(f"[{victim or 'host'}] player health {before:.0f} -> {after:.0f}")
        ctx.wait_on(attacker, "the attacker's copy of the victim shows its health",
                lambda actors: is_close(actors.get(str(copy), {}).get("health"), ctx.player_health(victim)), 5)
        if (ctx.player_snapshot(victim).get("ai") or {}).get("combat_target"):
            raise ctx.failure(f"[{victim or 'host'}] the victim's player went into combat with its teammate")
        ctx.sheathe(attacker)
        ctx.console_on(victim, "player.restoreav health 10000")
        ctx.hold()


    def shout_at_lying_player(ctx, attacker, health=0, respawn=True):
        """The other player is pushed down on its own game, then the attacker's three-word shout hits it lying.
        `health` > 0 leaves it that much health first: Dragonborn.esm's disintegrate effect (its ash pile script,
        TurnUnDisintegrate01FXS) kills it, and its game runs on, its player respawning with its body, opaque
        (docs/re/systems/death-disintegrate.md)."""
        victim = "client" if attacker == "" else ""
        copy = ctx.other_player(ctx.actors_on(attacker))
        if copy is None:
            raise ctx.failure(f"[{attacker or 'host'}] no copy of the victim")
        recovery = ctx.actor_value(attacker, 0x14, 86)  # kShoutRecoveryMult
        words = {(instance, word): ctx.word_state(instance, word)
                 for instance in ("", "client") for word in UNRELENTING_FORCE_WORDS}
        def knock(instance, actor):
            return (ctx.actor_snapshot(instance, actor).get("state") or {}).get("knock", 0)
        # The disintegrate effect also asks for the Black Book reward (DLC2BlackBookReward3 == 1) and for its caster to
        # be the Player (GetIsID, judged for the casting player's copy: conditions.md); the guest follows the host's.
        had_reward = any("= 1" in line for line in ctx.console_on("", "show DLC2BlackBookReward3") or [])
        try:
            if health > 0:
                ctx.console_on("", "set DLC2BlackBookReward3 to 1")
                ctx.wait_for("the guest holds the Black Book reward",
                             lambda: any("= 1" in line for line in ctx.console_on("client", "show DLC2BlackBookReward3") or []), 5)
            for word in UNRELENTING_FORCE_WORDS:
                ctx.console_on(attacker, f"player.teachword {word:X}")
                ctx.console_on(attacker, f"player.unlockword {word:X}")
            ctx.console_on(attacker, f"player.addshout {UNRELENTING_FORCE:X}")
            ctx.console_on(attacker, f"player.equipshout {UNRELENTING_FORCE:X}")
            ctx.console_on(attacker, "player.setav shoutrecoverymult 0")
            ctx.console_on(victim, "player.restoreav health 10000")
            ctx.place_before(attacker, copy, 300, bearing=90)
            ctx.wait_for("the three-word Unrelenting Force shout is equipped",
                     lambda: ctx.local_player(attacker).get("selected_power") == UNRELENTING_FORCE, 5)
            ctx.wait_for(f"[{victim or 'host'}] the victim stands before its push", lambda: not knock(victim, 0x14), 25)
            ctx.console_on(victim, "player.pushactoraway player 5")
            ctx.wait_for(f"[{victim or 'host'}] the victim lies in its ragdoll", lambda: 0 < knock(victim, 0x14) <= 4, 5)
            ctx.wait_for(f"[{attacker or 'host'}] the victim's copy lies", lambda: 0 < knock(attacker, copy) <= 4, 5)
            # Hidden by its own camera in first person: after the respawn it is as it was.
            hidden_before = (ctx.player_snapshot(victim).get("mod") or {}).get("body_hidden")
            if health > 0:
                ctx.console_on(victim, f"player.damageav health {max(0.0, ctx.player_health(victim) - health):g}")
            before = ctx.player_health(victim)
            _, since = ctx.engine_events(attacker)
            frame = ctx.game_frame(attacker)
            ctx.press(attacker, "shout")
            try:
                ctx.wait_for("the shout button is held through a full three-word charge",
                         lambda: ctx.game_frame(attacker) >= frame + 120, 5)
            finally:
                ctx.release(attacker, "shout")
            ctx.wait_for("the native three-word shout is cast",
                     lambda: any(e["kind"] == "spell_cast" and e.get("caster") == 0x14 and
                                 e.get("spell") == UNRELENTING_FORCE_THREE
                                 for e in ctx.engine_events(attacker, since)[0]), 5)
            if health > 0:
                ctx.wait_for(f"[{victim or 'host'}] the disintegrate effect's script plays its shader on the victim",
                             lambda: TURN_UN_DISINTEGRATE in (ctx.player_snapshot(victim).get("visuals") or []), 5)
                ctx.wait_for(f"[{victim or 'host'}] the shout kills the lying victim",
                             lambda: ctx.is_dead(ctx.player_snapshot(victim)), 5)
                # Its ash script's end hides its body on its own game, as the shader leaves its copy unseen.
                ctx.wait_for(f"[{victim or 'host'}] the dead victim's body is hidden, not left lying",
                             lambda: (ctx.player_snapshot(victim).get("mod") or {}).get("body_hidden"), 10)
            else:
                ctx.wait_for(f"[{victim or 'host'}] the shout hits the lying victim",
                             lambda: (h := ctx.player_health(victim)) is not None and h < before - 1, 5)
            frame = ctx.game_frame(victim)
            ctx.wait_for(f"[{victim or 'host'}] the victim's game keeps running",
                         lambda: ctx.game_frame(victim) >= frame + 120, 10)
            if health > 0 and respawn:
                def respawned():
                    snapshot = ctx.player_snapshot(victim)
                    state = snapshot.get("state") or {}
                    return (not ctx.is_dead(snapshot) and (snapshot.get("placement") or {}).get("has_3d")
                            and state.get("alpha", 0) >= 0.99
                            and (snapshot.get("mod") or {}).get("body_hidden") == hidden_before)
                ctx.wait_for(f"[{victim or 'host'}] the victim respawns with its body, opaque, shown as before", respawned, 40,
                             explain=lambda: str(ctx.player_snapshot(victim).get("state")))
        finally:
            if health > 0 and not had_reward:
                ctx.console_on("", "set DLC2BlackBookReward3 to 0")
            ctx.console_on(attacker, f"player.setav shoutrecoverymult {recovery:g}")
            for (instance, word), (known, unlocked) in words.items():
                ctx.player_command(instance, {"cmd": "form_known", "form": word, "known": known, "unlocked": unlocked})


    def body_report(ctx, instance):
        """That game's player's body, as both games show it: where the actor stands, how its skeleton stands over the
        floor (pose), its ragdoll's bodies, its knock state and death; then its copy's on the other game."""
        other = "" if instance else "client"
        def part(snapshot):
            return {"position": (snapshot.get("placement") or {}).get("position"), "pose": snapshot.get("pose"),
                    "ragdoll": snapshot.get("ragdoll"), "knock": (snapshot.get("state") or {}).get("knock"),
                    "dead": ctx.is_dead(snapshot), "alpha": (snapshot.get("state") or {}).get("alpha"),
                    "body_hidden": (snapshot.get("mod") or {}).get("body_hidden"),
                    "visuals": snapshot.get("visuals"), "has_3d": (snapshot.get("placement") or {}).get("has_3d"),
                    "shown": {k: (snapshot.get("mod") or {}).get(k)
                              for k in ("visuals_shown", "fade_state", "pose_visibility", "components")}}
        copy = ctx.other_player(ctx.actors_on(other))
        return {"own": part(ctx.player_snapshot(instance)), "copy": part(ctx.actor_snapshot(other, copy) if copy else {})}


    def alpha_trace(ctx, victim, seconds=30.0):
        """The victim's player's alpha (state.alpha, Papyrus SetAlpha's) on its own game and on its copy in the other,
        with its death, every half second for `seconds`: one line per change."""
        other = "" if victim else "client"
        lines, last, end = [], None, time.time() + seconds
        while time.time() < end:
            own = ctx.player_snapshot(victim)
            copy = ctx.other_player(ctx.actors_on(other))
            mirror = ctx.actor_snapshot(other, copy) if copy else {}
            now = ((own.get("state") or {}).get("alpha"), ctx.is_dead(own), (mirror.get("state") or {}).get("alpha"))
            if now != last:
                lines.append(f"{seconds - (end - time.time()):5.1f}s own alpha {now[0]} dead {now[1]}, copy alpha {now[2]}")
                last = now
            time.sleep(0.5)
        return lines


    def fus_ro_dah_player(ctx, attacker):
        """Use the real three-word shout on the other player; both games see the fall and recovery."""
        recovery = ctx.actor_value(attacker, 0x14, 86)  # kShoutRecoveryMult
        words = {(instance, word): ctx.word_state(instance, word)
                 for instance in ("", "client") for word in UNRELENTING_FORCE_WORDS}
        try:
            victim = "client" if attacker == "" else ""
            copy = ctx.other_player(ctx.actors_on(attacker))
            if copy is None:
                raise ctx.failure(f"[{attacker or 'host'}] no copy of the victim")
            for word in UNRELENTING_FORCE_WORDS:
                ctx.console_on(attacker, f"player.teachword {word:X}")
                ctx.console_on(attacker, f"player.unlockword {word:X}")
            ctx.console_on(attacker, f"player.addshout {UNRELENTING_FORCE:X}")
            ctx.console_on(attacker, f"player.equipshout {UNRELENTING_FORCE:X}")
            ctx.console_on(attacker, "player.setav shoutrecoverymult 0")
            ctx.console_on(attacker, "player.restoreav health 10000")
            ctx.console_on(victim, "player.restoreav health 10000")
            ctx.place_before(attacker, copy, 300, bearing=90)  # shout toward the clear west end, away from QA clutter
            ctx.wait_for("the three-word Unrelenting Force shout is equipped",
                     lambda: ctx.local_player(attacker).get("selected_power") == UNRELENTING_FORCE, 5)
            start = ctx.my_position(victim)
            _, since = ctx.engine_events(attacker)
            def knock(instance, actor):
                return (ctx.actor_snapshot(instance, actor).get("state") or {}).get("knock", 0)
            frame = ctx.game_frame(attacker)
            ctx.press(attacker, "shout")
            try:
                ctx.wait_for("the shout button is held through a full three-word charge",
                         lambda: ctx.game_frame(attacker) >= frame + 120, 5)
            finally:
                ctx.release(attacker, "shout")
            ctx.wait_for("the native three-word shout is cast, rather than its weaker versions",
                     lambda: any(e["kind"] == "spell_cast" and e.get("caster") == 0x14 and
                                 e.get("spell") == UNRELENTING_FORCE_THREE
                                 for e in ctx.engine_events(attacker, since)[0]), 5)
            ctx.wait_for(f"[{victim or 'host'}] the victim's own player is knocked down",
                     lambda: 0 < knock(victim, 0x14) <= 4, 5)
            ctx.wait_for(f"[{attacker or 'host'}] the victim's copy is knocked down",
                     lambda: 0 < knock(attacker, copy) <= 4, 5)
            ctx.wait_for(f"[{victim or 'host'}] the shout displaces the victim",
                     lambda: distance(ctx.my_position(victim)[:2], start[:2]) > 30, 5)
            ctx.wait_for(f"[{victim or 'host'}] the victim gets up alive",
                     lambda: not knock(victim, 0x14) and not ctx.is_dead(ctx.player_snapshot(victim)), 25)
            ctx.wait_for("the copy gets up with its owner", lambda: not knock(attacker, copy), 5)
            ctx.face(victim, ctx.other_player(ctx.actors_on(victim)))
            before_walk = ctx.my_position(victim)
            ctx.press(victim, "forward")
            try:
                ctx.wait_for("the recovered player can walk again",
                         lambda: distance(ctx.my_position(victim)[:2], before_walk[:2]) > 30, 5)
            finally:
                ctx.release(victim, "forward")
        finally:
            ctx.console_on(attacker, f"player.setav shoutrecoverymult {recovery:g}")
            for (instance, word), (known, unlocked) in words.items():
                ctx.player_command(instance, {"cmd": "form_known", "form": word, "known": known, "unlocked": unlocked})


    def spell_hits_host_npc(ctx, spell, name):
        """Client casts `spell` at the host's NPC. The host's real NPC must take the damage, and the
        client's copy must only ever show the host's value: a copy that drops below its owner applied
        the effect itself (two games simulating one actor)."""
        npc, _, copy = ctx.spawn_host_npc(LYDIA, restrained=True)
        ctx.give_spell("client", spell)
        ctx.place_before("client", copy, SPELL_GAP)
        ctx.draw("client")
        before = ctx.host_npc_health(npc)
        worst_gap = 0.0
        def landed():
            nonlocal worst_gap
            owner, mirror = ctx.host_npc_health(npc), ctx.actors_on("client").get(str(copy), {}).get("health")
            if owner is not None and mirror is not None:
                worst_gap = max(worst_gap, owner - mirror)
            return owner is not None and owner < before - 1 and owner
        try:
            ctx.cast_spell("client", copy, spell)
            after = ctx.wait_for(f"[host] the client's {name} lands", landed, 3)
        finally:
            ctx.stop_casting("client")
        log(f"host's NPC health {before:.0f} -> {after:.0f}")
        if worst_gap > 5:
            raise ctx.failure(f"the client's copy dropped {worst_gap:.0f} below the host's real NPC: the client applied the spell itself")
        ctx.copy_health_matches(npc, copy)
        ctx.health_bar_shows("client", copy)
        ctx.hold()


    def kill_credit(ctx, weapon):
        npc, _, copy = ctx.spawn_host_npc(WOLF, restrained=True)
        client_on_host = ctx.other_player(ctx.actors_on(""))
        dead = lambda: ctx.host_world(npc).get("state", {}).get("dead")
        # In front of its head, as a player would face it (its heading: 0 north, clockwise).
        front = math.degrees(ctx.actors_on("client")[str(copy)]["rotation"][2])
        with GodMode(ctx):
            if weapon == "spell":
                ctx.give_spell("client", FIREBOLT)
                ctx.place_before("client", copy, SPELL_GAP, front)
            else:
                ctx.give_sword("client")
                ctx.place_before("client", copy, MELEE_GAP, front)
            ctx.draw("client")
            # A killing blow can start a kill move instead (KillMoveRandom): the kill move then kills it.
            in_kill_move = lambda: ctx.player_command("", {"cmd": "kill_move_check", "attacker": npc, "victim": npc})["in_kill_move"]
            for hit in range(1, 16):
                before = ctx.host_npc_health(npc)
                if weapon == "spell":
                    ctx.cast_spell("client", copy, FIREBOLT)
                    ctx.hit_lands(npc, before, f"hit {hit}", 3)
                else:
                    ctx.attack("client")
                    ctx.wait_for(f"[host] hit {hit} lands or starts a kill move",
                             lambda: (h := ctx.host_npc_health(npc)) is not None and h < before - 0.5 or in_kill_move(), 3)
                if dead() or in_kill_move():
                    break
            ctx.wait_for("[host] the wolf dies", dead, 8)
        killer = ctx.wait_for("[host] killer recorded", lambda: ctx.host_world(npc).get("ai", {}).get("killer"), 5)
        if killer != client_on_host:
            raise ctx.failure(f"host records killer {killer:08X}, expected the client's character {client_on_host:08X}")
        log(f"host's wolf killed by the client's character {killer:08X} after {hit} hits")
        ctx.wait_on("client", "the wolf's copy dies too", lambda actors: actors.get(str(copy), {}).get("dead"), 5)
        # The death plays out: the copy's body comes to rest where its owner's lies.
        rest = ctx.wait_for("[host] the wolf's body comes to rest",
                        ctx.comes_to_rest(lambda: ctx.host_world(npc)["placement"]["position"]), 10)
        ctx.wait_on("client", "the copy's body lies with it",
                lambda actors: distance((actors.get(str(copy)) or {}).get("actual") or [1e9] * 3, rest) <= POSITION_TOLERANCE, 3)
        ctx.hold()


    def active_spells(ctx, instance, actor):
        """The spells of the actor's active effects in that game."""
        _game = ctx.game(instance)
        effects = (((_game.read_status() or {}).get("world") or {}).get(str(actor)) or {}).get("effects") or []
        return {e.get("spell") for e in effects}


    def shown_visuals(ctx, instance, form_id):
        """The shaders and art in that game's list on the actor, the finished ones still fading out too: a one-shot
        hit shader is finished from its start ("visuals" leaves those out)."""
        _game = ctx.game(instance)
        return set(((((_game.read_status() or {}).get("world") or {}).get(str(form_id)) or {}).get("mod") or {}).get("visuals_shown") or [])


    def cell_objects(ctx, instance):
        """The doors and containers in that game's cell, by reference id ({"cmd": "objects"})."""
        _game = ctx.game(instance)
        response = _game.rpc({"cmd": "objects"}) or {}
        return {o["id"]: o for o in response.get("objects", [])}


    def inner_door(ctx):
        """A door in the host's cell that is not a load door (QASmoke has two), closed."""
        doors = [o for o in ctx.cell_objects("").values() if o["door"] and not o["load_door"]]
        if not doors:
            raise ctx.failure("no door in the host's cell that stays in it")
        return min(doors, key=lambda o: o["id"])["id"]


    def object_command(ctx, instance, request):
        """{"cmd": "activate"/"lock", ...} in that game; the player does it, as by hand."""
        ctx.player_command(instance, request)
        log(f"[{instance or 'host'}] {request['cmd']} {request['object']:X}")


    def activate_on(ctx, instance, door):
        ctx.object_command(instance, {"cmd": "activate", "object": door})


    def lock_on(ctx, instance, door, locked, level=0):
        ctx.object_command(instance, {"cmd": "lock", "object": door, "locked": locked, "level": level})


    def door_state(ctx, instance, door):
        return ctx.cell_objects(instance).get(door, {}).get("open_state")


    def door_lock(ctx, instance, door):
        o = ctx.cell_objects(instance).get(door, {})
        return (o.get("locked"), o.get("lock_level")) if o.get("has_lock") else (False, None)

