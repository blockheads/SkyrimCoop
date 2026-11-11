"""Scenarios dialogue for the co-op harness."""
import math
import time

from ..comparisons import head_track_differs
from ..common import (
    ACTIVATE_KEY,
    AMREN,
    ANDURS,
    ANDURS_AMULET,
    ANDURS_KNOW_ARKAY,
    ANDURS_WHAT_LOST,
    BANNERED_MARE,
    BEGGARS_QUEST,
    BRENUIN,
    CARRY_WEIGHT,
    CLAW_FOUND_LINES,
    CLAW_TOPIC,
    DOWN_KEY,
    DO_NOTHING_PACKAGE,
    GIFT_OF_CHARITY,
    GIVE_COIN_INFO,
    GIVE_COIN_TOPIC,
    GOLD,
    GOLDEN_CLAW,
    GOLDEN_CLAW_QUEST,
    HOME_CELL,
    HULDA,
    HULDA_COLLEGE_LINES,
    HULDA_COLLEGE_TOPIC,
    INN,
    LUCAN,
    LUCAN_TROUBLE_TOPIC,
    LYDIA,
    MAGELIGHT,
    NORD_RACE,
    NO_CLAW_LINE,
    ONE_HANDED_SKILL,
    ONMUND,
    ONMUND_FEW_NORDS,
    ONMUND_GREETINGS,
    ONMUND_NOT_A_NORD,
    PERSUADE_ACHIEVEMENT_STATS,
    PLAYER_BASE,
    RENT_ROOM_NO_GOLD_INFO,
    RENT_ROOM_TOPIC,
    ROOT,
    SPEECH_SKILL,
    STEED_ABILITY,
    TAB_KEY,
    TRAINING_MENU,
    TRAINING_TOPIC,
    UNGRIEN,
    UNGRIEN_PERSUADE_TOPIC,
    UNGRIEN_TOPIC,
    UP_KEY,
)
from ..errors import TestFailure
from ..objects import AnimTrace
from ..comparisons import distance
from ..utilities import log

LUCAN_SHOP = 0x7F6BB  # "What have you got for sale?"
CLIP_TOPICS = 6  # topics asked before Lucan is taken to have no gesture to give
CLIP_DELAY = 1.0  # seconds a copy's clip may start after its owner's: 99% of saved starts were within 0.85
BLENDED_CLIPS = ("MT_Walk", "MT_Run", "MT_Turn", "Sneak_")  # a blender's children: variables weigh them, none is started
COUNTER_ARMS = "AnimObjectCounterRightToArm"  # a shopkeeper at a counter crossing their arms: no action starts it
COUNTER_CROSSED, COUNTER_DOWN = "AnimObjectCounterIdleArmsLoopRight", "AnimObjectCounterIdleLoopRight"


def scenario_copy_ends_voice_line(ctx):
    """The host player talks to its NPC: she greets him, and the client's copy plays the same line
    (NotifyDialogue) with the same subtitle (NotifySubtitle) and ends it, as the host's NPC does. SpeakSound only queues a line; the engine's
    voice upkeep in Character::UpdateAI starts and ends it (Actor::UpdateSoundCallback,
    Character::UpdateVoiceTimer), which copies run from HookUpdateAI. A copy holds a package, its owner's
    (AiStateService) or the engine's do-nothing one (HookCheckForNewPackage): an actor without one gets no
    UpdateAI once its 3D is far enough from the camera to fade (Character::ShouldOnlyCheckPackage)."""
    npc, _, copy = ctx.spawn_host_npc(LYDIA, restrained=True)
    try:
        ctx.wait_for(f"[client] copy {copy:08X} holds its owner's package or the do-nothing one",
                 lambda: ctx.package_type("client", copy) == DO_NOTHING_PACKAGE
                 or (ctx.package_type("client", copy) not in (None, -1)
                     and ctx.ai_state("client", copy).get("package") == ctx.ai_state("", npc).get("package")), 5)
        ctx.place_before("", npc, 100)
        ctx.activate_on("", npc)
        ctx.wait_for("[host] NPC's line plays", lambda: ctx.line_playing("", npc), 10)
        ctx.wait_for(f"[client] copy {copy:08X}'s line plays", lambda: ctx.line_playing("client", copy), 5)
        text = ctx.wait_for("[host] NPC's subtitle shows", lambda: ctx.subtitle("", npc), 5)
        ctx.wait_for(f"[client] copy {copy:08X} shows the same subtitle", lambda: ctx.subtitle("client", copy) == text, 5)
        ctx.wait_for("[host] NPC's line ends", lambda: ctx.line_over("", npc), 30)
        ctx.wait_for(f"[client] copy {copy:08X}'s line ends", lambda: ctx.line_over("client", copy), 5)
    finally:
        ctx.close_dialogues()
    ctx.hold()

def scenario_dialogue_peer_starts(ctx):
    """The client talks to its copy of the host's NPC: the host's player is the one talking (its game runs the
    conversation with the real NPC, without its menu: the host plays on), and the client's menu opens on the copy
    with the host's list: its topics, lines and texts, in its order (docs/design/shared-dialogue.md)."""
    npc, _, copy = ctx.spawn_host_npc(LYDIA, restrained=True)
    try:
        ctx.place_before("", npc, 110)
        ctx.place_before("client", copy, 110)
        ctx.look_apart()
        ctx.activate_on("client", copy)
        ctx.wait_for(f"[host] the player talks to {npc:08X} in the background", lambda: ctx.in_conversation("", npc), 10)
        ctx.wait_for(f"[client] the menu opens on copy {copy:08X}", lambda: ctx.talking_to("client", copy), 10)
        ctx.wait_for("[host] the conversation lists options", lambda: ctx.topics_shown(""), 5)
        ctx.client_shows_hosts_list("at the open")
        if ctx.dialogue("").get("menu"):
            raise ctx.failure("[host] the dialogue menu opened for a conversation it did not start")
        ctx.hold()
    finally:
        ctx.close_dialogues()
        ctx.remove_host_npc(npc)

def scenario_released_copy_takes_up_its_ai(ctx):
    # hand-written: the engine path is read in IDA, and a copy on its own do-nothing package needs a harness control.
    """The guest leaves while its copy of a game's own NPC holds its do-nothing package. Released, the NPC is the guest's own
    again and picks a package of its own, rather than standing frozen on one nothing ever ends (actor-ai-and-copies.md)."""
    ctx.both_go("Lucan's shop", f"player.moveto {LUCAN:X}")
    ctx.wait_for(f"[client] Lucan {LUCAN:X} is a copy here", lambda: ctx.actor_on("client", LUCAN).get("role") == "copy", 30)
    package_type = lambda: (ctx.actor_on("client", LUCAN).get("mod") or {}).get("package_type")
    def holds_do_nothing():  # the owner's next AI state puts its package back: ask until it shows
        ctx.player_command("client", {"cmd": "copy_do_nothing", "actor": LUCAN})
        return package_type() == 27
    ctx.wait_for(f"[client] copy {LUCAN:X} holds the do-nothing package", holds_do_nothing, 10)
    ctx.disconnect_client()
    try:
        ctx.wait_for(f"[client] {LUCAN:X}, released, picks a package of its own", lambda: package_type() not in (27, None), 10)
    finally:
        ctx.joins_with_world("joins again, as a player does")  # with the host's save: a running game's rejoin keeps its own world
        ctx.both_go("the blank cell", "coc QASmoke")  # the end's comparison is of the session, not of this shop (bugs.md, After a guest rejoins)


def scenario_copy_answers_its_ai_procedure(ctx):
    # hand-written: the engine path is read (GetCurrentAIProcedure 21521) and it needs no play: a console line on each copy.
    """A condition asks a player's copy GetCurrentAIProcedure, as a script or a dialogue line can. The copy holds a do-nothing
    package of ours: with no procedure type the engine indexes its table at -1 and the game crashes (actor-ai-and-copies.md)."""
    for game in ("client", ""):
        copy = ctx.wait_for(f"[{game or 'host'}] the other player's copy is here", lambda: ctx.other_player(ctx.actors_on(game)), 10)
        printed = ctx.console_on(game, f"{copy:08X}.GetCurrentAIProcedure")
        if not any("Procedure >>" in line for line in printed):
            raise ctx.failure(f"[{game or 'host'}] copy {copy:08X} gave no procedure: {printed}")


def scenario_dialogue_copy_looks_at_speaker(ctx):
    """The client talks to its copy of the host's NPC: the NPC turns its head to the client's character (a
    headtrack slot, HighProcessData::headTrackTarget), and the client's copy looks at the same one, the client's
    own player (ai_state.head_track: what the engine reads of the slots, by session id). Copies run no AI, which sets those slots."""
    npc, _, copy = ctx.spawn_host_npc(LYDIA, restrained=True)
    try:
        ctx.place_before("", npc, 110)
        ctx.place_before("client", copy, 110)
        ctx.activate_on("client", copy)
        ctx.wait_for(f"[client] the menu opens on copy {copy:08X}", lambda: ctx.talking_to("client", copy), 10)
        def looks_at(game, form):  # "unread" or nobody is no look
            targets = ctx.ai_state(game, form).get("head_track")
            return targets if targets and isinstance(targets, list) else None
        owner = ctx.wait_for(f"[host] NPC {npc:08X} looks at someone", lambda: looks_at("", npc), 10)
        log(f"host NPC {npc:08X} head targets: {owner}")
        ctx.wait_for(f"[client] copy {copy:08X} looks where its owner does ({owner})",
                     lambda: (seen := looks_at("client", copy)) and (now := looks_at("", npc)) and not head_track_differs(now, seen), 5)
        ctx.hold()
    finally:
        ctx.close_dialogues()
        ctx.remove_host_npc(npc)

