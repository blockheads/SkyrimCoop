"""Scenarios ai for the co-op harness."""
from ..common import (
    POTENTIAL_MARRIAGE,
    SANDBOX_1024,
    SANDBOX_256,
    VILKAS,
)
from ..errors import TestFailure


def scenario_copy_runs_owners_package(ctx):
    """The host's Vilkas runs one package, then another (staged: frozen, the package put in place as his evaluation
    would). The client's copy runs the same one each time: its package field and the condition GetIsCurrentPackage,
    which dialogue lines read on the speaker, answer as on the host, and its running slot, which plugins (Open
    Animation Replacer) read inline, holds it (copy-contract.md, "AI state")."""
    try:
        ctx.stage_host_npc(VILKAS)
        for package in (SANDBOX_256, SANDBOX_1024):
            ctx.player_command("", {"cmd": "put_package", "actor": VILKAS, "package": package})
            if not ctx.is_current_package("", VILKAS, package):
                raise ctx.failure(f"[host] Vilkas does not run {package:X} after it was put in place")
            try:
                ctx.wait_for(f"[client] its Vilkas runs {package:X}", lambda: ctx.ai_state("client", VILKAS).get("package") == package
                         and ctx.is_current_package("client", VILKAS, package) and ctx.running_package("client", VILKAS) == package, 3)
            except TestFailure:
                raise ctx.failure(f"[client] its Vilkas does not run the host's {package:X}: package "
                                  f"{ctx.ai_state('client', VILKAS).get('package', 0):X}, GetIsCurrentPackage "
                                  f"{ctx.is_current_package('client', VILKAS, package)}, running slot "
                                  f"{ctx.running_package('client', VILKAS):X}")
    finally:
        ctx.release_host_npc(VILKAS)

def scenario_copy_in_owners_combat(ctx):
    """The host's Vilkas (staged, his AI on: combat needs it) starts combat with the host's player, then stops. On the client his copy, which has no combat controller, answers IsInCombat and GetCombatState
    as the host's Vilkas does (copy-contract.md, "AI state"). Its combat target is not checked: the engine clears the
    field on a copy, which its own callers see out of combat (progress.md, overnight slice 3)."""
    try:
        ctx.stage_host_npc(VILKAS)
        ctx.freeze("", VILKAS, False)  # combat needs his AI (restrained, he does not start it either)
        ctx.console_on("", f"{VILKAS:X}.startcombat player")
        ctx.wait_for("[host] Vilkas is in combat", lambda: ctx.ai_state("", VILKAS).get("in_combat"), 5)
        host_player = ctx.other_player(ctx.actors_on("client"))
        try:
            ctx.wait_for("[client] its Vilkas is in combat with the host's character",
                     lambda: (s := ctx.ai_state("client", VILKAS)).get("in_combat") and s.get("combat") == ctx.ai_state("", VILKAS).get("combat"), 3)
        except TestFailure:
            raise ctx.failure(f"[client] its Vilkas is not in the host's combat: {ctx.ai_state('client', VILKAS)} "
                              f"(host {ctx.ai_state('', VILKAS)}, the host's character here {host_player:X})")
        ctx.console_on("", f"{VILKAS:X}.stopcombat")
        ctx.wait_for("[host] Vilkas is out of combat", lambda: not ctx.ai_state("", VILKAS).get("in_combat"), 5)
        ctx.wait_for("[client] its Vilkas is out of combat", lambda: not ctx.ai_state("client", VILKAS).get("in_combat"), 3)
    finally:
        ctx.freeze("", VILKAS)  # first: thawed, he takes up the fight with the host's player again after a stopcombat
        for command in (f"{VILKAS:X}.stopcombat", "player.stopcombat"):
            ctx.console_on("", command)
        ctx.release_host_npc(VILKAS)

def scenario_guest_player_in_hosts_combat(ctx):
    """The host's Vilkas (staged, his AI on) starts combat with the client's character, then stops. The client's
    player counts as in combat while he fights it, as a player does from the combat groups targeting it (41238):
    the host's NPCs are copies on the client, with no combat groups, so their AI state counts instead."""
    try:
        ctx.stage_host_npc(VILKAS)
        ctx.freeze("", VILKAS, False)
        client_character = ctx.client_on_host()
        ctx.console_on("", f"{VILKAS:X}.startcombat {client_character:X}")
        ctx.wait_for("[host] Vilkas fights the client's character",
                 lambda: ctx.ai_state("", VILKAS).get("combat_target") == client_character, 5)
        try:
            ctx.wait_for("[client] the player is in combat", lambda: ctx.ai_state("client", 0x14).get("in_combat"), 3)
        except TestFailure:
            raise ctx.failure(f"[client] the player is not in combat: its Vilkas {ctx.ai_state('client', VILKAS)}")
        ctx.console_on("", f"{VILKAS:X}.stopcombat")
        ctx.wait_for("[client] the player is out of combat", lambda: not ctx.ai_state("client", 0x14).get("in_combat"), 5)
    finally:
        ctx.freeze("", VILKAS)  # first: thawed, he takes up the fight with the host's player again after a stopcombat
        for command in (f"{VILKAS:X}.stopcombat", "player.stopcombat"):
            ctx.console_on("", command)
        ctx.console_on("client", "player.stopcombat")
        ctx.release_host_npc(VILKAS)

