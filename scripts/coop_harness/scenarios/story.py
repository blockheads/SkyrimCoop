"""Scenarios story for the co-op harness."""
import time
from ..common import (
    AB_VAMPIRE_01,
    ACTIVATE_KEY,
    ALL_CONTROLS,
    BANNERED_MARE,
    BEAST_FORM,
    C00_QUEST,
    COOKED_BEEF,
    COOKING_SPIT,
    CR03_QUEST,
    CR03_SPAWN_ALIAS,
    CRAFT_RAISED,
    CREATED_ID,
    CREATE_YES,
    CRITTER_SPAWNER,
    EMBERSHARD_MINE,
    EMBERSHARD_REFS,
    ENC_DRAGON,
    FADE_TO_BLACK_IMOD,
    FRIENDS_COUNT_RIFT,
    GIVE_CONTROLS,
    HARBINGER_FACTION,
    HOME_CELL,
    LUTE_QUEST,
    LYDIA,
    MELEE_GAP,
    MONK_INTRO_IMOD,
    MONK_LOOP_IMOD,
    MONK_OUTRO_IMOD,
    MONK_SCENE_QUEST,
    MQ105,
    MQ206,
    NORD_VAMPIRE_RACE,
    OUTDOOR_CELL,
    PLAYER_IS_VAMPIRE,
    PLAYER_VAMPIRE_QUEST,
    QA_SHOUT_BUTTON,
    QA_SHOUT_SCRIPT,
    QA_SHOUT_WORDS,
    RAW_BEEF,
    SALT_PILE,
    SANGUINARE_VAMPIRIS,
    SHRINES,
    SHRINE_CELL,
    STAGE_ON_DEATH,
    STONE_CANCEL,
    STONE_MESSAGE,
    STORY_NODE_PASSED,
    TAKE_CONTROLS,
    VALDR,
    VALDR_QUEST,
    VILKAS,
    VILKAS_HATE,
    VILKAS_TRAINING,
    VOLUNRUUD_AXE,
    VOLUNRUUD_AXE_SHOWN,
    VOLUNRUUD_CELL,
    VOLUNRUUD_DOOR,
    VOLUNRUUD_QUEST,
    VOLUNRUUD_QUEST_ID,
    VOLUNRUUD_SCRIPT,
    WORD_FUS,
)
from ..errors import TestFailure
from ..comparisons import distance
from ..utilities import log


def scenario_global_reaches_guest(ctx):
    """A global the host's game changes (the console's `set`, as a fragment's SetValue does) takes the
    host's value on the client; the client's own change does not reach the host."""
    start = ctx.global_value("", FRIENDS_COUNT_RIFT)
    try:
        ctx.console_on("", f"set FriendsCountRift to {start + 3:g}")
        ctx.wait_for(f"[host] FriendsCountRift is {start + 3:g}", lambda: ctx.global_value("", FRIENDS_COUNT_RIFT) == start + 3, 5)
        ctx.wait_for(f"[client] FriendsCountRift follows the host to {start + 3:g}",
                 lambda: ctx.global_value("client", FRIENDS_COUNT_RIFT) == start + 3, 5)
        ctx.console_on("client", f"set FriendsCountRift to {start + 7:g}")
        ctx.wait_for("[client] FriendsCountRift is its own 7 more", lambda: ctx.global_value("client", FRIENDS_COUNT_RIFT) == start + 7, 5)
        if ctx.global_value("", FRIENDS_COUNT_RIFT) != start + 3:
            raise ctx.failure("[host] FriendsCountRift took the client's value: a guest decided world state")
    finally:
        ctx.console_on("", f"set FriendsCountRift to {start:g}")
        ctx.wait_for(f"[client] FriendsCountRift back at the host's {start:g}",
                 lambda: ctx.global_value("client", FRIENDS_COUNT_RIFT) == start, 5)

def scenario_late_join_gets_globals(ctx):
    """A client that joins after the host changed a global starts with the host's value."""
    start = ctx.global_value("", FRIENDS_COUNT_RIFT)
    ctx.disconnect_client()
    try:
        ctx.console_on("", f"set FriendsCountRift to {start + 5:g}")
        ctx.wait_for(f"[host] FriendsCountRift is {start + 5:g}", lambda: ctx.global_value("", FRIENDS_COUNT_RIFT) == start + 5, 5)
        if ctx.global_value("client", FRIENDS_COUNT_RIFT) == start + 5:
            raise ctx.failure("[client] has the host's value while away: the test shows nothing")
        ctx.rejoin_client()
        ctx.wait_for(f"[client] joins with the host's FriendsCountRift {start + 5:g}",
                 lambda: ctx.global_value("client", FRIENDS_COUNT_RIFT) == start + 5, 5)
    finally:
        if ctx.game_state("client").get("online") is False:
            ctx.rejoin_client()
        ctx.console_on("", f"set FriendsCountRift to {start:g}")
        ctx.wait_for(f"[client] FriendsCountRift back at the host's {start:g}",
                 lambda: ctx.global_value("client", FRIENDS_COUNT_RIFT) == start, 5)

def scenario_guest_story_starts_no_quest(ctx):
    """Both players walk into the Bannered Mare: each game raises its own arrival story event (a location
    change). The host's story manager starts quests for it (the bard's song, the hold's radiant quests); the
    client's decides nothing, its event handled with no quest started: quests come from the host."""
    since = {}
    for name in ("", "client"):
        ctx.player_command(name, {"cmd": "story_trace", "on": True})
        ctx.arrival_story_events(name)
        since[name] = ctx.engine_events(name)[1]
    try:
        ctx.both_go(BANNERED_MARE)
        for name, wanted in (("", True), ("client", False)):
            who = f"[{name or 'host'}]"
            events = ctx.arrival_story_events(name)
            if not events:
                raise ctx.failure(f"{who} raised no location change story event on arriving")
            event_id = events[-1][2]
            ctx.wait_for(f"{who} its story manager handled the arrival (event {event_id})",
                     lambda name=name, event_id=event_id: ctx.story_event_outcome(name, event_id, since[name]) is not None, 15)
            if ctx.story_event_outcome(name, event_id, since[name]) != wanted:
                raise ctx.failure(f"{who} its story manager {'started no quest' if wanted else 'started quests'} for the "
                                  f"arrival: {'the test shows nothing' if wanted else 'a guest decided quests'}")
    finally:
        for name in ("", "client"):
            ctx.player_command(name, {"cmd": "story_trace", "on": False})
        ctx.both_go(HOME_CELL)

def scenario_guest_location_change_starts_story(ctx):
    """The client leads the party into the Bannered Mare (party-model.md rule 4). Its own game's story manager starts
    nothing; it sends its arrival (CLOC) to the host, whose game raises it for the client's character and judges it
    for that character. The party arrives once: of the two arrivals the host's story manager sees, the copy's and its
    own player's, the first it processes starts the arrival's quests (a node passes), the second starts none (the bard's
    song and the greeting start once)."""
    copy = ctx.player_copy("")
    if copy is None:
        raise ctx.failure("[host] shows no copy of the client's character")
    ctx.player_command("", {"cmd": "story_trace", "on": True})
    ctx.story_trace_read("")
    since = ctx.engine_events("")[1]
    captured, outcomes = [], []
    try:
        before = {name: ctx.player_cell(name) for name in ("", "client")}
        ctx.console_on("client", f"coc {BANNERED_MARE}")
        for name in ("", "client"):
            ctx.wait_for(f"[{name or 'host'}] arrived in {BANNERED_MARE}", lambda name=name: ctx.player_cell(name) not in (None, before[name]), 60)

        def arrivals():
            more_captured, more_outcomes = ctx.story_trace_read("")
            captured.extend(more_captured)
            outcomes.extend(more_outcomes)
            return {who: [e[2] for e in captured if e[3] == who] for who in (copy, 0x14)}
        ctx.wait_for("[host] its game raised the client's arrival for its character and its own player's arrival",
                 lambda: all(arrivals().values()), 30)
        ids = {who: found[-1] for who, found in arrivals().items()}
        ctx.wait_for("[host] its story manager processed both arrivals",
                 lambda: len(ctx.story_processing_order("", set(ids.values()), since)) == 2, 15)
        order = ctx.story_processing_order("", set(ids.values()), since)
        first, second = order
        whose = {ids[copy]: "the client's character's", ids[0x14]: "its own player's"}
        log(f"[host] processed {whose[first[0]]} arrival first, then {whose[second[0]]}")
        if not first[1]:
            raise ctx.failure(f"[host] the party's arrival ({whose[first[0]]}, processed first) started no quest")
        if second[1]:
            raise ctx.failure(f"[host] {whose[second[0]]} arrival started quests again: the party arrived twice")
        arrivals()
        if first[0] == ids[copy] and not any(o[0] == ids[copy] and o[1] == STORY_NODE_PASSED for o in outcomes):
            raise ctx.failure("[host] no story node passed for the client's character's arrival")
    finally:
        ctx.player_command("", {"cmd": "story_trace", "on": False})
        ctx.both_go(HOME_CELL)

