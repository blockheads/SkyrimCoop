"""Scenarios crime for the co-op harness."""
from ..common import (
    ESCAPE_KEY,
    HOME_CELL,
    ITEM_LISTS,
    LISTS_SHOWN,
    SMALL_CHEST,
    SWEETROLL,
    TAKE_ALL_KEY,
)
from ..comparisons import distance
from ..utilities import log
from ..objects import GodMode

import time
import math
FALKREATH, SECOND_HOLD = 0x28170, 0x2816B


def crime(ctx, game="", action="read", **values):
    return ctx.player_command(game, {"cmd": "crime", "action": action, **values})

def bounty(ctx, game=""):
    return {r["faction"]: (r["violent"], r["nonviolent"]) for r in crime(ctx, game)["bounty"]}

def enemies(ctx, game=""):
    return {r["faction"] for r in crime(ctx, game)["bounty"] if r["enemy"]}

def shared(ctx, faction, amounts):
    ctx.wait_for(f"party bounty {faction:X} is {amounts}",
                lambda: all(bounty(ctx, g).get(faction, (0, 0)) == amounts for g in ("", "client")), 10)

def restore(ctx, rows, hostile=()):
    for faction in set(bounty(ctx)) | set(rows):
        violent, nonviolent = rows.get(faction, (0, 0))
        crime(ctx, action="set", faction=faction, violent=violent, nonviolent=nonviolent)
    for faction in enemies(ctx) ^ set(hostile):
        crime(ctx, action="set_enemy", faction=faction, enemy=faction in hostile)

def spawn_npc(ctx, base, spawned, at=None):
    before = set(ctx.local_player("").get("local_actors", {}))
    ctx.console_on("", f"player.placeatme {base:X} 1")
    actor = int(next(iter(ctx.wait_for("a local crime-fixture NPC",
        lambda: set(ctx.local_player("").get("local_actors", {})) - before, 10))))
    spawned.append(actor)
    if at is not None:
        ctx.console_on("", f"{actor:X}.moveto {at:X}")
    return actor

def face_guest(ctx, witness):
    guest, npc = ctx.local_player("client")["position"], ctx.host_npc_state(witness)["position"]
    yaw = math.degrees(math.atan2(guest[0] - npc[0], guest[1] - npc[1])) % 360
    ctx.console_on("", f"{witness:X}.setangle z {yaw}")

def totals(ctx):
    """Native host bounty writes and pardons mirror separately for two holds; guests cannot overwrite them."""
    before, hostile = bounty(ctx), enemies(ctx)
    try:
        crime(ctx, action="set", faction=FALKREATH, violent=1000, nonviolent=25)
        crime(ctx, action="set", faction=SECOND_HOLD, violent=0, nonviolent=17)
        shared(ctx, FALKREATH, (1000, 25))
        shared(ctx, SECOND_HOLD, (0, 17))
        crime(ctx, "client", "set", faction=FALKREATH, violent=0, nonviolent=9999)
        shared(ctx, FALKREATH, (1000, 25))
        crime(ctx, action="set", faction=FALKREATH)
        shared(ctx, FALKREATH, (0, 0))
        shared(ctx, SECOND_HOLD, (0, 17))
    finally:
        restore(ctx, before, hostile)

def guest_pays(ctx):
    """A guest pays the shared native fine once, from the party's purse (party-purse.md): every player carries less."""
    before, hostile = bounty(ctx), enemies(ctx)
    gold = ctx.party_gold()
    amount = 110
    try:
        if gold < amount:
            ctx.set_party_gold(amount)
        payer_gold = ctx.party_gold()
        crime(ctx, action="set", faction=FALKREATH, violent=75, nonviolent=35)
        shared(ctx, FALKREATH, (75, 35))
        crime(ctx, "client", "pay", faction=FALKREATH)
        shared(ctx, FALKREATH, (0, 0))
        ctx.wait_party_gold("the party pays the fine", payer_gold - amount)
        time.sleep(1)
        if ctx.party_gold() != payer_gold - amount:
            raise ctx.failure(f"the party was charged more than once: it carries {ctx.party_gold()}")
    finally:
        restore(ctx, before, hostile)
        ctx.set_party_gold(gold, wait=False)

