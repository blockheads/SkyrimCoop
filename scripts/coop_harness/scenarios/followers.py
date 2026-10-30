"""Scenarios followers for the co-op harness."""
from ..common import (
    ACTIVATE_KEY,
    HOME_CELL,
    INN,
    INN_DOOR_INSIDE,
    ITEM_LISTS,
    LISTS_SHOWN,
    LYDIA,
    TAB_KEY,
)

from contextlib import contextmanager
import math
RECRUIT, FOLLOW, WAIT, DISMISS, TRADE = 0xB0EE6, 0x75083, 0x75084, 0x5C80C, 0x60020


def go_home(ctx):
    if any(ctx.player_cell(game) != 0x32AE7 for game in ("", "client")):
        ctx.both_go(HOME_CELL)

def choose(ctx, game, npc, copy, topic):
    ctx.close_dialogues()
    target = copy if game else npc
    ctx.place_before(game, target, 110)
    ctx.activate_on(game, target)
    ctx.wait_for("the follower dialogue opens for its picker", lambda: ctx.talking_to(game, target), 10)
    ctx.wait_for(f"the follower offers topic {topic:X}", lambda: topic in {r["topic"] for r in ctx.dialogue(game).get("options", [])}, 10)
    ctx.pick_with_keys(game, topic)
    if topic in (WAIT, FOLLOW):
        expected = 1 if topic == WAIT else 0
        ctx.wait_for("the follower's command completes", lambda: ctx.actor_value("", npc, 95) == expected, 10)

@contextmanager
def recruited(ctx, game="client", base=None, slot=0, topic=RECRUIT, outdoors=False, reference=None, cell=None):
    ctx.close_dialogues()
    if cell:
        ctx.both_go(cell)
    elif outdoors:
        ctx.both_go("Whiterun plains", "cow Tamriel 0 -4")
    else:
        go_home(ctx)
    if ctx.follower_in_slot(slot):
        raise ctx.failure("the follower fixture needs the shared vanilla slot empty")
    original = ctx.actor_place("", reference) if reference else None
    if reference:
        npc = reference
        ctx.console_on("", f"{npc:X}.enable")
        ctx.player_command("", {"cmd": "move_actor", "actor": npc, "position": ctx.my_position("")})
    else:
        before = set(ctx.local_player("").get("local_actors", {}))
        ctx.console_on("", f"player.placeatme {base or LYDIA} 1")
        npc = int(next(iter(ctx.wait_for("the follower NPC loads near the party", lambda: set(ctx.local_player("").get("local_actors", {})) - before, 15))))
    sid = ctx.wait_for("the follower gets a session id", lambda: (value := ctx.host_npc_state(npc).get("session_id")) is not None and value >= 0 and (value,), 10)[0]
    copy = ctx.copy_of("client", sid)
    ctx.wait_for("the follower and its copy finish fading in", lambda: all(ctx.actor_state(g, f).get("mod", {}).get("fade_state") == 0 for g, f in (("", npc), ("client", copy))), 10)
    try:
        if topic is None:  # WEDogFollowerScript recruits on activation, without a dialogue menu.
            target = copy if game else npc
            ctx.place_before(game, target, 110)
            ctx.activate_on(game, target)
        else:
            choose(ctx, game, npc, copy, topic)
        ctx.wait_for("vanilla recruitment fills the shared follower alias", lambda: ctx.follower_in_slot(slot) == npc, 10)
        ctx.close_dialogues()
        yield npc, sid, copy
    finally:
        for game in ("client", ""):
            if "ContainerMenu" in ctx.open_menus(game):
                ctx.tap_key(game, TAB_KEY)
                ctx.wait_for("the follower trade menu closes for cleanup", lambda game=game: "ContainerMenu" not in ctx.open_menus(game), 10)
        if not ctx.game_state("client").get("online"):
            ctx.rejoin_client()
        ctx.close_dialogues()
        ctx.dismiss_follower(npc, slot)
        if reference:
            ctx.player_command("", {"cmd": "move_actor", "actor": npc, "home": True})
            if original["disabled"]:
                ctx.console_on("", f"{npc:X}.disable")
        else:
            ctx.remove_host_npc(npc)
        go_home(ctx)

def follows(ctx, npc, member, timeout=15):
    def matches():
        expected = member() if callable(member) else member
        return bool(expected) and ctx.actor_state("", npc).get("ai", {}).get("follow_target") == expected
    ctx.wait_for("the follower's native pathing target is the requested member", matches, timeout)

