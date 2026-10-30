"""Scenarios rewards for the co-op harness."""
from ..common import (
    AMREN,
    BEAST_FORM,
    GOLD,
    WORD_FUS,
    WORD_RO,
)
from ..errors import TestFailure
from ..comparisons import distance
from ..utilities import log

import re
GAMES = ("", "client")
LUTE_QUEST, INGE, LUTE_TOPIC, LUTE = 0xD93FA, 0x198CD, 0xD93D5, 0xDABAB
LUTE_SKILLS = {12: "LightArmor", 13: "Pickpocket", 14: "Lockpicking", 15: "Sneak", 16: "Alchemy", 17: "Speechcraft"}
MARA_QUEST, DINYA, MARA_TOPIC, MARA_PERK = 0x211D5, 0x19DD6, 0x26E79, 0xCC799
CLAW_QUEST, LUCAN, CLAW_TOPIC, CLAW = 0x39645, 0x13487, 0x3961A, 0x39647


def record(ctx, game):
    return ctx.player_command(game, {"cmd": "player_record"})["record"]

def has_perk(ctx, game, actor, perk):
    output = ctx.console_on(game, f"{actor:X}.hasperk {perk:X}")
    found = re.search(r">>\s*([0-9.]+)", " ".join(output or []))
    return bool(float(found[1])) if found else None

def copy_perks_follow_native_removal(ctx):
    owned = {g: f"0:{MARA_PERK:X}" in record(ctx, g)["perks"] for g in GAMES}
    try:
        for owner in GAMES:
            observer = "" if owner else "client"
            copy = ctx.player_copy(observer)
            for command, expected in (("addperk", True), ("removeperk", False)):
                ctx.console_on(owner, f"player.{command} {MARA_PERK:X}")
                ctx.wait_for(f"[{owner or 'host'}] native {command} completes", lambda:
                            (f"0:{MARA_PERK:X}" in record(ctx, owner)["perks"]) == expected, 10)
                ctx.wait_for(f"[{observer or 'host'}] the owner's copy follows {command}", lambda:
                            has_perk(ctx, observer, copy, MARA_PERK) is expected, 10)
    finally:
        for game in GAMES:
            command = "addperk" if owned[game] else "removeperk"
            ctx.console_on(game, f"player.{command} {MARA_PERK:X}")

def events(ctx, game, quest):
    return ctx.player_command(game, {"cmd": "quest_fragments", "quest": quest})

def completed(ctx, quest):
    for game in GAMES:
        ctx.wait_for(f"[{game or 'host'}] quest {quest:X} completed", lambda game=game:
                    bool(ctx.quest_snapshot(game, quest)["flags"] & 2), 15)
    try:
        ctx.client_quest_matches(quest, "the completed turn-in")
    except TestFailure:
        log(f"Completed snapshots: host={ctx.synced_quest('', quest)}, guest={ctx.synced_quest('client', quest)}")
        raise
    host, guest = events(ctx, "", quest), events(ctx, "client", quest)
    if host["runs"] < 1 or guest["runs"]:
        raise ctx.failure(f"fragments must run on the host alone: host {host}, guest {guest}")
    if guest["stages"] < 1 or guest["items_done"] < 1 or guest["completions"] != 1:
        raise ctx.failure(f"[guest] missing native completion/stage events for mods: {guest}")
    log(f"Quest {quest:X}: native events host={host}, guest={guest}")

def prepare(ctx, quest, stage):
    ctx.console_on("", f"resetquest {quest:X}")
    ctx.console_on("", f"startquest {quest:X}")
    ctx.console_on("", f"setstage {quest:X} {stage}")
    ctx.wait_for(f"[host] quest {quest:X} ready to turn in at {stage}", lambda:
                ctx.quest_stage("", quest) == stage, 10)
    ctx.wait_for(f"[guest] quest {quest:X} is ready", lambda: ctx.quest_stage("client", quest) == stage, 10)
    ctx.reset_quest_fragments()

def fill(ctx, quest, alias, ref):
    ctx.player_command("", {"cmd": "quest_force_alias", "quest": quest, "alias": alias, "ref": ref})
    ctx.wait_for(f"[host] quest {quest:X} alias {alias} holds {ref:X}", lambda:
                ctx.quest_snapshot("", quest)["aliases"].get(alias, (0,))[0] == ref, 5)