def guest_theft(ctx, from_menu=False, alarm=None):
    """A positive host theft control, a guest theft seen by host witnesses, and a detection-off negative control."""
    if ctx.player_cell("") != ctx.player_cell("client"):
        raise ctx.failure("party must start together")
    refs = {r["id"]: r for r in ctx.player_command("", {"cmd": "world_state"})["references"]}
    chest = refs.get(SMALL_CHEST)
    if chest is None:
        raise ctx.failure("the test-room chest is not loaded")
    ref = chest["id"]
    ctx.place_before("", ref, 160)
    spawned = []
    before, owner = bounty(ctx), chest.get("owner", 0)
    hostile = enemies(ctx)
    chest_before = ctx.container_items("", ref) if from_menu else {}
    lock_before = ctx.door_lock("", ref) if alarm == "lockpick" else None
    picks_before = ctx.item_count("client", 0x14, 0xA) if lock_before else 0
    guest_before = {item: ctx.item_count("client", 0x14, item)
                    for item in set(chest_before) | {0xF, SWEETROLL}} if from_menu else {}
    disabled_detection = False
    invisible = ctx.player_command("", {"cmd": "skill", "actor": 0x14, "index": 54})["level"]
    try:
        npc = spawn_npc(ctx, 0x13479, spawned)
        crime(ctx, action="set_owner", target=ref, owner=0x13479)
        ctx.place_before("", npc, 100, bearing=0)
        ctx.console_on("", f"{npc:X}.setangle z 0")
        npc_copy = ctx.copy_of("client", ctx.host_npc_state(npc)["session_id"])
        ctx.place_before("client", npc_copy, 120, bearing=20)
        ctx.wait_for("Orgnar sees the host for the positive control",
                    lambda: crime(ctx, action="detection", actor=npc, target=0x14)["level"] > 0, 10)
        restore(ctx, {})
        crime(ctx, action="theft", target=ref, item=0xF, count=100)
        ctx.wait_for("the positive host theft earns native bounty",
                    lambda: sum(sum(row) for row in bounty(ctx).values()) > 0, 10)
        restore(ctx, {})
        crime(ctx, action="reset_alarm")
        ctx.console_on("", "player.setav invisibility 1")
        # The positive control's witness remembers the host through invisibility.
        # A fresh witness has never seen the hidden host; keep the same guest placement.
        old_witness = npc
        npc = spawn_npc(ctx, 0x13479, spawned, at=old_witness)
        ctx.console_on("", f"{npc:X}.setangle z 0")
        crime(ctx, action="reset_alarm", actor=old_witness)
        ctx.console_on("", f"{old_witness:X}.disable")
        npc_copy = ctx.copy_of("client", ctx.host_npc_state(npc)["session_id"])
        ctx.wait_for("the host is unseen in the guest theft control",
                    lambda: crime(ctx, action="detection", actor=npc, target=0x14)["level"] <= 0, 10)
        copy = ctx.player_copy("")
        ctx.wait_for("Orgnar sees the guest's host-side copy",
                    lambda: crime(ctx, action="detection", actor=npc, target=copy)["level"] > 0, 10)
        if from_menu:
            ctx.console_on("client", f"player.additem {SWEETROLL:X} 1")
            ctx.console_on("", f"{ref:X}.additem {SWEETROLL:X} 3")
            ctx.console_on("", f"{ref:X}.additem f 100")
            ctx.wait_for("the guest sees the owned chest's contents",
                        lambda: ctx.container_items("client", ref) == ctx.container_items("", ref), 10)
            ctx.place_before("client", ref, 100)
            face_guest(ctx, npc)
            ctx.wait_for("the host witness sees the guest at the container",
                        lambda: crime(ctx, action="detection", actor=npc, target=copy)["level"] > 0, 10)
            ctx.activate_on("client", ref)
            ctx.wait_for("the guest's actual container menu is ready",
                        lambda: ctx.menu_var("client", "ContainerMenu", ITEM_LISTS + ".currentState") in LISTS_SHOWN, 5)
            ctx.tap_key("client", TAKE_ALL_KEY)
            ctx.wait_for("the guest takes all and closes the menu", lambda: ctx.takes_input("client"), 5)
        elif alarm == "lockpick":
            ctx.lock_on("", ref, True, 25)
            ctx.wait_for("the owned lock reaches the guest", lambda: ctx.door_lock("client", ref) == (True, 25), 10)
            if not picks_before: ctx.console_on("client", "player.additem a 1")
            ctx.place_before("client", ref, 100)
            face_guest(ctx, npc)
            ctx.wait_for("the host sees the guest at the lock", lambda: crime(ctx, action="detection", actor=npc, target=copy)["level"] > 0, 10)
            ctx.activate_on("client", ref)
            ctx.wait_for("the actual guest lock menu opens", lambda: "Lockpicking Menu" in ctx.menus("client"), 5)
            crime(ctx, "client", "lock_check")
            ctx.wait_for("the lock menu receives the host's caught answer", lambda: crime(ctx, "client", "lock_check")["caught"], 10)
            charged = bounty(ctx)
            crime(ctx, "client", "lock_check")
            time.sleep(.5)
            if bounty(ctx) != charged: raise ctx.failure("a caught lock attempt was charged twice")
            ctx.tap_key("client", ESCAPE_KEY)
            ctx.wait_for("the lock menu closes", lambda: "Lockpicking Menu" not in ctx.menus("client"), 5)
        elif alarm == "pickpocket":
            crime(ctx, "client", "pickpocket", target=npc_copy, item=0xF, count=5)
        elif alarm:
            victim = npc
            if alarm == "murder":
                # The victim cannot witness its own murder. Keep Orgnar as the witness
                # and exercise the alarm with a separate victim, without changing death code.
                victim = spawn_npc(ctx, 0x13475, spawned, at=npc)
            crime(ctx, action=alarm, actor=copy, target=victim)
        else:
            crime(ctx, "client", "theft", target=ref, item=0xF, count=100)
        ctx.wait_for("host witnesses judge the guest crime",
                    lambda: sum(sum(row) for row in bounty(ctx).values()) > 0, 10)
        ctx.wait_for("the guest receives the resulting party bounty", lambda: bounty(ctx) == bounty(ctx, "client"), 10)
        if alarm is None:
            if not crime(ctx, action="known_crime", actor=0x14, target=ref)["known"]:
                raise ctx.failure("the host's native crime record was not retained")
            if not crime(ctx, action="known_crime", actor=copy, target=ref)["known"]:
                raise ctx.failure("native crime queries did not recognize the guest as a party criminal")
        if from_menu:
            charged = [(f, v + n) for f, (v, n) in bounty(ctx).items() if v or n]
            if len(charged) != 1: raise ctx.failure(f"expected one crime faction, saw {charged}")
            faction, fine = charged[0]
            if ctx.party_gold() < fine: ctx.set_party_gold(fine)
            coins = ctx.party_gold()
            crime(ctx, "client", "pay", faction=faction, stolen=True)
            shared(ctx, faction, (0, 0))
            ctx.wait_for("the fine confiscates stolen rolls and keeps the unowned one",
                        lambda: ctx.item_count("client", 0x14, SWEETROLL) == guest_before[SWEETROLL] + 1, 10)
            ctx.wait_party_gold("the party pays the real container theft's fine once", coins - fine)
        restore(ctx, {})
        ctx.console_on("", "tdetect")
        disabled_detection = True
        crime(ctx, "client", "theft", target=ref, item=0xF, count=100)
        time.sleep(2)
        if any(v or n for v, n in bounty(ctx).values()):
            raise ctx.failure("an unseen guest theft acquired bounty")
    finally:
        if lock_before:
            if "Lockpicking Menu" in ctx.menus("client"): ctx.tap_key("client", ESCAPE_KEY)
            ctx.lock_on("", ref, lock_before[0], lock_before[1] or 0)
            if not picks_before: ctx.console_on("client", "player.removeitem a 1")
        if disabled_detection:
            ctx.console_on("", "tdetect")
        ctx.console_on("", f"player.setav invisibility {invisible}")
        crime(ctx, action="set_owner", target=ref, owner=owner)
        restore(ctx, before, hostile)
        if from_menu:
            now = ctx.container_items("", ref)
            for item in set(now) | set(chest_before):
                diff = chest_before.get(item, 0) - now.get(item, 0)
                if diff: ctx.console_on("", f"{ref:X}.{'additem' if diff > 0 else 'removeitem'} {item:X} {abs(diff)}")
            for item, wanted in guest_before.items():
                have = ctx.item_count("client", 0x14, item)
                if have != wanted: ctx.console_on("client", f"player.{'additem' if have < wanted else 'removeitem'} {item:X} {abs(have - wanted)}")
        for actor in spawned:
            crime(ctx, action="reset_alarm", actor=actor)
            ctx.console_on("", f"{actor:X}.disable")
            ctx.console_on("", f"{actor:X}.markfordelete")