def scenario_npc_clips_match(ctx):
    """At Lucan's in Riverwood every NPC, Lucan unrestrained in conversation with the host until his copy takes an idle,
    has each clip's file before its copy starts it (harness trace `clips`), as its owner does. Names the clips skipped.
    The talk is quest-gold-turn-in-host's teleport and activation, from behind him and with him unrestrained."""
    started = not ctx.quest_running("", GOLDEN_CLAW_QUEST)
    ctx.both_go("Lucan's shop", f"player.moveto {LUCAN:X}")
    try:
        # His topics are this quest's: a run before this one left it stopped.
        if started:
            ctx.console_on("", "resetquest MS13")
            ctx.console_on("", "startquest MS13")
            ctx.wait_for("[host] MS13 runs", lambda: ctx.quest_running("", GOLDEN_CLAW_QUEST), 10)
        ctx.wait_for(f"[host] Lucan {LUCAN:X} is here", lambda: ctx.host_npc_state(LUCAN).get("session_id", -1) >= 0, 30)
        owners = {int(i): s["session_id"] for i, s in ctx.local_player("").get("local_actors", {}).items() if s.get("session_id", -1) >= 0}
        names = {i: f"{i:X}" for i in owners}
        copies = {ctx.copy_of("client", sid): names[i] for i, sid in owners.items()}
        log(f"watching {sorted(names.values())}")
        lucan = names[LUCAN]
        lucan_copy = next(copy for copy, name in copies.items() if name == lucan)
        with AnimTrace(ctx, "npc-clips", {"": names, "client": copies}) as trace:
            def yaw():
                trace.take()
                return next((e["yaw"] for e in reversed(trace.entries) if e["game"] == "" and e["who"] == lucan and e["kind"] == "pos"), None)
            # As a player found it: teleported behind him and talked, the other game looking on.
            behind = (math.degrees(ctx.wait_for("[host] Lucan's heading is read", yaw, 10)) + 180) % 360
            ctx.place_before("", LUCAN, 110, behind)
            ctx.place_before("client", lucan_copy, 200, (behind + 180) % 360)
            # Back from a conversation he walks to his counter again: mid sit-down he opens no menu (dev's plugin too).
            ctx.wait_for("[host] Lucan stands or sits, not between the two",
                         lambda: ctx.actor_state("", LUCAN).get("state", {}).get("sit_sleep") in (0, 3), 30)
            ctx.activate_on("", LUCAN)
            def unopened():  # why his menu stayed shut: his state, and what the host's conversation holds
                state, talk = ctx.actor_state("", LUCAN).get("state", {}), ctx.dialogue("")
                return (f"Lucan sit state {state.get('sit_sleep')}, AI {ctx.ai_state('', LUCAN)}; the host's dialogue: "
                        f"menu {talk.get('menu')}, speaker {talk.get('speaker')}, {dict((k, v) for k, v in talk.items() if k not in ('menu', 'speaker', 'swf'))}")
            ctx.wait_for("[host] the menu opens on Lucan", lambda: ctx.talking_to("", LUCAN), 15, explain=unopened)
            talking = time.time()
            def owner_idle():
                # The trace up to this moment: a gesture at his line's end is recorded a frame after the line is over.
                asked = time.time()
                while time.time() < asked + 5:
                    trace.take()
                    if any(e["game"] == "" and e["time"] >= asked for e in reversed(trace.entries)):
                        break
                return next((e for e in trace.entries if e["game"] == "" and e["who"] == lucan and is_clip_action(e, talking)), None)
            # Past his greeting he stands silent: asking him things makes him speak, and a line brings its gesture.
            for _ in range(CLIP_TOPICS):
                if owner_idle():
                    break
                if not ctx.talking_to("", LUCAN):
                    ctx.activate_on("", LUCAN)
                    ctx.wait_for("[host] the menu opens on Lucan again", lambda: ctx.talking_to("", LUCAN), 15)
                # His last line can end the talk, and a run before this one may have asked everything: his greeting
                # brings a line and a gesture too.
                ctx.wait_for("[host] Lucan's menu takes a pick or closes",
                             lambda: ctx.menu_takes_pick("") or not ctx.talking_to("", LUCAN), 30)
                if not ctx.talking_to("", LUCAN):
                    continue
                # His shop is no line: it opens the barter menu over the conversation.
                shown = ctx.topics_shown("")
                asks = [topic for topic in shown if topic != LUCAN_SHOP]
                if not shown:
                    ctx.wait_for("[host] Lucan finishes his greeting", lambda: ctx.line_over("", LUCAN), 60)
                    ctx.close_dialogues()
                    continue
                if not asks:  # everything else is asked
                    break
                ctx.pick_with_keys("", asks[0])
                ctx.wait_for("[host] Lucan answers", lambda: ctx.line_playing("", LUCAN) or not ctx.talking_to("", LUCAN), 15)
                ctx.wait_for("[host] Lucan finishes", lambda: ctx.line_over("", LUCAN), 60)
            idle = ctx.wait_for("[host] Lucan performs an idle in conversation", owner_idle, 5)
            def copy_idle():
                trace.take()
                return next((e for e in trace.entries if e["game"] == "client" and e["who"] == lucan and is_clip_action(e, talking)
                             and e["idle"] == idle["idle"]), None)
            taken = ctx.wait_for(f"[client] Lucan's copy takes {idle['event']}", copy_idle, 10)
            def copy_clips_after():
                trace.take()
                return any(e["game"] == "client" and e["who"] == lucan and e["kind"] == "clips" and e["time"] >= taken["time"] for e in trace.entries)
            ctx.wait_for("[client] the copy's clips change with it", copy_clips_after, 10)
            through = watch_through(ctx, trace, taken["time"])
        played = sum(len(e["detail"]) for e in trace.entries if e["kind"] == "clips" and e["game"] == "client")
        if skipped := skipped_clips(trace):
            raise ctx.failure(f"{len(skipped)} of {played} clips started without their files: " + " | ".join(skipped[:6]))
        log(f"[client] all {played} clips read had their files")
        require_clips_match(ctx, trace, through=through)
    finally:
        ctx.wait_for("[host] Lucan finishes his line", lambda: ctx.line_over("", LUCAN), 60)
        ctx.close_dialogues()
        # What he was asked moved his quest on: the next run starts it again.
        ctx.console_on("", "stopquest MS13")
        ctx.both_go(HOME_CELL)


def scenario_npc_counter_matches(ctx):
    """Lucan at his counter crosses and uncrosses his arms by his graph alone, no action starting it: his copy does the
    same at the same moments. Nobody talks to him, so a repeat finds him as this run did."""
    # His shop hours: by evening his schedule takes him from the counter.
    ctx.console_on("", "set gamehour to 12")
    ctx.both_go("Lucan's shop", f"player.moveto {LUCAN:X}")
    try:
        watch_counter(ctx)
    finally:
        ctx.both_go(HOME_CELL)


def scenario_npc_counter_copy_falls_back_in(ctx):
    """Lucan's copy starting mid-loop, its graph stream started over while his arms are crossed (as a copy arriving
    mid-loop starts, which a tethered guest cannot stage): from its new sync point it keeps step with him."""
    # His shop hours: by evening his schedule takes him from the counter.
    ctx.console_on("", "set gamehour to 12")
    ctx.both_go("Lucan's shop", f"player.moveto {LUCAN:X}")
    try:
        watch_counter(ctx, push=True)
    finally:
        ctx.both_go(HOME_CELL)


def watch_counter(ctx, push=False):
    """Two crossings of Lucan's arms from his first, compared from there. With `push`, his copy starts again mid-loop first."""
    ctx.wait_for(f"[host] Lucan {LUCAN:X} is here", lambda: ctx.host_npc_state(LUCAN).get("session_id", -1) >= 0, 30)
    session = ctx.host_npc_state(LUCAN)["session_id"]
    ctx.wait_for("[client] Lucan's copy is here", lambda: ctx.copy_of("client", session), 30)
    lucan = f"{LUCAN:X}"
    with AnimTrace(ctx, "npc-counter", {"": {LUCAN: lucan}, "client": {ctx.copy_of("client", session): lucan}}) as trace:
        def arms(game, count):
            trace.take()
            return len([s for s in clip_starts(trace, game, lucan) if s[1] == COUNTER_ARMS]) >= count
        def at_counter():
            trace.take()
            return any(e["kind"] == "clips" and e["game"] == "" and any("AnimObjectCounter" in c for c in e["clips"]) for e in trace.entries)
        ctx.wait_for("[host] Lucan stands at his counter", at_counter, 60)
        # In front of him, so the trace's videos show his arms.
        heading = math.degrees(next(e["yaw"] for e in reversed(trace.entries) if e["kind"] == "pos" and e["game"] == "")) % 360
        ctx.place_before("", LUCAN, 170, heading)
        ctx.place_before("client", ctx.copy_of("client", session), 170, heading)
        pushed = 0.0
        if push:
            def host_plays(clip):
                def plays():
                    trace.take()
                    now = [e for e in trace.entries if e["kind"] == "clips" and e["game"] == ""]
                    return now and now[-1]["clips"] == [clip]
                return plays
            copy = ctx.copy_of("client", session)
            def on_stream():
                actors = (ctx.game("client").rpc({"cmd": "graph_stream"}, timeout=10) or {}).get("actors", [])
                return any(a["form"] == copy and a["on_stream"] for a in actors)
            ctx.wait_for("[host] Lucan's arms are crossed", host_plays(COUNTER_CROSSED), 120)
            # A copy arriving mid-loop starts from a sync point (graph-input-stream.md, Sync points).
            ctx.game("client").rpc({"cmd": "graph_stream", "restart": copy}, timeout=10)
            ctx.wait_for("[client] his copy starts again on his graph stream", on_stream, 10)
            pushed = time.time()
        first = len([s for s in clip_starts(trace, "", lucan) if s[1] == COUNTER_ARMS and s[0] < pushed])
        ctx.wait_for("[host] Lucan crosses his arms", lambda: arms("", first + 1), 90)
        since = [s[0] for s in clip_starts(trace, "", lucan) if s[1] == COUNTER_ARMS][first]
        ctx.wait_for("[host] Lucan crosses his arms again", lambda: arms("", first + 2), 90)
        crossed = len([s for s in clip_starts(trace, "client", lucan) if s[1] == COUNTER_ARMS and s[0] >= since])
        ctx.wait_for("[client] his copy crosses its arms with him", lambda: arms("client", crossed + 1) or crossed >= 2, CLIP_DELAY * 3)
        through = watch_through(ctx, trace, clip_starts(trace, "", lucan)[-1][0])
    require_clips_match(ctx, trace, since, through)


def watch_through(ctx, trace, moment):
    """Keeps the trace on until both games have been watched a clip's delay past that moment. Returns that time."""
    through = moment + CLIP_DELAY + 0.1
    def watched():
        trace.take()
        return watched_until(trace) >= through
    ctx.wait_for("[both] a clip's delay is watched", watched, CLIP_DELAY * 5)
    return through


def watched_until(trace):
    """The last moment both games' traces cover, by anything they recorded (a place each frame), clips or not."""
    ends = [max((e["time"] for e in trace.entries if e["game"] == game), default=0.0) for game in ("", "client")]
    return min(ends)


def require_clips_match(ctx, trace, since=0.0, through=None):
    """Fails naming every clip one game started and the other did not, within the delay, from `since`; and if the trace
    does not reach `through`, the last moment that must be compared."""
    if through is not None and watched_until(trace) < through:
        raise ctx.failure(f"the trace stops {through - watched_until(trace):.1f}s short of the last clip's comparison")
    different, blended = clip_mismatches(trace, since=since)
    if blended:
        log(f"not compared, blended by variables: {blended} clip starts")
    if different:
        raise ctx.failure(f"{len(different)} clips differ between the games: " + " | ".join(different[:8]))
    log("every clip started in both games")


def clip_starts(trace, game, who):
    """(time, clip) for each clip that actor's graph started: one new in the list, or one whose start moved on (time less
    local time), which a loop's does by whole lengths. Whatever was playing when the trace began started earlier."""
    starts, before = [], None
    for entry in trace.entries:
        if entry["kind"] != "clips" or entry["game"] != game or entry["who"] != who:
            continue
        detail = entry.get("detail") or {}
        now = {clip: entry["time"] - (detail.get(clip) or {}).get("local_time", 0.0) for clip in entry["clips"]}
        if before is not None:
            for clip, began in now.items():
                local = (detail.get(clip) or {}).get("local_time", 0.0)
                length = (detail.get(clip) or {}).get("duration", 0.0)
                moved = began - before.get(clip, began)
                loops = length > 0 and abs(moved - round(moved / length) * length) < 0.25
                if clip not in before or (moved > 0.3 and local < 0.5 and not loops):
                    starts.append((entry["time"], clip))
        before = now
    return starts