def scenario_guest_craft_raises_story(ctx):
    """The client cooks beef at QASmoke's cooking spit: E opens its menu, E takes the recipe list, Up or Down to the one
    recipe its salt and beef make, E and the box's Yes cook it. Its own game's story manager starts nothing for the
    craft (CRFT); it sends the craft to the host, whose game raises it for the client's character and processes it."""
    copy = ctx.player_copy("")
    _, host_mark = ctx.log_trail("", 1 << 62)
    _, client_mark = ctx.log_trail("client", 1 << 62)
    since = ctx.engine_events("")[1]
    ctx.player_command("client", {"cmd": "strip"})
    for item in (SALT_PILE, RAW_BEEF):
        ctx.console_on("client", f"player.additem {item:08X} 1")
    try:
        ctx.open_crafting_station("client", COOKING_SPIT, "cooking spit")
        # The list holds every recipe, the ones the player cannot make greyed (enabled 0): to the one it can.
        count = int(ctx.crafting_entries("client", "entryList.length") or 0)
        target = next((i for i in range(count) if ctx.crafting_entries("client", f"entryList.{i}.enabled")), None)
        if target is None:
            raise ctx.failure(f"[client] none of the {count} recipes is one its salt and beef make")
        ctx.select_crafting_entry("client", target)
        selected = lambda field: ctx.crafting_entries("client", f"selectedEntry.{field}")
        ctx.tap_key("client", ACTIVATE_KEY)
        ctx.wait_for("[client] asks whether to create it", lambda: "MessageBoxMenu" in ctx.open_menus("client"), 5)
        ctx.presses_box_button("client", CREATE_YES)
        ctx.wait_for(f"[client] cooked {selected('text')}", lambda: ctx.item_count("client", 0x14, COOKED_BEEF) == 1, 5)
        # The menu adds it past AddObjectToContainer (TESObjectREFR::sub_19688): the copy carries it too.
        ctx.wait_for("[host] its copy of the client carries the cooked beef", lambda: ctx.item_count("", copy, COOKED_BEEF) == 1, 10)
        ctx.wait_for("[client] sent its craft to the host",
                 lambda: any("GuestWorldState: story event CRFT sent" in line["text"] for line in ctx.log_trail("client", client_mark)[0]), 5)

        def raised():
            for line in ctx.log_trail("", host_mark)[0]:
                found = CRAFT_RAISED.search(line["text"])
                if found:
                    return int(found.group(1)), int(found.group(2), 16)
            return None
        ctx.wait_for("[host] its game raised the client's craft", lambda: raised() is not None, 10)
        event_id, character = raised()
        if character != copy:
            raise ctx.failure(f"[host] the craft was raised for {character:08X}, not the client's character {copy:08X}")
        ctx.wait_for(f"[host] its story manager processed the craft (event {event_id})",
                 lambda: ctx.story_event_outcome("", event_id, since) is not None, 15)
    finally:
        ctx.leave_crafting_station("client", "cooking spit")

def scenario_guest_spawns_no_critters(ctx):
    """The host places a critter spawner: its script (CritterSpawn, on loading) places the dragonflies on the
    host's game, and the client shows those. The client's own spawner (made as the host's created object) runs
    no world script, so it places none of its own."""
    ctx.console_on("", "set gamehour to 12")
    before = {name: ctx.created_by_base(name) for name in ("", "client")}
    before_ids = {i for i in ctx.world_state("") if i >= CREATED_ID}
    ctx.console_on("", f"player.placeatme {CRITTER_SPAWNER:X} 1")
    try:
        def critters(name):
            return {base: n - before[name].get(base, 0) for base, n in ctx.created_by_base(name).items()
                    if base != CRITTER_SPAWNER and n > before[name].get(base, 0)}
        host = ctx.wait_for("[host] its spawner places the dragonflies", lambda: critters("") or None, 20)
        ctx.wait_for("[client] shows the host's dragonflies", lambda: all(critters("client").get(b, 0) >= n for b, n in host.items()), 10)
        stable_until = time.monotonic() + 4.0
        ctx.wait_for("[client] places none of its own (as many as the host's for 4 s)",
                 lambda: critters("client") == critters("") and time.monotonic() >= stable_until, 10)
    finally:
        # The spawner first (it places more while it lives), then what it placed; the client's twins go with the
        # host's (its own, if it placed any, stay: the world check names them).
        def placed():  # made since the scenario began, alive: not disabled (0x800) or deleted (0x20)
            return [(ref, r) for ref, r in ctx.world_state("").items()
                    if ref >= CREATED_ID and ref not in before_ids and not r.get("flags", 0) & 0x820]
        for ref, _ in sorted(placed(), key=lambda item: item[1]["base"] != CRITTER_SPAWNER):
            ctx.console_on("", f"{ref:X}.disable")
            ctx.console_on("", f"{ref:X}.markfordelete")
        ctx.wait_for("[host] nothing the spawner placed is left", lambda: not placed(), 10)

def scenario_wildlife_takes_hosts_pick(ctx):
    """Riverwood's placed leveled wildlife (a Rabbit reference can roll a Fox) rolls on each game: the client's
    copy takes the host's pick (its base swapped, CharacterService::ApplyPlacedBase) and stays enabled and loaded."""
    _game = ctx.game()
    ctx.both_go(OUTDOOR_CELL)
    try:
        def leveled():
            _game = ctx.game('')
            return {s["mod"]["session_id"]: s["identity"] for s in (_game.read_status() or {}).get("world", {}).values()
                    if s["role"] == "own" and not s["player"] and s["identity"].get("temporary_base")}
        host = ctx.wait_for("[host] has placed leveled wildlife around", lambda: leveled() or None, 20, _game=_game)

        def wrong():
            _game = ctx.game('client')
            copies = {s["mod"]["session_id"]: s["identity"] for s in (_game.read_status() or {}).get("world", {}).values()
                      if s["role"] == "copy"}
            return [f"{who['name']} sid {sid}: " + (f"client's is a {copies[sid]['name']} (race {copies[sid]['race']:X})"
                                                    if sid in copies else "no loaded copy on the client")
                    for sid, who in host.items()
                    if sid not in copies or (copies[sid]["name"], copies[sid]["race"]) != (who["name"], who["race"])]
        try:
            ctx.wait_for(f"[client] {len(host)} loaded copies, each the host's pick", lambda: not wrong(), 20, _game=_game)
        except TestFailure:
            raise ctx.failure("[client] copies of the host's leveled wildlife: " + "; ".join(wrong()))
    finally:
        ctx.both_go(HOME_CELL)

