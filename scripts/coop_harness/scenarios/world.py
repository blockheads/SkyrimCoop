"""Scenarios world for the co-op harness."""
from pathlib import Path
import json
import re
import time
import zipfile
from ..comparisons import is_close
from ..common import (
    CLEAR_WEATHER,
    ESCAPE_KEY,
    HOME_CELL,
    INN,
    OTHER_WEATHER,
    OUTDOOR_CELL,
)
from ..errors import TestFailure
from ..comparisons import near


def scenario_host_time_reaches_client(ctx):
    """The host sets the hour: it stays set on the host (the host's clock is the session's) and the
    client's clock follows."""
    hour = (ctx.game_state("").get("time", {}).get("hour", 0) + 6) % 24
    ctx.console_on("", f"set gamehour to {hour:.2f}")
    def near(value, target, tolerance=0.2):
        return value is not None and min(abs(value - target), 24 - abs(value - target)) < tolerance
    ctx.wait_for("[client] the client's clock follows the host's",
             lambda: near(ctx.game_state("client").get("time", {}).get("hour"), hour), 10)
    # And it holds on both: nothing else drives the host's clock back.
    end = time.monotonic() + 2.0
    while time.monotonic() < end:
        for name in ("", "client"):
            if not near(ctx.game_state(name).get("time", {}).get("hour"), hour, 0.3):
                raise ctx.failure(f"[{name or 'host'}] the hour moved off {hour:.2f}: something else drives the clock")
        time.sleep(0.2)

def scenario_host_wait_reaches_guest(ctx):
    """The host waits 3 hours across midnight in the Wait menu, as a player does: the client's calendar takes
    the jump, days passed (GameDaysPassed and the whole days the engine counts on from) and the date."""
    ctx.console_on("", "set gamehour to 22.5")
    ctx.wait_for("[client] at the host's 22:30", lambda: is_close(ctx.calendar("client").get("hour"), 22.5, 0.2), 10)
    before = ctx.calendar("")
    ctx.wait_hours("", 3)
    after = ctx.calendar("")
    if after.get("raw_days") != before.get("raw_days") + 1:
        raise ctx.failure(f"[host] 3 hours from 22:30 passed {after.get('raw_days') - before.get('raw_days')} midnights")
    try:
        ctx.wait_for("[client] days passed and the date follow the host's", ctx.same_calendar, 10)
    except TestFailure:
        raise ctx.failure(ctx.calendars("[client] days passed and the date are not the host's"))

def scenario_shared_wait_host(ctx):
    """The host waits across midnight; both menus, calendars and native health recovery agree."""
    ctx.shared_wait_roundtrip("")

def scenario_shared_wait_guest(ctx):
    """The guest asks for a wait; the host drives both menus and calendars across midnight."""
    ctx.shared_wait_roundtrip("client")

def scenario_shared_sleep_host(ctx):
    """The host sleeps in the inn; the guest waits, both calendars jump and the sleeper recovers health."""
    ctx.shared_wait_roundtrip("", sleeping=True)

def scenario_shared_sleep_guest(ctx):
    """The guest sleeps in the inn; the host represents the party's sleep and drives the countdown."""
    ctx.shared_wait_roundtrip("client", sleeping=True)

