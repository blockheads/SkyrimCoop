"""Shared bounty scenarios. Native alarms exercise the production hooks and network route."""
import time
import math
import coop_test as ct

FALKREATH, SECOND_HOLD = 0x28170, 0x2816B


def crime(game="", action="read", **values):
    return ct.player_command(game, {"cmd": "crime", "action": action, **values})


def bounty(game=""):
    return {r["faction"]: (r["violent"], r["nonviolent"]) for r in crime(game)["bounty"]}


def enemies(game=""):
    return {r["faction"] for r in crime(game)["bounty"] if r["enemy"]}


def shared(faction, amounts):
    ct.wait_for(f"party bounty {faction:X} is {amounts}",
                lambda: all(bounty(g).get(faction, (0, 0)) == amounts for g in ("", "client")), 10)


def restore(rows, hostile=()):
    for faction in set(bounty()) | set(rows):
        violent, nonviolent = rows.get(faction, (0, 0))
        crime(action="set", faction=faction, violent=violent, nonviolent=nonviolent)
    for faction in enemies() ^ set(hostile):
        crime(action="set_enemy", faction=faction, enemy=faction in hostile)


def spawn_npc(base, spawned, at=None):
    before = set(ct.local_player("").get("local_actors", {}))
    ct.console_on("", f"player.placeatme {base:X} 1")
    actor = int(next(iter(ct.wait_for("a local crime-fixture NPC",
        lambda: set(ct.local_player("").get("local_actors", {})) - before, 10))))
    spawned.append(actor)
    if at is not None:
        ct.console_on("", f"{actor:X}.moveto {at:X}")
    return actor


def face_guest(witness):
    guest, npc = ct.local_player("client")["position"], ct.host_npc_state(witness)["position"]
    yaw = math.degrees(math.atan2(guest[0] - npc[0], guest[1] - npc[1])) % 360
    ct.console_on("", f"{witness:X}.setangle z {yaw}")


def totals():
    """Native host bounty writes and pardons mirror separately for two holds; guests cannot overwrite them."""
    before, hostile = bounty(), enemies()
    try:
        crime(action="set", faction=FALKREATH, violent=1000, nonviolent=25)
        crime(action="set", faction=SECOND_HOLD, violent=0, nonviolent=17)
        shared(FALKREATH, (1000, 25))
        shared(SECOND_HOLD, (0, 17))
        crime("client", "set", faction=FALKREATH, violent=0, nonviolent=9999)
        shared(FALKREATH, (1000, 25))
        crime(action="set", faction=FALKREATH)
        shared(FALKREATH, (0, 0))
        shared(SECOND_HOLD, (0, 17))
    finally:
        restore(before, hostile)


def guest_pays():
    """A guest pays the shared native fine once, from the party's purse (party-purse.md): every player carries less."""
    before, hostile = bounty(), enemies()
    gold = ct.party_gold()
    amount = 110
    try:
        if gold < amount:
            ct.set_party_gold(amount)
        payer_gold = ct.party_gold()
        crime(action="set", faction=FALKREATH, violent=75, nonviolent=35)
        shared(FALKREATH, (75, 35))
        crime("client", "pay", faction=FALKREATH)
        shared(FALKREATH, (0, 0))
        ct.wait_party_gold("the party pays the fine", payer_gold - amount)
        time.sleep(1)
        if ct.party_gold() != payer_gold - amount:
            raise ct.TestFailure(f"the party was charged more than once: it carries {ct.party_gold()}")
    finally:
        restore(before, hostile)
        ct.set_party_gold(gold, wait=False)


