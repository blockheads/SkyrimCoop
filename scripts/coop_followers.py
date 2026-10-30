"""Shared vanilla followers: native recruitment, pathing, commands and member fallback."""
from contextlib import contextmanager
import math
import coop_test as ct

RECRUIT, FOLLOW, WAIT, DISMISS, TRADE = 0xB0EE6, 0x75083, 0x75084, 0x5C80C, 0x60020


alias = ct.follower_in_slot


def go_home():
    if any(ct.player_cell(game) != 0x32AE7 for game in ("", "client")):
        ct.both_go(ct.HOME_CELL)


def choose(game, npc, copy, topic):
    ct.close_dialogues()
    target = copy if game else npc
    ct.place_before(game, target, 110)
    ct.activate_on(game, target)
    ct.wait_for("the follower dialogue opens for its picker", lambda: ct.talking_to(game, target), 10)
    ct.wait_for(f"the follower offers topic {topic:X}", lambda: topic in {r["topic"] for r in ct.dialogue(game).get("options", [])}, 10)
    ct.pick_with_keys(game, topic)
    if topic in (WAIT, FOLLOW):
        expected = 1 if topic == WAIT else 0
        ct.wait_for("the follower's command completes", lambda: ct.actor_value("", npc, 95) == expected, 10)


@contextmanager
def recruited(game="client", base=None, slot=0, topic=RECRUIT, outdoors=False, reference=None, cell=None):
    ct.close_dialogues()
    if cell:
        ct.both_go(cell)
    elif outdoors:
        ct.both_go("Whiterun plains", "cow Tamriel 0 -4")
    else:
        go_home()
    if alias(slot):
        raise ct.TestFailure("the follower fixture needs the shared vanilla slot empty")
    original = ct.actor_place("", reference) if reference else None
    if reference:
        npc = reference
        ct.console_on("", f"{npc:X}.enable")
        ct.player_command("", {"cmd": "move_actor", "actor": npc, "position": ct.my_position("")})
    else:
        before = set(ct.local_player("").get("local_actors", {}))
        ct.console_on("", f"player.placeatme {base or ct.LYDIA} 1")
        npc = int(next(iter(ct.wait_for("the follower NPC loads near the party", lambda: set(ct.local_player("").get("local_actors", {})) - before, 15))))
    sid = ct.wait_for("the follower gets a session id", lambda: (value := ct.host_npc_state(npc).get("session_id")) is not None and value >= 0 and (value,), 10)[0]
    copy = ct.copy_of("client", sid)
    ct.wait_for("the follower and its copy finish fading in", lambda: all(ct.actor_state(g, f).get("mod", {}).get("fade_state") == 0 for g, f in (("", npc), ("client", copy))), 10)
    try:
        if topic is None:  # WEDogFollowerScript recruits on activation, without a dialogue menu.
            target = copy if game else npc
            ct.place_before(game, target, 110)
            ct.activate_on(game, target)
        else:
            choose(game, npc, copy, topic)
        ct.wait_for("vanilla recruitment fills the shared follower alias", lambda: alias(slot) == npc, 10)
        ct.close_dialogues()
        yield npc, sid, copy
    finally:
        for game in ("client", ""):
            if "ContainerMenu" in ct.open_menus(game):
                ct.tap_key(game, ct.TAB_KEY)
                ct.wait_for("the follower trade menu closes for cleanup", lambda game=game: "ContainerMenu" not in ct.open_menus(game), 10)
        if not ct.game_state("client").get("online"):
            ct.rejoin_client()
        ct.close_dialogues()
        ct.dismiss_follower(npc, slot)
        if reference:
            ct.player_command("", {"cmd": "move_actor", "actor": npc, "home": True})
            if original["disabled"]:
                ct.console_on("", f"{npc:X}.disable")
        else:
            ct.remove_host_npc(npc)
        go_home()


def follows(npc, member, timeout=15):
    def matches():
        expected = member() if callable(member) else member
        return bool(expected) and ct.actor_state("", npc).get("ai", {}).get("follow_target") == expected
    ct.wait_for("the follower's native pathing target is the requested member", matches, timeout)


