"""The dialogue soak (docs/design/dialogue-stress.md, 2): what only Skyrim shows of the walker's actions. Both players
talk to Andurs and the host picks his four-part answer; then the world or a player interrupts it."""
# hand-written: the walker found and the duo measured each case first (re/systems/dialogue.md, "The speaker dies or fights")

from ..common import CLAW_FOUND_LINES, CLAW_TOPIC, GOLDEN_CLAW, GOLDEN_CLAW_QUEST, LUCAN, LUCAN_TROUBLE_TOPIC

ANDURS = "00013BA8"
LONG_ANSWER = 2  # "I'm not sure. Tell me about him.": four responses


def both_talk_and_host_picks(ctx):
    """Andurs staged between the players, both in his conversation, the host's pick of his long answer said."""
    ctx.stage({"npcs": [{"name": "andurs", "base": ANDURS, "frozen": False}],
               "players": {"host": {"near": "andurs", "gap": 110, "bearing": 90},
                           "guest": {"near": "andurs", "gap": 110, "bearing": 270}}})
    ctx.talk("", "andurs", [])
    ctx.talk("client", "andurs", [])
    ctx.dialogue_highlight(instance="", index=LONG_ANSWER)
    ctx.wait_takes_pick(instance="")
    ctx.dialogue_press(instance="")
    ctx.wait_for("[host] Andurs answers", lambda: ctx.dialogue("").get("current_info"), 5)


def conversation_ends_everywhere(ctx, why):
    for instance in ("", "client"):
        ctx.wait_for(f"[{instance or 'host'}] {why}: its menu closes", lambda: ctx.menu_closed(instance), 5)
    ctx.wait_for(f"[host] {why}: its conversation ends", lambda: not ctx.dialogue("").get("speaker"), 5)


def speaker_killed(ctx):
    """Andurs is killed mid-answer: the conversation ends on both games (no menu left on a corpse)."""
    try:
        both_talk_and_host_picks(ctx)
        ctx.console_on("", f"{ctx.resolve('andurs', 'host'):X}.kill")
        conversation_ends_everywhere(ctx, "the speaker died")
    finally:
        ctx.unstage()


def speaker_fights(ctx):
    """Andurs starts a fight mid-answer: the conversation ends on both games."""
    try:
        both_talk_and_host_picks(ctx)
        ctx.console_on("", f"{ctx.resolve('andurs', 'host'):X}.startcombat {ctx.resolve('guest_player', 'host'):X}")
        conversation_ends_everywhere(ctx, "the speaker fights")
    finally:
        ctx.unstage()


def guest_rejoins_quickly(ctx, times=5):
    """The guest leaves the menu and talks again `times` times mid-answer: every line the host's engine voices reaches
    its copy as often (the `Voice:` log, by actor and file) and the last one out ends it; the menus' agreement is the runner's
    dialogue check after every scenario."""
    try:
        both_talk_and_host_picks(ctx)
        ctx.voice_log_mark()
        for _ in range(times):
            ctx.leave_dialogue("client")
            ctx.talk("client", "andurs", [])
        ctx.wait_takes_pick(instance="", seconds=60)  # his answer over: the next list
        ctx.expect_same_voice_starts("andurs")
        ctx.close_dialogues()
    finally:
        ctx.unstage()