def guest_theft(from_menu=False, alarm=None):
    """A positive host theft control, a guest theft seen by host witnesses, and a detection-off negative control."""
    if ct.player_cell("") != ct.player_cell("client"):
        raise ct.TestFailure("party must start together")
    refs = {r["id"]: r for r in ct.player_command("", {"cmd": "world_state"})["references"]}
    chest = refs.get(ct.SMALL_CHEST)
    if chest is None:
        raise ct.TestFailure("the test-room chest is not loaded")
    ref = chest["id"]
    ct.place_before("", ref, 160)
    spawned = []
    before, owner = bounty(), chest.get("owner", 0)
    hostile = enemies()
    chest_before = ct.container_items("", ref) if from_menu else {}
    lock_before = ct.door_lock("", ref) if alarm == "lockpick" else None
    picks_before = ct.item_count("client", 0x14, 0xA) if lock_before else 0
    guest_before = {item: ct.item_count("client", 0x14, item)
                    for item in set(chest_before) | {0xF, ct.SWEETROLL}} if from_menu else {}
    disabled_detection = False
    invisible = ct.player_command("", {"cmd": "skill", "actor": 0x14, "index": 54})["level"]
    try:
        npc = spawn_npc(0x13479, spawned)
        crime(action="set_owner", target=ref, owner=0x13479)
        ct.place_before("", npc, 100, bearing=0)
        ct.console_on("", f"{npc:X}.setangle z 0")
        npc_copy = ct.copy_of("client", ct.host_npc_state(npc)["session_id"])
        ct.place_before("client", npc_copy, 120, bearing=20)
        ct.wait_for("Orgnar sees the host for the positive control",
                    lambda: crime(action="detection", actor=npc, target=0x14)["level"] > 0, 10)
        restore({})
        crime(action="theft", target=ref, item=0xF, count=100)
        ct.wait_for("the positive host theft earns native bounty",
                    lambda: sum(sum(row) for row in bounty().values()) > 0, 10)
        restore({})
        crime(action="reset_alarm")
        ct.console_on("", "player.setav invisibility 1")
        # The positive control's witness remembers the host through invisibility.
        # A fresh witness has never seen the hidden host; keep the same guest placement.
        old_witness = npc
        npc = spawn_npc(0x13479, spawned, at=old_witness)
        ct.console_on("", f"{npc:X}.setangle z 0")
        crime(action="reset_alarm", actor=old_witness)
        ct.console_on("", f"{old_witness:X}.disable")
        npc_copy = ct.copy_of("client", ct.host_npc_state(npc)["session_id"])
        ct.wait_for("the host is unseen in the guest theft control",
                    lambda: crime(action="detection", actor=npc, target=0x14)["level"] <= 0, 10)
        copy = ct.player_copy("")
        ct.wait_for("Orgnar sees the guest's host-side copy",
                    lambda: crime(action="detection", actor=npc, target=copy)["level"] > 0, 10)
        if from_menu:
            ct.console_on("client", f"player.additem {ct.SWEETROLL:X} 1")
            ct.console_on("", f"{ref:X}.additem {ct.SWEETROLL:X} 3")
            ct.console_on("", f"{ref:X}.additem f 100")
            ct.wait_for("the guest sees the owned chest's contents",
                        lambda: ct.container_items("client", ref) == ct.container_items("", ref), 10)
            ct.place_before("client", ref, 100)
            face_guest(npc)
            ct.wait_for("the host witness sees the guest at the container",
                        lambda: crime(action="detection", actor=npc, target=copy)["level"] > 0, 10)
            ct.activate_on("client", ref)
            ct.wait_for("the guest's actual container menu is ready",
                        lambda: ct.menu_var("client", "ContainerMenu", ct.ITEM_LISTS + ".currentState") in ct.LISTS_SHOWN, 5)
            ct.tap_key("client", ct.TAKE_ALL_KEY)
            ct.wait_for("the guest takes all and closes the menu", lambda: ct.takes_input("client"), 5)
        elif alarm == "lockpick":
            ct.lock_on("", ref, True, 25)
            ct.wait_for("the owned lock reaches the guest", lambda: ct.door_lock("client", ref) == (True, 25), 10)
            if not picks_before: ct.console_on("client", "player.additem a 1")
            ct.place_before("client", ref, 100)
            face_guest(npc)
            ct.wait_for("the host sees the guest at the lock", lambda: crime(action="detection", actor=npc, target=copy)["level"] > 0, 10)
            ct.activate_on("client", ref)
            ct.wait_for("the actual guest lock menu opens", lambda: "Lockpicking Menu" in ct.menus("client"), 5)
            crime("client", "lock_check")
            ct.wait_for("the lock menu receives the host's caught answer", lambda: crime("client", "lock_check")["caught"], 10)
            charged = bounty()
            crime("client", "lock_check")
            time.sleep(.5)
            if bounty() != charged: raise ct.TestFailure("a caught lock attempt was charged twice")
            ct.tap_key("client", ct.ESCAPE_KEY)
            ct.wait_for("the lock menu closes", lambda: "Lockpicking Menu" not in ct.menus("client"), 5)
        elif alarm == "pickpocket":
            crime("client", "pickpocket", target=npc_copy, item=0xF, count=5)
        elif alarm:
            victim = npc
            if alarm == "murder":
                # The victim cannot witness its own murder. Keep Orgnar as the witness
                # and exercise the alarm with a separate victim, without changing death code.
                victim = spawn_npc(0x13475, spawned, at=npc)
            crime(action=alarm, actor=copy, target=victim)
        else:
            crime("client", "theft", target=ref, item=0xF, count=100)
        ct.wait_for("host witnesses judge the guest crime",
                    lambda: sum(sum(row) for row in bounty().values()) > 0, 10)
        ct.wait_for("the guest receives the resulting party bounty", lambda: bounty() == bounty("client"), 10)
        if alarm is None:
            if not crime(action="known_crime", actor=0x14, target=ref)["known"]:
                raise ct.TestFailure("the host's native crime record was not retained")
            if not crime(action="known_crime", actor=copy, target=ref)["known"]:
                raise ct.TestFailure("native crime queries did not recognize the guest as a party criminal")
        if from_menu:
            charged = [(f, v + n) for f, (v, n) in bounty().items() if v or n]
            if len(charged) != 1: raise ct.TestFailure(f"expected one crime faction, saw {charged}")
            faction, fine = charged[0]
            if ct.party_gold() < fine: ct.set_party_gold(fine)
            coins = ct.party_gold()
            crime("client", "pay", faction=faction, stolen=True)
            shared(faction, (0, 0))
            ct.wait_for("the fine confiscates stolen rolls and keeps the unowned one",
                        lambda: ct.item_count("client", 0x14, ct.SWEETROLL) == guest_before[ct.SWEETROLL] + 1, 10)
            ct.wait_party_gold("the party pays the real container theft's fine once", coins - fine)
        restore({})
        ct.console_on("", "tdetect")
        disabled_detection = True
        crime("client", "theft", target=ref, item=0xF, count=100)
        time.sleep(2)
        if any(v or n for v, n in bounty().values()):
            raise ct.TestFailure("an unseen guest theft acquired bounty")
    finally:
        if lock_before:
            if "Lockpicking Menu" in ct.menus("client"): ct.tap_key("client", ct.ESCAPE_KEY)
            ct.lock_on("", ref, lock_before[0], lock_before[1] or 0)
            if not picks_before: ct.console_on("client", "player.removeitem a 1")
        if disabled_detection:
            ct.console_on("", "tdetect")
        ct.console_on("", f"player.setav invisibility {invisible}")
        crime(action="set_owner", target=ref, owner=owner)
        restore(before, hostile)
        if from_menu:
            now = ct.container_items("", ref)
            for item in set(now) | set(chest_before):
                diff = chest_before.get(item, 0) - now.get(item, 0)
                if diff: ct.console_on("", f"{ref:X}.{'additem' if diff > 0 else 'removeitem'} {item:X} {abs(diff)}")
            for item, wanted in guest_before.items():
                have = ct.item_count("client", 0x14, item)
                if have != wanted: ct.console_on("client", f"player.{'additem' if have < wanted else 'removeitem'} {item:X} {abs(have - wanted)}")
        for actor in spawned:
            crime(action="reset_alarm", actor=actor)
            ct.console_on("", f"{actor:X}.disable")
            ct.console_on("", f"{actor:X}.markfordelete")


