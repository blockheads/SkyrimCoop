"""Crime verbs for repros (docs/re/systems/crime.md): ownership, the party's bounty, what a witness saw."""
import math
from ..comparisons import flatten
from ..utilities import log


class CrimeActions:
    def crime_command(ctx, instance, action, **values):
        return ctx.player_command(instance, {"cmd": "crime", "action": action, **values})

    def party_bounty(ctx):
        """Each game's bounty rows: {"host": {faction: [violent, nonviolent]}, "guest": {...}}."""
        return {label: {f"{row['faction']:X}": [row["violent"], row["nonviolent"]]
                        for row in ctx.crime_command(instance, "read")["bounty"]}
                for label, instance in (("host", ""), ("guest", "client"))}

    def bounty_total(ctx):
        """The host's bounty, every faction, violent + nonviolent."""
        return sum(sum(amounts) for amounts in ctx.party_bounty()["host"].values())

    def report_crimes(ctx):
        """Turns crime reporting on in both games (Game.SetPlayerReportCrime), logging what it was."""
        for instance in ("", "client"):
            log(f"[{instance or 'host'}] crime reporting was {ctx.crime_command(instance, 'read')['reporting']}")
            call = ctx.player_command(instance, {"cmd": "papyrus_call", "class": "Game", "function": "SetPlayerReportCrime",
                                                 "args": [True]})
            ctx.wait_for(f"[{instance or 'host'}] crime reporting is on", lambda: (ctx.player_command(
                instance, {"cmd": "papyrus_result", "id": call["id"]}) or {}).get("done"), 10)
        return {"reporting": True}

    def own(ctx, target, owner):
        """Makes `owner` (a hex base form: an NPC base or a faction) own the handle `target` in both games."""
        for label, instance in (("host", ""), ("guest", "client")):
            ctx.crime_command(instance, "set_owner", target=ctx.resolve(target, label), owner=int(owner, 16))
        return {"owner": owner}

    def witness(ctx, npc, of="guest_player"):
        """What the host's `npc` last computed of a player (host_player or guest_player), read without asking for a
        new detection: known, queued, level, age (seconds since the witness's detection pass)."""
        return ctx.crime_command("", "knowledge", actor=ctx.resolve(npc, "host"), target=ctx.resolve(of, "host"))

    def witness_mood(ctx, npc):
        """The host's `npc`: angry (kAngryWithPlayer), pending_crime, faction_enemy, attack_on_sight and its combat target."""
        actor = ctx.resolve(npc, "host")
        state = ctx.crime_command("", "actor_state", actor=actor)
        combat = ctx.player_command("", {"cmd": "combat_targets", "actor": actor})
        return {**state, "combat_target": flatten(ctx.actor_row("", actor)).get("ai.combat_target"),
                "candidates": [(c["actor"], c["flags"], c["detection"]) for c in combat.get("candidates", [])]}

    def make_angry(ctx, npc):
        """Gives the host's `npc` kAngryWithPlayer, the state an NPC keeps from an earlier fight (crime.md)."""
        ctx.crime_command("", "set_angry", actor=ctx.resolve(npc, "host"))
        return ctx.witness_mood(npc)

    def use_up_warnings(ctx):
        """Spends the party's two theft warnings on the host: a seen theft then alarms and adds bounty at once (crime.md)."""
        ctx.crime_command("", "set_warnings", warnings=2)
        return {"warnings": ctx.crime_command("", "read")["warnings"]}

    def set_warnings(ctx, warnings=0):
        """Sets the party's spent theft warnings on the host (TheftWarnings, crime.md) and reads them back."""
        ctx.crime_command("", "set_warnings", warnings=warnings)
        return ctx.expect_warnings(warnings)

    def expect_warnings(ctx, count, seconds=0.0):
        """The host's spent theft warnings (the party's, 37556) are `count`, within `seconds` (a guest's theft is judged
        0.5 s after it, crime.md)."""
        read = lambda: ctx.crime_command("", "read")["warnings"]
        try:
            if read() != count:
                ctx.wait_for(f"the party has spent {count} theft warnings", lambda: read() == count, seconds)
        except Exception:
            raise ctx.failure(f"the party has spent {read()} theft warnings, expected {count}") from None
        return {"warnings": count}

    def stand_witness(ctx, npc, near="guest_player", gap=180):
        """Moves the host's `npc` `gap` units north of a player, to witness what they do without them walking."""
        ctx.refresh_handles()
        target = ctx.resolve(near, "host")
        if near == "guest_player":  # the host's copy follows a teleport a moment later
            there = lambda: ctx.place_of("client", 0x14)
            ctx.wait_for("the host's copy of the guest reaches the guest",
                         lambda: math.dist(ctx.place_of("", target), there()) < 60, 10)
        ctx.console_on("", f"{ctx.resolve(npc, 'host'):X}.moveto {target:X} 0 {gap} 0")
        return {"near": near}

    def witness_faces(ctx, npc, toward="guest_player"):
        """Turns the host's `npc` to look at `toward` (setangle z: 0 is north, clockwise)."""
        me, other = ctx.resolve(npc, "host"), ctx.resolve(toward, "host")
        here, there = ctx.place_of("", me), ctx.place_of("", other)
        angle = math.degrees(math.atan2(there[0] - here[0], there[1] - here[1])) % 360
        ctx.console_on("", f"{me:X}.setangle z {angle:.0f}")
        return {"angle": round(angle)}

    def wait_seen(ctx, npc, of="guest_player", seconds=30.0):
        """Asks the host's `npc` for its detection of a player until it is above 0. The asking is a request (37764):
        it makes the witness look, so this tests what follows a sighting, not whether one happens unasked."""
        actor, target = ctx.resolve(npc, "host"), ctx.resolve(of, "host")
        level = lambda: ctx.crime_command("", "detection", actor=actor, target=target)["level"]
        ctx.wait_for(f"{npc} sees {of}", lambda: level() > 0, seconds, explain=lambda: str(ctx.witness(npc, of)))
        return ctx.witness(npc, of)

    def expect_bounty(ctx, at_least=1, seconds=10.0):
        """The party's bounty (every faction, violent + nonviolent) reaches `at_least` in both games within `seconds`."""
        total = lambda rows: sum(sum(amounts) for amounts in rows.values())
        try:
            ctx.wait_for(f"the party's bounty reaches {at_least} in both games",
                         lambda: all(total(rows) >= at_least for rows in ctx.party_bounty().values()), seconds)
        except Exception:
            raise ctx.failure(f"the party's bounty is {ctx.party_bounty()}, expected at least {at_least} in both games") from None
        return ctx.party_bounty()

    def hud_mark(ctx):
        """How many HUD lines each game has shown so far: pass it to expect_bounty_message to count only later ones."""
        return {label: ctx.player_command(instance, {"cmd": "hud_messages"}).get("shown", 0)
                for label, instance in (("host", ""), ("guest", "client"))}

    def hud_since(ctx, instance, mark):
        """The HUD lines `instance` showed after `mark` (its count from hud_mark) still in the harness's last 32."""
        reply = ctx.player_command(instance, {"cmd": "hud_messages"})
        messages, shown = reply.get("messages") or [], reply.get("shown", 0)
        return messages[len(messages) - min(len(messages), max(0, shown - mark)):]

    def expect_bounty_message(ctx, mark, seconds=10.0):
        """A bounty HUD line ("<n> <sAddCrimeGold> <hold>", 37679) the host shows after `mark` (hud_mark) shows on the
        guest too, also after `mark`, within `seconds`: every player hears the party's bounty."""
        bounty_lines = lambda lines: [line for line in lines if line.split(" ", 1)[0].isdigit()]
        ctx.wait_for("the host shows a new bounty line", lambda: bounty_lines(ctx.hud_since("", mark["host"])), seconds)
        line = bounty_lines(ctx.hud_since("", mark["host"]))[-1]
        try:
            ctx.wait_for(f"the guest shows '{line}'", lambda: line in ctx.hud_since("client", mark["guest"]), seconds)
        except Exception:
            raise ctx.failure(f"the guest's new HUD lines are {ctx.hud_since('client', mark['guest'])}, "
                              f"expected '{line}'") from None
        return {"line": line}

    def expect_target(ctx, npc, player="guest_player", seconds=10.0):
        """The host's `npc` targets `player` (combat or package target) within `seconds`: a crime's reaction goes to
        the player who did it."""
        ctx.refresh_handles()
        wanted = ctx.resolve(player, "host")
        read = lambda: flatten(ctx.actor_row("", ctx.resolve(npc, "host")))
        try:
            ctx.wait_for(f"{npc} targets {player}",
                         lambda: wanted in (read().get("ai.combat_target"), read().get("ai.package_target")), seconds)
        except Exception:
            row = read()
            raise ctx.failure(f"{npc}: combat target {row.get('ai.combat_target')}, package target "
                              f"{row.get('ai.package_target')} ({row.get('ai.running_package')}), expected "
                              f"{player} {wanted}") from None
        return {"target": wanted}

    def stop_scene(ctx, scene):
        """Stops the host's scene `scene` (hex form id, Papyrus Scene.Stop) and waits until it is not playing: a scene
        replaces an actor's created package, the steal warning's too (crime.md)."""
        form = int(scene, 16)
        def call(function):
            started = ctx.player_command("", {"cmd": "papyrus_call", "class": "Scene", "function": function,
                                              "self": form, "args": []})
            reply = ctx.wait_for(f"Scene.{function} on {scene} returns", lambda: (ctx.player_command(
                "", {"cmd": "papyrus_result", "id": started["id"]}) or {}).get("done") and ctx.player_command(
                "", {"cmd": "papyrus_result", "id": started["id"]}), 10)
            return reply.get("value")
        call("Stop")
        ctx.wait_for(f"scene {scene} stopped", lambda: call("IsPlaying") is False, 10)
        return {"scene": scene, "playing": False}

    def expect_given_back(ctx, item, to, player="guest_player", bounty=0, seconds=30.0):
        """The steal warning ends as vanilla's does (procedure 40, crime.md): within `seconds` `player`'s game carries
        none of `item` (hex base) and the host's `to` (the owner, or its merchant chest: a handle) holds it, with the
        party's bounty still `bounty`."""
        base = int(item, 16)
        thief = ("", 0x14) if player == "host_player" else ("client", 0x14)
        npc = to
        owner = ctx.resolve(to, "host")
        carried = lambda: ctx.item_count(thief[0], thief[1], base) or 0
        held = lambda: ctx.item_count("", owner, base) or 0
        try:
            ctx.wait_for(f"{player} gave {item} back to {npc}", lambda: carried() == 0 and held() > 0, seconds)
        except Exception:
            raise ctx.failure(f"{player} carries {carried()} of {item} and {npc} holds {held()}, expected 0 and "
                              f"at least 1: the warning did not take it back") from None
        if ctx.bounty_total() != bounty:
            raise ctx.failure(f"the party's bounty is {ctx.party_bounty()}, expected {bounty}: the warning reported it")
        return {"carried": 0, "held": held()}