def scenario_death_stage_runs_once(ctx):
    """The host kills an NPC whose script sets a quest stage on its death (Valdr: defaultSetStageOnDeath, which
    then goes to its "inactive" state). The script runs on the host's game; the client's copy dies too, but its
    script stays waiting: a stage it set would run the quest's fragment a second time."""
    ctx.reset_quest_fragments()
    npc, _, copy = ctx.spawn_host_npc(f"{VALDR:08X}", restrained=True)
    try:
        for name, actor in (("", npc), ("client", copy)):
            if ctx.stage_script_state(name, actor) != "waiting":
                raise ctx.failure(f"[{name or 'host'}] {STAGE_ON_DEATH} on {actor:X} is "
                                  f"'{ctx.stage_script_state(name, actor)}', not waiting")
        ctx.console_on("", f"{npc:08X}.kill")
        ctx.wait_on("client", f"NPC copy {copy:08X} dies", lambda actors: actors.get(str(copy), {}).get("dead"))
        ctx.wait_for("[host] its death script ran", lambda: ctx.stage_script_state("", npc) == "inactive", 10)
        waiting_until = time.monotonic() + 3.0
        ctx.wait_for("[client] its copy's death script stays waiting (3 s)",
                 lambda: ctx.stage_script_state("client", copy) != "waiting" or time.monotonic() >= waiting_until, 10)
        if (state := ctx.stage_script_state("client", copy)) != "waiting":
            raise ctx.failure(f"[client] its copy's {STAGE_ON_DEATH} ran ('{state}'): the client's own script set the stage")
        if runs := ctx.quest_fragments("client", VALDR_QUEST):
            raise ctx.failure(f"[client] ran {runs} of the quest's fragments: only the host's game runs them")
    finally:
        ctx.console_on("", f"resetquest {VALDR_QUEST:X}")

def scenario_quest_stage_reaches_guest(ctx):
    """A stage the host's game sets is done on the client too, with its objective, through the client's
    own SetStage; the stage's fragment (a world script) runs on the host only."""
    ctx.console_on("", "resetquest BardsCollegeLute")
    ctx.client_quest_matches(LUTE_QUEST, "Finn's Lute is reset")
    ctx.reset_quest_fragments()
    try:
        for stage in (15, 20):
            ctx.console_on("", f"setstage BardsCollegeLute {stage}")
            ctx.wait_for(f"[host] stage {stage} is done", lambda: stage in ctx.quest_snapshot("", LUTE_QUEST)["done"], 5)
            ctx.client_quest_matches(LUTE_QUEST, f"stage {stage} is done")
        if ctx.synced_quest("client", LUTE_QUEST)["objectives"].get(20) != 1:
            raise ctx.failure("[client] objective 20 is not shown")
        if not ctx.quest_fragments("", LUTE_QUEST):
            raise ctx.failure("[host] ran no fragment for its stages: the test shows nothing")
        if ctx.quest_fragments("client", LUTE_QUEST):
            raise ctx.failure(f"[client] ran {ctx.quest_fragments('client', LUTE_QUEST)} of the stages' fragments")
    finally:
        ctx.console_on("", "resetquest BardsCollegeLute")
        ctx.client_quest_matches(LUTE_QUEST, "Finn's Lute is reset again")

def scenario_guest_script_stage_reaches_host(ctx):
    """A script on the client's own game (an item's, as the Elder Scroll's MQ206.SetStage(20)) sets a quest's stage:
    the host's game sets it, for the client (PlayerNatives::SendToHost), and runs its fragment; the client takes the
    stage from the host, its fragment skipped."""
    ctx.console_on("", "resetquest BardsCollegeLute")
    ctx.client_quest_matches(LUTE_QUEST, "Finn's Lute is reset")
    ctx.reset_quest_fragments()
    try:
        ctx.script_call("client", "Quest", "SetCurrentStageID", [15], on=LUTE_QUEST)
        ctx.wait_for("[host] stage 15 is done", lambda: 15 in ctx.quest_snapshot("", LUTE_QUEST)["done"], 5)
        ctx.client_quest_matches(LUTE_QUEST, "stage 15 is done")
        if not ctx.quest_fragments("", LUTE_QUEST):
            raise ctx.failure("[host] ran no fragment for the client's stage")
        if ctx.quest_fragments("client", LUTE_QUEST):
            raise ctx.failure(f"[client] ran {ctx.quest_fragments('client', LUTE_QUEST)} of the stage's fragments")
        ctx.console_on("", "resetquest BardsCollegeLute")
        ctx.client_quest_matches(LUTE_QUEST, "Finn's Lute is reset again")
    finally:
        for name in ("", "client"):
            ctx.console_on(name, "resetquest BardsCollegeLute")

def scenario_quest_completes_for_guest(ctx):
    """The host's game completes a quest: completed on the client, its objectives as the host's."""
    ctx.console_on("", "resetquest BardsCollegeLute")
    ctx.client_quest_matches(LUTE_QUEST, "Finn's Lute is reset")
    try:
        ctx.console_on("", "setstage BardsCollegeLute 200")
        ctx.wait_for("[host] Finn's Lute is completed", lambda: ctx.quest_snapshot("", LUTE_QUEST)["flags"] & 0x2, 5)
        ctx.client_quest_matches(LUTE_QUEST, "Finn's Lute is completed")
    finally:
        ctx.console_on("", "resetquest BardsCollegeLute")
        ctx.client_quest_matches(LUTE_QUEST, "Finn's Lute is reset again")

def scenario_quest_roles_match_host(ctx):
    """A radiant quest the host's game starts runs on the client with the host's picks for its roles
    (who gives the job), not rolled by the client, and the client runs none of its start's fragments."""
    ctx.console_on("", "stopquest CR03")
    ctx.client_quest_matches(CR03_QUEST, "CR03 is stopped")
    ctx.reset_quest_fragments()
    try:
        ctx.console_on("", "startquest CR03")
        ctx.wait_for("[host] CR03 runs with a quest giver",
                 lambda: ctx.synced_quest("", CR03_QUEST)["flags"] & 1 and ctx.synced_quest("", CR03_QUEST)["aliases"], 5)
        ctx.client_quest_matches(CR03_QUEST, "CR03 runs with the host's roles")
        if ctx.quest_fragments("client", CR03_QUEST):
            raise ctx.failure("[client] ran CR03's start fragments itself")
    finally:
        ctx.console_on("", "stopquest CR03")
        ctx.client_quest_matches(CR03_QUEST, "CR03 is stopped again")

def scenario_quest_fill_loads_on_guest(ctx):
    """The host's quest fills a role with a reference in a cell neither game loaded (as a radiant quest fills its
    target): the host's game reads it from the plugin, and so does the client's, the quest's role filled the same."""
    _game = ctx.game()
    ctx.console_on("", "stopquest CR03")
    ctx.client_quest_matches(CR03_QUEST, "CR03 is stopped")
    try:
        ctx.console_on("", "startquest CR03")
        ctx.client_quest_matches(CR03_QUEST, "CR03 runs with the host's roles")
        ref = next((r for r in EMBERSHARD_REFS if not ctx.in_memory("client", r)), None)
        if ref is None:
            raise ctx.failure("[client] every Embershard Mine reference is in memory already: restart the games")
        _game = ctx.game('')
        response = _game.rpc({"cmd": "quest_force_alias", "quest": CR03_QUEST, "alias": CR03_SPAWN_ALIAS,
                        "ref": ref, "parent": EMBERSHARD_MINE}) or {}
        if not response.get("ok"):
            raise ctx.failure(f"[host] cannot fill CR03's alias {CR03_SPAWN_ALIAS}: {response.get('error')}")
        ctx.wait_for(f"[client] {ref:X} loaded and in the role",
                 lambda: ctx.quest_snapshot("client", CR03_QUEST)["aliases"].get(CR03_SPAWN_ALIAS, (0,))[0] == ref, 10, _game=_game)
        ctx.client_quest_matches(CR03_QUEST, "CR03's roles are the host's")
    finally:
        ctx.console_on("", "stopquest CR03")