def scenario_guest_sleep_fires_host_script(ctx):
    """A guest wait grants no host rested bonus; a guest sleep runs the host's vanilla PlayerSleepQuest."""
    quest, script = 0xFC1A2, "PlayerSleepQuestScript"
    if not ctx.quest_running("", quest):
        raise ctx.failure("[host] PlayerSleepQuest must be running for the native sleep-listener fixture")
    housekeeping = ctx.script_var("", quest, script, "CHScript")
    if ctx.script_var("", housekeeping, "CompanionsHousekeepingScript", "PlayerHasBeastBlood"):
        raise ctx.failure("[host] rested-bonus fixture requires a player without beast blood")
    lover = ctx.script_var("", quest, script, "pDoomLoverAbility")
    before = set(ctx.player_record("")["spells"])
    if f"0:{lover:X}" in before:
        raise ctx.failure("[host] rested-bonus fixture requires a player without the Lover Stone ability")
    bonuses = [ctx.script_var("", quest, script, name) for name in ("Rested", "WellRested", "MarriageSleepAbility")]
    if not all(bonuses):
        raise ctx.failure("[host] PlayerSleepQuest has an empty rested-spell property")
    bonus_keys = {f"0:{spell:X}" for spell in bonuses}
    ctx.both_go(INN)
    bed_owner = ctx.player_command("", {"cmd": "form", "form": 0x5EDA7}).get("owner")
    try:
        ctx.console_on("", "0005EDA7.setownership 00000007")
        ctx.wait_for("both games allow the guest to use the bed",
                 lambda: all(ctx.player_command(name, {"cmd": "form", "form": 0x5EDA7}).get("owner") == 7
                             for name in ("", "client")), 5)
        for spell in bonuses:
            if f"0:{spell:X}" in before:
                ctx.console_on("", f"player.removespell {spell:08X}")
        ctx.wait_for("[host] has no previous rested bonus", lambda: not bonus_keys.intersection(ctx.player_record("")["spells"]), 5)

        ctx.begin_shared_wait("client", 1)
        ctx.wait_for("the guest's control wait completes", lambda: all(ctx.takes_input(name) for name in ("", "client")), 15)
        if bonus_keys.intersection(ctx.player_record("")["spells"]):
            raise ctx.failure("[host] a guest wait incorrectly ran the sleep-stop bonus handler")

        ctx.begin_shared_wait("client", 1, sleeping=True)
        ctx.wait_for("the guest's sleep completes", lambda: all(ctx.takes_input(name) for name in ("", "client")), 15)
        # These are the quest's scripted abilities, not native health regeneration or the bed's furniture spell.
        ctx.wait_for("[host] its vanilla sleep-stop script grants a new rested bonus",
                 lambda: bool(bonus_keys.intersection(ctx.player_record("")["spells"])), 10)
        ctx.wait_for("[host] PlayerSleepQuest finishes its event", lambda: not ctx.script_running("", script), 5)
    finally:
        for name in ("", "client"):
            if ctx.shared_wait_menu(name):
                ctx.tap_key(name, ESCAPE_KEY)
        ctx.wait_for("both shared rest menus release", lambda: all(ctx.takes_input(name) for name in ("", "client")), 10)
        current = set(ctx.player_record("")["spells"])
        for spell in bonuses:
            key = f"0:{spell:X}"
            if (key in current) != (key in before):
                action = "addspell" if key in before else "removespell"
                ctx.console_on("", f"player.{action} {spell:08X}")
        ctx.wait_for("[host] its original rested abilities are restored",
                 lambda: bonus_keys.intersection(ctx.player_record("")["spells"]) == bonus_keys.intersection(before), 5)
        if bed_owner is not None:
            ctx.console_on("", f"0005EDA7.setownership {bed_owner:08X}")
            ctx.wait_for("both games restore the bed's owner",
                     lambda: all(ctx.player_command(name, {"cmd": "form", "form": 0x5EDA7}).get("owner") == bed_owner
                                 for name in ("", "client")), 5)
        ctx.both_go(HOME_CELL)

def scenario_shared_wait_cancel(ctx):
    """The guest cancels a host wait; both native menus close before all the requested hours pass."""
    before = ctx.calendar("").get("days_passed")
    ctx.begin_shared_wait("", 24)
    ctx.tap_key("client", ESCAPE_KEY)
    ctx.wait_for("a guest's cancel releases both menus", lambda: all(ctx.takes_input(name) for name in ("", "client")), 10)
    if ctx.calendar("").get("days_passed") - before >= 0.5:
        raise ctx.failure("the party completed half a day despite the guest's cancellation")
    ctx.wait_for("the calendars agree after cancelling", ctx.same_calendar, 5)