def guest_container_fine():
    """A guest loots a real owned container, earns host bounty and pays with personal confiscation."""
    guest_theft(from_menu=True)


def guest_pickpocket_alarm():
    """A guest's native caught-pickpocket alarm is adjudicated by the host."""
    guest_theft(alarm="pickpocket")


def guest_assault_alarm():
    """The host's native assault alarm accepts its guest copy as the criminal."""
    guest_theft(alarm="assault")


def guest_murder_alarm():
    """The host's native murder alarm accepts its guest copy as the criminal."""
    guest_theft(alarm="murder")


def guest_lockpick_alarm():
    """The guest's real lock menu uses host witnesses and cannot charge a caught attempt twice."""
    guest_theft(alarm="lockpick")


def guest_trespass_state():
    """Host ownership refreshes the guest copy's trespass state while copy AI stays suppressed."""
    copy = ct.player_copy("")
    before = crime(action="trespass_state", actor=copy)
    try:
        crime(action="set_cell_owner", owner=0x13479)
        ct.wait_for("host ownership marks the guest copy as trespassing",
                    lambda: all(crime(action="trespass_state", actor=copy)[key] for key in ("flag", "trespass")), 5)
        crime(action="set_cell_owner", owner=7) # the party's shared player base
        ct.wait_for("party ownership clears the guest copy's trespass state",
                    lambda: not any(crime(action="trespass_state", actor=copy)[key] for key in ("flag", "trespass")), 5)
    finally:
        crime(action="set_cell_owner", owner=before["owner"])