def lucan_claw_under_stress(ctx):
    """Both players in Lucan's claw conversation (MS13 at 60), the guest carrying the claw: it asks about the theft
    and, during his long answer, leaves and talks again twice; then it hands the claw in. Every line the host's engine
    voices reaches its copy once, the claw leaves the guest's bag alone, and MS13's stage reads the same on both games."""
    started = not ctx.quest_running("", GOLDEN_CLAW_QUEST)
    copy = ctx.stage_host_npc(LUCAN)
    ctx.console_on("", f"{LUCAN:08X}.setrestrained 1")  # his AI runs (a frozen speaker never ends a line), kept home
    ctx.freeze("", LUCAN, False)
    try:
        if started:
            ctx.console_on("", "resetquest MS13")
            ctx.console_on("", "startquest MS13")
            ctx.wait_for("[host] MS13 runs", lambda: ctx.quest_running("", GOLDEN_CLAW_QUEST), 10)
        ctx.console_on("", "setstage MS13 60")
        guest_copy = ctx.player_copy("")
        ctx.console_on("client", f"player.additem {GOLDEN_CLAW:X} 1")
        ctx.wait_for("[host] the guest's character carries the claw", lambda: ctx.item_count("", guest_copy, GOLDEN_CLAW) == 1, 10)
        ctx.place_before("", LUCAN, 110)
        ctx.place_before("client", copy, 110)
        ctx.activate_on("client", copy)
        ctx.wait_for(f"[client] the menu opens on copy {copy:08X}", lambda: ctx.talking_to("client", copy), 10)
        ctx.activate_on("", LUCAN)
        ctx.wait_for("[host] joins: its menu opens on Lucan", lambda: ctx.talking_to("", LUCAN), 10)
        ctx.wait_for("[client] its menu offers Lucan's trouble", lambda: ctx.lists_topic("client", LUCAN_TROUBLE_TOPIC), 30)
        ctx.voice_log_mark()
        ctx.pick_with_keys("client", LUCAN_TROUBLE_TOPIC)
        ctx.wait_for("[host] Lucan answers", lambda: ctx.dialogue("").get("current_info"), 10)
        for _ in range(2):
            ctx.leave_dialogue("client")
            ctx.activate_on("client", copy)
            ctx.wait_for(f"[client] its menu opens on copy {copy:08X} again", lambda: ctx.talking_to("client", copy), 10)
        ctx.wait_for("[client] its choices offer its claw line", lambda: set(ctx.topic_infos("client", CLAW_TOPIC)) & set(CLAW_FOUND_LINES), 90)
        ctx.name_handle("lucan", {"host": LUCAN, "guest": copy})
        ctx.expect_same_voice_starts("lucan")
        found = next(i for i in ctx.topic_infos("client", CLAW_TOPIC) if i in CLAW_FOUND_LINES)
        ctx.pick_with_keys("client", CLAW_TOPIC, found)
        ctx.wait_for("[client] the claw leaves its bag", lambda: not ctx.item_count("client", 0x14, GOLDEN_CLAW), 20)
        if ctx.item_count("", 0x14, GOLDEN_CLAW):
            raise ctx.failure("[host] its player was given the claw")
        def handed_in():
            host, guest = ctx.quest_stage("", GOLDEN_CLAW_QUEST), ctx.quest_stage("client", GOLDEN_CLAW_QUEST)
            return host is not None and host == guest and host > 60
        ctx.wait_for("MS13 moves past 60, and reads the same on both games", handed_in, 20)
    finally:
        ctx.close_dialogues()
        ctx.console_on("", f"{LUCAN:08X}.setrestrained 0")
        ctx.release_host_npc(LUCAN)
        for name in ("", "client"):
            if ctx.item_count(name, 0x14, GOLDEN_CLAW):
                ctx.console_on(name, f"player.removeitem {GOLDEN_CLAW:X} 1")
        if started:  # only the quest this scenario started
            ctx.console_on("", "stopquest MS13")


def guest_talks_again_after_leaving_mid_answer(ctx):
    """The guest alone in Andurs's conversation picks his long answer, leaves mid-answer (its leaving ends the
    conversation; he finishes his line) and talks to him again at once: its menu must not replay his answer over its
    copy's (the opening's replay is for the greeting, which a start during his line does not say)."""
    try:
        ctx.stage({"npcs": [{"name": "andurs", "base": ANDURS, "frozen": False}],
                   "players": {"host": {"near": "andurs", "gap": 300, "bearing": 90},
                               "guest": {"near": "andurs", "gap": 110, "bearing": 270}}})
        ctx.talk("client", "andurs", [])
        ctx.dialogue_highlight(instance="client", index=LONG_ANSWER)
        ctx.wait_takes_pick(instance="client")
        ctx.dialogue_press(instance="client")
        ctx.wait_for("[host] Andurs answers", lambda: ctx.dialogue("").get("current_info"), 5)
        ctx.voice_log_mark()
        ctx.leave_dialogue("client")
        ctx.talk("client", "andurs", [])
        ctx.wait_takes_pick(instance="client", seconds=60)  # his answer over, the new conversation's list
        ctx.expect_same_voice_starts("andurs")
        ctx.close_dialogues()
    finally:
        ctx.unstage()
