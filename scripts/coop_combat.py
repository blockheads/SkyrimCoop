"""Native melee reactions on the victim's owner."""
import functools

import coop_test as ct


def melee_bash(attacker):
    """A real shield bash on the other player's copy staggers its owner and sends damage only once."""
    victim = "" if attacker else "client"
    shield = 0x12EB6
    held = ct.item_count(attacker, 0x14, shield)
    try:
        for game in ("", "client"):
            ct.console_on(game, "player.restoreav health 10000")
        ct.give_sword(attacker)
        if not held:
            ct.console_on(attacker, f"player.additem {shield:X} 1")
        ct.console_on(attacker, f"player.equipitem {shield:X}")
        ct.draw(attacker)
        copy = ct.player_copy(attacker)
        ct.place_before(attacker, copy, ct.MELEE_GAP)
        before = ct.player_health(victim)
        ct.press(attacker, "left")
        ct.wait_for("the attacker blocks with its shield", lambda:
                    ct.graph_bools(attacker, "IsBlocking")["IsBlocking"], 5)
        ct.press(attacker, "right")
        ct.wait_for("the victim's own game plays the bash stagger", lambda:
                    ct.graph_bools(victim, "IsStaggering")["IsStaggering"], 5)
        ct.release(attacker, "right")
        ct.release(attacker, "left")
        ct.wait_for("the victim's own game takes the bash damage", lambda:
                    ct.player_health(victim) < before, 5)
        ct.wait_for("the stagger finishes on its owner", lambda:
                    not ct.graph_bools(victim, "IsStaggering")["IsStaggering"], 10)
    finally:
        ct.release(attacker, "right")
        ct.release(attacker, "left")
        ct.sheathe(attacker)
        ct.console_on(victim, "player.restoreav health 10000")
        if not held:
            ct.console_on(attacker, f"player.removeitem {shield:X} 1")


def revived_player_knockdown():
    """The guest can be knocked down before and after its real death and party revival."""
    ct.scenario_guest_knockdown_lies_on_host()
    ct.scenario_spectator_watches_kill_move()
    ct.scenario_guest_knockdown_lies_on_host()


def register(scenarios, tags):
    scenarios["revived-player-can-be-knocked-down"] = revived_player_knockdown
    tags["revived-player-can-be-knocked-down"] = {"combat", "death", "physics"}
    for role, game in (("host", ""), ("guest", "client")):
        name = f"melee-bash-staggers-{role}s-victim"
        scenarios[name] = functools.partial(melee_bash, game)
        tags[name] = {"combat", "animation", "inventory"}
