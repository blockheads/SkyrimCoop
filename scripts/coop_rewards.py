"""Quest rewards: real vanilla dialogue turn-ins, on the host and on a guest."""
import functools

import coop_test as ct

GAMES = ("", "client")
LUTE_QUEST, INGE, LUTE_TOPIC, LUTE = 0xD93FA, 0x198CD, 0xD93D5, 0xDABAB
LUTE_SKILLS = {12: "LightArmor", 13: "Pickpocket", 14: "Lockpicking", 15: "Sneak", 16: "Alchemy", 17: "Speechcraft"}
MARA_QUEST, DINYA, MARA_TOPIC, MARA_PERK = 0x211D5, 0x19DD6, 0x26E79, 0xCC799
CLAW_QUEST, LUCAN, CLAW_TOPIC, CLAW = 0x39645, 0x13487, 0x3961A, 0x39647


def record(game):
    return ct.player_command(game, {"cmd": "player_record"})["record"]


def events(game, quest):
    return ct.player_command(game, {"cmd": "quest_fragments", "quest": quest})


def completed(quest):
    for game in GAMES:
        ct.wait_for(f"[{game or 'host'}] quest {quest:X} completed", lambda game=game:
                    bool(ct.quest_snapshot(game, quest)["flags"] & 2), 15)
    try:
        ct.client_quest_matches(quest, "the completed turn-in")
    except ct.TestFailure:
        ct.log(f"Completed snapshots: host={ct.synced_quest('', quest)}, guest={ct.synced_quest('client', quest)}")
        raise
    host, guest = events("", quest), events("client", quest)
    if host["runs"] < 1 or guest["runs"]:
        raise ct.TestFailure(f"fragments must run on the host alone: host {host}, guest {guest}")
    if guest["stages"] < 1 or guest["items_done"] < 1 or guest["completions"] != 1:
        raise ct.TestFailure(f"[guest] missing native completion/stage events for mods: {guest}")
    ct.log(f"Quest {quest:X}: native events host={host}, guest={guest}")


def prepare(quest, stage):
    ct.console_on("", f"resetquest {quest:X}")
    ct.console_on("", f"startquest {quest:X}")
    ct.console_on("", f"setstage {quest:X} {stage}")
    ct.wait_for(f"[host] quest {quest:X} ready to turn in at {stage}", lambda:
                ct.quest_stage("", quest) == stage, 10)
    ct.wait_for(f"[guest] quest {quest:X} is ready", lambda: ct.quest_stage("client", quest) == stage, 10)
    ct.reset_quest_fragments()


def fill(quest, alias, ref):
    ct.player_command("", {"cmd": "quest_force_alias", "quest": quest, "alias": alias, "ref": ref})
    ct.wait_for(f"[host] quest {quest:X} alias {alias} holds {ref:X}", lambda:
                ct.quest_snapshot("", quest)["aliases"].get(alias, (0,))[0] == ref, 5)


def turn_in(game, npc, copy, topic):
    target = copy if game else npc
    ct.place_before(game, target, 110)
    other = "" if game else "client"
    ct.place_before(other, npc if game else copy, 800, 90)
    ct.activate_on(game, target)
    ct.wait_for(f"[{game or 'host'}] quest giver opens dialogue", lambda: ct.talking_to(game, target), 10)
    ct.wait_for(f"[{game or 'host'}] real turn-in topic {topic:X} is offered", lambda: ct.lists_topic(game, topic), 30)
    ct.pick_with_keys(game, topic)


def stage_giver(npc):
    ct.freeze("", npc)
    goal = ct.stage_spot()
    ct.player_command("", {"cmd": "move_actor", "actor": npc, "position": goal})
    ct.wait_for("quest giver has arrived", lambda: ct.host_npc_state(npc).get("session_id", -1) >= 0, 10)
    ct.freeze("", npc)
    ct.player_command("", {"cmd": "move_actor", "actor": npc, "position": goal})
    ct.wait_for("quest giver stands on the stage", lambda:
                ct.distance(ct.host_npc_state(npc)["position"][:2], goal[:2]) < 40, 10)
    copy = ct.copy_of("client", ct.host_npc_state(npc)["session_id"])
    ct.wait_on("client", "quest giver's copy stands on the stage", lambda actors:
                ct.distance((actors.get(str(copy), {}).get("actual") or [1e9, 1e9])[:2], goal[:2]) < 60, 10)
    ct.freeze("", npc, False)
    ct.console_on("", f"{npc:X}.setrestrained 1")
    return copy