def scenario_guest_sets_time(ctx):
    """At 22:30 the client types `/settime 6 0` in the chat: the host's clock runs on to 06:00 the next day, as
    a wait does, and both games show it, days passed and date included. The command is no chat line."""
    ctx.console_on("", "set gamehour to 22.5")
    ctx.wait_for("[client] at the host's 22:30", lambda: is_close(ctx.calendar("client").get("hour"), 22.5, 0.2), 10)
    before = ctx.calendar("")
    ctx.say("client", "/settime 6 0")
    ctx.wait_for("[host] its clock is at 06:00", lambda: is_close(ctx.calendar("").get("hour"), 6.0, 0.1), 5)
    if ctx.calendar("").get("raw_days") != before.get("raw_days") + 1:
        raise ctx.failure(f"[host] 22:30 to 06:00 passed {ctx.calendar('').get('raw_days') - before.get('raw_days')} midnights")
    try:
        ctx.wait_for("[client] at the host's 06:00, days passed and date", lambda: ctx.same_calendar()
                 and is_close(ctx.calendar("client").get("hour"), ctx.calendar("").get("hour"), 0.05), 10)
    except TestFailure:
        raise ctx.failure(ctx.calendars("[client] not on the host's calendar after the set time"))
    for name in ("", "client"):
        if any("settime" in l["text"] for l in ctx.chat(name).get("lines", [])):
            raise ctx.failure(f"[{name or 'host'}] shows the command as a chat line")

def scenario_bug_report_writes_every_game(ctx):
    """The client types `/bug duo check`: both games say where they wrote their part of one report, each part has
    its note, log, messages and snapshot, the host's the name of its save, kept once in reports/saves; the host's
    /export zips them."""
    _game = ctx.game()
    ctx.say("client", "/bug duo check")
    parts = {}
    for name in ("", "client"):
        ctx.wait_for(f"[{name or 'host'}] says its part is saved", lambda: ctx.system_line(name, "Bug report "), 10)
        line = ctx.system_line(name, "Bug report ")
        report_id, folder = re.match(r"Bug report (\S+) saved: (.*)$", line).groups()
        parts[name] = (report_id, Path(folder))
    if parts[""][0] != parts["client"][0]:
        raise ctx.failure(f"two report ids: {parts[''][0]} and {parts['client'][0]}")
    for name, (_, folder) in parts.items():
        wanted = ["note.txt", "log.txt", "messages.txt", "snapshot.json"] + (["save.txt"] if name == "" else [])
        ctx.wait_for(f"[{name or 'host'}] its part has {', '.join(wanted)}", lambda: all((folder / f).exists() for f in wanted), 15)
        if "duo check" not in (folder / "note.txt").read_text():
            raise ctx.failure(f"[{name or 'host'}] note.txt lacks the text")
        if not (folder / "log.txt").read_text().strip():
            raise ctx.failure(f"[{name or 'host'}] log.txt is empty")
        if not json.loads((folder / "snapshot.json").read_text()).get("world"):
            raise ctx.failure(f"[{name or 'host'}] snapshot.json has no actors")
    save = parts[""][1].parent.parent / (parts[""][1] / "save.txt").read_text().strip()
    if not save.exists():
        raise ctx.failure(f"[host] its report names {save}, which is not kept")
    if (parts["client"][1] / "save.txt").exists():
        raise ctx.failure("[client] a guest's part names a save")
    ctx.say("", "/export")
    ctx.wait_for("[host] says where its zip is", lambda: ctx.system_line("", "Saved "), 30)
    zip_path = Path(re.match(r"Saved (.*) \(path copied\)$", ctx.system_line("", "Saved ")).group(1))
    with zipfile.ZipFile(zip_path) as archive:
        names = archive.namelist()
    if not any(n.endswith(f"{parts[''][0]}/host-" + parts[""][1].name.split("-", 1)[1] + "/note.txt") for n in names):
        raise ctx.failure(f"the host's zip lacks its part: {names[:20]}")
    if f"reports/saves/{save.name}" not in names:
        raise ctx.failure(f"the host's zip lacks the report's save {save.name}")