def guest_recruits(ctx):
    """The guest's real recruitment dialogue makes Lydia follow its host-side player copy and move toward it."""
    with recruited(ctx, outdoors=True) as (npc, sid, copy):
        follows(ctx, npc, lambda: ctx.player_copy(""))
        before = ctx.actor_state("", npc)["placement"]["position"]
        start = ctx.my_position("client")
        way = ctx.clear_way("client", [0, 90, 180, 270], 650)
        ctx.console_on("client", f"player.setangle z {way}")
        ctx.press("client", "forward")
        try:
            ctx.wait_for("the followed guest walks away", lambda: math.dist(ctx.my_position("client"), start) > 600, 15)
        finally:
            ctx.release("client", "forward")
        ctx.wait_for("Lydia physically walks after the guest", lambda: math.dist(ctx.actor_state("", npc)["placement"]["position"], before) > 150, 15)
        ctx.wait_for("the follower copy reaches its owner's walking position",
                    lambda: math.dist(ctx.actor_state("client", copy)["placement"]["position"],
                                      ctx.actor_state("", npc)["placement"]["position"]) < 100, 10)

def switches_anchor(ctx):
    """Anyone can ask the shared follower to wait; the next Follow command changes its physical anchor."""
    with recruited(ctx) as (npc, sid, copy):
        follows(ctx, npc, lambda: ctx.player_copy(""))
        choose(ctx, "", npc, copy, WAIT)
        ctx.wait_for("the host's wait command takes effect", lambda: ctx.actor_value("", npc, 95) == 1, 10)
        choose(ctx, "", npc, copy, FOLLOW)
        follows(ctx, npc, 0x14)
        choose(ctx, "client", npc, copy, WAIT)
        choose(ctx, "client", npc, copy, FOLLOW)
        follows(ctx, npc, lambda: ctx.player_copy(""))
        choose(ctx, "client", npc, copy, DISMISS)
        ctx.wait_for("the guest can dismiss the shared follower", lambda: ctx.follower_in_slot() != npc, 15)

def trade_to_picker(ctx):
    """The follower's inventory opens on the picker, whose own inventory receives the item without changing the anchor."""
    with recruited(ctx) as (npc, sid, copy):
        follows(ctx, npc, lambda: ctx.player_copy(""))
        ctx.console_on("", f"{npc:X}.removeallitems")
        ctx.console_on("", f"{npc:X}.additem A 3")
        ctx.wait_for("the guest sees the follower's trade items", lambda: ctx.item_count("client", copy, 0xA) == 3, 10)
        before = {game: ctx.item_count(game, 0x14, 0xA) for game in ("", "client")}
        try:
            choose(ctx, "client", npc, copy, TRADE)
            ctx.wait_for("only the trade picker gets the follower container menu",
                        lambda: "ContainerMenu" in ctx.open_menus("client"), 10)
            if "ContainerMenu" in ctx.open_menus(""):
                raise ctx.failure("the guest's trade opened the host's inventory menu")
            ctx.wait_for("the follower trade list takes input", lambda: ctx.menu_var("client", "ContainerMenu", ITEM_LISTS + ".currentState") in LISTS_SHOWN, 5)
            ctx.select_entry("client", "ContainerMenu", ITEM_LISTS + ".ItemsList", "Lockpick")
            ctx.tap_key("client", ACTIVATE_KEY)
            ctx.wait_for("the guest receives the traded lockpicks", lambda: ctx.item_count("client", 0x14, 0xA) > before["client"], 10)
            received = ctx.item_count("client", 0x14, 0xA) - before["client"]
            ctx.wait_for("the follower loses exactly those lockpicks", lambda: ctx.item_count("", npc, 0xA) == 3 - received, 10)
            if ctx.item_count("", 0x14, 0xA) != before[""]:
                raise ctx.failure("the guest's follower trade changed the host's personal inventory")
            ctx.tap_key("client", TAB_KEY)
            ctx.wait_for("the guest closes its follower trade", lambda: "ContainerMenu" not in ctx.open_menus("client"), 10)
            follows(ctx, npc, lambda: ctx.player_copy(""))
            choose(ctx, "", npc, copy, TRADE)
            ctx.wait_for("the host can trade after the guest closes", lambda: "ContainerMenu" in ctx.open_menus(""), 10)
            ctx.wait_for("the host's follower inventory takes input", lambda: ctx.menu_var("", "ContainerMenu", ITEM_LISTS + ".currentState") in LISTS_SHOWN, 5)
            ctx.tap_key("", TAB_KEY)
            ctx.wait_for("the host closes its follower trade", lambda: "ContainerMenu" not in ctx.open_menus(""), 10)
            follows(ctx, npc, lambda: ctx.player_copy(""))
        finally:
            for game, wanted in before.items():
                have = ctx.item_count(game, 0x14, 0xA)
                if have > wanted:
                    ctx.console_on(game, f"player.removeitem A {have - wanted}")