def guest_crime_flags():
    """Native bounty/hostility flags include guests; an NPC reacts to the visible guest and a fine ends it."""
    before, hostile = bounty(), enemies()
    coins = ct.item_count("client", 0x14, 0xF)
    invisible = ct.player_command("", {"cmd": "skill", "actor": 0x14, "index": 54})["level"]
    spawned = []
    copy = ct.player_copy("")
    try:
        restore({})
        crime(action="set_enemy", faction=SECOND_HOLD, enemy=True)
        ct.wait_for("a zero-bounty criminal flag reaches the guest",
                    lambda: SECOND_HOLD in enemies("client"), 10)
        crime("client", "set_enemy", faction=SECOND_HOLD, enemy=False)
        ct.wait_for("the guest cannot clear the party's criminal flag",
                    lambda: SECOND_HOLD in enemies("client"), 10)
        crime(action="set_enemy", faction=SECOND_HOLD, enemy=False)
        ct.wait_for("the host's pardon clears that flag", lambda: SECOND_HOLD not in enemies("client"), 10)
        ct.place_before("", ct.SMALL_CHEST, 160)
        ct.console_on("", "player.setav invisibility 1")
        victim = spawn_npc(0x13479, spawned)
        victim_copy = ct.copy_of("client", ct.host_npc_state(victim)["session_id"])
        ct.place_before("client", victim_copy, 120, bearing=20)
        ct.console_on("", f"{victim:X}.setangle z 0")
        ct.wait_for("the assault victim sees the guest's actual body",
                    lambda: crime(action="detection", actor=victim, target=copy)["level"] > 0, 10)
        result = crime(action="assault", actor=copy, target=victim)
        ct.log(f"native guest assault: {result['bounty']}")
        whiterun = 0x267EA
        ct.wait_for("the native guest assault flags the party as criminals",
                    lambda: whiterun in enemies() and sum(bounty().get(whiterun, (0, 0))) > 0, 10)
        fine = sum(bounty()[whiterun])
        shared(whiterun, bounty()[whiterun])
        for game, actor in (("", copy), ("client", 0x14)):
            if crime(game, "actor_state", actor=actor, faction=whiterun)["bounty"] != fine:
                raise ct.TestFailure("a native actor query did not expose the party bounty")
        # This NPC did not receive the assault alarm. It must recognize the guest from
        # native crime state, without StartCombat or a synthetic attack on the guest.
        npc = spawn_npc(0x13479, spawned, at=victim)
        ct.console_on("", f"{npc:X}.setav aggression 1")
        ct.console_on("", f"{npc:X}.setav confidence 4")
        ct.console_on("", f"{npc:X}.setangle z 0")
        ct.wait_for("the host remains unseen", lambda: crime(action="detection", actor=npc, target=0x14)["level"] <= 0, 10)
        ct.wait_for("the NPC sees the guest", lambda: crime(action="detection", actor=npc, target=copy)["level"] > 0, 10)
        if crime(action="reaction", actor=npc, target=copy)["crime_faction"] != whiterun:
            raise ct.TestFailure("the NPC fixture is outside the crime's faction")
        ct.wait_for("the NPC recognizes the guest's criminal hostility",
                    lambda: crime(action="reaction", actor=npc, target=copy)["hostile"], 10)
        ct.wait_for("the NPC naturally fights the guest criminal",
                    lambda: (ct.host_world(npc).get("ai") or {}).get("combat_target") == copy, 15)
        if crime(action="actor_state", actor=npc, faction=whiterun)["bounty"]:
            raise ct.TestFailure("a normal NPC inherited the party's bounty")
        have = ct.item_count("client", 0x14, 0xF)
        if have < fine:
            ct.console_on("client", f"player.additem f {fine - have}")
        ct.wait_for("the host knows the chooser's purse",
                    lambda: ct.item_count("", copy, 0xF) == ct.item_count("client", 0x14, 0xF), 10)
        crime("client", "pay", faction=whiterun)
        shared(whiterun, (0, 0))
        ct.wait_for("payment ends the guest's native alarm/combat",
                    lambda: not (ct.host_world(npc).get("ai") or {}).get("combat_target"), 10)
        ct.wait_for("the NPC's crime hostility towards the guest clears",
                    lambda: not crime(action="reaction", actor=npc, target=copy)["hostile"], 10)
        if crime(action="actor_state", actor=copy, faction=whiterun)["bounty"]:
            raise ct.TestFailure("the guest's native bounty view kept a paid fine")
    finally:
        crime(action="reset_alarm", actor=copy)
        crime(action="reset_alarm")
        for actor in spawned:
            crime(action="reset_alarm", actor=actor)
            ct.console_on("", f"{actor:X}.disable")
            ct.console_on("", f"{actor:X}.markfordelete")
        restore(before, hostile)
        ct.console_on("", f"player.setav invisibility {invisible}")
        have = ct.item_count("client", 0x14, 0xF)
        if have != coins:
            ct.console_on("client", f"player.{'additem' if have < coins else 'removeitem'} f {abs(have - coins)}")