def clip_mismatches(trace, delay=CLIP_DELAY, since=0.0):
    """([each clip only one game started], blended clip starts left out) for the actors both games traced, from `since`."""
    different, blended = [], 0
    for who in sorted({e["who"] for e in trace.entries if e["kind"] == "clips"}):
        owner, copy = clip_starts(trace, "", who), clip_starts(trace, "client", who)
        owner_clips = [e["time"] for e in trace.entries if e["kind"] == "clips" and e["game"] == "" and e["who"] == who]
        copy_clips = [e["time"] for e in trace.entries if e["kind"] == "clips" and e["game"] == "client" and e["who"] == who]
        if not owner_clips or not copy_clips:
            continue
        # Clips are recorded when they change: how long each game was watched is in everything it recorded.
        first = max(owner_clips[0], copy_clips[0], since)
        owner_end = max(e["time"] for e in trace.entries if e["game"] == "" and e.get("who") == who)
        copy_end = max(e["time"] for e in trace.entries if e["game"] == "client" and e.get("who") == who)
        blended += sum(s[1].startswith(BLENDED_CLIPS) for s in owner + copy)
        owner = [s for s in owner if not s[1].startswith(BLENDED_CLIPS)]
        copy = [s for s in copy if not s[1].startswith(BLENDED_CLIPS)]
        taken = set()
        for at, clip in owner:
            match = next((i for i, (when, name) in enumerate(copy) if i not in taken and name == clip and at - 0.2 <= when <= at + delay), None)
            if match is not None:
                taken.add(match)
            elif first <= at <= copy_end - delay:
                different.append(f"{who} host {clip} at {at - first:.1f}s")
        different += [f"{who} guest {clip} at {at - first:.1f}s" for i, (at, clip) in enumerate(copy)
                      if i not in taken and first + delay <= at <= owner_end]
    return different, blended


def is_clip_action(entry, since):
    """An action that starts an idle or a gesture, after `since` (a move or a stop starts none of its own)."""
    event = entry.get("event", "")
    return (entry["kind"] == "action" and entry["time"] >= since and event.startswith(("Idle", "Dialogue", "MotionDriven"))
            and not event.startswith("IdleStop"))


def skipped_clips(trace):
    """Every clip a game started without its file: it reads as over at once (the engine's 0.01333 s), and the graph runs on."""
    return [f"[{e['game'] or 'host'}] {e['who']}: {clip}" for e in trace.entries if e["kind"] == "clips"
            for clip, state in e["detail"].items() if not state["loaded"] and state["at_end"]]


def scenario_dialogue_last_one_out_ends_it(ctx):
    """A player leaving (Tab) leaves the conversation on for the others; the last one out ends it. The host talks
    and the client joins by talking to its copy; the client leaves, the host's stays, and the host's Tab ends it.
    Then the other way round: the host leaves first and its conversation goes on without its menu, until the
    client leaves too."""
    npc, _, copy = ctx.spawn_host_npc(LYDIA, restrained=True)
    try:
        ctx.place_before("", npc, 110)
        ctx.place_before("client", copy, 110)
        for first, last in (("client", ""), ("", "client")):
            ctx.activate_on("", npc)
            ctx.wait_for(f"[host] the player talks to {npc:08X}", lambda: ctx.talking_to("", npc), 10)
            if ctx.dialogue("client").get("menu"):
                raise ctx.failure("[client] the host's conversation opened its menu: joining is by talking")
            ctx.activate_on("client", copy)
            ctx.wait_for(f"[client] talking to copy {copy:08X} joins", lambda: ctx.talking_to("client", copy), 10)
            ctx.leave_dialogue(first)
            frame = ctx.game_frame("")
            def stays():
                if not ctx.talking_to(last, copy if last else npc) or not ctx.in_conversation("", npc):
                    raise ctx.failure(f"[{first or 'host'}] leaving ended the conversation for the other player")
                return ctx.game_frame("") > frame + 120
            ctx.wait_for(f"[{last or 'host'}] the conversation goes on", stays, 10)
            ctx.leave_dialogue(last)
            ctx.wait_for("[host] the last one out ends the conversation", lambda: not ctx.dialogue("").get("speaker"), 10)
            ctx.hold()
    finally:
        ctx.close_dialogues()
        ctx.remove_host_npc(npc)

def scenario_dialogue_nobody_moves(ctx):
    """The client talks to its copy while the host stands 1500 units off: the host's player is the one talking
    (its game runs the conversation from where it stands, without its menu), and neither player is moved for it.
    The client's menu lists the options and stays open; its Tab, the last one out, ends it."""
    npc, _, copy = ctx.spawn_host_npc(LYDIA, restrained=True)
    try:
        ctx.place_before("", npc, 1500, 90)
        ctx.place_before("client", copy, 110)
        spots = {name: ctx.my_position(name) for name in ("", "client")}
        ctx.activate_on("client", copy)
        ctx.wait_for(f"[host] the player talks to {npc:08X} in the background", lambda: ctx.in_conversation("", npc), 10)
        ctx.wait_for(f"[client] the menu opens on copy {copy:08X}", lambda: ctx.talking_to("client", copy), 10)
        ctx.wait_for("[client] the menu lists options",
                 lambda: ctx.talking_to("client", copy) and ctx.dialogue("client").get("options"), 10)
        for name in ("", "client"):
            if distance(ctx.my_position(name)[:2], spots[name][:2]) > 5:
                raise ctx.failure(f"[{name or 'host'}] the player was moved for the conversation")
        ctx.hold()
        ctx.tap_key("client", TAB_KEY)
        ctx.wait_for("[client] the menu closes", lambda: ctx.menu_closed("client"), 10)
        ctx.wait_for("[host] its conversation ends", lambda: not ctx.in_conversation("", npc), 10)
    finally:
        ctx.close_dialogues()
        ctx.remove_host_npc(npc)

def scenario_dialogue_join_by_talking(ctx):
    """The host talks to its NPC with the client 2000 units off: the client stays out of it (no menu) and reads
    her line on its screen (relayed with the engine's force flag, so at any distance). Coming up to her copy and
    talking to it, the client joins: its menu opens on the copy, the host's conversation goes on."""
    npc, _, copy = ctx.spawn_host_npc(LYDIA, restrained=True)
    try:
        ctx.place_before("client", copy, 2000, 90)
        ctx.place_before("", npc, 110)
        ctx.activate_on("", npc)
        ctx.wait_for(f"[host] the player talks to {npc:08X}", lambda: ctx.talking_to("", npc), 10)
        ctx.wait_for(f"[client] the HUD shows copy {copy:08X}'s line", lambda: ctx.subtitle_on_screen("client") == copy, 10)
        ctx.place_before("client", copy, 110, 90)
        if ctx.dialogue("client").get("menu"):
            raise ctx.failure("[client] near her copy, the menu opened without talking to it")
        ctx.activate_on("client", copy)
        ctx.wait_for(f"[client] talking to copy {copy:08X} joins", lambda: ctx.talking_to("client", copy), 10)
        ctx.wait_for("[client] the menu lists options", lambda: ctx.talking_to("client", copy) and ctx.dialogue("client").get("options"), 10)
        if not ctx.talking_to("", npc):
            raise ctx.failure("[host] the client joining ended the host's conversation")
        ctx.hold()
    finally:
        ctx.close_dialogues()
        ctx.remove_host_npc(npc)

def scenario_dialogue_host_in_background(ctx, *, _game=None):
    """The client talks to its copy with the host 800 units off: the host's game runs the conversation in the
    background (no menu, its player not turned, free to move) and shows her line on its HUD, while the client's
    menu opens. The host walks off and the conversation waits for a pick; walking back and talking to her, the
    host joins: its menu opens on the conversation under way. Then the
    host leaves it (Tab ends it for both), the client talks again, and a pick in the host's background
    conversation says her line: the follower line ends it for both."""
    _game = ctx.game(_game)
    npc, _, copy = ctx.spawn_host_npc(LYDIA, restrained=True)
    try:
        ctx.place_before("", npc, 800, 90)
        ctx.place_before("client", copy, 110)
        ctx.activate_on("client", copy)
        ctx.wait_for(f"[host] the player talks to {npc:08X} in the background", lambda: ctx.in_conversation("", npc), 10, _game=_game)
        ctx.wait_for(f"[client] the menu opens on copy {copy:08X}", lambda: ctx.talking_to("client", copy), 10, _game=_game)
        ctx.wait_for("[host] the conversation lists options", lambda: ctx.dialogue("").get("options"), 10, _game=_game)
        if ctx.dialogue("").get("menu"):
            raise ctx.failure("[host] 800 units off, the dialogue menu opened")
        ctx.wait_for(f"[host] the HUD shows {npc:08X}'s line", lambda: ctx.subtitle_on_screen("") == npc, 10, _game=_game)
        start = ctx.my_position("")
        # Straight away from her: checked placement may not put the host on the asked bearing.
        her = ((ctx.game("").read_status() or {}).get("player") or {}).get("local_actors", {}).get(str(npc), {}).get("position") or start
        away = math.degrees(math.atan2(start[0] - her[0], start[1] - her[1])) % 360
        ctx.console_on("", f"player.setangle z {away:.0f}")
        ctx.press("", "forward")  # the host takes input: no menu
        try:
            ctx.wait_for("[host] walks away from her", lambda: distance(ctx.my_position("")[:2], start[:2]) > 200, 10, _game=_game)
        except TestFailure:
            player = (ctx.game("").read_status() or {}).get("player") or {}
            log(f"[host] started at {[round(v) for v in start]}, now at {[round(v) for v in player.get('position', [])]}, "
                f"turned {player.get('rotation')}, NPC at {(player.get('local_actors') or {}).get(str(npc), {}).get('position')}")
            raise
        finally:
            ctx.release("", "forward")
        # Waiting for a pick, with no menu: the conversation stays (600 frames, about 10 s).
        frame = ctx.game_frame("")
        def waited():
            if not ctx.in_conversation("", npc) or not ctx.talking_to("client", copy):
                raise ctx.failure("[host] waiting for a pick without the menu, the conversation ended")
            return ctx.game_frame("") > frame + 600
        ctx.wait_for("[host] the conversation waits for a pick", waited, 30, _game=_game)
        ctx.face("", npc)
        ctx.press("", "forward")
        try:
            ctx.wait_for("[host] walks back to her", lambda: distance(ctx.my_position("")[:2], start[:2]) < 300, 10, _game=_game)
        finally:
            ctx.release("", "forward")
        if ctx.dialogue("").get("menu"):
            raise ctx.failure("[host] near her, the menu opened without talking to her")
        ctx.activate_on("", npc)
        ctx.wait_for(f"[host] talking to {npc:08X} joins: the menu opens", lambda: ctx.talking_to("", npc), 10, _game=_game)
        ctx.wait_for("[host] the menu lists options", lambda: ctx.dialogue("").get("options"), 10, _game=_game)
        ctx.hold()
        ctx.close_dialogues()

        ctx.place_before("", npc, 800, 90)
        ctx.activate_on("client", copy)
        ctx.wait_for(f"[host] the player talks to {npc:08X} in the background", lambda: ctx.in_conversation("", npc), 10, _game=_game)
        ctx.wait_for(f"[client] the menu opens on copy {copy:08X}", lambda: ctx.talking_to("client", copy), 10, _game=_game)
        options = ctx.wait_for("[host] the conversation lists options", lambda: ctx.dialogue("").get("options"), 10, _game=_game)
        log(f"[host] options: {[o['text'] for o in options]}")
        ctx.player_command("", {"cmd": "dialogue_pick", "index": 0})
        ctx.wait_for("[host] her line (a goodbye) ends the conversation", lambda: not ctx.in_conversation("", npc), 20, _game=_game)
        ctx.wait_for("[client] the menu closes", lambda: ctx.menu_closed("client"), 10, _game=_game)
        if not ctx.takes_input(""):
            raise ctx.failure(f"[host] after the conversation, a menu is open: {(_game.read_status() or {}).get('menus')}")
        ctx.wait_for(f"[host] the follower line recruits {npc:08X}", lambda: ctx.follower_in_slot() == npc, 10, _game=_game)
    finally:
        ctx.close_dialogues()
        ctx.remove_host_npc(npc)