def scenario_late_join_gets_quests(ctx):
    """A client that joins after the host's game moved a quest on starts with the host's quest, taken as a
    load takes it: no fragment runs on the client."""
    ctx.console_on("", "resetquest BardsCollegeLute")
    ctx.client_quest_matches(LUTE_QUEST, "Finn's Lute is reset")
    ctx.disconnect_client()
    try:
        ctx.console_on("", "setstage BardsCollegeLute 20")
        ctx.wait_for("[host] stage 20 is done", lambda: 20 in ctx.quest_snapshot("", LUTE_QUEST)["done"], 5)
        if 20 in ctx.quest_snapshot("client", LUTE_QUEST)["done"]:
            raise ctx.failure("[client] has stage 20 while away: the test shows nothing")
        ctx.reset_quest_fragments()
        ctx.rejoin_client()
        ctx.client_quest_matches(LUTE_QUEST, "joins with stage 20 done")
        if ctx.quest_fragments("client", LUTE_QUEST):
            raise ctx.failure("[client] ran the quest's fragments taking the host's quest at join")
    finally:
        if ctx.game_state("client").get("online") is False:
            ctx.rejoin_client()
        ctx.console_on("", "resetquest BardsCollegeLute")
        ctx.client_quest_matches(LUTE_QUEST, "Finn's Lute is reset again")

def scenario_words_reach_party(ctx):
    """Forms as a native's arguments, and the party's words (docs/design/dragonborn.md): the client presses the QA
    shout button. The host's game runs its script for the client (party-model.md, rule 2), and Game.TeachWord and
    Game.UnlockWord act on this game's player (NativeReach), so the client's game runs them, each word carried by its
    GameId. Each word its player learns, and each a script unlocks, is the party's: the host's player learns and
    unlocks the twelve too."""
    def reset():
        ctx.forget_words(QA_SHOUT_WORDS)
        # The button as a first press finds it (a press closes it for good).
        ctx.player_command("", {"cmd": "script_var", "object": QA_SHOUT_BUTTON, "script": QA_SHOUT_SCRIPT, "var": "currentIndex", "value": 0})
        ctx.player_command("", {"cmd": "script_state", "object": QA_SHOUT_BUTTON, "script": QA_SHOUT_SCRIPT, "state": "open"})
        for name in ("", "client"):
            ctx.console_on(name, f"{QA_SHOUT_BUTTON:X}.sae Open")
    reset()
    try:
        ctx.place_before("client", QA_SHOUT_BUTTON, 110)
        ctx.activate_on("client", QA_SHOUT_BUTTON)
        ctx.wait_for("[client] learns and unlocks the twelve words",
                 lambda: all(ctx.word_state("client", w) == (True, True) for w in QA_SHOUT_WORDS), 15)
        ctx.wait_for("[host] its player learns and unlocks them too",
                 lambda: all(ctx.word_state("", w) == (True, True) for w in QA_SHOUT_WORDS), 10)
    finally:
        reset()

def scenario_host_word_reaches_guest(ctx):
    """The host's own script (a quest's, no player's action behind it) teaches its player a word and unlocks it: the
    client's player learns it, through its engine's own Game.TeachWord (the word's HUD, the shout), and has it
    unlocked; a word forgotten again stays forgotten on the other game (only learning is shared)."""
    ctx.forget_words([WORD_FUS])
    try:
        ctx.script_call("", "Game", "TeachWord", [{"form": WORD_FUS}])
        ctx.wait_for("[client] learns the host's word", lambda: ctx.word_state("client", WORD_FUS)[0], 10)
        if ctx.word_state("client", WORD_FUS)[1]:
            raise ctx.failure("[client] the word came unlocked: the host's script only taught it")
        ctx.script_call("", "Game", "UnlockWord", [{"form": WORD_FUS}])
        ctx.wait_for("[client] has it unlocked", lambda: ctx.word_state("client", WORD_FUS) == (True, True), 10)
        ctx.wait_for("[host] its own player has it", lambda: ctx.word_state("", WORD_FUS) == (True, True), 5)
    finally:
        ctx.forget_words([WORD_FUS])

def scenario_dragon_soul_reaches_party(ctx):
    """A dragon each player kills gives every player its soul (dragonborn.md, "A soul, traced"). The host spawns a
    dragon on the stage and it dies by the killer's hand (Actor.Kill: OnDeath's killer is the host's player, then the
    client's character on the host). Its script waits for the killer near (deadAndWaiting), then
    MQKillDragonScript.DeathSequence absorbs the soul into Game.GetPlayer(): the killer (rule 2 for the client's
    kill). Each game's player gains exactly one soul per dragon, through its engine's own value, and the client's
    copy of the dragon dissolves as the host's does (its art: what a reference shows), and every game shows the
    absorb's streams between the dragon and its own player (a death is the world's event)."""
    def arts(name, refs):
        """The art objects the game plays on the references or aims at them: (art, target, aimed at)."""
        return {(effect["form"], effect["target"], effect["aim_target"]) for ref in refs
                for effect in ctx.player_command(name, {"cmd": "reference_effects", "form": ref})["effects"] if not effect["shader"]}

    def death_sequence_runs():
        return any(frame.lower().startswith("mqkilldragonscript.deathsequence")
                   for stack in ctx.player_command("", {"cmd": "script_stacks"})["stacks"] for frame in stack["frames"])

    if ctx.script_running("", "dragonActorSCRIPT"):
        raise ctx.failure("[host] an earlier dragon's script still waits for its killer: restart the duo")
    for killer_name in ("", "client"):
        before = {name: ctx.dragon_souls(name) for name in ("", "client")}
        # Where placeatme puts it, by the host: a dragon fits on no stage spot.
        actors = set(ctx.local_player("").get("local_actors", {}))
        ctx.console_on("", f"player.placeatme {ENC_DRAGON:X} 1")
        npc = int(next(iter(ctx.wait_for("[host] the dragon is loaded",
                                     lambda: set(ctx.local_player("").get("local_actors", {})) - actors, 15))))
        killer = 0x14 if not killer_name else ctx.player_copy("")
        try:
            ctx.script_call("", "Actor", "Kill", [{"form": killer}], on=npc)
            ctx.wait_for("[host] the dragon's script waits for its killer (deadAndWaiting)",
                     lambda: ctx.script_running("", "dragonActorSCRIPT"), 10)
            # The killer walks up to the body, as a player would; the other player comes along.
            session_id = ctx.host_npc_state(npc)["session_id"]
            ctx.place_before("", npc, 400)
            copy = ctx.copy_of("client", session_id)
            ctx.place_before("client", copy, 400)
            # Each game's art, sampled until the souls arrive (the absorb's streams last seconds).
            shown = {"": set(), "client": set()}

            def souls_gained():
                shown[""] |= arts("", {npc, 0x14, killer})
                shown["client"] |= arts("client", {copy, 0x14})
                return all(ctx.dragon_souls(name) >= before[name] + 1 for name in ("", "client"))
            ctx.wait_for(f"[both] gain the soul of the dragon the {killer_name or 'host'} killed", souls_gained, 60)
            ctx.wait_for("[host] the dragon's death sequence ends", lambda: not death_sequence_runs(), 30)
            if missing := {art for art, _, _ in shown[""]} - {art for art, _, _ in shown["client"]}:
                raise ctx.failure(f"[client] never showed the host's art {sorted(f'{art:X}' for art in missing)}")
            if not any(target == 0x14 and aimed == copy for _, target, aimed in shown["client"]):
                raise ctx.failure("[client] no absorb stream ran on its player toward its copy of the dragon")
            if gained := {name or "host": ctx.dragon_souls(name) - before[name] for name in ("", "client")
                          if ctx.dragon_souls(name) != before[name] + 1}:
                raise ctx.failure(f"souls gained other than one: {gained}")
        finally:
            ctx.console_on("", f"{npc:08X}.disable")
            ctx.console_on("", f"{npc:08X}.markfordelete")