def turn_in(ctx, game, npc, copy, topic):
    target = copy if game else npc
    ctx.place_before(game, target, 110)
    other = "" if game else "client"
    ctx.place_before(other, npc if game else copy, 800, 90)
    ctx.activate_on(game, target)
    ctx.wait_for(f"[{game or 'host'}] quest giver opens dialogue", lambda: ctx.talking_to(game, target), 10)
    ctx.wait_for(f"[{game or 'host'}] real turn-in topic {topic:X} is offered", lambda: ctx.lists_topic(game, topic), 30)
    ctx.pick_with_keys(game, topic)

def stage_giver(ctx, npc):
    ctx.freeze("", npc)
    goal = ctx.stage_spot()
    ctx.player_command("", {"cmd": "move_actor", "actor": npc, "position": goal})
    ctx.wait_for("quest giver has arrived", lambda: ctx.host_npc_state(npc).get("session_id", -1) >= 0, 10)
    ctx.freeze("", npc)
    ctx.player_command("", {"cmd": "move_actor", "actor": npc, "position": goal})
    ctx.wait_for("quest giver stands on the stage", lambda:
                distance(ctx.host_npc_state(npc)["position"][:2], goal[:2]) < 40, 10)
    copy = ctx.copy_of("client", ctx.host_npc_state(npc)["session_id"])
    ctx.wait_on("client", "quest giver's copy stands on the stage", lambda actors:
                distance((actors.get(str(copy), {}).get("actual") or [1e9, 1e9])[:2], goal[:2]) < 60, 10)
    ctx.freeze("", npc, False)
    ctx.console_on("", f"{npc:X}.setrestrained 1")
    return copy

def lute_turn_in(ctx, game):
    """Finn's Lute: picker returns it through Inge's real dialogue; all six skills rise on both games once."""
    before = {g: {av: ctx.skill(g, av) for av in LUTE_SKILLS} for g in GAMES}
    npc = INGE
    copy = stage_giver(ctx, npc)
    try:
        for g, level in (("", 25), ("client", 40)):
            for name in LUTE_SKILLS.values():
                ctx.console_on(g, f"player.setav {name} {level}")
        prepare(ctx, LUTE_QUEST, 40)
        fill(ctx, LUTE_QUEST, 17, npc)
        ctx.console_on(game, f"player.additem {LUTE:X} 1")
        baseline = {g: {av: ctx.skill(g, av)[0] for av in LUTE_SKILLS} for g in GAMES}
        turn_in(ctx, game, npc, copy, LUTE_TOPIC)
        for g in GAMES:
            ctx.wait_for(f"[{g or 'host'}] six quest skill rewards reach its own engine", lambda g=g:
                        all(ctx.skill(g, av)[0] == level + 1 for av, level in baseline[g].items()), 15)
        completed(ctx, LUTE_QUEST)
        ctx.close_dialogues()
        ctx.screen_marker("", "client")
        for g in GAMES:
            actual = {av: ctx.skill(g, av)[0] for av in LUTE_SKILLS}
            wanted = {av: value + 1 for av, value in baseline[g].items()}
            if actual != wanted:
                raise ctx.failure(f"[{g or 'host'}] rewards must arrive once: {actual}, wanted {wanted}")
    finally:
        ctx.close_dialogues()
        ctx.console_on("", f"resetquest {LUTE_QUEST:X}")
        ctx.console_on("", f"{npc:X}.setrestrained 0")
        ctx.release_host_npc(npc)
        for g in GAMES:
            for av, name in LUTE_SKILLS.items():
                ctx.console_on(g, f"player.setav {name} {before[g][av][0]}")

def mara_turn_in(ctx, game):
    """The Book of Love: the final conversation with Dinya completes T02 and grants Agent of Mara to both."""
    owned = {g: f"0:{MARA_PERK:X}" in record(ctx, g)["perks"] for g in GAMES}
    npc = DINYA
    copy = stage_giver(ctx, npc)
    try:
        for g in GAMES:
            ctx.console_on(g, f"player.removeperk {MARA_PERK:X}")
        prepare(ctx, MARA_QUEST, 100)
        fill(ctx, MARA_QUEST, 0, npc)
        turn_in(ctx, game, npc, copy, MARA_TOPIC)
        ctx.wait_for("Dinya finishes her response and the end fragment sets stage 200", lambda:
                    ctx.quest_stage("", MARA_QUEST) == 200, 60)
        for g in GAMES:
            ctx.wait_for(f"[{g or 'host'}] its own player has Agent of Mara", lambda g=g:
                        f"0:{MARA_PERK:X}" in record(ctx, g)["perks"], 15)
        for g in GAMES:
            body = ctx.player_copy(g)
            ctx.wait_for(f"[{g or 'host'}] the other player's copy carries Agent of Mara", lambda g=g, body=body:
                        has_perk(ctx, g, body, MARA_PERK) is True, 10)
        completed(ctx, MARA_QUEST)
    finally:
        ctx.close_dialogues()
        ctx.console_on("", f"resetquest {MARA_QUEST:X}")
        ctx.console_on("", f"{npc:X}.setrestrained 0")
        ctx.release_host_npc(npc)
        for g in GAMES:
            if not owned[g]:
                ctx.console_on(g, f"player.removeperk {MARA_PERK:X}")