def lute_turn_in(game):
    """Finn's Lute: picker returns it through Inge's real dialogue; all six skills rise on both games once."""
    before = {g: {av: ct.skill(g, av) for av in LUTE_SKILLS} for g in GAMES}
    npc = INGE
    copy = stage_giver(npc)
    try:
        for g, level in (("", 25), ("client", 40)):
            for name in LUTE_SKILLS.values():
                ct.console_on(g, f"player.setav {name} {level}")
        prepare(LUTE_QUEST, 40)
        fill(LUTE_QUEST, 17, npc)
        ct.console_on(game, f"player.additem {LUTE:X} 1")
        baseline = {g: {av: ct.skill(g, av)[0] for av in LUTE_SKILLS} for g in GAMES}
        turn_in(game, npc, copy, LUTE_TOPIC)
        for g in GAMES:
            ct.wait_for(f"[{g or 'host'}] six quest skill rewards reach its own engine", lambda g=g:
                        all(ct.skill(g, av)[0] == level + 1 for av, level in baseline[g].items()), 15)
        completed(LUTE_QUEST)
        ct.close_dialogues()
        ct.screen_marker("", "client")
        for g in GAMES:
            actual = {av: ct.skill(g, av)[0] for av in LUTE_SKILLS}
            wanted = {av: value + 1 for av, value in baseline[g].items()}
            if actual != wanted:
                raise ct.TestFailure(f"[{g or 'host'}] rewards must arrive once: {actual}, wanted {wanted}")
    finally:
        ct.close_dialogues()
        ct.console_on("", f"resetquest {LUTE_QUEST:X}")
        ct.console_on("", f"{npc:X}.setrestrained 0")
        ct.release_host_npc(npc)
        for g in GAMES:
            for av, name in LUTE_SKILLS.items():
                ct.console_on(g, f"player.setav {name} {before[g][av][0]}")


def mara_turn_in(game):
    """The Book of Love: the final conversation with Dinya completes T02 and grants Agent of Mara to both."""
    owned = {g: f"0:{MARA_PERK:X}" in record(g)["perks"] for g in GAMES}
    npc = DINYA
    copy = stage_giver(npc)
    try:
        for g in GAMES:
            ct.console_on(g, f"player.removeperk {MARA_PERK:X}")
        prepare(MARA_QUEST, 100)
        fill(MARA_QUEST, 0, npc)
        turn_in(game, npc, copy, MARA_TOPIC)
        ct.wait_for("Dinya finishes her response and the end fragment sets stage 200", lambda:
                    ct.quest_stage("", MARA_QUEST) == 200, 60)
        for g in GAMES:
            ct.wait_for(f"[{g or 'host'}] its own player has Agent of Mara", lambda g=g:
                        f"0:{MARA_PERK:X}" in record(g)["perks"], 15)
        completed(MARA_QUEST)
    finally:
        ct.close_dialogues()
        ct.console_on("", f"resetquest {MARA_QUEST:X}")
        ct.console_on("", f"{npc:X}.setrestrained 0")
        ct.release_host_npc(npc)
        for g in GAMES:
            if not owned[g]:
                ct.console_on(g, f"player.removeperk {MARA_PERK:X}")