def scenario_title_reaches_party(ctx):
    """The story's titles and blood are the party's (dragonborn.md): a quest's script (no player's action behind it)
    makes the client's character on the host Harbinger (C06's Game.GetPlayer().AddToFaction, run for the client) and
    gives the host's player Beast Form (C03's rampage): both players are Harbinger and both can transform. The story
    takes Beast Form back from both."""
    def has_beast_form(name):
        return f"0:{BEAST_FORM:X}" in ctx.player_command(name, {"cmd": "player_record"})["record"]["spells"]

    def reset():
        # The console's own calls: not a script's, so not the party's.
        for name in ("", "client"):
            if ctx.faction_rank(name, 0x14, HARBINGER_FACTION) >= 0:
                ctx.console_on(name, f"player.removefromfaction {HARBINGER_FACTION:X}")
            if has_beast_form(name):
                ctx.console_on(name, f"player.removespell {BEAST_FORM:X}")

    reset()
    try:
        ctx.script_call("", "Actor", "AddToFaction", [{"form": HARBINGER_FACTION}], on=ctx.player_copy(""))
        ctx.wait_for("[both] players are Harbinger", lambda: all(ctx.faction_rank(name, 0x14, HARBINGER_FACTION) >= 0 for name in ("", "client")), 10)
        ctx.script_call("", "Actor", "AddSpell", [{"form": BEAST_FORM}, False], on=0x14)
        ctx.wait_for("[client] its player has Beast Form", lambda: has_beast_form("client"), 10)
        ctx.script_call("", "Actor", "RemoveSpell", [{"form": BEAST_FORM}], on=0x14)
        ctx.wait_for("[both] Beast Form is gone", lambda: not has_beast_form("client") and not has_beast_form(""), 10)
    finally:
        reset()

def scenario_vampirism_is_each_players(ctx):
    """Vampirism is each player's own (docs/design/system-quests.md): the client catches Sanguinare Vampiris (the
    trap's, no catch roll); three days on (the host's clock run fast) its effect's script calls
    PlayerVampireQuest.VampireChange, which makes the quest one per player on every game. The client turns, and the
    host's copy of it; the host's player stays as it was; PlayerIsVampire is each game's own. Falion's cure (VC01's
    stage 200, the story) runs on every game, each on its own player: the client is cured."""
    def has_ability(name):
        return f"0:{AB_VAMPIRE_01:X}" in ctx.player_command(name, {"cmd": "player_record"})["record"]["spells"]

    races = {name: ctx.own_race(name) for name in ("", "client")}
    if any(race == NORD_VAMPIRE_RACE or race is None for race in races.values()):
        raise ctx.failure(f"a player is a vampire already, or has no race: {races}")
    try:
        ctx.console_on("client", f"player.addspell {SANGUINARE_VAMPIRIS:X}")
        ctx.wait_for("[client] the disease is on its player", lambda: SANGUINARE_VAMPIRIS in ctx.effect_spells("client"), 5)
        ctx.console_on("", "set timescale to 10000")
        ctx.wait_for("[client] its player turns into a vampire", lambda: ctx.own_race("client") not in (races["client"], None), 90)
        ctx.console_on("", "set timescale to 20")
        vampire = ctx.own_race("client")
        log(f"the client's vampire race: {vampire:X}")
        ctx.wait_for("[host] its copy of the client is a vampire", lambda: ctx.player_race_on_host("client") == vampire, 20)
        ctx.wait_for("[client] its player has the vampire's ability", lambda: has_ability("client"), 10)
        ctx.wait_for("[both] PlayerIsVampire is each player's own",
                 lambda: ctx.global_value("client", PLAYER_IS_VAMPIRE) == 1 and ctx.global_value("", PLAYER_IS_VAMPIRE) == 0, 10)
        if ctx.own_race("") != races[""] or has_ability(""):
            raise ctx.failure("[host] its player turned too: the vampire quest ran there for the client")
        ctx.console_on("", "setstage VC01 200")
        ctx.wait_for("[client] Falion's cure gives its player its race back",
                 lambda: ctx.own_race("client") == races["client"] and not has_ability("client"), 30)
        ctx.wait_for("[client] PlayerIsVampire is back to 0", lambda: ctx.global_value("client", PLAYER_IS_VAMPIRE) == 0, 10)
        if ctx.own_race("") != races[""]:
            raise ctx.failure(f"[host] the cure changed its player's race: {ctx.own_race(''):X}")
    finally:
        ctx.console_on("", "set timescale to 20")
        if SANGUINARE_VAMPIRIS in ctx.effect_spells("client"):  # turning removes it
            ctx.console_on("client", f"player.removespell {SANGUINARE_VAMPIRIS:X}")
        for name in ("", "client"):
            if ctx.own_race(name) != races[name]:  # its own instance's cure, run there
                ctx.script_call(name, "PlayerVampireQuestScript", "VampireCure", [{"form": 0x14}], on=PLAYER_VAMPIRE_QUEST)

def scenario_vampirism_kept_across_rejoin(ctx):
    """A guest's vampirism is kept in its record (system-quests.md, rule 5): the client turns, and the host's record
    of it keeps its own PlayerVampireQuest instance (its hunger's status and registration). Its game starts again at
    the main menu and joins with the host's world, whose save holds the host's player's instance: its player is the
    vampire it was, its instance its own again (VampireStatus 1, PlayerIsVampire 1, the host's 0)."""
    def kept_instance():
        kept = ctx.kept_records("").get(key) or {}
        return (kept.get("system_quests") or {}).get(f"0:{PLAYER_VAMPIRE_QUEST:X}") or {}

    def status(name):
        return ctx.script_var(name, PLAYER_VAMPIRE_QUEST, "PlayerVampireQuestScript", "VampireStatus")

    races = {name: ctx.own_race(name) for name in ("", "client")}
    if any(race == NORD_VAMPIRE_RACE or race is None for race in races.values()):
        raise ctx.failure(f"a player is a vampire already, or has no race: {races}")
    key = ctx.player_record("client")["key"]
    try:
        ctx.console_on("client", f"player.addspell {SANGUINARE_VAMPIRIS:X}")
        ctx.wait_for("[client] the disease is on its player", lambda: SANGUINARE_VAMPIRIS in ctx.effect_spells("client"), 5)
        ctx.console_on("", "set timescale to 10000")
        ctx.wait_for("[client] its player turns into a vampire", lambda: ctx.own_race("client") not in (races["client"], None), 90)
        ctx.console_on("", "set timescale to 20")
        vampire = ctx.own_race("client")
        ctx.wait_for("[host] its record of the client keeps the client's vampire quest, at status 1 and registered",
                 lambda: any("VampireStatus" in name and value == 1 for name, value in kept_instance().get("variables", {}).items())
                 and "RegisterForUpdateGameTime" in kept_instance().get("timers", []), 30)
        ctx.restart_client_at_main_menu()
        ctx.joins_with_world("as a vampire")
        ctx.wait_for("[client] its player is the vampire it was", lambda: ctx.own_race("client") == vampire, 30)
        ctx.wait_for("[client] its instance is its own again", lambda: status("client") == 1, 20)
        ctx.wait_for("[both] PlayerIsVampire is each player's own",
                 lambda: ctx.global_value("client", PLAYER_IS_VAMPIRE) == 1 and ctx.global_value("", PLAYER_IS_VAMPIRE) == 0, 10)
        if status("") != 0 or ctx.own_race("") != races[""]:
            raise ctx.failure(f"[host] its player's instance changed: status {status('')}, race {ctx.own_race(''):X}")
    finally:
        ctx.console_on("", "set timescale to 20")
        if SANGUINARE_VAMPIRIS in ctx.effect_spells("client"):
            ctx.console_on("client", f"player.removespell {SANGUINARE_VAMPIRIS:X}")
        for name in ("", "client"):
            if ctx.own_race(name) != races[name]:
                ctx.script_call(name, "PlayerVampireQuestScript", "VampireCure", [{"form": 0x14}], on=PLAYER_VAMPIRE_QUEST)

