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
from ..comparisons import is_close
from ..common import STAGE_DEPTH
from ..errors import TestFailure
from ..comparisons import distance


class ActorsActions:
    def host_npc_state(ctx, npc):
        return ctx.local_player("").get("local_actors", {}).get(str(npc), {})


    def host_world(ctx, form_id):
        _game = ctx.game('')
        return ((_game.read_status() or {}).get("world") or {}).get(str(form_id)) or {}


    def copy_of(ctx, instance, session_id, timeout=15):
        """The form id of the copy, in that game, of the actor with this session id."""
        return ctx.wait_on(instance, f"shows server actor {session_id:X}",
                       lambda actors: next((int(a) for a, s in actors.items()
                                            if s.get("session_id") == session_id and s.get("has_3d")), None), timeout)


    def stage_spot(ctx):
        """Where scenarios put their NPC: the middle of the hall behind the host (the two home spots are
        in an archway, walls on both sides). The client then stands between the host and the NPC, so
        nothing is in its line of fire. (`placeatme` drops an NPC wherever the navmesh lets it, in
        QASmoke well out of sight.)"""
        host, client = ctx.load_home("")["position"], ctx.load_home("client")["position"]
        dx, dy = host[0] - client[0], host[1] - client[1]
        length = math.hypot(dx, dy) or 1.0
        return [host[0] + dx / length * STAGE_DEPTH, host[1] + dy / length * STAGE_DEPTH, host[2]]


    def spawn_host_npc(ctx, base, restrained=False):
        """The host spawns an NPC on the stage: (host ref id, session id, the client's copy).
        Restrained, it stays there (an NPC otherwise walks up to greet the players)."""
        before = set(ctx.local_player("").get("local_actors", {}))
        ctx.console_on("", f"player.placeatme {base} 1")
        npc = int(next(iter(ctx.wait_for("[host] new NPC loaded",
                                     lambda: set(ctx.local_player("").get("local_actors", {})) - before, 15))))
        # Unassigned, it reads -1.
        session_id = ctx.wait_for("[host] NPC has a session id",
                              lambda: (sid := ctx.host_npc_state(npc).get("session_id")) is not None and sid >= 0 and (sid,), 10)[0]
        if restrained:
            ctx.console_on("", f"{npc:08X}.setrestrained 1")
        goal = ctx.stage_spot()
        ctx.place("", f"{npc:08X}", goal, ctx.host_npc_state(npc)["position"])
        ctx.wait_for("[host] NPC on the stage", lambda: distance(ctx.host_npc_state(npc)["position"][:2], goal[:2]) < 40
                 and abs(ctx.host_npc_state(npc)["position"][2] - goal[2]) < 60, 10)
        copy = ctx.copy_of("client", session_id)
        ctx.wait_on("client", "its copy on the stage",
                lambda actors: distance((actors.get(str(copy), {}).get("actual") or [1e9, 1e9])[:2], goal[:2]) < 60, 10)
        return npc, session_id, copy


    def spawn_beside_host(ctx, base):
        """The host spawns an NPC where it stands, in any cell (spawn_host_npc's stage is the home cell's); returns it once
        it has a session id and the client shows its copy."""
        before = set(ctx.local_player("").get("local_actors", {}))
        ctx.console_on("", f"player.placeatme {base} 1")
        npc = int(next(iter(ctx.wait_for("[host] the new NPC spawns", lambda: set(ctx.local_player("").get("local_actors", {})) - before, 15))))
        session_id = ctx.wait_for("[host] the new NPC gets a session id",
                              lambda: (sid := ctx.host_npc_state(npc).get("session_id")) is not None and sid >= 0 and (sid,), 10)[0]
        ctx.copy_of("client", session_id)
        return npc


    def freeze(ctx, instance, actor, frozen=True):
        """Freezes (or thaws) the actor in that game: no AI, not pushed, players pass through it; it still takes hits."""
        ctx.player_command(instance, {"cmd": "freeze", "actor": actor, "frozen": frozen})


    def stage_host_npc(ctx, npc):
        """Brings one of the host's placed NPCs (wherever it is, in furniture or not) onto the stage and freezes it there;
        returns the client's copy. release_host_npc sends it back."""
        goal = ctx.stage_spot()
        # Its own package can bring it into the cell elsewhere (an unloaded actor travelling to the player arrives at
        # the door): it is frozen once here, then moved within the cell.
        ctx.player_command("", {"cmd": "move_actor", "actor": npc, "position": goal})
        ctx.wait_for(f"[host] {npc:X} is in the home cell", lambda: ctx.host_npc_state(npc).get("session_id", -1) >= 0, 10)
        ctx.freeze("", npc)
        ctx.player_command("", {"cmd": "move_actor", "actor": npc, "position": goal})
        try:
            ctx.wait_for(f"[host] {npc:X} stands on the stage", lambda: distance(ctx.host_npc_state(npc)["position"][:2], goal[:2]) < 40, 5)
        except TestFailure:
            raise ctx.failure(f"[host] {npc:X} is not on the stage {[round(x) for x in goal]}: {ctx.host_world(npc).get('placement')}")
        ctx.freeze("", npc)  # again: its character controller exists now
        copy = ctx.copy_of("client", ctx.host_npc_state(npc)["session_id"])
        ctx.wait_on("client", f"its copy of {npc:X} on the stage",
                lambda actors: distance((actors.get(str(copy), {}).get("actual") or [1e9, 1e9])[:2], goal[:2]) < 60, 10)
        return copy


    def release_host_npc(ctx, npc):
        """Sends a staged NPC back where the game placed it and thaws it; the client's (its own reference, shown as a
        copy) follows the host's there. Thawed once there: an unloaded actor's AI walks it on in jumps."""
        ctx.player_command("", {"cmd": "move_actor", "actor": npc, "home": True})
        ctx.wait_for(f"[host] {npc:X} has gone home", lambda: not ctx.host_npc_state(npc), 10)
        try:
            ctx.copy_follows_host(npc, "goes home with the host's")
        finally:
            ctx.freeze("", npc, False)


    def actor_place(ctx, instance, actor):
        """Where that game has the actor, loaded or not ({} if it has none): cell, worldspace, position, has_3d,
        disabled, deleted, and whether it is a copy there."""
        _game = ctx.game(instance)
        response = _game.rpc({"cmd": "actor_place", "actor": actor}) or {}
        return response if response.get("ok") else {}


    def copy_follows_host(ctx, npc, what, timeout=10):
        """The client's reference of the host's NPC is where the host's is (its cell, within 64 units) and as enabled,
        and still a copy: never left behind as the client's own (copy-contract.md, "When the owner's actor goes away")."""
        def follows():
            host, client = ctx.actor_place("", npc), ctx.actor_place("client", npc)
            return (host and client and client["copy"] and client["cell"] == host["cell"]
                    and distance(client["position"], host["position"]) < 64 and client["disabled"] == host["disabled"]
                    and client)
        try:
            return ctx.wait_for(f"[client] its {npc:X} {what}", follows, timeout)
        except TestFailure:
            raise ctx.failure(f"[client] its {npc:X} did not follow the host's ({what}): host {ctx.actor_place('', npc)}, "
                              f"client {ctx.actor_place('client', npc)}")


    def remove_host_npc(ctx, npc):
        """Takes a spawned NPC out of the scene for good, out of the follower slot first (a follower picked "Follow me").
        First every copy must match its owner: the run's own desync check comes after the scenario has removed its NPCs."""
        if not ctx.FAILING and (problems := ctx.check_desync()):
            raise ctx.failure(f"desync before removing {npc:08X}: " + " | ".join(problems))
        ctx.dismiss_follower(npc)
        ctx.console_on("", f"{npc:08X}.disable")
        ctx.console_on("", f"{npc:08X}.markfordelete")


    def host_npc_health(ctx, npc):
        return ctx.host_npc_state(npc).get("health")


    def hit_lands(ctx, npc, before, what, timeout=2.0):
        """The host's real NPC loses health within `timeout`: the hit landed and its owner applied it."""
        return ctx.wait_for(f"[host] {what} lands", lambda: (h := ctx.host_npc_health(npc)) is not None and h < before - 0.5 and h,
                        timeout)


    def copy_health_matches(ctx, npc, copy):
        ctx.wait_on("client", "the copy shows the host NPC's health",
                lambda actors: is_close(actors.get(str(copy), {}).get("health"), ctx.host_npc_health(npc)), 5)


    def health_bar_shows(ctx, instance, target):
        """The attacker's HUD shows its target's health bar (hit feedback is the attacker's own; the
        owner applies the hit)."""
        ctx.wait_for(f"[{instance or 'host'}] HUD shows {target:X}'s health bar",
                 lambda: ctx.local_player(instance).get("enemy_health_target") == target, 5)


    def visuals(ctx, instance, form_id):
        """The effect shaders and art the engine plays on that actor in that game."""
        _game = ctx.game(instance)
        return set((((_game.read_status() or {}).get("world") or {}).get(str(form_id)) or {}).get("visuals") or [])


    def game_state(ctx, instance):
        _game = ctx.game(instance)
        return (_game.read_status() or {}).get("game") or {}


    def session_players(ctx, instance):
        """{player name: is host} for the other players this game lists."""
        return {p["name"]: p["host"] for p in ctx.game_state(instance).get("players", [])}


    def disconnect_client(ctx):
        """The client leaves the session like a player quitting."""
        ctx.player_command("client", {"cmd": "disconnect"})
        ctx.wait_for("[client] disconnected", lambda: ctx.game_state("client").get("online") is False, 10)


    def status_table(ctx):
        """One row per character, as each game sees it: owner's values next to the copies'."""
        _game = ctx.game()
        games = {}
        for name in ("", "client"):
            _game = ctx.game(name)
            games[name or "host"] = _game.read_status() or {}
        host, client = games["host"], games["client"]
        rows = []

        def describe(entry):
            if not entry:
                return "-"
            hp = entry.get("health")
            parts = [f"hp {hp:5.0f}" if isinstance(hp, (int, float)) else "hp     ?"]
            if entry.get("dead"):
                parts.append("DEAD")
            if entry.get("weapon_drawn"):
                parts.append("drawn")
            if any(entry.get("casting") or []):
                parts.append("casting")
            return " ".join(parts)

        host_player = host.get("player") or {}
        client_player = client.get("player") or {}
        host_on_client = (client.get("actors") or {}).get(str(ctx.other_player(client.get("actors") or {})))
        client_on_host = (host.get("actors") or {}).get(str(ctx.other_player(host.get("actors") or {})))
        rows.append(("host's character", describe(host_player), describe(host_on_client)))
        rows.append(("client's character", describe(client_on_host), describe(client_player)))
        # Pair each host NPC with the client copy linked to the same server actor; anything left
        # unpaired on either side is a desync (a copy the host no longer has, or a missing copy).
        copies = {v.get("session_id"): v for v in (client.get("actors") or {}).values() if not v.get("other_player")}
        for npc, owned in sorted((host_player.get("local_actors") or {}).items()):
            copy = copies.pop(owned.get("session_id"), None)
            gap = f"  {distance(owned['position'], copy['actual']):.0f} apart" if copy and owned.get("position") else ""
            mine, theirs = owned.get("identity") or {}, (copy or {}).get("identity") or {}
            # Same name and race, or the copy is a different-looking actor (built from the wrong record).
            wrong = "" if not copy or (mine.get("name"), mine.get("race")) == (theirs.get("name"), theirs.get("race")) \
                else f" WRONG-ACTOR({theirs.get('name') or '?'})"
            label = f"NPC {mine.get('name') or ''} {int(npc):08X}".replace("  ", " ")[:20]
            rows.append((label, describe(owned), (describe(copy) + wrong if copy else "MISSING") + gap))
        for session_id, copy in copies.items():
            rows.append((f"NPC server {session_id}", "GONE on host", describe(copy)))
        lines = [f"{'':20s} {'HOST game':24s} {'CLIENT game':24s}"]
        lines += [f"{who:20s} {h:24s} {c:24s}" for who, h, c in rows]
        return "\n".join(lines)