def guest_recruits():
    """The guest's real recruitment dialogue makes Lydia follow its host-side player copy and move toward it."""
    with recruited(outdoors=True) as (npc, sid, copy):
        follows(npc, lambda: ct.player_copy(""))
        before = ct.actor_state("", npc)["placement"]["position"]
        start = ct.my_position("client")
        way = ct.clear_way("client", [0, 90, 180, 270], 650)
        ct.console_on("client", f"player.setangle z {way}")
        ct.press("client", "forward")
        try:
            ct.wait_for("the followed guest walks away", lambda: math.dist(ct.my_position("client"), start) > 600, 15)
        finally:
            ct.release("client", "forward")
        ct.wait_for("Lydia physically walks after the guest", lambda: math.dist(ct.actor_state("", npc)["placement"]["position"], before) > 150, 15)
        ct.wait_for("the follower copy reaches its owner's walking position",
                    lambda: math.dist(ct.actor_state("client", copy)["placement"]["position"],
                                      ct.actor_state("", npc)["placement"]["position"]) < 100, 10)


def switches_anchor():
    """Anyone can ask the shared follower to wait; the next Follow command changes its physical anchor."""
    with recruited() as (npc, sid, copy):
        follows(npc, lambda: ct.player_copy(""))
        choose("", npc, copy, WAIT)
        ct.wait_for("the host's wait command takes effect", lambda: ct.actor_value("", npc, 95) == 1, 10)
        choose("", npc, copy, FOLLOW)
        follows(npc, 0x14)
        choose("client", npc, copy, WAIT)
        choose("client", npc, copy, FOLLOW)
        follows(npc, lambda: ct.player_copy(""))
        choose("client", npc, copy, DISMISS)
        ct.wait_for("the guest can dismiss the shared follower", lambda: alias() != npc, 15)


def trade_to_picker():
    """The follower's inventory opens on the picker, whose own inventory receives the item without changing the anchor."""
    with recruited() as (npc, sid, copy):
        follows(npc, lambda: ct.player_copy(""))
        ct.console_on("", f"{npc:X}.removeallitems")
        ct.console_on("", f"{npc:X}.additem A 3")
        ct.wait_for("the guest sees the follower's trade items", lambda: ct.item_count("client", copy, 0xA) == 3, 10)
        before = {game: ct.item_count(game, 0x14, 0xA) for game in ("", "client")}
        try:
            choose("client", npc, copy, TRADE)
            ct.wait_for("only the trade picker gets the follower container menu",
                        lambda: "ContainerMenu" in ct.open_menus("client"), 10)
            if "ContainerMenu" in ct.open_menus(""):
                raise ct.TestFailure("the guest's trade opened the host's inventory menu")
            ct.wait_for("the follower trade list takes input", lambda: ct.menu_var("client", "ContainerMenu", ct.ITEM_LISTS + ".currentState") in ct.LISTS_SHOWN, 5)
            ct.select_entry("client", "ContainerMenu", ct.ITEM_LISTS + ".ItemsList", "Lockpick")
            ct.tap_key("client", ct.ACTIVATE_KEY)
            ct.wait_for("the guest receives the traded lockpicks", lambda: ct.item_count("client", 0x14, 0xA) > before["client"], 10)
            received = ct.item_count("client", 0x14, 0xA) - before["client"]
            ct.wait_for("the follower loses exactly those lockpicks", lambda: ct.item_count("", npc, 0xA) == 3 - received, 10)
            if ct.item_count("", 0x14, 0xA) != before[""]:
                raise ct.TestFailure("the guest's follower trade changed the host's personal inventory")
            ct.tap_key("client", ct.TAB_KEY)
            ct.wait_for("the guest closes its follower trade", lambda: "ContainerMenu" not in ct.open_menus("client"), 10)
            follows(npc, lambda: ct.player_copy(""))
            choose("", npc, copy, TRADE)
            ct.wait_for("the host can trade after the guest closes", lambda: "ContainerMenu" in ct.open_menus(""), 10)
            ct.wait_for("the host's follower inventory takes input", lambda: ct.menu_var("", "ContainerMenu", ct.ITEM_LISTS + ".currentState") in ct.LISTS_SHOWN, 5)
            ct.tap_key("", ct.TAB_KEY)
            ct.wait_for("the host closes its follower trade", lambda: "ContainerMenu" not in ct.open_menus(""), 10)
            follows(npc, lambda: ct.player_copy(""))
        finally:
            for game, wanted in before.items():
                have = ct.item_count(game, 0x14, 0xA)
                if have > wanted:
                    ct.console_on(game, f"player.removeitem A {have - wanted}")


def disconnected_anchor():
    """A disconnected guest's follower uses the host, then resolves the returning member's replacement copy."""
    with recruited() as (npc, sid, copy):
        follows(npc, lambda: ct.player_copy(""))
        ct.disconnect_client()
        follows(npc, 0x14)
        ct.rejoin_client()
        follows(npc, lambda: ct.player_copy(""))