def scenario_marriage_is_the_partys(ctx):
    """A spouse is the party's (dragonborn.md): the wedding's fragment, run for the client
    (QF_RelationshipMarriageWeddin's Alias_LoveInterest.SetRelationshipRank(Game.GetPlayer(), 4)), sets the NPC's
    relationship with the client's character on the host; it is the player's (0x7), so the host's own conditions
    (GetRelationshipRank with the player: the spouse's dialogue) read lover."""
    npc, _, _ = ctx.spawn_host_npc(LYDIA, restrained=True)
    try:
        before = ctx.player_command("", {"cmd": "relationship_rank", "actor": npc, "other": 0x14})["result"]
        ctx.script_call("", "Actor", "SetRelationshipRank", [{"form": ctx.player_copy("")}, 4], on=npc)
        ctx.wait_for("[host] the NPC is the player's lover", lambda: ctx.player_command(
            "", {"cmd": "relationship_rank", "actor": npc, "other": 0x14})["result"] == 4, 5)
        if before == 4:
            raise ctx.failure(f"[host] the NPC {npc:X} was already the player's lover")
    finally:
        ctx.console_on("", f"{npc:08X}.disable")
        ctx.console_on("", f"{npc:08X}.markfordelete")

def scenario_cutscene_fade_reaches_party(ctx):
    """In the host's cutscene (controls taken) its script fades to black (ImageSpaceModifier.Apply on FadeToBlackImod)
    and shakes the camera: the client's screen fades and shakes too, and clears with the host's; the controls come
    back with the host's."""
    try:
        ctx.script_call("", "Game", "DisablePlayerControls", TAKE_CONTROLS)
        ctx.party_controls(None, "taken by the host's cutscene")
        ctx.script_call("", "ImageSpaceModifier", "Apply", [1.0], on=FADE_TO_BLACK_IMOD)
        ctx.script_call("", "Game", "ShakeCamera", [None, 0.5, 2.0])
        for name in ("", "client"):
            ctx.wait_for(f"[{name or 'host'}] fades to black", lambda name=name: FADE_TO_BLACK_IMOD in ctx.screen(name)["imods"], 5)
            ctx.wait_for(f"[{name or 'host'}] shakes", lambda name=name: ctx.screen(name)["shake"]["seconds"] > 0, 5)
        ctx.script_call("", "ImageSpaceModifier", "Remove", [], on=FADE_TO_BLACK_IMOD)
        for name in ("", "client"):
            ctx.wait_for(f"[{name or 'host'}] the fade clears", lambda name=name: FADE_TO_BLACK_IMOD not in ctx.screen(name)["imods"], 5)
        ctx.script_call("", "Game", "EnablePlayerControls", GIVE_CONTROLS)
        ctx.party_controls(ALL_CONTROLS, "back")
    finally:
        ctx.script_call("", "ImageSpaceModifier", "Remove", [], on=FADE_TO_BLACK_IMOD)
        ctx.script_call("", "Game", "EnablePlayerControls", GIVE_CONTROLS)

def scenario_guest_cutscene_reaches_host(ctx):
    """A script with no player's action behind it on the client's game takes its player's controls and fades to
    black: the host's controls and screen follow, and come back with the client's. (Its player's own script would be
    its moment alone: player-moment-stays-theirs.)"""
    try:
        ctx.script_call("client", "Game", "DisablePlayerControls", TAKE_CONTROLS)
        ctx.party_controls(None, "taken by the client's cutscene")
        ctx.script_call("client", "ImageSpaceModifier", "Apply", [1.0], on=FADE_TO_BLACK_IMOD)
        ctx.wait_for("[host] fades to black", lambda: FADE_TO_BLACK_IMOD in ctx.screen("")["imods"], 5)
        ctx.script_call("client", "ImageSpaceModifier", "Remove", [], on=FADE_TO_BLACK_IMOD)
        ctx.wait_for("[host] the fade clears", lambda: FADE_TO_BLACK_IMOD not in ctx.screen("")["imods"], 5)
        ctx.script_call("client", "Game", "EnablePlayerControls", GIVE_CONTROLS)
        ctx.party_controls(ALL_CONTROLS, "back with the client's")
    finally:
        ctx.script_call("client", "ImageSpaceModifier", "Remove", [], on=FADE_TO_BLACK_IMOD)
        ctx.script_call("client", "Game", "EnablePlayerControls", GIVE_CONTROLS)

def scenario_player_moment_stays_theirs(ctx):
    """A script one player's action started (a lever, fishing, the lute) takes that player's controls and fades their
    screen: that game's alone, the other's controls and screen stay as they are (PartyNatives; docs/design/cutscenes.md,
    "Whose moment"). The host's player first, then the client's."""
    for player, other in (("", "client"), ("client", "")):
        try:
            ctx.player_script_call(player, "Game", "DisablePlayerControls", TAKE_CONTROLS)
            ctx.player_script_call(player, "ImageSpaceModifier", "Apply", [1.0], on=FADE_TO_BLACK_IMOD)
            ctx.wait_for(f"[{player or 'host'}] its own moment takes its controls and fades",
                     lambda: ctx.screen(player)["controls"] != ALL_CONTROLS and FADE_TO_BLACK_IMOD in ctx.screen(player)["imods"], 5)
            ctx.screen_marker(player, other)
            if ctx.screen(other)["controls"] != ALL_CONTROLS:
                raise ctx.failure(f"[{other or 'host'}] its controls are {ctx.screen(other)['controls']:X}: another player's moment took them")
            if FADE_TO_BLACK_IMOD in ctx.screen(other)["imods"]:
                raise ctx.failure(f"[{other or 'host'}] fades with another player's moment")
            ctx.player_script_call(player, "ImageSpaceModifier", "Remove", [], on=FADE_TO_BLACK_IMOD)
            ctx.player_script_call(player, "Game", "EnablePlayerControls", GIVE_CONTROLS)
            ctx.wait_for(f"[{player or 'host'}] its controls come back", lambda: ctx.screen(player)["controls"] == ALL_CONTROLS, 5)
        finally:
            # Each game's own, never sent: a party give-back could land after the next direction's moment.
            for name in ("", "client"):
                ctx.player_script_call(name, "ImageSpaceModifier", "Remove", [], on=FADE_TO_BLACK_IMOD)
                ctx.player_script_call(name, "Game", "EnablePlayerControls", GIVE_CONTROLS)

def scenario_elder_scroll_vision_reaches_party(ctx):
    """(duo --save LateAfterElderKnowledge) MQ206's stage sends Game.GetPlayer() into the Elder Scroll's vision
    (moveTo its marker): the story's call on the player, so each game's own player goes there by its own MoveTo. The
    client's mirrored move leads no one: the host's game gets no party travel from it."""
    ctx.needs_quest(MQ206, "Alduin's Bane", "LateAfterElderKnowledge")
    _, host_mark = ctx.log_trail("", 1 << 62)
    _, client_mark = ctx.log_trail("client", 1 << 62)
    before = {name: ctx.my_position(name) for name in ("", "client")}
    ctx.console_on("", "setstage MQ206 20")
    ctx.wait_for("[host] its player moved", lambda: distance(ctx.my_position(""), before[""]) > 500, 60)
    ctx.wait_for("[client] its own player ran the host's MoveTo", lambda: ctx.mirrored_here("client", client_mark, "ObjectReference.MoveTo"), 30)
    ctx.wait_for("[client] stands where the host's player went", lambda: distance(ctx.my_position("client"), ctx.my_position("")) < 300, 60)
    led = [line["text"] for line in ctx.log_trail("", host_mark)[0] if "Party travel to" in line["text"]]
    if led:
        raise ctx.failure(f"[host] the client's mirrored move led the party: {led[:2]}")