def guard_arrest(outcome="jail", offender="client"):
    """A witnessed theft opens vanilla arrest dialogue; a player's jail choice brings the party along."""
    faction, jail_topic, fine_topic, resist_topic = 0x267EA, 0x267DD, 0x267DB, 0x267DC
    before, hostile = bounty(), enemies()
    invisible = ct.player_command("", {"cmd": "skill", "actor": 0x14, "index": 54})["level"]
    spawned = []
    old_cell = ct.player_cell("")
    chest_before = ct.container_items("", ct.SMALL_CHEST)
    inventory = {g: {item: ct.item_count(g, 0x14, item) for item in set(chest_before) - {0xF}}
                 for g in ("", "client")}
    gold = ct.party_gold()
    refs = ct.player_command("", {"cmd": "world_state"})["references"]
    owner = next(r.get("owner", 0) for r in refs if r["id"] == ct.SMALL_CHEST)
    try:
        restore({})
        ct.place_before("", ct.SMALL_CHEST, 160)
        if offender:
            ct.console_on("", "player.setav invisibility 1")
        guard = spawn_npc(0x267EE, spawned)
        guard_copy = ct.copy_of("client", ct.host_npc_state(guard)["session_id"])
        ct.place_before(offender, guard_copy if offender else guard, 120, bearing=20)
        ct.console_on("", f"{guard:X}.setangle z 0")
        body = ct.player_copy("") if offender else 0x14
        ct.wait_for("the guard sees the offender", lambda: crime(action="detection", actor=guard, target=body)["level"] > 0, 10)
        if offender:
            ct.wait_for("the guard cannot see the host", lambda: crime(action="detection", actor=guard, target=0x14)["level"] <= 0, 10)
        crime(action="set_owner", target=ct.SMALL_CHEST, owner=0x13479)
        if offender:
            ct.wait_for("the offender sees the owned chest", lambda: ct.player_command(offender, {"cmd": "form", "form": ct.SMALL_CHEST}).get("owner") == 0x13479, 10)
            ct.place_before(offender, ct.SMALL_CHEST, 100)
            pos, npc_pos = ct.local_player(offender)["position"], ct.host_npc_state(guard)["position"]
            yaw = math.degrees(math.atan2(pos[0] - npc_pos[0], pos[1] - npc_pos[1])) % 360
            ct.console_on("", f"{guard:X}.setangle z {yaw}")
            ct.wait_for("the guard sees the actual theft", lambda: crime(action="detection", actor=guard, target=body)["level"] > 0, 10)
            ct.activate_on(offender, ct.SMALL_CHEST)
            ct.wait_for("the offender's container menu is ready",
                        lambda: ct.menu_var(offender, "ContainerMenu", ct.ITEM_LISTS + ".currentState") in ct.LISTS_SHOWN, 5)
            ct.tap_key(offender, ct.TAKE_ALL_KEY)
            ct.wait_for("the theft closes the container", lambda: "ContainerMenu" not in ct.menus(offender), 5)
        else:
            # The host control isolates jail travel from container inventory persistence.
            crime(action="theft", target=ct.SMALL_CHEST, item=0xF, count=100)
        ct.wait_for("the witnessed theft charges the party", lambda: sum(bounty().get(faction, (0, 0))) > 0, 10)
        amount = sum(bounty()[faction])
        purse = ct.party_gold()
        ct.wait_for("the guard arrests the party without being activated", lambda: ct.talking_to("", guard), 20)
        ct.wait_for("the arrested guest gets the guard dialogue", lambda: ct.talking_to("client", guard_copy), 10)
        choices = ct.client_shows_hosts_list("for the guard arrest")
        if not {fine_topic, jail_topic, resist_topic}.issubset({row[0] for row in choices}):
            raise ct.TestFailure(f"vanilla arrest choices missing: {choices}")
        ct.wait_for("the guard's arrest greeting finishes", lambda: ct.line_over("", guard), 30)
        ct.pick_with_keys(offender, {"jail": jail_topic, "fine": fine_topic, "resist": resist_topic}[outcome])
        if outcome == "resist":
            ct.wait_for("the guard fights the resisting guest",
                        lambda: (ct.host_world(guard).get("ai") or {}).get("combat_target") == body, 15)
            if crime(action="jail_state")["sentence"]:
                raise ct.TestFailure("resisting arrest started a jail sentence")
            return
        ct.wait_for("the sentence or fine moves the host", lambda: ct.player_cell("") not in (None, old_cell), 30)
        destination = ct.player_cell("")
        ct.wait_for("the tether brings the whole party", lambda: ct.player_cell("client") == destination and ct.both_see_each_other(), 30)
        ct.wait_for("both arrivals finish", lambda: all(ct.takes_input(g) for g in ("", "client")), 15)
        state = crime(action="jail_state")
        if outcome == "jail":
            if destination != 0x4A376 or state["faction"] != faction or state["sentence"] <= 0:
                raise ct.TestFailure(f"no native jail sentence: {state}")
            places = {g: ct.local_player(g)["position"] for g in ("", "client")}
            ct.log(f"party jailed in {destination:X}: {state}, places={places}")
            if ct.distance(places[""], places["client"]) > 100:
                raise ct.TestFailure(f"the tether put the guest outside the host's jail cell: {places}")
        else:
            if state["sentence"]:
                raise ct.TestFailure("paying a fine started a jail sentence")
            shared(faction, (0, 0))
            ct.wait_party_gold("the party paid the fine once", purse - amount)
    finally:
        if crime(action="jail_state")["sentence"]:
            prison = ct.player_cell("")
            ct.console_on("", "serveTime")
            ct.wait_for("cleanup completes the native sentence and both releases",
                        lambda: not crime(action="jail_state")["sentence"]
                        and ct.player_cell("") not in (None, prison)
                        and ct.player_cell("client") == ct.player_cell("") and ct.both_see_each_other()
                        and all(ct.takes_input(g) for g in ("", "client")), 45)
        crime(action="reset_alarm")
        if copy := ct.player_copy(""):
            crime(action="reset_alarm", actor=copy)
        for actor in spawned:
            crime(action="reset_alarm", actor=actor)
            ct.console_on("", f"{actor:X}.disable")
            ct.console_on("", f"{actor:X}.markfordelete")
        if ct.player_cell("") != old_cell:
            ct.both_go(ct.HOME_CELL)
        crime(action="set_owner", target=ct.SMALL_CHEST, owner=owner)
        restore(before, hostile)
        ct.console_on("", f"player.setav invisibility {invisible}")
        now = ct.container_items("", ct.SMALL_CHEST)
        for item in set(now) | set(chest_before):
            diff = chest_before.get(item, 0) - now.get(item, 0)
            if diff:
                ct.console_on("", f"{ct.SMALL_CHEST:X}.{'additem' if diff > 0 else 'removeitem'} {item:X} {abs(diff)}")
        for game, items in inventory.items():
            for item, wanted in items.items():
                have = ct.item_count(game, 0x14, item)
                if have != wanted:
                    ct.console_on(game, f"player.{'additem' if have < wanted else 'removeitem'} {item:X} {abs(have - wanted)}")
        ct.set_party_gold(gold, wait=False)