def dead_anchor():
    """The follower switches to the standing host while its guest anchor is dead and returns after recovery."""
    with recruited() as (npc, sid, copy):
        follows(npc, lambda: ct.player_copy(""))
        ct.console_on("client", "player.kill")
        ct.wait_for("the followed guest is dead", lambda: ct.is_dead(ct.player_snapshot("client")), 10)
        follows(npc, 0x14)
        ct.wait_for("the guest recovers beside the standing party", lambda: not ct.is_dead(ct.player_snapshot("client")), 30)
        follows(npc, lambda: ct.player_copy(""))


def saved_anchor(reference=None):
    """The host save restores the guest's desired anchor and rebinds the follower to the new player copy."""
    with recruited(reference=reference) as (npc, sid, copy):
        follows(npc, lambda: ct.player_copy(""))
        ct.console_on("", "set gamehour to 12")
        saved = ct.save_game("", "FollowerAnchorWorld")
        ct.wait_for("the host saves follower anchors with its world", ct.saves_written(saved), 15)
        choose("", npc, copy, WAIT)
        choose("", npc, copy, FOLLOW)
        follows(npc, 0x14)
        ct.close_dialogues()
        ct.console_on("", "set gamehour to 17")
        ct.wait_for("the party advances beyond the saved hour", lambda: ct.game_state("client").get("time", {}).get("hour", 0) > 16, 10)
        ct.console_on("", "load FollowerAnchorWorld")
        ct.wait_for("the saved follower world loads and the guest rejoins",
                    lambda: ct.game_state("client").get("online") and ct.both_see_each_other()
                    and all(ct.takes_input(g) for g in ("", "client"))
                    and alias() == npc and ct.actor_value("", npc, 95) == 0
                    and ct.game_state("client").get("time", {}).get("hour", 24) < 13, 120)
        follows(npc, lambda: ct.player_copy(""))


def housecarl_recruits():
    """A second housecarl uses the shared slot and follows its guest recruiter."""
    with recruited(base="A2C91") as (npc, sid, copy):
        follows(npc, lambda: ct.player_copy(""))


def hireling_recruits():
    """A guest pays the party's hire fee once and becomes the hired follower's anchor."""
    before = ct.party_gold()
    ct.set_party_gold(max(before, 1000))
    funded = ct.party_gold()
    try:
        with recruited(base="B9982", topic=0xBCC84) as (npc, sid, copy):
            follows(npc, lambda: ct.player_copy(""))
            ct.wait_party_gold("the shared purse pays the hire fee once", funded - 500)
    finally:
        ct.set_party_gold(before, wait=False)


def dog_recruits():
    """A guest recruits the wandering dog into the separate animal slot and becomes its pathing anchor."""
    with recruited(base="109487", slot=1, topic=None) as (npc, sid, copy):
        follows(npc, lambda: ct.player_copy(""))


def travels_with_anchor():
    """A guest-anchored follower walks through a real load door with the party without a second recruitment."""
    with recruited(cell=ct.INN) as (npc, sid, copy):
        follows(npc, lambda: ct.player_copy(""))
        host_before = ct.player_cell("")
        ct.look_apart()
        ct.travel("client", lambda: ct.activate_on("client", ct.INN_DOOR_INSIDE), ct.player_cell("client"))
        ct.follows("", host_before)
        ct.players_together("outside the inn with a guest-anchored NPC follower")
        ct.wait_for("the native follower reaches the party's destination",
                    lambda: ct.actor_state("", npc).get("placement", {}).get("cell") == ct.player_cell(""), 30)
        follows(npc, lambda: ct.player_copy(""))
        current = ct.wait_for("the travelling follower has a session id", lambda: ct.host_npc_state(npc).get("session_id"), 10)
        ct.copy_of("client", current)
        if alias() != npc:
            raise ct.TestFailure("travel changed the shared follower alias")


def register():
    scenarios = {"follower-guest-recruits": guest_recruits, "follower-switches-anchor": switches_anchor, "follower-trade-to-picker": trade_to_picker, "follower-anchor-disconnect": disconnected_anchor,
                 "follower-anchor-dies": dead_anchor, "follower-anchor-saved": saved_anchor,
                 "follower-placed-anchor-saved": lambda: saved_anchor(0xA2C94),
                 "follower-housecarl": housecarl_recruits, "follower-hireling": hireling_recruits,
                 "follower-dog": dog_recruits, "follower-travels-with-anchor": travels_with_anchor}
    ct.SCENARIOS.update(scenarios)
    for name in scenarios:
        ct.TAGS[name] = {"followers", "dialogue", "npc"}