def scenario_greybeards_scene_casts_party(ctx):
    """(duo --save LateAfterDragonRising) MQ105's stage 270 starts an engine scene that casts the host's player (a
    package on its alias): each guest's own player runs the same package (GuestScene) while the scene holds the host's
    player, and is its own again when the scene lets it go."""
    ctx.needs_quest(MQ105, "The Way of the Voice", "LateAfterDragonRising")
    scene = lambda name: ((ctx.player_snapshot(name).get("ai_state") or {}).get("scene") or 0)
    package = lambda name: ctx.running_package(name, 0x14)
    ctx.console_on("", "setstage MQ105 270")
    ctx.wait_for("[host] a scene casts its player", lambda: scene("") != 0, 60)
    ctx.wait_for(f"[client] its own player runs the host's player's scene package",
             lambda: package("") and package("client") == package(""), 15)
    ctx.wait_for("[host] the scene lets its player go", lambda: scene("") == 0, 180)
    ctx.wait_for("[client] its player is its own again", lambda: package("client") != package("") or not package(""), 15)

def scenario_player_stage_cutscene_reaches_party(ctx):
    """A player's own script shaking the camera shakes that game only; a quest stage that player's script sets is the
    story, and its fragment's screen calls reach the client too (MG04MonkSceneQuest stage 200: its loop pops to its
    outro, ImageSpaceModifier.PopTo)."""
    for name in ("", "client"):
        ctx.wait_for(f"[{name or 'host'}] no shake yet", lambda name=name: ctx.screen(name)["shake"]["seconds"] <= 0, 5)
    try:
        ctx.player_script_call("", "Game", "ShakeCamera", [None, 0.5, 2.0])
        ctx.wait_for("[host] its own script shakes it", lambda: ctx.screen("")["shake"]["seconds"] > 0, 5)
        ctx.script_call("", "ImageSpaceModifier", "Apply", [1.0], on=FADE_TO_BLACK_IMOD)  # the marker: a party call after it
        ctx.wait_for("[client] the marker's fade arrives", lambda: FADE_TO_BLACK_IMOD in ctx.screen("client")["imods"], 5)
        if ctx.screen("client")["shake"]["seconds"] > 0:
            raise ctx.failure("[client] shakes with the host player's own script")
        ctx.script_call("", "ImageSpaceModifier", "Remove", [], on=FADE_TO_BLACK_IMOD)

        ctx.player_command("", {"cmd": "quest_allow_rerun", "quest": MONK_SCENE_QUEST})  # it runs once per save
        ctx.script_call("", "Quest", "Start", [], on=MONK_SCENE_QUEST)  # the world's: its intro and loop are everyone's
        for name in ("", "client"):
            ctx.wait_for(f"[{name or 'host'}] the scene's loop", lambda name=name: MONK_LOOP_IMOD in ctx.screen(name)["imods"], 10)
        ctx.player_script_call("", "Quest", "SetCurrentStageID", [200], on=MONK_SCENE_QUEST)
        ctx.wait_for("[host] its stage 200 pops to the outro", lambda: MONK_OUTRO_IMOD in ctx.screen("")["imods"], 5)
        ctx.wait_for("[client] the host player's stage pops it to the outro too", lambda: MONK_OUTRO_IMOD in ctx.screen("client")["imods"], 5)
    finally:
        for imod in (FADE_TO_BLACK_IMOD, MONK_INTRO_IMOD, MONK_LOOP_IMOD, MONK_OUTRO_IMOD):
            ctx.script_call("", "ImageSpaceModifier", "Remove", [], on=imod)
        for name in ("", "client"):
            ctx.wait_for(f"[{name or 'host'}] its screen clears", lambda name=name: not ctx.screen(name)["imods"], 5)
        ctx.console_on("", "resetquest MG04MonkSceneQuest")
        ctx.client_quest_matches(MONK_SCENE_QUEST, "the monks' scene is reset")

def scenario_screen_reaches_party(ctx):
    """Outside a cutscene too, a script's shake and fade are the party's (a trap, a boss's music, a spell's look): the
    host's script shakes the camera and fades, and the client's screen does too, and clears with the host's."""
    try:
        ctx.script_call("", "Game", "ShakeCamera", [None, 0.5, 2.0])
        ctx.script_call("", "ImageSpaceModifier", "Apply", [1.0], on=FADE_TO_BLACK_IMOD)
        for name in ("", "client"):
            ctx.wait_for(f"[{name or 'host'}] shakes and fades",
                     lambda name=name: ctx.screen(name)["shake"]["seconds"] > 0 and FADE_TO_BLACK_IMOD in ctx.screen(name)["imods"], 5)
        ctx.script_call("", "ImageSpaceModifier", "Remove", [], on=FADE_TO_BLACK_IMOD)
        ctx.wait_for("[client] the fade clears", lambda: FADE_TO_BLACK_IMOD not in ctx.screen("client")["imods"], 5)
        if ctx.screen("client")["controls"] != ALL_CONTROLS:
            raise ctx.failure(f"[client] its controls are {ctx.screen('client')['controls']:X} though no script took them")
    finally:
        ctx.script_call("", "ImageSpaceModifier", "Remove", [], on=FADE_TO_BLACK_IMOD)

def scenario_story_message_reaches_party(ctx):
    """The host's messages go to everyone (party-model.md rule 5, party-calls.md): a quest's notification (no player's
    action behind it) shows on every game. The client's own action's notification stays on the client: a quest's
    notification on the client after it reaches the host, and the client's own never did."""
    stamp = int(time.time())
    story, own, after = f"The ground shakes {stamp}", f"You feel lighter {stamp}", f"A cold wind {stamp}"
    ctx.script_call("", "Debug", "Notification", [story])
    for name in ("", "client"):
        ctx.wait_for(f"[{name or 'host'}] shows the host's quest notification", lambda name=name: story in ctx.hud_messages(name), 5)
    ctx.player_script_call("client", "Debug", "Notification", [own])
    ctx.wait_for("[client] shows its own action's notification", lambda: own in ctx.hud_messages("client"), 5)
    ctx.script_call("client", "Debug", "Notification", [after])
    ctx.wait_for("[host] shows the client's quest notification", lambda: after in ctx.hud_messages(""), 5)
    if own in ctx.hud_messages(""):
        raise ctx.failure("[host] showed the client's own action's notification")

def scenario_story_box_host_answers(ctx):
    """A quest's message box with buttons (Message.Show, no player's action behind it) shows on every game; the host's
    player answers it (Cancel), and the client's copy closes with the host's answer (party-calls.md, "A box's answer";
    the user's choice, 2026-10-27: the host answers, the others see it)."""
    try:
        ctx.script_call("", "Message", "Show", [0.0] * 9, on=STONE_MESSAGE)
        for name in ("", "client"):
            ctx.wait_for(f"[{name or 'host'}] shows the quest's box", lambda name=name: "MessageBoxMenu" in ctx.menus(name), 5)
        ctx.presses_box_button("", STONE_CANCEL)
        ctx.wait_for("[host] its player's answer closes its box", lambda: "MessageBoxMenu" not in ctx.menus(""), 5)
        ctx.wait_for("[client] the host's answer closes its copy", lambda: "MessageBoxMenu" not in ctx.menus("client"), 5)
    finally:
        for name in ("", "client"):
            if "MessageBoxMenu" in ctx.menus(name):
                ctx.presses_box_button(name, STONE_CANCEL)