def scenario_late_join_gets_calendar(ctx):
    """The host waits a day: leaving, the client's own clock counts on from the host's days passed, not those of
    the save it loaded. The host waits again while the client is away: it joins with the host's days passed and date."""
    ctx.wait_hours("", 24)
    try:
        ctx.wait_for("[client] days passed and the date follow the host's", ctx.same_calendar, 10)
    except TestFailure:
        raise ctx.failure(ctx.calendars("[client] not on the host's calendar after its wait"))
    joined = ctx.calendar("client").get("days_passed")
    ctx.disconnect_client()
    ctx.wait_hours("", 12)
    left = ctx.calendar("client").get("days_passed")  # its own clock ran on for the host's wait
    if abs(left - joined) > 0.05:
        raise ctx.failure(f"[client] days passed went from {joined} to {left} after leaving")
    if ctx.same_calendar():
        raise ctx.failure("[client] has the host's days passed while away: the test shows nothing")
    ctx.rejoin_client()
    try:
        ctx.wait_for("[client] joins with the host's days passed and date", ctx.same_calendar, 10)
    except TestFailure:
        raise ctx.failure(ctx.calendars("[client] joined off the host's calendar"))

def scenario_weather_follows_host(ctx):
    """Both players outdoors; the host's weather changes: the client's game takes it (the host's
    weather is the session's), and keeps it while its own weather system would move on."""
    try:
        ctx.both_go(OUTDOOR_CELL)
        for name in ("", "client"):
            ctx.wait_for(f"[{name or 'host'}] outdoors, under a sky", lambda name=name: ctx.game_state(name).get("weather"), 30)
        weather = OTHER_WEATHER if ctx.game_state("").get("weather") == CLEAR_WEATHER else CLEAR_WEATHER
        ctx.console_on("", f"fw {weather:x}")
        ctx.wait_for("[host] shows the forced weather", lambda: ctx.game_state("").get("weather") == weather, 10)
        ctx.wait_for("[client] follows the host's weather", lambda: ctx.game_state("client").get("weather") == weather, 10)
        end = time.monotonic() + 3.0  # long enough for the client's own weather system to move on
        while time.monotonic() < end:
            if (held := ctx.game_state("client").get("weather")) != weather:
                raise ctx.failure(f"the client lost the host's weather: {held:X}, host's {weather:X}")
            time.sleep(0.2)
    finally:
        ctx.both_go(HOME_CELL)

def scenario_clock_slows_for_everyone(ctx):
    """A game slowing its clock slows every game's (a kill cam, Slow Time, a bow zoom; here the console's
    `sgtm`, the same engine call), and back to normal, every game is."""
    try:
        for slower in ("client", ""):
            ctx.console_on(slower, "sgtm 0.5")
            ctx.clocks_run_at(0.5, f"the {slower or 'host'}'s slowed")
            ctx.console_on(slower, "sgtm 1")
            ctx.clocks_run_at(1.0, f"the {slower or 'host'}'s back")
        # Two at once: the slowest holds both until it ends.
        ctx.console_on("", "sgtm 0.5")
        ctx.console_on("client", "sgtm 0.25")
        ctx.clocks_run_at(0.25, "the client's slower than the host's")
        ctx.console_on("client", "sgtm 1")
        ctx.clocks_run_at(0.5, "the host's still slowed")
        ctx.console_on("", "sgtm 1")
        ctx.clocks_run_at(1.0, "both back")
    finally:
        for name in ("", "client"):
            ctx.console_on(name, "sgtm 1")