def scenario_dialogue_options_are_the_hosts(ctx):
    """Every menu shows the host's list, judged for the host's player: Brenuin offers "Here. Have a gold piece." only
    to a player carrying gold, and the party's gold is one purse (party-purse.md). The party carries 10: the client
    talks to him (the host's game in the background) and its menu lists the coin. The client picks it: the host's game
    says Brenuin's answer, its script runs for the client (party-model.md, rule 2) and takes one coin from the party;
    the chat on both games names who said it. With the purse empty, nobody's menu lists the coin."""
    gold = ctx.party_gold()
    npc, _, copy = ctx.spawn_host_npc(BRENUIN, restrained=True)
    try:
        # The coin's line comes back 4 hours after it is said (an earlier run said it).
        ctx.console_on("", "set gamehour to gamehour + 5")
        ctx.set_party_gold(10)
        names = [name for name, host in ctx.session_players("").items() if not host]
        if len(names) != 1:
            raise ctx.failure(f"[host] lists {names} as the other players, the scenario needs the client alone")
        ctx.place_before("", npc, 110)
        ctx.place_before("client", copy, 110)
        ctx.activate_on("client", copy)
        ctx.wait_for(f"[client] the menu opens on copy {copy:08X}", lambda: ctx.talking_to("client", copy), 10)
        ctx.wait_for(f"[host] the player talks to {npc:08X} in the background", lambda: ctx.in_conversation("", npc), 10)
        ctx.wait_for("[host] its list has the coin", lambda: ctx.lists_topic("", GIVE_COIN_TOPIC), 30)
        ctx.client_shows_hosts_list("with the coin")
        text = next(o["text"] for o in ctx.dialogue("client")["options"] if o["topic"] == GIVE_COIN_TOPIC)
        ctx.pick_with_keys("client", GIVE_COIN_TOPIC)
        ctx.wait_for(f"[host] {npc:08X} says his answer to the coin",
                 lambda: ctx.dialogue("").get("current_info") == GIVE_COIN_INFO, 10)
        ctx.wait_for(f"[client] copy {copy:08X} voices it", lambda: ctx.line_playing("client", copy), 5)
        ctx.wait_party_gold("the party gave the one coin", 9)
        for name in ("", "client"):
            ctx.wait_for(f"[{name or 'host'}] the chat reads {names[0]}: {text}",
                     lambda name=name: ctx.chat_has(name, names[0], text), 5)
        ctx.close_dialogues()

        ctx.console_on("", "set gamehour to gamehour + 5")
        ctx.set_party_gold(0)
        ctx.activate_on("client", copy)
        ctx.wait_for(f"[client] the menu opens on copy {copy:08X} again", lambda: ctx.talking_to("client", copy), 10)
        ctx.wait_for("[client] the menu shows its list", lambda: ctx.list_shown("client"), 30)
        ctx.client_shows_hosts_list("without the coin")
        if ctx.lists_topic("client", GIVE_COIN_TOPIC):
            raise ctx.failure("[client] its menu lists the coin, which the party cannot give")
        ctx.hold()
    finally:
        ctx.close_dialogues()
        ctx.remove_host_npc(npc)
        for name in ("", "client"):
            ctx.console_on(name, f"player.dispel {GIFT_OF_CHARITY:X}")  # the coin's blessing on the giver
        ctx.set_party_gold(gold, wait=False)

def scenario_copy_knows_learned_spell(ctx):
    """A spell the client learns is known to its character on the host (PerkService carries the added spells with
    the perks): a host condition judged for the client (a College line's HasSpell on the target) reads the client's.
    It forgets it: the copy does too. The host's own player learns nothing."""
    character = ctx.client_on_host()
    try:
        if ctx.has_spell("", character, MAGELIGHT):
            raise ctx.failure(f"[host] the client's character {character:08X} knows Magelight before the client does")
        ctx.console_on("client", f"player.addspell {MAGELIGHT:X}")
        ctx.wait_for(f"[host] the client's character {character:08X} knows the spell the client learned",
                 lambda: ctx.has_spell("", character, MAGELIGHT), 10)
        if ctx.has_spell("", 0x14, MAGELIGHT):
            raise ctx.failure("[host] its own player learned the client's spell")
        ctx.console_on("client", f"player.removespell {MAGELIGHT:X}")
        ctx.wait_for(f"[host] the client's character {character:08X} forgets it with the client",
                 lambda: not ctx.has_spell("", character, MAGELIGHT), 10)
    finally:
        if ctx.has_spell("client", 0x14, MAGELIGHT):
            ctx.console_on("client", f"player.removespell {MAGELIGHT:X}")

def scenario_ability_stays_on_copy(ctx):
    """A permanent ability the client takes (the Steed Stone's) is its character's on the host: the copy has the spell
    (a host condition's HasSpell) and the client's carry weight, the host's own player neither. Both stay after the
    party changes cell (the copy's 3D unloads and loads) and after the client rejoins; removed, both go from the copy."""
    def holds(when):
        copy = ctx.client_on_host()
        ctx.wait_for(f"[host] the client's character has the Steed Stone {when}", lambda: ctx.has_spell("", copy, STEED_ABILITY), 10)
        carried = ctx.actor_value("client", 0x14, CARRY_WEIGHT)
        ctx.wait_for(f"[host] the client's character carries {carried:g}, as the client's own {when}",
                 lambda: ctx.actor_value("", copy, CARRY_WEIGHT) == carried, 10)
        return carried

    before = ctx.actor_value("client", 0x14, CARRY_WEIGHT)
    try:
        ctx.console_on("client", f"player.addspell {STEED_ABILITY:X}")
        if holds("once it takes it") <= before:
            raise ctx.failure(f"[client] the Steed Stone left its carry weight at {before:g}")
        if ctx.has_spell("", 0x14, STEED_ABILITY):
            raise ctx.failure("[host] its own player took the client's ability")
        ctx.both_go(BANNERED_MARE)
        holds("in the Bannered Mare")
        ctx.both_go(HOME_CELL)
        holds("back home")
        ctx.disconnect_client()
        ctx.wait_on("", "the client's character leaves the host's game", lambda actors: ctx.other_player(actors) is None)
        ctx.rejoin_client()
        holds("after rejoining")
        ctx.console_on("client", f"player.removespell {STEED_ABILITY:X}")
        copy = ctx.client_on_host()
        ctx.wait_for("[host] the client's character loses the Steed Stone with the client",
                 lambda: not ctx.has_spell("", copy, STEED_ABILITY), 10)
        ctx.wait_for(f"[host] the client's character carries {before:g} again",
                 lambda: ctx.actor_value("", copy, CARRY_WEIGHT) == before, 10)
    finally:
        if ctx.has_spell("client", 0x14, STEED_ABILITY):
            ctx.console_on("client", f"player.removespell {STEED_ABILITY:X}")

def scenario_dialogue_line_judged_for_guest(ctx):
    """The host judges a line for the player it is for (JudgedFor, shared-dialogue.md): Lucan's claw topic asks
    whether the player holds the golden claw, a condition run on PlayerRef. Judged for the client's character on the
    host, it reads that character's bag (the client's, which its copy carries), not the host's player's: with the claw
    in the client's bag only, the client no longer gets the line for a player without it and the host still does;
    with it in the host's only, the other way round."""
    copy = ctx.player_copy("")
    if not copy:
        raise ctx.failure("[host] shows no copy of the client's character")
    started = not ctx.quest_running("", GOLDEN_CLAW_QUEST)
    if started:
        ctx.console_on("", "resetquest MS13")  # a stopped quest starts again only once reset
        ctx.console_on("", "startquest MS13")
        ctx.wait_for("[host] MS13 runs", lambda: ctx.quest_running("", GOLDEN_CLAW_QUEST), 10)

    def holds(when, host_holds, client_holds):
        got = (ctx.judged_line("", CLAW_TOPIC, LUCAN, 0x14), ctx.judged_line("", CLAW_TOPIC, LUCAN, copy))
        if (got[0] != NO_CLAW_LINE, got[1] != NO_CLAW_LINE) != (host_holds, client_holds):
            raise ctx.failure(f"[host] {when}: Lucan's claw topic gives the host line {got[0]:X}, the client's "
                              f"character {got[1]:X} ({NO_CLAW_LINE:X} is his line for a player without the claw)")

    try:
        holds("nobody holds the claw", False, False)
        ctx.console_on("client", f"player.additem {GOLDEN_CLAW:X} 1")
        ctx.wait_for(f"[host] copy {copy:08X} carries the client's claw", lambda: ctx.item_count("", copy, GOLDEN_CLAW) == 1, 10)
        holds("the client holds the claw", False, True)
        ctx.console_on("client", f"player.removeitem {GOLDEN_CLAW:X} 1")
        ctx.wait_for(f"[host] copy {copy:08X} no longer carries it", lambda: not ctx.item_count("", copy, GOLDEN_CLAW), 10)
        ctx.console_on("", f"player.additem {GOLDEN_CLAW:X} 1")
        holds("the host holds the claw", True, False)
    finally:
        for name in ("", "client"):
            if ctx.item_count(name, 0x14, GOLDEN_CLAW):
                ctx.console_on(name, f"player.removeitem {GOLDEN_CLAW:X} 1")
        if started:
            ctx.console_on("", "stopquest MS13")

