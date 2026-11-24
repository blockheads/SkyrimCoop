"""The dialogue soak (docs/design/dialogue-stress.md, 2): what only Skyrim shows of the walker's actions. Both players
talk to Andurs and the host picks his four-part answer; then the world or a player interrupts it."""
# hand-written: the walker found and the duo measured each case first (re/systems/dialogue.md, "The speaker dies or fights")

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
    its copy once (the voice counts, speak_calls) and the last one out ends it; the menus' agreement is the runner's
    dialogue check after every scenario."""
    try:
        both_talk_and_host_picks(ctx)
        host_before, guest_before = ctx.speak_calls(""), ctx.speak_calls("client")
        for _ in range(times):
            ctx.leave_dialogue("client")
            ctx.talk("client", "andurs", [])
        ctx.wait_takes_pick(instance="", seconds=60)  # his answer over: the next list
        host_said = ctx.speak_calls("")["engine"] - host_before["engine"]
        copy_said = ctx.speak_calls("client")["relayed"] - guest_before["relayed"]
        if copy_said != host_said:
            raise ctx.failure(f"the host's engine voiced {host_said} line(s), the guest's copy said {copy_said}")
        ctx.close_dialogues()
    finally:
        ctx.unstage()