def gold_turn_in(game):
    """The Golden Claw: Lucan's real quest hand-in removes the picker's claw and pays the party's purse once
    (party-purse.md): every player carries the reward."""
    gold = ct.party_gold()
    claws = {g: ct.item_count(g, 0x14, CLAW) for g in GAMES}
    copy = stage_giver(LUCAN)
    try:
        prepare(CLAW_QUEST, 10)
        ct.console_on("", "stopquest MS13LucanCamillaScene")
        ct.console_on("", f"setstage {CLAW_QUEST:X} 60")
        # The host builds the shared dialogue list against its inventory (the documented condition gap).
        for g in GAMES:
            ct.console_on(g, f"player.additem {CLAW:X} 1")
        ct.wait_for("the picker's copy carries its claw", lambda:
                    ct.item_count("", ct.client_on_host(), CLAW) == claws["client"] + 1, 10)
        ct.reset_quest_fragments()
        turn_in(game, LUCAN, copy, CLAW_TOPIC)
        ct.wait_for("Lucan finishes his response and the end fragment sets stage 100", lambda:
                    ct.quest_stage("", CLAW_QUEST) == 100, 60)
        ct.wait_for("the picker receives Lucan's gold", lambda: ct.item_count(game, 0x14, ct.GOLD) > gold, 15)
        completed(CLAW_QUEST)
        ct.close_dialogues()
        paid = ct.party_gold() - gold
        ct.screen_marker("", "client")
        for g in GAMES:
            expected = claws[g] + (0 if g == game else 1)
            if ct.item_count(g, 0x14, CLAW) != expected:
                raise ct.TestFailure(f"[{g or 'host'}] the turn-in took the wrong player's claw")
        ct.log(f"Lucan paid the party {paid} gold through {game or 'host'}")
    finally:
        ct.close_dialogues()
        ct.console_on("", f"resetquest {CLAW_QUEST:X}")
        ct.console_on("", f"{LUCAN:X}.setrestrained 0")
        ct.release_host_npc(LUCAN)
        for g in GAMES:
            delta = claws[g] - ct.item_count(g, 0x14, CLAW)
            if delta:
                ct.console_on(g, f"player.{'additem' if delta > 0 else 'removeitem'} {CLAW:X} {abs(delta)}")
        ct.set_party_gold(gold, wait=False)


def skill_input():
    """A world script's AdvanceSkill input matches one calibrated native gain on each game's own player."""
    av, name, amount = 18, "Alteration", 0.25
    before = {g: ct.skill(g, av) for g in GAMES}
    try:
        for g, level in (("", 25), ("client", 40)):
            ct.console_on(g, f"player.setav {name} {level}")
        calibrated = {}
        for g in GAMES:
            baseline = ct.skill(g, av)
            ct.player_script_call(g, "Game", "AdvanceSkill", [name, amount])
            ct.wait_for(f"[{g or 'host'}] its personal native calibrates a gain", lambda g=g:
                        ct.skill(g, av) > baseline, 10)
            after = ct.skill(g, av)
            if after[0] != baseline[0]:
                raise ct.TestFailure("calibration crossed a skill threshold; use a fresh character")
            calibrated[g] = after[1] - baseline[1]
        baseline = {g: ct.skill(g, av) for g in GAMES}
        ct.script_call("", "Game", "AdvanceSkill", [name, amount])
        for g in GAMES:
            ct.wait_for(f"[{g or 'host'}] the reward reaches its own skill engine", lambda g=g:
                        ct.skill(g, av) > baseline[g], 10)
        ct.screen_marker("", "client")
        for g in GAMES:
            after = ct.skill(g, av)
            if after[0] != baseline[g][0] or abs(after[1] - baseline[g][1] - calibrated[g]) > 0.001:
                raise ct.TestFailure(f"[{g or 'host'}] reward {baseline[g]} -> {after}, calibrated one native gain {calibrated[g]}")
    finally:
        for g in GAMES:
            ct.console_on(g, f"player.setav {name} {before[g][0]}")


def explicit_rewards():
    """Direct book-style grants reach every player; a stat written for a guest belongs on its owner and each player."""
    before = {g: record(g) for g in GAMES}
    skill_before = {g: ct.skill(g, 6)[0] for g in GAMES}
    try:
        for g in GAMES:
            ct.console_on(g, "player.setav OneHanded 25")
        ct.player_script_call("", "Game", "IncrementSkillBy", ["OneHanded", 5])
        ct.player_script_call("", "Game", "AddPerkPoints", [2])
        copy = ct.client_on_host()
        ct.script_call("", "Actor", "ModActorValue", ["CarryWeight", 10.0], on=copy)
        for g in GAMES:
            ct.wait_for(f"[{g or 'host'}] direct skill levels, perk points and the copy's permanent reward reach its own player", lambda g=g:
                        ct.skill(g, 6)[0] == 30 and record(g)["perk_points"] == before[g]["perk_points"] + 2 and
                        abs(record(g)["permanent_values"]["32"] - before[g]["permanent_values"]["32"] - 10) < 0.01, 10)
        ct.wait_for("the host's copy shows the guest owner's carry weight", lambda:
                    abs(ct.actor_value("", copy, 32) - ct.actor_value("client", 0x14, 32)) < 0.01, 10)
        ct.screen_marker("", "client")
        for g in GAMES:
            if ct.skill(g, 6)[0] != 30 or record(g)["perk_points"] != before[g]["perk_points"] + 2:
                raise ct.TestFailure(f"[{g or 'host'}] an explicit reward arrived more than once")
    finally:
        for g in GAMES:
            ct.console_on(g, f"player.setav OneHanded {skill_before[g]}")
            delta = before[g]["permanent_values"]["32"] - record(g)["permanent_values"]["32"]
            ct.console_on(g, f"player.modav CarryWeight {delta}")
            points = before[g]["perk_points"] - record(g)["perk_points"]
            if points:
                ct.player_script_call(g, "Game", "SetPerkPoints", [before[g]["perk_points"]])