def scenario_guest_player_in_switched_combat(ctx):
    """The host's Vilkas (staged, his AI on) fights the host's player, then turns on the client's character without
    leaving combat: both are his targets, and the host's player steps 1500 units away (the closer one wins). A switch
    inside a fight sends no combat event (TESCombatEvent is a state change): the host's combat controller setting
    its target (33235) says so. The client's player counts as in combat from then on."""
    try:
        ctx.stage_host_npc(VILKAS)
        ctx.freeze("", VILKAS, False)
        client_character = ctx.client_on_host()
        ctx.console_on("", f"{VILKAS:X}.startcombat player")
        ctx.wait_for("[host] Vilkas fights the host's player", lambda: ctx.ai_state("", VILKAS).get("combat_target") == 0x14, 5)
        ctx.wait_for("[client] its Vilkas is in combat", lambda: ctx.ai_state("client", VILKAS).get("in_combat"), 3)
        if ctx.ai_state("client", 0x14).get("in_combat"):
            raise ctx.failure("[client] the player is in combat while Vilkas fights the host's player")
        ctx.console_on("", f"{VILKAS:X}.startcombat {client_character:X}")  # his second target; he keeps the first
        ctx.console_on("", f"player.setpos x {ctx.actor_place('', 0x14)['position'][0] + 1500:.0f}")
        ctx.wait_for("[host] Vilkas turns on the client's character",
                 lambda: ctx.ai_state("", VILKAS).get("combat_target") == client_character, 5)
        try:
            ctx.wait_for("[client] the player is in combat", lambda: ctx.ai_state("client", 0x14).get("in_combat"), 3)
        except TestFailure:
            raise ctx.failure(f"[client] the player is not in combat after the switch: its Vilkas {ctx.ai_state('client', VILKAS)} "
                              f"(host {ctx.ai_state('', VILKAS)})")
        ctx.console_on("", f"{VILKAS:X}.stopcombat")
        ctx.wait_for("[client] the player is out of combat", lambda: not ctx.ai_state("client", 0x14).get("in_combat"), 5)
    finally:
        ctx.freeze("", VILKAS)  # first: thawed, he takes up the fight with the host's player again after a stopcombat
        for command in (f"{VILKAS:X}.stopcombat", "player.stopcombat"):
            ctx.console_on("", command)
        ctx.console_on("client", "player.stopcombat")
        ctx.release_host_npc(VILKAS)

def scenario_copy_holds_owners_factions(ctx):
    """The host's Vilkas is given a rank in a faction, another, and leaves it (the console's AddToFaction, SetFactionRank,
    RemoveFromFaction). The client's copy answers GetFactionRank as he does each time: the owner sends its factions
    when the engine writes them, with no periodic check."""
    try:
        copy = ctx.stage_host_npc(VILKAS)
        # He is in it from his base at rank 0; out of it, the condition answers below 0 (-2 for a base faction removed).
        for command, member in ((f"addtofaction {POTENTIAL_MARRIAGE:X} 1", 1), (f"setfactionrank {POTENTIAL_MARRIAGE:X} 3", 3),
                                (f"removefromfaction {POTENTIAL_MARRIAGE:X}", None)):
            ctx.console_on("", f"{VILKAS:X}.{command}")
            rank = ctx.faction_rank("", VILKAS, POTENTIAL_MARRIAGE)
            if (rank != member) if member is not None else rank >= 0:
                raise ctx.failure(f"[host] Vilkas's rank is {rank} after {command}")
            try:
                ctx.wait_for(f"[client] its Vilkas has rank {rank}", lambda: ctx.faction_rank("client", copy, POTENTIAL_MARRIAGE) == rank, 3)
            except TestFailure:
                raise ctx.failure(f"[client] its Vilkas has rank {ctx.faction_rank('client', copy, POTENTIAL_MARRIAGE)} after the "
                                  f"host's {command}, not {rank}")
    finally:
        ctx.console_on("", f"{VILKAS:X}.setfactionrank {POTENTIAL_MARRIAGE:X} 0")  # as his base has him
        ctx.release_host_npc(VILKAS)