def guest_container_fine(ctx):
    """A guest loots a real owned container, earns host bounty and pays with personal confiscation."""
    guest_theft(ctx, from_menu=True)

def guest_pickpocket_alarm(ctx):
    """A guest's native caught-pickpocket alarm is adjudicated by the host."""
    guest_theft(ctx, alarm="pickpocket")

def guest_assault_alarm(ctx):
    """The host's native assault alarm accepts its guest copy as the criminal."""
    guest_theft(ctx, alarm="assault")

def guest_murder_alarm(ctx):
    """The host's native murder alarm accepts its guest copy as the criminal."""
    guest_theft(ctx, alarm="murder")

def guest_lockpick_alarm(ctx):
    """The guest's real lock menu uses host witnesses and cannot charge a caught attempt twice."""
    guest_theft(ctx, alarm="lockpick")

def guest_trespass_state(ctx):
    """Host ownership refreshes the guest copy's trespass state while copy AI stays suppressed."""
    copy = ctx.player_copy("")
    before = crime(ctx, action="trespass_state", actor=copy)
    try:
        crime(ctx, action="set_cell_owner", owner=0x13479)
        ctx.wait_for("host ownership marks the guest copy as trespassing",
                    lambda: all(crime(ctx, action="trespass_state", actor=copy)[key] for key in ("flag", "trespass")), 5)
        crime(ctx, action="set_cell_owner", owner=7) # the party's shared player base
        ctx.wait_for("party ownership clears the guest copy's trespass state",
                    lambda: not any(crime(ctx, action="trespass_state", actor=copy)[key] for key in ("flag", "trespass")), 5)
    finally:
        crime(ctx, action="set_cell_owner", owner=before["owner"])