def gold_turn_in(ctx, game):
    """The Golden Claw: Lucan's real quest hand-in removes the picker's claw and pays the party's purse once
    (party-purse.md): every player carries the reward."""
    gold = ctx.party_gold()
    claws = {g: ctx.item_count(g, 0x14, CLAW) for g in GAMES}
    copy = stage_giver(ctx, LUCAN)
    try:
        prepare(ctx, CLAW_QUEST, 10)
        ctx.console_on("", "stopquest MS13LucanCamillaScene")
        ctx.console_on("", f"setstage {CLAW_QUEST:X} 60")
        # The host builds the shared dialogue list against its inventory (the documented condition gap).
        for g in GAMES:
            ctx.console_on(g, f"player.additem {CLAW:X} 1")
        ctx.wait_for("the picker's copy carries its claw", lambda:
                    ctx.item_count("", ctx.client_on_host(), CLAW) == claws["client"] + 1, 10)
        ctx.reset_quest_fragments()
        turn_in(ctx, game, LUCAN, copy, CLAW_TOPIC)
        ctx.wait_for("Lucan finishes his response and the end fragment sets stage 100", lambda:
                    ctx.quest_stage("", CLAW_QUEST) == 100, 60)
        ctx.wait_for("the picker receives Lucan's gold", lambda: ctx.item_count(game, 0x14, GOLD) > gold, 15)
        completed(ctx, CLAW_QUEST)
        ctx.close_dialogues()
        paid = ctx.party_gold() - gold
        ctx.screen_marker("", "client")
        for g in GAMES:
            expected = claws[g] + (0 if g == game else 1)
            if ctx.item_count(g, 0x14, CLAW) != expected:
                raise ctx.failure(f"[{g or 'host'}] the turn-in took the wrong player's claw")
        log(f"Lucan paid the party {paid} gold through {game or 'host'}")
    finally:
        ctx.close_dialogues()
        ctx.console_on("", f"resetquest {CLAW_QUEST:X}")
        ctx.console_on("", f"{LUCAN:X}.setrestrained 0")
        ctx.release_host_npc(LUCAN)
        for g in GAMES:
            delta = claws[g] - ctx.item_count(g, 0x14, CLAW)
            if delta:
                ctx.console_on(g, f"player.{'additem' if delta > 0 else 'removeitem'} {CLAW:X} {abs(delta)}")
        ctx.set_party_gold(gold, wait=False)

def skill_input(ctx):
    """A world script's AdvanceSkill input matches one calibrated native gain on each game's own player."""
    av, name, amount = 18, "Alteration", 0.25
    before = {g: ctx.skill(g, av) for g in GAMES}
    try:
        for g, level in (("", 25), ("client", 40)):
            ctx.console_on(g, f"player.setav {name} {level}")
        calibrated = {}
        for g in GAMES:
            baseline = ctx.skill(g, av)
            ctx.player_script_call(g, "Game", "AdvanceSkill", [name, amount])
            ctx.wait_for(f"[{g or 'host'}] its personal native calibrates a gain", lambda g=g:
                        ctx.skill(g, av) > baseline, 10)
            after = ctx.skill(g, av)
            if after[0] != baseline[0]:
                raise ctx.failure("calibration crossed a skill threshold; use a fresh character")
            calibrated[g] = after[1] - baseline[1]
        baseline = {g: ctx.skill(g, av) for g in GAMES}
        ctx.script_call("", "Game", "AdvanceSkill", [name, amount])
        for g in GAMES:
            ctx.wait_for(f"[{g or 'host'}] the reward reaches its own skill engine", lambda g=g:
                        ctx.skill(g, av) > baseline[g], 10)
        ctx.screen_marker("", "client")
        for g in GAMES:
            after = ctx.skill(g, av)
            if after[0] != baseline[g][0] or abs(after[1] - baseline[g][1] - calibrated[g]) > 0.001:
                raise ctx.failure(f"[{g or 'host'}] reward {baseline[g]} -> {after}, calibrated one native gain {calibrated[g]}")
    finally:
        for g in GAMES:
            ctx.console_on(g, f"player.setav {name} {before[g][0]}")