def scenario_dialogue_options_are_the_partys(ctx):
    """Every menu shows the party's list, each player's own line for a topic (shared-dialogue.md, "Decided 2026-10-29"):
    the client holds Lucan's golden claw, the host does not. The client talks to Lucan and the host joins by talking
    to him: both menus list the same options in the same order, the claw topic twice: the line for a player holding the
    claw, named for the client, and his line for one without it, named for the host (2026-10-30, the user). The client
    presses its own: the host's game says the client's line, its script runs for the client (rule 2), and the claw
    leaves the client's bag."""
    started = not ctx.quest_running("", GOLDEN_CLAW_QUEST)
    copy = ctx.stage_host_npc(LUCAN)
    # His AI runs (a frozen speaker never finishes a line), restrained so his package does not walk him home.
    ctx.console_on("", f"{LUCAN:08X}.setrestrained 1")
    ctx.freeze("", LUCAN, False)
    try:
        if started:
            ctx.console_on("", "resetquest MS13")
            ctx.console_on("", "startquest MS13")
            ctx.wait_for("[host] MS13 runs", lambda: ctx.quest_running("", GOLDEN_CLAW_QUEST), 10)
        ctx.console_on("", "setstage MS13 60")
        client_copy = ctx.player_copy("")
        ctx.console_on("client", f"player.additem {GOLDEN_CLAW:X} 1")
        ctx.wait_for("[host] the client's character carries the claw", lambda: ctx.item_count("", client_copy, GOLDEN_CLAW) == 1, 10)
        ctx.place_before("", LUCAN, 110)
        ctx.place_before("client", copy, 110)
        ctx.activate_on("client", copy)
        ctx.wait_for(f"[client] the menu opens on copy {copy:08X}", lambda: ctx.talking_to("client", copy), 10)
        ctx.wait_for("[host] the player talks to Lucan in the background", lambda: ctx.in_conversation("", LUCAN), 10)
        ctx.activate_on("", LUCAN)
        ctx.wait_for("[host] joins: its menu opens on Lucan", lambda: ctx.talking_to("", LUCAN), 10)
        ctx.wait_for("[client] its menu offers Lucan's trouble", lambda: ctx.lists_topic("client", LUCAN_TROUBLE_TOPIC), 30)
        ctx.pick_with_keys("client", LUCAN_TROUBLE_TOPIC)
        ctx.wait_for("[client] its choices offer a claw line for a player holding it",
                 lambda: set(ctx.topic_infos("client", CLAW_TOPIC)) & set(CLAW_FOUND_LINES), 90)  # his answer is a long one
        ctx.client_shows_hosts_list("with both claw lines")
        found = next(i for i in ctx.topic_infos("client", CLAW_TOPIC) if i in CLAW_FOUND_LINES)
        if NO_CLAW_LINE not in ctx.topic_infos("", CLAW_TOPIC):
            raise ctx.failure(f"[host] its menu lacks Lucan's line for a player without the claw: {ctx.topic_infos('', CLAW_TOPIC)}")
        ctx.pick_with_keys("client", CLAW_TOPIC, found)
        ctx.wait_for("[host] Lucan says his answer to the client's claw", lambda: ctx.dialogue("").get("current_info") == found, 10)
        ctx.wait_for("[client] the claw leaves its bag", lambda: not ctx.item_count("client", 0x14, GOLDEN_CLAW), 20)
        if ctx.item_count("", 0x14, GOLDEN_CLAW):
            raise ctx.failure("[host] its player was given the claw")
    finally:
        ctx.close_dialogues()
        ctx.console_on("", f"{LUCAN:08X}.setrestrained 0")
        ctx.release_host_npc(LUCAN)
        for name in ("", "client"):
            if ctx.item_count(name, 0x14, GOLDEN_CLAW):
                ctx.console_on(name, f"player.removeitem {GOLDEN_CLAW:X} 1")
        ctx.console_on("", "stopquest MS13")

def scenario_dialogue_highlight_is_shared(ctx):
    """One highlight for the conversation (shared-dialogue.md, "Decided 2026-10-29"): the client talks to Lucan, the
    host joins. The client presses S: the host's menu highlights the same option; the host presses W: the client's
    follows back. The golden claw quest at its stage 60 gives Lucan several topics (without it he has one)."""
    started = not ctx.quest_running("", GOLDEN_CLAW_QUEST)
    copy = ctx.stage_host_npc(LUCAN)
    ctx.console_on("", f"{LUCAN:08X}.setrestrained 1")
    ctx.freeze("", LUCAN, False)
    try:
        if started:
            ctx.console_on("", "resetquest MS13")
            ctx.console_on("", "startquest MS13")
            ctx.wait_for("[host] MS13 runs", lambda: ctx.quest_running("", GOLDEN_CLAW_QUEST), 10)
        ctx.console_on("", "setstage MS13 60")
        ctx.place_before("", LUCAN, 110)
        ctx.place_before("client", copy, 110)
        ctx.activate_on("client", copy)
        ctx.wait_for(f"[client] the menu opens on copy {copy:08X}", lambda: ctx.talking_to("client", copy), 10)
        ctx.activate_on("", LUCAN)
        ctx.wait_for("[host] joins: its menu opens on Lucan", lambda: ctx.talking_to("", LUCAN), 10)
        for name in ("client", ""):
            ctx.wait_for(f"[{name or 'host'}] the menu takes a pick", lambda name=name: ctx.menu_takes_pick(name), 30)
        ctx.wait_for("both menus list the same topics", lambda: ctx.topics_shown("") == ctx.topics_shown("client") and len(ctx.topics_shown("")) >= 2, 10)
        for mover, follower, key, name in (("client", "", DOWN_KEY, "S"), ("", "client", UP_KEY, "W")):
            here = ctx.cursor(mover)
            ctx.tap_key(mover, key)
            ctx.wait_for(f"[{mover or 'host'}] {name} moves its highlight", lambda: ctx.cursor(mover) not in (0, here), 5)
            moved = ctx.cursor(mover)
            ctx.wait_for(f"[{follower or 'host'}] its menu highlights {moved:X} too", lambda: ctx.cursor(follower) == moved, 5)
    finally:
        ctx.close_dialogues()
        ctx.console_on("", f"{LUCAN:08X}.setrestrained 0")
        ctx.release_host_npc(LUCAN)
        ctx.console_on("", "stopquest MS13")

def scenario_dialogue_quest_option_from_host(ctx):
    """A guest's menu lists what the host's quests offer, whatever its own quest state: the client's game stops
    Brenuin's coin quest (its own copy of it; a guest runs no quests and keeps what the save had), so its engine would
    offer no coin. The host's quest runs and its player carries gold: the client talks to Brenuin and its menu lists
    the coin, the host's list. The quest is started again on the client afterwards."""
    host_gold = ctx.item_count("", 0x14, GOLD)
    npc, _, copy = ctx.spawn_host_npc(BRENUIN, restrained=True)
    quest = 0
    try:
        ctx.console_on("", "set gamehour to gamehour + 5")
        if host_gold < 10:
            ctx.console_on("", f"player.additem f {10 - host_gold}")
        ctx.place_before("", npc, 800, 90)
        ctx.place_before("client", copy, 110)
        ctx.activate_on("client", copy)
        ctx.wait_for(f"[client] the menu opens on copy {copy:08X}", lambda: ctx.talking_to("client", copy), 10)
        ctx.wait_for("[host] its list has the coin", lambda: ctx.lists_topic("", GIVE_COIN_TOPIC), 30)
        quest = next(o["quest"] for o in ctx.dialogue("")["options"] if o["topic"] == GIVE_COIN_TOPIC)
        ctx.close_dialogues()
        if not quest or not ctx.quest_running("", quest):
            raise ctx.failure(f"[host] the coin's quest {quest:X} does not run")
        ctx.console_on("client", f"stopquest {BEGGARS_QUEST}")
        ctx.wait_for(f"[client] its quest {quest:X} has stopped", lambda: not ctx.quest_running("client", quest), 5)

        ctx.activate_on("client", copy)
        ctx.wait_for(f"[client] the menu opens on copy {copy:08X} again", lambda: ctx.talking_to("client", copy), 10)
        ctx.wait_for("[client] the menu shows its list", lambda: ctx.list_shown("client"), 30)
        ctx.client_shows_hosts_list("with the coin, its own quest stopped")
        ctx.hold()
    finally:
        ctx.close_dialogues()
        ctx.remove_host_npc(npc)
        if quest and not ctx.quest_running("client", quest):
            ctx.console_on("client", f"startquest {BEGGARS_QUEST}")
        change = host_gold - ctx.item_count("", 0x14, GOLD)
        if change:
            ctx.console_on("", f"player.{'additem' if change > 0 else 'removeitem'} f {abs(change)}")

def scenario_dialogue_said_line_waits(ctx):
    """A line with reset hours is not offered again until they pass, on every game: the client gives Brenuin a
    coin (its line comes back after 4 hours), leaves, and talks to him again: its menu no longer lists the coin,
    as the host's game, which said the line, does not."""
    gold = ctx.item_count("", 0x14, GOLD)
    npc, _, copy = ctx.spawn_host_npc(BRENUIN, restrained=True)
    try:
        ctx.console_on("", "set gamehour to gamehour + 5")
        ctx.console_on("", "player.additem f 10")
        ctx.place_before("", npc, 800, 90)
        ctx.place_before("client", copy, 110)
        ctx.activate_on("client", copy)
        ctx.wait_for(f"[client] the menu opens on copy {copy:08X}", lambda: ctx.talking_to("client", copy), 10)
        ctx.wait_for("[client] its menu lists the coin", lambda: ctx.lists_topic("client", GIVE_COIN_TOPIC), 10)
        ctx.pick_with_keys("client", GIVE_COIN_TOPIC)
        ctx.wait_for(f"[host] {npc:08X} says his answer to the coin",
                 lambda: ctx.dialogue("").get("current_info") == GIVE_COIN_INFO, 10)
        ctx.close_dialogues()

        ctx.activate_on("client", copy)
        ctx.wait_for(f"[client] the menu opens on copy {copy:08X} again", lambda: ctx.talking_to("client", copy), 10)
        ctx.wait_for("[client] the menu shows its list", lambda: ctx.list_shown("client"), 30)
        if ctx.lists_topic("client", GIVE_COIN_TOPIC):
            raise ctx.failure("[client] its menu offers the coin again within its reset hours")
        ctx.hold()
    finally:
        ctx.close_dialogues()
        ctx.remove_host_npc(npc)
        for name in ("", "client"):
            ctx.console_on(name, f"player.dispel {GIFT_OF_CHARITY:X}")  # the coin's blessing on the giver
        change = gold - ctx.item_count("", 0x14, GOLD)
        if change:
            ctx.console_on("", f"player.{'additem' if change > 0 else 'removeitem'} f {abs(change)}")

def scenario_dialogue_pick_says_the_hosts_line(ctx):
    """A topic with a different line per player is said with the host's: Hulda's "rent a room" is the room for 10
    gold for a player with the gold, "cannot afford it" for one without. The party carries at least 10 (one purse,
    party-purse.md): the client's menu shows the host's line, the client picks it, and the host's game says it: the
    room. Its script runs for the client (party-model.md, rule 2): the client's character pays, once, from the party's
    gold. The room is the party's. Then both menus show the host's next list."""
    gold = ctx.party_gold()
    npc, _, copy = ctx.spawn_host_npc(HULDA, restrained=True)
    try:
        if gold < 10:
            ctx.set_party_gold(10)
        ctx.place_before("", npc, 110)
        ctx.place_before("client", copy, 110)
        ctx.activate_on("client", copy)
        ctx.wait_for(f"[client] the menu opens on copy {copy:08X}", lambda: ctx.talking_to("client", copy), 10)
        ctx.wait_for(f"[host] the player talks to {npc:08X} in the background", lambda: ctx.in_conversation("", npc), 10)
        host_info = ctx.wait_for("[host] its list has rent a room", lambda: ctx.info_shown("", RENT_ROOM_TOPIC), 30)
        if host_info == RENT_ROOM_NO_GOLD_INFO:
            raise ctx.failure(f"[host] carrying gold, its line for rent a room is the one without ({host_info:X})")
        ctx.client_shows_hosts_list("with the host's line for rent a room")
        party = ctx.party_gold()
        ctx.pick_with_keys("client", RENT_ROOM_TOPIC)
        ctx.wait_for(f"[host] {npc:08X} says the host's line {host_info:X}", lambda: ctx.dialogue("").get("current_info") == host_info, 10)
        ctx.wait_for(f"[client] copy {copy:08X} voices it", lambda: ctx.line_playing("client", copy), 5)
        ctx.wait_for("[client] the menu shows the next list", lambda: ctx.list_shown("client"), 30)
        ctx.client_shows_hosts_list("after the pick")
        ctx.wait_party_gold("the party paid for the room once", party - 10)
        # The room is the party's: rented by the client's character, the bed is the player's (0x7) on the host.
        bed = ctx.script_var("", npc, "RentRoomScript", "Bed")
        owner = ctx.player_command("", {"cmd": "form", "form": bed}).get("owner")
        if owner != PLAYER_BASE:
            raise ctx.failure(f"[host] the rented bed {bed:X} is owned by {owner}, not the player's base")
        ctx.hold()
    finally:
        ctx.close_dialogues()
        ctx.remove_host_npc(npc)
        ctx.set_party_gold(gold, wait=False)