def guest_crime_flags(ctx):
    """Native bounty/hostility flags include guests; an NPC reacts to the visible guest and a fine ends it."""
    before, hostile = bounty(ctx), enemies(ctx)
    coins = ctx.item_count("client", 0x14, 0xF)
    invisible = ctx.player_command("", {"cmd": "skill", "actor": 0x14, "index": 54})["level"]
    spawned = []
    copy = ctx.player_copy("")
    try:
        restore(ctx, {})
        crime(ctx, action="set_enemy", faction=SECOND_HOLD, enemy=True)
        ctx.wait_for("a zero-bounty criminal flag reaches the guest",
                    lambda: SECOND_HOLD in enemies(ctx, "client"), 10)
        crime(ctx, "client", "set_enemy", faction=SECOND_HOLD, enemy=False)
        ctx.wait_for("the guest cannot clear the party's criminal flag",
                    lambda: SECOND_HOLD in enemies(ctx, "client"), 10)
        crime(ctx, action="set_enemy", faction=SECOND_HOLD, enemy=False)
        ctx.wait_for("the host's pardon clears that flag", lambda: SECOND_HOLD not in enemies(ctx, "client"), 10)
        ctx.place_before("", SMALL_CHEST, 160)
        ctx.console_on("", "player.setav invisibility 1")
        victim = spawn_npc(ctx, 0x13479, spawned)
        victim_copy = ctx.copy_of("client", ctx.host_npc_state(victim)["session_id"])
        ctx.place_before("client", victim_copy, 120, bearing=20)
        ctx.console_on("", f"{victim:X}.setangle z 0")
        ctx.wait_for("the assault victim sees the guest's actual body",
                    lambda: crime(ctx, action="detection", actor=victim, target=copy)["level"] > 0, 10)
        result = crime(ctx, action="assault", actor=copy, target=victim)
        log(f"native guest assault: {result['bounty']}")
        whiterun = 0x267EA
        ctx.wait_for("the native guest assault flags the party as criminals",
                    lambda: whiterun in enemies(ctx) and sum(bounty(ctx).get(whiterun, (0, 0))) > 0, 10)
        fine = sum(bounty(ctx)[whiterun])
        shared(ctx, whiterun, bounty(ctx)[whiterun])
        for game, actor in (("", copy), ("client", 0x14)):
            if crime(ctx, game, "actor_state", actor=actor, faction=whiterun)["bounty"] != fine:
                raise ctx.failure("a native actor query did not expose the party bounty")
        # This NPC did not receive the assault alarm. It must recognize the guest from
        # native crime state, without StartCombat or a synthetic attack on the guest.
        npc = spawn_npc(ctx, 0x13479, spawned, at=victim)
        ctx.console_on("", f"{npc:X}.setav aggression 1")
        ctx.console_on("", f"{npc:X}.setav confidence 4")
        ctx.console_on("", f"{npc:X}.setangle z 0")
        ctx.wait_for("the host remains unseen", lambda: crime(ctx, action="detection", actor=npc, target=0x14)["level"] <= 0, 10)
        ctx.wait_for("the NPC sees the guest", lambda: crime(ctx, action="detection", actor=npc, target=copy)["level"] > 0, 10)
        if crime(ctx, action="reaction", actor=npc, target=copy)["crime_faction"] != whiterun:
            raise ctx.failure("the NPC fixture is outside the crime's faction")
        ctx.wait_for("the NPC recognizes the guest's criminal hostility",
                    lambda: crime(ctx, action="reaction", actor=npc, target=copy)["hostile"], 10)
        ctx.wait_for("the NPC naturally fights the guest criminal",
                    lambda: (ctx.host_world(npc).get("ai") or {}).get("combat_target") == copy, 15)
        if crime(ctx, action="actor_state", actor=npc, faction=whiterun)["bounty"]:
            raise ctx.failure("a normal NPC inherited the party's bounty")
        have = ctx.item_count("client", 0x14, 0xF)
        if have < fine:
            ctx.console_on("client", f"player.additem f {fine - have}")
        ctx.wait_for("the host knows the chooser's purse",
                    lambda: ctx.item_count("", copy, 0xF) == ctx.item_count("client", 0x14, 0xF), 10)
        crime(ctx, "client", "pay", faction=whiterun)
        shared(ctx, whiterun, (0, 0))
        ctx.wait_for("payment ends the guest's native alarm/combat",
                    lambda: not (ctx.host_world(npc).get("ai") or {}).get("combat_target"), 10)
        ctx.wait_for("the NPC's crime hostility towards the guest clears",
                    lambda: not crime(ctx, action="reaction", actor=npc, target=copy)["hostile"], 10)
        if crime(ctx, action="actor_state", actor=copy, faction=whiterun)["bounty"]:
            raise ctx.failure("the guest's native bounty view kept a paid fine")
    finally:
        crime(ctx, action="reset_alarm", actor=copy)
        crime(ctx, action="reset_alarm")
        for actor in spawned:
            crime(ctx, action="reset_alarm", actor=actor)
            ctx.console_on("", f"{actor:X}.disable")
            ctx.console_on("", f"{actor:X}.markfordelete")
        restore(ctx, before, hostile)
        ctx.console_on("", f"player.setav invisibility {invisible}")
        have = ctx.item_count("client", 0x14, 0xF)
        if have != coins:
            ctx.console_on("client", f"player.{'additem' if have < coins else 'removeitem'} f {abs(have - coins)}")