def explicit_rewards(ctx):
    """Direct book-style grants reach every player; a stat written for a guest belongs on its owner and each player."""
    before = {g: record(ctx, g) for g in GAMES}
    skill_before = {g: ctx.skill(g, 6)[0] for g in GAMES}
    try:
        for g in GAMES:
            ctx.console_on(g, "player.setav OneHanded 25")
        ctx.player_script_call("", "Game", "IncrementSkillBy", ["OneHanded", 5])
        ctx.player_script_call("", "Game", "AddPerkPoints", [2])
        copy = ctx.client_on_host()
        ctx.script_call("", "Actor", "ModActorValue", ["CarryWeight", 10.0], on=copy)
        for g in GAMES:
            ctx.wait_for(f"[{g or 'host'}] direct skill levels, perk points and the copy's permanent reward reach its own player", lambda g=g:
                        ctx.skill(g, 6)[0] == 30 and record(ctx, g)["perk_points"] == before[g]["perk_points"] + 2 and
                        abs(record(ctx, g)["permanent_values"]["32"] - before[g]["permanent_values"]["32"] - 10) < 0.01, 10)
        ctx.wait_for("the host's copy shows the guest owner's carry weight", lambda:
                    abs(ctx.actor_value("", copy, 32) - ctx.actor_value("client", 0x14, 32)) < 0.01, 10)
        ctx.screen_marker("", "client")
        for g in GAMES:
            if ctx.skill(g, 6)[0] != 30 or record(ctx, g)["perk_points"] != before[g]["perk_points"] + 2:
                raise ctx.failure(f"[{g or 'host'}] an explicit reward arrived more than once")
    finally:
        for g in GAMES:
            ctx.console_on(g, f"player.setav OneHanded {skill_before[g]}")
            delta = before[g]["permanent_values"]["32"] - record(ctx, g)["permanent_values"]["32"]
            ctx.console_on(g, f"player.modav CarryWeight {delta}")
            points = before[g]["perk_points"] - record(ctx, g)["perk_points"]
            if points:
                ctx.player_script_call(g, "Game", "SetPerkPoints", [before[g]["perk_points"]])

def reward_record_rejoin(ctx):
    """A guest keeps earned permanent rewards through a host-world load and receives no grants made while away."""
    before = {g: record(ctx, g)["permanent_values"]["32"] for g in GAMES}
    key = record(ctx, "client")["key"]
    try:
        # Deliberately different permanent totals: inheriting the host save's would be wrong.
        ctx.console_on("client", "player.modav CarryWeight 17")
        ctx.script_call("", "Actor", "ModActorValue", ["CarryWeight", 10.0], on=ctx.client_on_host())
        expected = {"": before[""] + 10, "client": before["client"] + 27}
        for g in GAMES:
            ctx.wait_for(f"[{g or 'host'}] received its permanent reward", lambda g=g:
                        abs(record(ctx, g)["permanent_values"]["32"] - expected[g]) < 0.01, 10)
        ctx.wait_for("the host keeps the guest's own permanent total", lambda:
                    abs(ctx.kept_records("").get(key, {}).get("permanent_values", {}).get("32", -1) - expected["client"]) < 0.01, 20)
        ctx.disconnect_client()
        ctx.wait_on("", "the guest has left", lambda actors: ctx.other_player(actors) is None)
        ctx.script_call("", "Actor", "ModActorValue", ["CarryWeight", 3.0], on=0x14)
        ctx.wait_for("the host took a reward while the guest was away", lambda:
                    abs(record(ctx, "")["permanent_values"]["32"] - expected[""] - 3) < 0.01, 10)
        ctx.joins_with_world("with its earned quest rewards")
        actual = record(ctx, "client")["permanent_values"]["32"]
        if abs(actual - expected["client"]) > 0.01:
            raise ctx.failure(f"guest's permanent rewards after loading the host world: {actual}, wanted {expected['client']}")
    finally:
        if not ctx.game_state("client").get("online") and not ctx.join_status("client").get("joining"):
            ctx.rejoin_client()
        for g in GAMES:
            delta = before[g] - record(ctx, g)["permanent_values"]["32"]
            ctx.console_on(g, f"player.modav CarryWeight {delta}")
        for g in GAMES:
            ctx.player_command(g, {"cmd": "place", "position": ctx.load_home(g)["position"]})
        for g in GAMES:
            ctx.wait_for(f"[{g or 'host'}] returns to its baseline after loading", lambda g=g:
                        distance(ctx.my_position(g), ctx.load_home(g)["position"]) < 15, 10)