def scenario_natives_reach_matches_ida(ctx):
    """The plugin's reading of which global natives act on this game's player (Papyrus/NativeReach, rule 2's natives
    on the player) matches IDA's over the game (scripts/re/papyrus_natives.py, docs/re/papyrus_natives.tsv): no
    native moves to the guest's game, or stays on the host, that IDA reads otherwise. Natives IDA does not list
    (SKSE's) are not compared."""
    expected = {}
    with open(ROOT / "docs/re/papyrus_natives.tsv") as table:
        header = table.readline().rstrip("\n").split("\t")
        for line in table:
            row = dict(zip(header, line.rstrip("\n").split("\t")))
            if row["self"] == "StaticFunctionTag" and row["function"] != "?":
                expected.setdefault(row["class"], {})[row["name"].lower()] = bool(row.get("reaches the player"))
    differ = []
    for klass, natives in sorted(expected.items()):
        read = {name.lower(): reaches for name, reaches in
                ctx.player_command("", {"cmd": "global_natives", "class": klass})["natives"].items()}
        for name, reaches in sorted(natives.items()):
            if name not in read:
                differ.append(f"{klass}.{name}: not in the VM")
            elif read[name] != reaches:
                differ.append(f"{klass}.{name}: the plugin reads {'the player' if read[name] else 'no player'}, IDA "
                              f"{'the player' if reaches else 'no player'}")
    if differ:
        raise ctx.failure(f"[host] {len(differ)} global natives read otherwise than IDA: " + "; ".join(differ))

def scenario_dialogue_picker_gets_speech_experience(ctx):
    """A persuade's Speech experience is the picker's: the host's game says every line and runs its script for the
    player who picked it (party-model.md, rule 2), and the Game.AdvanceSkill in it runs on that player's game. The
    client talks to Ungrien's copy, asks about the Black-Briars and persuades him (FavorDialogueScript.Persuade; the
    host's list, judged for the host's Speech, set to 30): the client's Speech gains experience, the host's does
    not. The persuade sizes it from Game.GetPlayer().GetAv("Speechcraft"), the client's character on the host, which
    carries the client's Speech (45). Its Game.QueryStat asks the client's game, whose bribes and intimidations are
    counted (the host's are not): the achievement it then gives goes to the client's game. Then the host persuades
    another Ungrien itself (his topic is gone once he is persuaded): its own Speech gains it, the client's not."""
    speech = {name: ctx.skill(name, SPEECH_SKILL)[0] for name in ("", "client")}
    stats = {name: {stat: ctx.misc_stat(name, stat) for stat in PERSUADE_ACHIEVEMENT_STATS} for name in ("", "client")}
    npc, _, copy = ctx.spawn_host_npc(UNGRIEN, restrained=True)
    def persuade(name, talk_to):
        other = "client" if name == "" else ""
        ctx.place_before(name, talk_to, 110)
        ctx.activate_on(name, talk_to)
        ctx.wait_for(f"[{name or 'host'}] the menu opens on {talk_to:08X}", lambda: ctx.talking_to(name, talk_to), 10)
        ctx.wait_for(f"[{name or 'host'}] its menu lists the Black-Briars", lambda: ctx.lists_topic(name, UNGRIEN_TOPIC), 30)
        ctx.pick_with_keys(name, UNGRIEN_TOPIC)
        ctx.wait_for(f"[{name or 'host'}] its menu lists the persuade", lambda: ctx.lists_topic(name, UNGRIEN_PERSUADE_TOPIC), 30)
        before = {player: ctx.skill(player, SPEECH_SKILL) for player in ("", "client")}
        ctx.pick_with_keys(name, UNGRIEN_PERSUADE_TOPIC)
        ctx.wait_for(f"[{name or 'host'}] its Speech gains the persuade's experience", lambda: ctx.skill(name, SPEECH_SKILL) > before[name], 30)
        frame = ctx.game_frame("")
        def other_unchanged():
            if ctx.skill(other, SPEECH_SKILL) != before[other]:
                raise ctx.failure(f"[{other or 'host'}] its Speech gained the persuade of {name or 'the host'}: "
                                  f"{before[other]} -> {ctx.skill(other, SPEECH_SKILL)}")
            return ctx.game_frame("") > frame + 120
        ctx.wait_for(f"[{other or 'host'}] its Speech stays as it was", other_unchanged, 10)
        ctx.close_dialogues()
    try:
        ctx.console_on("", "player.setav speechcraft 30")
        ctx.console_on("client", "player.setav speechcraft 45")
        for stat in PERSUADE_ACHIEVEMENT_STATS:
            ctx.set_misc_stat("", stat, 0)
            ctx.set_misc_stat("client", stat, max(stats["client"][stat], 1))
        character = ctx.client_on_host()
        ctx.wait_for(f"[host] the client's character {character:08X} carries the client's Speech",
                 lambda: ctx.actor_value("", character, SPEECH_SKILL) == ctx.skill("client", SPEECH_SKILL)[0], 10)
        ctx.place_before("", npc, 800, 90)
        _, since = ctx.log_trail("client")
        persuade("client", copy)
        ctx.wait_for("[client] the persuade's achievement runs here: its QueryStat answered for the client",
                 lambda: ctx.natives_run_for_host(since, "AddAchievement"), 10)
        ctx.remove_host_npc(npc)
        npc, _, copy = ctx.spawn_host_npc(UNGRIEN, restrained=True)
        ctx.place_before("client", copy, 800, 90)
        _, since = ctx.log_trail("client")
        persuade("", npc)
        if achievements := ctx.natives_run_for_host(since, "AddAchievement"):
            raise ctx.failure(f"[client] the host's own persuade gave the client's game an achievement: {achievements[0]}")
        ctx.hold()
    finally:
        ctx.close_dialogues()
        ctx.remove_host_npc(npc)
        for name in ("", "client"):
            ctx.console_on(name, f"player.setav speechcraft {speech[name]:.0f}")
            for stat, value in stats[name].items():
                ctx.set_misc_stat(name, stat, value)

def scenario_dialogue_refused_start(ctx):
    """A conversation the host's game cannot start is refused to the peer who asked: the client talks to the copy
    of an NPC that is fighting, its menu does not open and its chat says why. Once the fight is over, the host
    talks to that NPC itself: the client's menu stays closed (the refused ask is forgotten), and the client can
    then join by talking to the copy as usual."""
    npc, _, copy = ctx.spawn_host_npc(LYDIA)
    foe, _, _ = ctx.spawn_host_npc(BRENUIN)
    try:
        ctx.place_before("", npc, 300)
        ctx.place_before("client", copy, 110)
        ctx.console_on("", f"{foe:08X}.setav health 100000")
        ctx.console_on("", f"{npc:08X}.startcombat {foe:08X}")
        lines = len(ctx.chat("client").get("lines", []))
        ctx.activate_on("client", copy)
        ctx.wait_for("[client] its chat says the host can't talk to them",
                 lambda: any(l["system"] and "can't talk" in l["text"] for l in ctx.chat("client").get("lines", [])[lines:]), 10)
        if ctx.dialogue("client").get("menu") or ctx.dialogue("").get("speaker"):
            raise ctx.failure("[client] a conversation opened with an NPC in combat")
        ctx.console_on("", f"{npc:08X}.stopcombat")
        ctx.console_on("", f"{foe:08X}.stopcombat")
        ctx.remove_host_npc(foe)
        foe = None
        ctx.place_before("", npc, 110)
        ctx.activate_on("", npc)
        ctx.wait_for(f"[host] the player talks to {npc:08X}", lambda: ctx.talking_to("", npc), 10)
        frame = ctx.game_frame("client")
        def client_stays_out():
            if ctx.dialogue("client").get("menu"):
                raise ctx.failure("[client] its menu opened for the host's conversation: the refused ask was kept")
            return ctx.game_frame("client") > frame + 60
        ctx.wait_for("[client] its menu stays closed", client_stays_out, 10)
        ctx.activate_on("client", copy)
        ctx.wait_for(f"[client] talking to copy {copy:08X} joins: the menu opens", lambda: ctx.talking_to("client", copy), 10)
        ctx.hold()
    finally:
        ctx.close_dialogues()
        if foe:
            ctx.remove_host_npc(foe)
        ctx.remove_host_npc(npc)

def scenario_dialogue_ends_on_travel(ctx):
    """Walking off does not end a conversation (the host talks from anywhere), travel does: the client talks to
    Lydia's copy (the host's game in the background) and the host takes the party to the inn. The host's
    conversation ends and the client's menu closes. Back home, the next conversation runs as usual: the host
    talks to a new Lydia, the client's HUD shows her line and the client joins by talking to her copy (its copy of
    the first Lydia was deleted mid-conversation, which left its game unable to talk to anyone)."""
    npc, _, copy = ctx.spawn_host_npc(LYDIA, restrained=True)
    try:
        ctx.place_before("", npc, 800, 90)
        ctx.place_before("client", copy, 110)
        ctx.activate_on("client", copy)
        ctx.wait_for(f"[client] the menu opens on copy {copy:08X}", lambda: ctx.talking_to("client", copy), 10)
        ctx.wait_for(f"[host] the player talks to {npc:08X} in the background", lambda: ctx.in_conversation("", npc), 10)
        ctx.both_go(INN)
        ctx.wait_for("[host] its conversation ended", lambda: not ctx.dialogue("").get("speaker"), 10)
        ctx.wait_for("[client] its menu closed", lambda: ctx.menu_closed("client"), 10)
        ctx.both_go(HOME_CELL)
        ctx.remove_host_npc(npc)
        npc, _, copy = ctx.spawn_host_npc(LYDIA, restrained=True)
        ctx.place_before("", npc, 110)
        ctx.place_before("client", copy, 300)
        ctx.activate_on("", npc)
        ctx.wait_for(f"[host] the player talks to {npc:08X}", lambda: ctx.talking_to("", npc), 10)
        ctx.wait_for(f"[client] the HUD shows copy {copy:08X}'s line", lambda: ctx.subtitle_on_screen("client") == copy, 10)
        ctx.activate_on("client", copy)
        ctx.wait_for(f"[client] talking to copy {copy:08X} joins: the menu opens", lambda: ctx.talking_to("client", copy), 10)
        ctx.hold()
    finally:
        ctx.close_dialogues()
        if ctx.player_cell("") != ctx.player_cell("client") or not ctx.player_cell(""):
            ctx.both_go(HOME_CELL)
        ctx.remove_host_npc(npc)

def scenario_dialogue_npc_removed(ctx):
    """The NPC goes away mid-conversation (the host's game deletes it): the client's menu on its copy closes, and
    the client can talk to the next NPC (a copy deleted while its game talks to it left that game unable to start
    another conversation)."""
    npc, _, copy = ctx.spawn_host_npc(LYDIA, restrained=True)
    try:
        ctx.place_before("", npc, 800, 90)
        ctx.place_before("client", copy, 110)
        ctx.activate_on("client", copy)
        ctx.wait_for(f"[client] the menu opens on copy {copy:08X}", lambda: ctx.talking_to("client", copy), 10)
        ctx.wait_for(f"[host] the player talks to {npc:08X} in the background", lambda: ctx.in_conversation("", npc), 10)
        ctx.remove_host_npc(npc)
        ctx.wait_for("[client] its menu closes", lambda: ctx.menu_closed("client"), 10)
        npc, _, copy = ctx.spawn_host_npc(LYDIA, restrained=True)
        ctx.place_before("client", copy, 110)
        ctx.activate_on("client", copy)
        ctx.wait_for(f"[client] talking to the new copy {copy:08X} opens the menu", lambda: ctx.talking_to("client", copy), 10)
        ctx.hold()
    finally:
        ctx.close_dialogues()
        ctx.remove_host_npc(npc)