def guard_arrest(ctx, outcome="jail", offender="client"):
    """A witnessed theft opens vanilla arrest dialogue; a player's jail choice brings the party along."""
    faction, jail_topic, fine_topic, resist_topic = 0x267EA, 0x267DD, 0x267DB, 0x267DC
    before, hostile = bounty(ctx), enemies(ctx)
    invisible = ctx.player_command("", {"cmd": "skill", "actor": 0x14, "index": 54})["level"]
    spawned = []
    old_cell = ctx.player_cell("")
    chest_before = ctx.container_items("", SMALL_CHEST)
    inventory = {g: {item: ctx.item_count(g, 0x14, item) for item in set(chest_before) - {0xF}}
                 for g in ("", "client")}
    gold = ctx.party_gold()
    refs = ctx.player_command("", {"cmd": "world_state"})["references"]
    owner = next(r.get("owner", 0) for r in refs if r["id"] == SMALL_CHEST)
    try:
        restore(ctx, {})
        ctx.place_before("", SMALL_CHEST, 160)
        if offender:
            ctx.console_on("", "player.setav invisibility 1")
        guard = spawn_npc(ctx, 0x267EE, spawned)
        guard_copy = ctx.copy_of("client", ctx.host_npc_state(guard)["session_id"])
        ctx.place_before(offender, guard_copy if offender else guard, 120, bearing=20)
        ctx.console_on("", f"{guard:X}.setangle z 0")
        body = ctx.player_copy("") if offender else 0x14
        ctx.wait_for("the guard sees the offender", lambda: crime(ctx, action="detection", actor=guard, target=body)["level"] > 0, 10)
        if offender:
            ctx.wait_for("the guard cannot see the host", lambda: crime(ctx, action="detection", actor=guard, target=0x14)["level"] <= 0, 10)
        crime(ctx, action="set_owner", target=SMALL_CHEST, owner=0x13479)
        if offender:
            ctx.wait_for("the offender sees the owned chest", lambda: ctx.player_command(offender, {"cmd": "form", "form": SMALL_CHEST}).get("owner") == 0x13479, 10)
            ctx.place_before(offender, SMALL_CHEST, 100)
            pos, npc_pos = ctx.local_player(offender)["position"], ctx.host_npc_state(guard)["position"]
            yaw = math.degrees(math.atan2(pos[0] - npc_pos[0], pos[1] - npc_pos[1])) % 360
            ctx.console_on("", f"{guard:X}.setangle z {yaw}")
            ctx.wait_for("the guard sees the actual theft", lambda: crime(ctx, action="detection", actor=guard, target=body)["level"] > 0, 10)
            ctx.activate_on(offender, SMALL_CHEST)
            ctx.wait_for("the offender's container menu is ready",
                        lambda: ctx.menu_var(offender, "ContainerMenu", ITEM_LISTS + ".currentState") in LISTS_SHOWN, 5)
            ctx.tap_key(offender, TAKE_ALL_KEY)
            ctx.wait_for("the theft closes the container", lambda: "ContainerMenu" not in ctx.menus(offender), 5)
        else:
            # The host control isolates jail travel from container inventory persistence.
            crime(ctx, action="theft", target=SMALL_CHEST, item=0xF, count=100)
        ctx.wait_for("the witnessed theft charges the party", lambda: sum(bounty(ctx).get(faction, (0, 0))) > 0, 10)
        amount = sum(bounty(ctx)[faction])
        purse = ctx.party_gold()
        ctx.wait_for("the guard arrests the party without being activated", lambda: ctx.talking_to("", guard), 20)
        ctx.wait_for("the arrested guest gets the guard dialogue", lambda: ctx.talking_to("client", guard_copy), 10)
        choices = ctx.client_shows_hosts_list("for the guard arrest")
        if not {fine_topic, jail_topic, resist_topic}.issubset({row[0] for row in choices}):
            raise ctx.failure(f"vanilla arrest choices missing: {choices}")
        ctx.wait_for("the guard's arrest greeting finishes", lambda: ctx.line_over("", guard), 30)
        ctx.pick_with_keys(offender, {"jail": jail_topic, "fine": fine_topic, "resist": resist_topic}[outcome])
        if outcome == "resist":
            ctx.wait_for("the guard fights the resisting guest",
                        lambda: (ctx.host_world(guard).get("ai") or {}).get("combat_target") == body, 15)
            if crime(ctx, action="jail_state")["sentence"]:
                raise ctx.failure("resisting arrest started a jail sentence")
            return
        ctx.wait_for("the sentence or fine moves the host", lambda: ctx.player_cell("") not in (None, old_cell), 30)
        destination = ctx.player_cell("")
        ctx.wait_for("the tether brings the whole party", lambda: ctx.player_cell("client") == destination and ctx.both_see_each_other(), 30)
        ctx.wait_for("both arrivals finish", lambda: all(ctx.takes_input(g) for g in ("", "client")), 15)
        state = crime(ctx, action="jail_state")
        if outcome == "jail":
            if destination != 0x4A376 or state["faction"] != faction or state["sentence"] <= 0:
                raise ctx.failure(f"no native jail sentence: {state}")
            places = {g: ctx.local_player(g)["position"] for g in ("", "client")}
            log(f"party jailed in {destination:X}: {state}, places={places}")
            if distance(places[""], places["client"]) > 100:
                raise ctx.failure(f"the tether put the guest outside the host's jail cell: {places}")
        else:
            if state["sentence"]:
                raise ctx.failure("paying a fine started a jail sentence")
            shared(ctx, faction, (0, 0))
            ctx.wait_party_gold("the party paid the fine once", purse - amount)
    finally:
        if crime(ctx, action="jail_state")["sentence"]:
            prison = ctx.player_cell("")
            ctx.console_on("", "serveTime")
            ctx.wait_for("cleanup completes the native sentence and both releases",
                        lambda: not crime(ctx, action="jail_state")["sentence"]
                        and ctx.player_cell("") not in (None, prison)
                        and ctx.player_cell("client") == ctx.player_cell("") and ctx.both_see_each_other()
                        and all(ctx.takes_input(g) for g in ("", "client")), 45)
        crime(ctx, action="reset_alarm")
        if copy := ctx.player_copy(""):
            crime(ctx, action="reset_alarm", actor=copy)
        # The guard belongs to the fixture cell; jail release leaves that cell unloaded.
        if ctx.player_cell("") != old_cell:
            ctx.both_go(HOME_CELL)
        for actor in spawned:
            crime(ctx, action="reset_alarm", actor=actor)
            ctx.console_on("", f"{actor:X}.disable")
            ctx.console_on("", f"{actor:X}.markfordelete")
        crime(ctx, action="set_owner", target=SMALL_CHEST, owner=owner)
        restore(ctx, before, hostile)
        ctx.console_on("", f"player.setav invisibility {invisible}")
        now = ctx.container_items("", SMALL_CHEST)
        for item in set(now) | set(chest_before):
            diff = chest_before.get(item, 0) - now.get(item, 0)
            if diff:
                ctx.console_on("", f"{SMALL_CHEST:X}.{'additem' if diff > 0 else 'removeitem'} {item:X} {abs(diff)}")
        for game, items in inventory.items():
            for item, wanted in items.items():
                have = ctx.item_count(game, 0x14, item)
                if have != wanted:
                    ctx.console_on(game, f"player.{'additem' if have < wanted else 'removeitem'} {item:X} {abs(have - wanted)}")
        ctx.set_party_gold(gold, wait=False)

def guest_guard_jail(ctx):
    """A guest's crime causes guard arrest; its jail dialogue choice sends the whole party to jail."""
    guard_arrest(ctx)

def host_guard_jail(ctx):
    """The host's arrest and jail sentence bring every guest to the same jail cell."""
    guard_arrest(ctx, offender="")

def guest_guard_fine(ctx):
    """An arrested guest pays through vanilla guard dialogue, from the party's purse, with no jail sentence."""
    guard_arrest(ctx, outcome="fine")

def guest_guard_resists(ctx):
    """A guest refusing vanilla arrest is the guard's combat target while the host stays unseen."""
    with GodMode(ctx):
        guard_arrest(ctx, outcome="resist")