def disconnected_anchor(ctx):
    """A disconnected guest's follower uses the host, then resolves the returning member's replacement copy."""
    with recruited(ctx) as (npc, sid, copy):
        follows(ctx, npc, lambda: ctx.player_copy(""))
        ctx.disconnect_client()
        follows(ctx, npc, 0x14)
        ctx.rejoin_client()
        follows(ctx, npc, lambda: ctx.player_copy(""))

def dead_anchor(ctx):
    """The follower switches to the standing host while its guest anchor is dead and returns after recovery."""
    with recruited(ctx) as (npc, sid, copy):
        follows(ctx, npc, lambda: ctx.player_copy(""))
        ctx.console_on("client", "player.kill")
        ctx.wait_for("the followed guest is dead", lambda: ctx.is_dead(ctx.player_snapshot("client")), 10)
        follows(ctx, npc, 0x14)
        ctx.wait_for("the guest recovers beside the standing party", lambda: not ctx.is_dead(ctx.player_snapshot("client")), 30)
        follows(ctx, npc, lambda: ctx.player_copy(""))

def saved_anchor(ctx, reference=None):
    """The host save restores the guest's desired anchor and rebinds the follower to the new player copy."""
    _game = ctx.game()
    with recruited(ctx, reference=reference) as (npc, sid, copy):
        follows(ctx, npc, lambda: ctx.player_copy(""))
        ctx.console_on("", "set gamehour to 12")
        saved = ctx.save_game("", "FollowerAnchorWorld")
        ctx.wait_for("the host saves follower anchors with its world", ctx.saves_written(saved), 15)
        choose(ctx, "", npc, copy, WAIT)
        choose(ctx, "", npc, copy, FOLLOW)
        follows(ctx, npc, 0x14)
        ctx.close_dialogues()
        ctx.console_on("", "set gamehour to 17")
        ctx.wait_for("the party advances beyond the saved hour", lambda: ctx.game_state("client").get("time", {}).get("hour", 0) > 16, 10)
        ctx.console_on("", "load FollowerAnchorWorld")
        ctx.wait_for("the saved follower world loads and the guest rejoins",
                    lambda: ctx.game_state("client").get("online") and ctx.both_see_each_other()
                    and all(ctx.takes_input(g) for g in ("", "client"))
                    and ctx.follower_in_slot() == npc and ctx.actor_value("", npc, 95) == 0
                    and ctx.game_state("client").get("time", {}).get("hour", 24) < 13, 120)
        follows(ctx, npc, lambda: ctx.player_copy(""))

def housecarl_recruits(ctx):
    """A second housecarl uses the shared slot and follows its guest recruiter."""
    with recruited(ctx, base="A2C91") as (npc, sid, copy):
        follows(ctx, npc, lambda: ctx.player_copy(""))

def hireling_recruits(ctx):
    """A guest pays the party's hire fee once and becomes the hired follower's anchor."""
    before = ctx.party_gold()
    ctx.set_party_gold(max(before, 1000))
    funded = ctx.party_gold()
    try:
        with recruited(ctx, base="B9982", topic=0xBCC84) as (npc, sid, copy):
            follows(ctx, npc, lambda: ctx.player_copy(""))
            ctx.wait_party_gold("the shared purse pays the hire fee once", funded - 500)
    finally:
        ctx.set_party_gold(before, wait=False)

def dog_recruits(ctx):
    """A guest recruits the wandering dog into the separate animal slot and becomes its pathing anchor."""
    with recruited(ctx, base="109487", slot=1, topic=None) as (npc, sid, copy):
        follows(ctx, npc, lambda: ctx.player_copy(""))

def travels_with_anchor(ctx):
    """A guest-anchored follower walks through a real load door with the party without a second recruitment."""
    with recruited(ctx, cell=INN) as (npc, sid, copy):
        follows(ctx, npc, lambda: ctx.player_copy(""))
        host_before = ctx.player_cell("")
        ctx.look_apart()
        ctx.travel("client", lambda: ctx.activate_on("client", INN_DOOR_INSIDE), ctx.player_cell("client"))
        ctx.follows("", host_before)
        ctx.players_together("outside the inn with a guest-anchored NPC follower")
        ctx.wait_for("the native follower reaches the party's destination",
                    lambda: ctx.actor_state("", npc).get("placement", {}).get("cell") == ctx.player_cell(""), 30)
        follows(ctx, npc, lambda: ctx.player_copy(""))
        current = ctx.wait_for("the travelling follower has a session id", lambda: ctx.host_npc_state(npc).get("session_id"), 10)
        ctx.copy_of("client", current)
        if ctx.follower_in_slot() != npc:
            raise ctx.failure("travel changed the shared follower alias")