def scenario_dialogue_npc_starts(ctx):
    """An NPC starts the conversation (a force greet; NPCs run their AI on the host's game, so it greets the host's
    player): it greets the party, every game's menu opens on it (the clients' on their copies), with the host's
    topics, without anyone talking to it. While the client's conversation with another NPC runs in the
    background, a force greet waits: the host's game stays in the client's conversation."""
    npc, server_id, copy = ctx.spawn_host_npc(LYDIA, restrained=True)
    other, _, other_copy = ctx.spawn_host_npc(HULDA, restrained=True)
    try:
        ctx.place_before("", npc, 110)
        ctx.place_before("client", copy, 110)
        if not ctx.force_greet("", npc):
            raise ctx.failure(f"[host] {npc:08X}'s force greet did not start")
        ctx.wait_for(f"[host] the menu opens on {npc:08X}", lambda: ctx.talking_to("", npc), 10)
        host_topics = ctx.wait_for("[host] the conversation lists options", lambda: ctx.topics_shown(""), 5)
        ctx.wait_for(f"[client] greeted too: the menu opens on copy {copy:08X}", lambda: ctx.talking_to("client", copy), 10)
        ctx.wait_for("[client] the menu shows the host's topics",
                 lambda: ctx.topics_shown("client")[:len(host_topics)] == host_topics, 10)
        ctx.close_dialogues()

        ctx.place_before("client", other_copy, 110)
        ctx.activate_on("client", other_copy)
        ctx.wait_for(f"[client] the menu opens on copy {other_copy:08X}", lambda: ctx.talking_to("client", other_copy), 10)
        ctx.wait_for(f"[host] the player talks to {other:08X} in the background", lambda: ctx.in_conversation("", other), 10)
        if ctx.force_greet("", npc):
            raise ctx.failure(f"[host] {npc:08X}'s force greet started during the client's conversation")
        frame = ctx.game_frame("")
        def stays():
            if not ctx.in_conversation("", other) or not ctx.talking_to("client", other_copy):
                raise ctx.failure("the client's conversation did not survive the force greet")
            return ctx.game_frame("") > frame + 60
        ctx.wait_for("[host] its game stays in the client's conversation", stays, 10)
        ctx.hold()
    finally:
        ctx.close_dialogues()
        ctx.remove_host_npc(npc)
        ctx.remove_host_npc(other)

def scenario_dialogue_several_questions_deep(ctx):
    """A conversation three questions deep, picked by turns: Andurs greets with a question; the client answers,
    the host (joined by talking) asks the follow-up, the client answers the last one. After each pick both
    menus show the host's next list (topics, lines, texts), the line's choices only, and a peer's not before the NPC
    has answered."""
    npc, _, copy = ctx.spawn_host_npc(ANDURS, restrained=True)
    def next_list(what, topics):
        def shows(name):
            shown = set(ctx.topics_shown(name)) if ctx.list_shown(name) else None
            if shown == topics and name and ctx.line_playing("", npc):
                raise ctx.failure(f"[{name}] the menu offers {what} while the host's NPC still answers")
            return shown == topics
        for name in ("client", ""):
            ctx.wait_for(f"[{name or 'host'}] the menu shows {what}", lambda name=name: shows(name) and shows("client"), 60)
    try:
        ctx.place_before("", npc, 110)
        ctx.place_before("client", copy, 300)
        ctx.activate_on("client", copy)
        ctx.wait_for(f"[client] the menu opens on copy {copy:08X}", lambda: ctx.talking_to("client", copy), 10)
        ctx.wait_for(f"[host] the player talks to {npc:08X} in the background", lambda: ctx.in_conversation("", npc), 10)
        ctx.activate_on("", npc)
        ctx.wait_for(f"[host] talking to {npc:08X} joins: the menu opens", lambda: ctx.talking_to("", npc), 10)
        ctx.wait_for("[client] the menu lists Andurs' question", lambda: ctx.lists_topic("client", ANDURS_KNOW_ARKAY), 30)
        ctx.client_shows_hosts_list("at his question")
        ctx.pick_with_keys("client", ANDURS_KNOW_ARKAY)
        next_list("the follow-up", {ANDURS_WHAT_LOST})
        ctx.client_shows_hosts_list("at the follow-up")
        ctx.pick_with_keys("", ANDURS_WHAT_LOST)
        next_list("the amulet's choices", ANDURS_AMULET)
        ctx.client_shows_hosts_list("at the amulet's choices")
        ctx.pick_with_keys("client", ctx.topics_shown("client")[0])
        ctx.wait_for(f"[host] {npc:08X} says the last answer", lambda: ctx.line_playing("", npc), 10)
        ctx.hold()
    finally:
        ctx.close_dialogues()
        ctx.remove_host_npc(npc)

def scenario_dialogue_guest_skips_line(ctx):
    """A guest presses E through the NPC's answer, as a player skips a line: the host's game, running the conversation
    without its menu, moves on at each press (the next response, then the next list), and the guest's copy goes
    quiet with it. Left alone Andurs answers for tens of seconds."""
    npc, _, copy = ctx.spawn_host_npc(ANDURS, restrained=True)
    def left():
        return ctx.voice("", npc).get("seconds") or -1
    try:
        ctx.place_before("", npc, 800, 90)
        ctx.place_before("client", copy, 110)
        ctx.activate_on("client", copy)
        ctx.wait_for(f"[client] the menu opens on copy {copy:08X}", lambda: ctx.talking_to("client", copy), 10)
        ctx.wait_for("[client] the menu lists Andurs' question", lambda: ctx.lists_topic("client", ANDURS_KNOW_ARKAY), 30)
        ctx.pick_with_keys("client", ANDURS_KNOW_ARKAY)
        ctx.wait_for(f"[host] {npc:08X} answers", lambda: ctx.line_playing("", npc), 10)
        skipped = 0
        while not ctx.list_shown("client"):
            ctx.wait_for("[client] the menu takes a press",
                         lambda: (ctx.dialogue("client").get("swf") or {}).get("allow_progress") == 1 or ctx.list_shown("client"), 5)
            before = left()
            if ctx.list_shown("client") or before < 3:
                ctx.wait_for("[host] the short line ends", lambda: ctx.list_shown("client") or left() > before, 5)
                continue
            log(f"[client] presses E with {before:.1f} s of the line left")
            ctx.tap_key("client", ACTIVATE_KEY)
            ctx.wait_for(f"[host] the press ends the line ({before:.1f} s left)",
                         lambda: ctx.list_shown("client") or left() <= 0 or left() > before, 2)
            skipped += 1
        if not skipped:
            raise ctx.failure("no line was long enough to skip")
        ctx.wait_for(f"[client] copy {copy:08X} goes quiet with the host's NPC", lambda: ctx.line_over("client", copy), 2)
        ctx.hold()
    finally:
        ctx.close_dialogues()
        ctx.remove_host_npc(npc)

def scenario_dialogue_said_once_line(ctx):
    """A say-once line is not said again once said, on any game: the client asks Hulda about the College (one of
    her two say-once lines), leaves and talks to her again: its menu, as the host's (joined by talking), offers
    the topic with her other line, not the one said. Both lines are marked unsaid on both games before and
    after (they stay said in a game otherwise)."""
    def unsay():
        for name in ("", "client"):
            for line in HULDA_COLLEGE_LINES:
                ctx.info_said(name, line, False)
    npc, _, copy = ctx.spawn_host_npc(HULDA, restrained=True)
    try:
        unsay()
        ctx.place_before("", npc, 800, 90)
        ctx.place_before("client", copy, 110)
        ctx.activate_on("client", copy)
        ctx.wait_for(f"[client] the menu opens on copy {copy:08X}", lambda: ctx.talking_to("client", copy), 10)
        ctx.wait_for("[client] its menu offers the College", lambda: ctx.lists_topic("client", HULDA_COLLEGE_TOPIC), 10)
        ctx.pick_with_keys("client", HULDA_COLLEGE_TOPIC)
        said = ctx.wait_for(f"[host] {npc:08X} says a College line",
                        lambda: (info := ctx.dialogue("").get("current_info")) in HULDA_COLLEGE_LINES and info, 10)
        ctx.close_dialogues()

        ctx.activate_on("client", copy)
        ctx.wait_for(f"[client] the menu opens on copy {copy:08X} again", lambda: ctx.talking_to("client", copy), 10)
        ctx.place_before("", npc, 110)
        ctx.activate_on("", npc)
        ctx.wait_for(f"[host] talking to {npc:08X} joins: the menu opens", lambda: ctx.talking_to("", npc), 10)
        for name in ("", "client"):
            ctx.wait_for(f"[{name or 'host'}] the menu shows its list", lambda name=name: ctx.list_shown(name), 30)
        lines = {name or "host": ctx.info_shown(name, HULDA_COLLEGE_TOPIC) for name in ("", "client")}
        if said in lines.values() or lines["host"] != lines["client"]:
            raise ctx.failure(f"after line {said:X} was said, the College lines offered are {lines}: the said one "
                              f"again, or not the host's")
        ctx.hold()
    finally:
        ctx.close_dialogues()
        ctx.remove_host_npc(npc)
        unsay()

def scenario_dialogue_question_answered_by_who_can(ctx):
    """A question only another player can answer is that player's to answer (shared-dialogue.md, "Decided
    2026-10-29"): Onmund's "Why aren't there many Nords at the College?" has one answer, for a player who is not a
    Nord; the host is a Nord, the client a Breton for the scenario. The client asks it, the host joined by talking:
    both menus list the client's answer, named for it. The host presses it: nothing is said, and only the host's chat
    says "I'm sorry <name>, you don't know how to say that." The client presses it: the host's game says the client's
    line and the conversation goes on. His greetings, said once in a game, are marked unsaid before and after."""
    def unsay():
        for name in ("", "client"):
            for line in ONMUND_GREETINGS:
                ctx.info_said(name, line, False)
    sorry = lambda instance: [l["text"] for l in ctx.chat(instance).get("lines", [])
                              if l.get("system") and l["text"].startswith("I'm sorry ")]
    npc, _, copy = ctx.spawn_host_npc(ONMUND, restrained=True)
    try:
        unsay()
        ctx.console_on("client", "player.setrace BretonRace")
        ctx.place_before("", npc, 110, 90)
        ctx.place_before("client", copy, 110, 270)
        ctx.activate_on("client", copy)
        ctx.wait_for(f"[client] the menu opens on copy {copy:08X}", lambda: ctx.talking_to("client", copy), 10)
        ctx.wait_for("[client] his greeting is over: the menu shows his question's answers",
                 lambda: ctx.list_shown("client") and ctx.lists_topic("client", ONMUND_FEW_NORDS), 30)
        ctx.activate_on("", npc)
        ctx.wait_for(f"[host] talking to {npc:08X} joins: the menu opens", lambda: ctx.talking_to("", npc), 10)
        ctx.pick_with_keys("client", ONMUND_FEW_NORDS)
        for name in ("client", ""):
            ctx.wait_for(f"[{name or 'host'}] its menu lists the client's answer, not being a Nord",
                     lambda name=name: ctx.lists_topic(name, ONMUND_NOT_A_NORD), 60)
        answer = ctx.topic_info("client", ONMUND_NOT_A_NORD)
        text = next(t for topic, _, t in ctx.options_shown("") if topic == ONMUND_NOT_A_NORD)
        if not text.endswith("]"):
            raise ctx.failure(f"[host] the client's answer is not named for it: {text!r}")
        before = len(sorry(""))
        ctx.pick_with_keys("", ONMUND_NOT_A_NORD)
        ctx.wait_for("[host] its chat says its player does not know how to say it", lambda: len(sorry("")) > before, 5)
        if not sorry("")[-1].endswith(", you don't know how to say that."):
            raise ctx.failure(f"[host] its chat line reads {sorry('')[-1]!r}")
        if sorry("client"):
            raise ctx.failure("[client] its chat shows the host's refusal")
        if ctx.dialogue("").get("current_info") == answer:
            raise ctx.failure("[host] its game said the client's answer for the host's press")
        ctx.pick_with_keys("client", ONMUND_NOT_A_NORD)
        ctx.wait_for("[host] Onmund answers the client's line", lambda: ctx.dialogue("").get("current_info") == answer, 10)
        if not ctx.dialogue("").get("speaker"):
            raise ctx.failure("[host] the conversation ended")
        ctx.hold()
    finally:
        ctx.close_dialogues()
        ctx.remove_host_npc(npc)
        unsay()
        ctx.console_on("client", "player.setrace NordRace")
        ctx.wait_for("[host] shows the client a Nord again", lambda: ctx.player_race_on_host("client") == NORD_RACE, 30)