def reward_record_rejoin():
    """A guest keeps earned permanent rewards through a host-world load and receives no grants made while away."""
    before = {g: record(g)["permanent_values"]["32"] for g in GAMES}
    key = record("client")["key"]
    try:
        # Deliberately different permanent totals: inheriting the host save's would be wrong.
        ct.console_on("client", "player.modav CarryWeight 17")
        ct.script_call("", "Actor", "ModActorValue", ["CarryWeight", 10.0], on=ct.client_on_host())
        expected = {"": before[""] + 10, "client": before["client"] + 27}
        for g in GAMES:
            ct.wait_for(f"[{g or 'host'}] received its permanent reward", lambda g=g:
                        abs(record(g)["permanent_values"]["32"] - expected[g]) < 0.01, 10)
        ct.wait_for("the host keeps the guest's own permanent total", lambda:
                    abs(ct.kept_records("").get(key, {}).get("permanent_values", {}).get("32", -1) - expected["client"]) < 0.01, 20)
        ct.disconnect_client()
        ct.wait_on("", "the guest has left", lambda actors: ct.other_player(actors) is None)
        ct.script_call("", "Actor", "ModActorValue", ["CarryWeight", 3.0], on=0x14)
        ct.wait_for("the host took a reward while the guest was away", lambda:
                    abs(record("")["permanent_values"]["32"] - expected[""] - 3) < 0.01, 10)
        ct.joins_with_world("with its earned quest rewards")
        actual = record("client")["permanent_values"]["32"]
        if abs(actual - expected["client"]) > 0.01:
            raise ct.TestFailure(f"guest's permanent rewards after loading the host world: {actual}, wanted {expected['client']}")
    finally:
        if not ct.game_state("client").get("online") and not ct.join_status("client").get("joining"):
            ct.rejoin_client()
        for g in GAMES:
            delta = before[g] - record(g)["permanent_values"]["32"]
            ct.console_on(g, f"player.modav CarryWeight {delta}")
        for g in GAMES:
            ct.player_command(g, {"cmd": "place", "position": ct.load_home(g)["position"]})
        for g in GAMES:
            ct.wait_for(f"[{g or 'host'}] returns to its baseline after loading", lambda g=g:
                        ct.distance(ct.my_position(g), ct.load_home(g)["position"]) < 15, 10)


def register(scenarios, tags):
    for stem, function in (("quest-skills", lute_turn_in), ("quest-perk", mara_turn_in), ("quest-gold", gold_turn_in)):
        for game, suffix in (("", "host"), ("client", "guest")):
            name = f"{stem}-turn-in-{suffix}"
            scenarios[name] = functools.partial(function, game)
            tags[name] = {"scripts", "quest", "dialogue", "spawn", "rewards"}
    scenarios["quest-skill-input-reaches-party"] = skill_input
    tags["quest-skill-input-reaches-party"] = {"scripts", "quest", "rewards"}
    scenarios["quest-explicit-rewards-reach-party"] = explicit_rewards
    tags["quest-explicit-rewards-reach-party"] = {"scripts", "quest", "rewards"}
    scenarios["quest-rewards-kept-on-rejoin"] = reward_record_rejoin
    tags["quest-rewards-kept-on-rejoin"] = {"scripts", "quest", "session", "rewards"}