def created_actor_alias(ctx):
    """A quest alias filled with a created actor names that actor's existing copy on the guest."""
    npc, _, copy = ctx.spawn_host_npc(AMREN, restrained=True)
    try:
        prepare(ctx, LUTE_QUEST, 40)
        fill(ctx, LUTE_QUEST, 17, npc)
        ctx.wait_for("the guest's quest role names the created actor's copy", lambda:
                    ctx.quest_snapshot("client", LUTE_QUEST)["aliases"].get(17, (0,))[0] == copy, 10)
    finally:
        ctx.console_on("", f"resetquest {LUTE_QUEST:X}")
        ctx.remove_host_npc(npc)

def offline_story_character(ctx):
    """A returning character takes story spell changes and keeps only its own purchased word unlocks."""
    spell, personal = BEAST_FORM, 0x12FD0
    words = (WORD_FUS, WORD_RO)
    before_words = {(g, w): ctx.word_state(g, w) for g in GAMES for w in words}
    before_spells = {(g, s): f"0:{s:X}" in record(ctx, g)["spells"] for g in GAMES for s in (spell, personal)}
    key = record(ctx, "client")["key"]
    try:
        for word in words:
            ctx.script_call("", "Game", "TeachWord", [{"form": word}])
        ctx.wait_for("both players learn both story words", lambda:
                    all(ctx.word_state(g, w)[0] for g in GAMES for w in words), 10)
        for g in GAMES:
            if f"0:{spell:X}" in record(ctx, g)["spells"]:
                ctx.console_on(g, f"player.removespell {spell:X}")
            for word in words:
                ctx.player_command(g, {"cmd": "form_known", "form": word, "known": True,
                                      "unlocked": g == "client" and word == WORD_FUS})
        ctx.console_on("client", f"player.addspell {personal:X}")
        ctx.wait_for("the host keeps the guest's personal spell and purchased Fus", lambda:
                    (kept := ctx.kept_records("").get(key)) and f"0:{personal:X}" in kept["spells"] and
                    f"0:{WORD_FUS:X}" in kept["words"] and f"0:{WORD_RO:X}" not in kept["words"], 15)
        ctx.disconnect_client()
        ctx.script_call("", "Actor", "AddSpell", [{"form": spell}, False], on=0x14)
        ctx.player_command("", {"cmd": "form_known", "form": WORD_RO, "known": True, "unlocked": True})
        ctx.wait_for("the story gives the host Beast Form while the guest is away", lambda:
                    f"0:{spell:X}" in record(ctx, "")["spells"], 10)
        ctx.joins_with_world("after the story grants Beast Form")
        if f"0:{spell:X}" not in record(ctx, "client")["spells"] or f"0:{personal:X}" not in record(ctx, "client")["spells"]:
            raise ctx.failure("the returning character lost its story or personal spell")
        if ctx.word_state("client", WORD_FUS) != (True, True) or ctx.word_state("client", WORD_RO) != (True, False):
            raise ctx.failure("the returning character did not keep exactly its own purchased words")
        ctx.disconnect_client()
        ctx.script_call("", "Actor", "RemoveSpell", [{"form": spell}], on=0x14)
        ctx.wait_for("the story takes Beast Form back while the guest is away", lambda:
                    f"0:{spell:X}" not in record(ctx, "")["spells"], 10)
        ctx.joins_with_world("after the story takes Beast Form back")
        if f"0:{spell:X}" in record(ctx, "client")["spells"] or f"0:{personal:X}" not in record(ctx, "client")["spells"]:
            raise ctx.failure("the returning character did not take the story's spell removal")
    finally:
        if not ctx.game_state("client").get("online") and not ctx.join_status("client").get("joining"):
            ctx.rejoin_client()
        ctx.script_call("", "Actor", "AddSpell" if before_spells[("", spell)] else "RemoveSpell",
                       [{"form": spell}], on=0x14)
        for (g, s), had in before_spells.items():
            if had != (f"0:{s:X}" in record(ctx, g)["spells"]):
                ctx.console_on(g, f"player.{'addspell' if had else 'removespell'} {s:X}")
        for (g, word), (known, unlocked) in before_words.items():
            ctx.player_command(g, {"cmd": "form_known", "form": word, "known": known, "unlocked": unlocked})
        for g in ("client", ""):
            ctx.reset_player(g, ctx.load_home(g))
            ctx.wait_for(f"[{g or 'host'}] the returning character settles at home",
                        ctx.comes_to_rest(lambda g=g: ctx.my_position(g), still=1), 10)