def scenario_dialogue_picker_trains(ctx):
    """A trainer's menu opens on the game of whoever picked its line: the client talks to Amren's copy and picks
    training. The host's game says the line and runs its script for the client (party-model.md, rule 2), whose
    Game.ShowTrainingMenu runs on the client's game, with the copy, over its dialogue menu as in single player; the
    host's game opens nothing. The client trains once: its One-handed goes up, the host's stays as it was, and the
    party's gold (one purse, party-purse.md) goes down once, on every player (the menu pays from the inventory itself,
    InventoryChanges::RemoveItem). Leaving the training menu, the client is back in the conversation, as in
    single player."""
    gold = ctx.party_gold()
    trained = {name: (ctx.skill(name, ONE_HANDED_SKILL)[0], ctx.trainings(name)) for name in ("", "client")}
    npc, _, copy = ctx.spawn_host_npc(AMREN, restrained=True)
    try:
        ctx.set_party_gold(gold + 2000)
        ctx.place_before("", npc, 800, 90)
        ctx.place_before("client", copy, 110)
        ctx.activate_on("client", copy)
        ctx.wait_for(f"[client] the menu opens on copy {copy:08X}", lambda: ctx.talking_to("client", copy), 10)
        ctx.wait_for(f"[host] the player talks to {npc:08X} in the background", lambda: ctx.in_conversation("", npc), 10)
        ctx.wait_for(f"[host] {npc:08X}'s greeting is over", lambda: ctx.line_over("", npc), 30)
        ctx.wait_for("[client] its menu lists training", lambda: ctx.lists_topic("client", TRAINING_TOPIC), 5)
        before = {name: ctx.skill(name, ONE_HANDED_SKILL) for name in ("", "client")}
        party = ctx.party_gold()
        ctx.pick_with_keys("client", TRAINING_TOPIC)
        ctx.wait_for(f"[host] {npc:08X} says the training line", lambda: ctx.line_playing("", npc), 10)
        ctx.wait_for("[client] the training menu opens", lambda: TRAINING_MENU in ctx.menus("client"), 30)
        frame = ctx.game_frame("")
        def host_has_none():
            if TRAINING_MENU in ctx.menus(""):
                raise ctx.failure("[host] the training menu opened on the host's game")
            return ctx.game_frame("") > frame + 60
        ctx.wait_for("[host] no training menu", host_has_none, 10)
        ctx.tap_key("client", ACTIVATE_KEY)
        ctx.wait_for("[client] its One-handed goes up a level",
                 lambda: ctx.skill("client", ONE_HANDED_SKILL)[0] == before["client"][0] + 1, 10)
        if ctx.skill("", ONE_HANDED_SKILL) != before[""]:
            raise ctx.failure(f"[host] its One-handed changed: {before['']} -> {ctx.skill('', ONE_HANDED_SKILL)}")
        left = ctx.item_count("client", 0x14, GOLD)
        if left >= party:
            raise ctx.failure(f"[client] training cost nothing: {party} gold before, {left} after")
        ctx.wait_party_gold(f"every player carries the {left} gold the party has left", left, 5)
        ctx.tap_key("client", TAB_KEY)
        ctx.wait_for("[client] the training menu closes", lambda: TRAINING_MENU not in ctx.menus("client"), 10)
        if not ctx.talking_to("client", copy):
            raise ctx.failure("[client] left the conversation with the training menu")
        ctx.wait_for('the returned dialogue accepts another training choice', lambda: ctx.menu_takes_pick('client'), 15)
        trained_once = ctx.skill('client', ONE_HANDED_SKILL)
        ctx.pick_with_keys('client', TRAINING_TOPIC)
        ctx.wait_for('training can reopen from the returned dialogue', lambda: TRAINING_MENU in ctx.menus('client'), 30)
        ctx.tap_key('client', TAB_KEY)
        ctx.wait_for('the second training menu closes without a purchase', lambda: TRAINING_MENU not in ctx.menus('client'), 10)
        ctx.wait_for('dialogue is usable after the second training return', lambda: ctx.menu_takes_pick('client'), 15)
        if ctx.skill('client', ONE_HANDED_SKILL) != trained_once:
            raise ctx.failure('opening training again trained the player without a purchase')
        ctx.wait_party_gold('opening training again does not charge the party', left)
        ctx.leave_dialogue('client')
        ctx.wait_for('the guest can exit after returning from training', lambda:
                     not ctx.dialogue('')['speaker'] and ctx.takes_input('client'), 10)
        ctx.hold()
    finally:
        if TRAINING_MENU in ctx.menus("client"):
            ctx.tap_key("client", TAB_KEY)
            ctx.wait_for("[client] the training menu closes", lambda: TRAINING_MENU not in ctx.menus("client"), 10)
        ctx.close_dialogues()
        ctx.remove_host_npc(npc)
        ctx.set_party_gold(gold, wait=False)
        for name in ("", "client"):
            level, count = trained[name]
            if ctx.skill(name, ONE_HANDED_SKILL)[0] != level:
                ctx.console_on(name, f"player.setav onehanded {level:.0f}")
            ctx.trainings(name, count)

def scenario_dialogue_pick_reaches_host(ctx):
    """The client talks to its copy (the host's game runs it in the background, the host 800 units off) and picks
    an option with its keys: the client's menu does not say it, the host's game does (her line on the host's NPC,
    voiced on the client's copy), and both games follow it: a goodbye ends the conversation for both, any other
    line leads both to the same next list."""
    npc, _, copy = ctx.spawn_host_npc(LYDIA, restrained=True)
    _, pick_mark = ctx.log_trail('', 1 << 62)
    try:
        ctx.place_before("", npc, 800, 90)
        ctx.place_before("client", copy, 110)
        ctx.activate_on("client", copy)
        ctx.wait_for(f"[host] the player talks to {npc:08X} in the background", lambda: ctx.in_conversation("", npc), 10)
        ctx.wait_for(f"[client] the menu opens on copy {copy:08X}", lambda: ctx.talking_to("client", copy), 10)
        options = ctx.wait_for("[client] the menu lists options",
                           lambda: ctx.talking_to("client", copy) and ctx.dialogue("client").get("options"), 10)
        ctx.wait_for("[host] its NPC's greeting is over", lambda: ctx.line_over("", npc), 30)
        # The client's copy voices it a moment later; a press in the frame its greeting ends is lost (B3, docs/bugs.md).
        ctx.wait_for("[client] its copy's greeting is over", lambda: ctx.line_over("client", copy), 30)
        ctx.wait_for("[client] the menu takes a pick", lambda: ctx.menu_takes_pick("client"), 10)
        selection = ctx.dialogue('client')
        before_swf = selection.get('swf') or {}
        index = before_swf.get('cursor_index', -1)
        if not 0 <= index < len(selection.get('options') or []):
            raise ctx.failure(f'the ready dialogue menu has no selected option: {selection}')
        picked = selection['options'][index]
        log(f"[client] picks {picked['text']!r} (its menu's SWF: {before_swf})")
        ctx.tap_key("client", ACTIVATE_KEY)
        try:
            ctx.wait_for('the host accepts the selected topic and INFO', lambda: any(
                f"picked topic {picked['topic']:X} (info {picked['info']:X})" in line['text']
                for line in ctx.log_trail('', pick_mark)[0]), 10)
            ctx.wait_for(f"[host] {npc:08X} says the picked line", lambda: ctx.line_playing("", npc), 10)
        except TestFailure as error:
            # Where the press went: the SWF before it and now, and whether this game asked the host at all.
            lines, _ = ctx.log_trail("client", 0)
            asked = [l["text"] for l in lines if l["text"].startswith("Dialogue:")][-4:]
            raise ctx.failure(f"{error} (the client's SWF before the press {before_swf}, now {ctx.dialogue('client').get('swf')}; "
                              f"its last dialogue lines: {asked})") from None
        ctx.wait_for(f"[client] copy {copy:08X} voices it", lambda: ctx.line_playing("client", copy), 5)
        def followed():
            host, client = ctx.dialogue(""), ctx.dialogue("client")
            if not host.get("speaker"):
                return not host.get('menu') and not client.get('menu') and not client.get('speaker')
            def ready(state):
                swf = state.get('swf') or {}
                return swf.get('state') == 1 and swf.get('allow_progress') == 1
            return (client.get('menu') and ready(client) and (not host.get('menu') or ready(host))
                    and bool(host.get('options')) and client.get('options') == host.get('options'))
        ctx.wait_for("[both] the conversation follows the pick (ended for both, or the same next list)", followed, 30)
        ctx.hold()
    finally:
        ctx.close_dialogues()
        ctx.remove_host_npc(npc)

def scenario_dialogue_npc_removed_mid_conversation(ctx):
    """The client talks to its copy of the host's NPC, and the host's game removes the NPC mid-conversation (a quest
    disabling it, the console's disable and markfordelete): the client's copy goes without crashing its game, its
    menu closes and both games end the conversation."""
    npc, _, copy = ctx.spawn_host_npc(LYDIA, restrained=True)
    try:
        ctx.place_before("", npc, 800, 90)
        ctx.place_before("client", copy, 110)
        ctx.activate_on("client", copy)
        ctx.wait_for(f"[client] the menu opens on copy {copy:08X}", lambda: ctx.talking_to("client", copy), 10)
        ctx.wait_for(f"[host] the player talks to {npc:08X} in the background", lambda: ctx.in_conversation("", npc), 10)
        ctx.console_on("", f"{npc:08X}.disable")
        ctx.console_on("", f"{npc:08X}.markfordelete")
        ctx.wait_on("client", f"copy {copy:08X} removed", lambda actors: str(copy) not in actors)
        ctx.wait_for("[client] its menu closes", lambda: ctx.menu_closed("client"), 10)
        ctx.wait_for("[both] the conversation ends", lambda: not ctx.dialogue("").get("speaker") and not ctx.dialogue("client").get("speaker"), 10)
        ctx.hold()
    finally:
        ctx.close_dialogues()