def scenario_timer_asks_party_position(ctx):
    """Where the player is, asked with no player's action behind it (a quest's timer), is asked of the party (party-model.md
    rule 3, party-calls.md "Questions"): Lydia stands with the client close and the host across the room; a quest asking
    her distance to the player gets the client's, the nearer; the host's own action asking it gets the host's."""
    npc, _, copy = ctx.spawn_host_npc(LYDIA, restrained=True)
    ctx.place_before("client", copy, 150)
    ctx.place_before("", npc, 900)
    ctx.wait_for("[host] the client's character stands by Lydia", lambda: distance(ctx.host_world(ctx.client_on_host())["placement"]["position"], ctx.host_npc_state(npc)["position"]) < 250, 5)
    # The player as the call's object, not an argument: a call naming this game's player that no script queued is
    # that player's own event (GuestScripts' cause), as a quest's timer asking from its own stack is not.
    asked = ctx.script_answer("", "ObjectReference", "GetDistance", [{"form": npc}], on=0x14)
    if not asked < 250:
        raise ctx.failure(f"[host] a quest's question measured {asked:.0f} units: not the nearer client's")
    own = ctx.script_answer("", "ObjectReference", "GetDistance", [{"form": npc}], on=0x14, player_action=True)
    if not own > 600:
        raise ctx.failure(f"[host] the host's own action's question measured {own:.0f} units: not the host's own")

def scenario_relief_takes_client_relics(ctx):
    """The client, carrying Volunruud's ceremonial axe, puts it in the relief. On the host the door's script holds the
    host's player (stored when its cell loaded) and asks that variable for the relics: in a stack run for the client,
    the native sees the client's character in its place (party-model.md, rule 2: a stored player), so the axe is
    taken from the client and the relief shows it on both games, though the host's player holds none; the box saying
    the sword is still missing is the client's (its action's Message.Show runs on its game, party-calls.md), the host
    shows none. The quest is set to stage 10 first
    (a first activation sets it, with a box) and stays there: stages only go forward. The relief is emptied after."""
    try:
        ctx.both_go(VOLUNRUUD_CELL)
        copy = ctx.client_on_host()
        if ctx.item_count("", 0x14, VOLUNRUUD_AXE):
            raise ctx.failure(f"[host] the host's player holds the axe {VOLUNRUUD_AXE:X} to begin with")
        for name in ("", "client"):
            if ctx.ref_enabled(name, VOLUNRUUD_AXE_SHOWN):
                raise ctx.failure(f"[{name or 'host'}] the axe {VOLUNRUUD_AXE_SHOWN:X} stands in the relief to begin with")
        ctx.wait_for("[host] the door's script holds the host's player",
                 lambda: ctx.script_var("", VOLUNRUUD_DOOR, VOLUNRUUD_SCRIPT, "player") == 0x14, 10)
        if (ctx.quest_stage("", VOLUNRUUD_QUEST_ID) or 0) < 10:
            ctx.console_on("", f"setstage {VOLUNRUUD_QUEST} 10")
        ctx.wait_for(f"[host] {VOLUNRUUD_QUEST} is at stage 10", lambda: ctx.quest_stage("", VOLUNRUUD_QUEST_ID) == 10, 5)
        ctx.console_on("client", f"player.additem {VOLUNRUUD_AXE:08X} 1")
        ctx.wait_for("[host] the client's character holds the axe", lambda: ctx.item_count("", copy, VOLUNRUUD_AXE) == 1, 5)
        for name in ("", "client"):
            ctx.place_before(name, VOLUNRUUD_DOOR, 200)
        ctx.activate_on("client", VOLUNRUUD_DOOR)
        ctx.wait_for("[client] the player gave up the axe", lambda: ctx.item_count("client", 0x14, VOLUNRUUD_AXE) == 0, 10)
        for name in ("", "client"):
            ctx.wait_for(f"[{name or 'host'}] the axe stands in the relief", lambda name=name: ctx.ref_enabled(name, VOLUNRUUD_AXE_SHOWN), 5)
        ctx.wait_for("[client] the relief says the sword is still missing", lambda: "MessageBoxMenu" in ctx.menus("client"), 5)
        if "MessageBoxMenu" in ctx.menus(""):
            raise ctx.failure("[host] shows the client's box: its action's message is the client's")
        ctx.close_message_box("client")
        if ctx.item_count("", 0x14, VOLUNRUUD_AXE):
            raise ctx.failure("[host] the host's player holds an axe it never had")
    finally:
        for name in ("", "client"):
            if "MessageBoxMenu" in ctx.menus(name):
                ctx.close_message_box(name)
        if count := ctx.item_count("client", 0x14, VOLUNRUUD_AXE):
            ctx.console_on("client", f"player.removeitem {VOLUNRUUD_AXE:08X} {count}")
        if ctx.ref_enabled("", VOLUNRUUD_AXE_SHOWN):
            ctx.console_on("", f"{VOLUNRUUD_AXE_SHOWN:08X}.disable")
        ctx.script_var("", VOLUNRUUD_DOOR, VOLUNRUUD_SCRIPT, "relic01in", 0)
        ctx.both_go(HOME_CELL)

def scenario_shrine_blesses_client(ctx):
    """The host and the client each pray at a different temple shrine: each player gets only their own
    shrine's blessing, though each game sees the other's character pray too (the host's script casts at
    the client's character, which its owner's game applies; upstream's patch skipped other players)."""
    try:
        ctx.both_go(SHRINE_CELL)
        for name in ("", "client"):
            if ctx.blessings(name):
                raise ctx.failure(f"[{name or 'host'}] the player is blessed to begin with")
        for name, (shrine, blessing) in SHRINES.items():
            ctx.place_before(name, shrine, 150)
            ctx.activate_on(name, shrine)
            ctx.wait_for(f"[{name or 'host'}] the player is blessed by shrine {shrine:X}", lambda name=name, blessing=blessing: blessing in ctx.blessings(name), 5)
        for name, (_, blessing) in SHRINES.items():
            if ctx.blessings(name) != {blessing}:
                raise ctx.failure(f"[{name or 'host'}] the player holds blessings {sorted(hex(b) for b in ctx.blessings(name))}, not only its own {blessing:X}")
    finally:
        for name in ("", "client"):
            if ctx.blessings(name):
                ctx.console_on(name, "player.dispelallspells")
        ctx.both_go(HOME_CELL)

def scenario_vilkas_counts_client_hits(ctx):
    """The client spars with Vilkas for the Companions' trial. On the host his trial script counts the
    client's character as the player (rule 1): three hits pass the trial instead of ending it ("someone
    else hit him"). Rule 1 only: nothing the hits start asks for the player."""
    try:
        ctx.console_on("", "resetquest C00VilkasTrainingQuest")
        ctx.console_on("", "setstage C00 20")
        ctx.wait_for("[host] C00 is at stage 20", lambda: ctx.host_quest(C00_QUEST)[0] == 20, 5)
        vilkas = ctx.stage_host_npc(VILKAS)
        ctx.console_on("", "setstage C00VilkasTrainingQuest 10")
        ctx.wait_for("[host] the trial fight starts", lambda: ctx.host_quest(VILKAS_TRAINING)[0] == 10, 5)
        full = ctx.host_npc_health(VILKAS)
        ctx.give_sword("client")
        ctx.draw("client")
        for hit in range(1, 4):
            ctx.place_before("client", vilkas, MELEE_GAP)  # each hit knocks him back out of reach
            # He heals between hits: a hit is only seen against his full health.
            ctx.wait_for("[host] Vilkas's health is full", lambda: ctx.host_npc_health(VILKAS) >= full, 5)
            ctx.attack("client")
            ctx.hit_lands(VILKAS, full, f"the client's hit {hit}")
            if 110 in ctx.host_quest(VILKAS_TRAINING)[1]:
                raise ctx.failure("[host] the trial ended: the client's hit counted as someone else's")
        ctx.wait_for("[host] the trial is passed", lambda: 150 in ctx.host_quest(VILKAS_TRAINING)[1], 5)
    finally:
        ctx.release_host_npc(VILKAS)
        for command in (f"{VILKAS:X}.setfactionrank {VILKAS_HATE:X} -1", f"{VILKAS:X}.stopcombat",
                        f"{VILKAS:X}.restoreav health 1000", "resetquest C00VilkasTrainingQuest"):
            ctx.console_on("", command)