def guest_guard_jail():
    """A guest's crime causes guard arrest; its jail dialogue choice sends the whole party to jail."""
    guard_arrest()


def host_guard_jail():
    """The host's arrest and jail sentence bring every guest to the same jail cell."""
    guard_arrest(offender="")


def guest_guard_fine():
    """An arrested guest pays through vanilla guard dialogue, from the party's purse, with no jail sentence."""
    guard_arrest(outcome="fine")


def guest_guard_resists():
    """A guest refusing vanilla arrest is the guard's combat target while the host stays unseen."""
    with ct.GodMode():
        guard_arrest(outcome="resist")


def register(scenarios, tags):
    for name, run in {"party-bounty-totals": totals, "guest-pays-party-fine": guest_pays,
                      "guest-theft-host-witnesses": guest_theft,
                      "guest-container-theft-and-fine": guest_container_fine,
                      "guest-pickpocket-party-alarm": guest_pickpocket_alarm,
                      "guest-assault-party-alarm": guest_assault_alarm,
                      "guest-lockpick-party-alarm": guest_lockpick_alarm,
                      "guest-trespass-state": guest_trespass_state,
                      "guest-crime-flags": guest_crime_flags,
                      "guest-guard-arrest-jail": guest_guard_jail,
                      "host-guard-arrest-jail": host_guard_jail,
                      "guest-guard-arrest-fine": guest_guard_fine,
                      "guest-guard-arrest-resist": guest_guard_resists,
                      "guest-murder-party-alarm": guest_murder_alarm}.items():
        scenarios[name] = run
        tags[name] = {"crime", "world", "items"}
