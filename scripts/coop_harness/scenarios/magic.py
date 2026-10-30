"""Scenarios magic for the co-op harness."""
from ..common import (
    BEAST_FORM,
    CHARITY_SHADER,
    COMPANIONS_HOUSEKEEPING,
    DOOR_CLOSED,
    DOOR_OPEN,
    FLAMES,
    GIFT_OF_CHARITY,
    HOME_CELL,
    INN,
    LYDIA,
    OAKFLESH,
    PLAYER_WEREWOLF_FACTION,
    PLAYER_WEREWOLF_QUEST,
    SPELL_GAP,
    VILKAS,
    WEREWOLF_BEAST_RACE,
    WEREWOLF_CHANGE_EFFECT,
    WEREWOLF_TRANSFORM_VISUAL,
)
from ..errors import TestFailure
from ..utilities import log


def scenario_burning_shows_on_copy(ctx):
    """Client's Flames set the host's NPC burning: the host's game applies the effect, and the
    client's copy shows the same fire (its owner's active effects, played as visuals only). When
    the burning ends there, it ends on the copy too."""
    npc, _, copy = ctx.spawn_host_npc(LYDIA, restrained=True)
    ctx.give_spell("client", FLAMES)
    ctx.place_before("client", copy, SPELL_GAP)
    ctx.draw("client")
    try:
        ctx.cast_spell("client", copy, FLAMES)
        burning = ctx.wait_for("[host] the NPC burns", lambda: ctx.visuals("", npc), 3)
        log(f"host's NPC burning: {', '.join(f'{v:X}' for v in sorted(burning))}")
        ctx.wait_for("[client] its copy shows the same fire", lambda: burning <= ctx.visuals("client", copy), 3)
    finally:
        ctx.stop_casting("client")
    ctx.wait_for("[host] the burning ends", lambda: not (burning & ctx.visuals("", npc)), 10)
    ctx.wait_for("[client] the copy's fire ends too", lambda: not (burning & ctx.visuals("client", copy)), 3)
    ctx.hold()

def scenario_copy_made_later_shows_effects(ctx):
    """The host's staged Vilkas casts Oakflesh on himself while the client is away. The client joins again: the
    copy it makes then shows the visuals from its spawn, nothing having changed on the host since (the owner sends
    its effects only when the engine starts or ends one). A dispel ends them on both."""
    try:
        ctx.stage_host_npc(VILKAS)
        session_id = ctx.host_npc_state(VILKAS)["session_id"]
        ctx.disconnect_client()
        ctx.wait_on("", "the client's character leaves the host's game", lambda actors: ctx.other_player(actors) is None)
        ctx.console_on("", f"{VILKAS:X}.cast {OAKFLESH:08X} {VILKAS:X} left")
        shown = ctx.wait_for("[host] Vilkas shows Oakflesh", lambda: ctx.visuals("", VILKAS), 3)
        ctx.rejoin_client()
        copy = ctx.copy_of("client", session_id)
        if OAKFLESH not in ctx.active_spells("", VILKAS):
            raise ctx.failure(f"[host] Vilkas's Oakflesh ended before the client came back: {ctx.active_spells('', VILKAS)}")
        try:
            ctx.wait_for("[client] the copy made on joining shows it", lambda: shown <= ctx.visuals("client", copy), 3)
        except TestFailure:
            raise ctx.failure(f"[client] its copy of Vilkas shows {ctx.visuals('client', copy)}, not the host's {shown}")
        ctx.console_on("", f"{VILKAS:X}.dispelallspells")
        ctx.wait_for("[host] Oakflesh is dispelled", lambda: not (shown & ctx.visuals("", VILKAS)), 3)
        ctx.wait_for("[client] the copy's visuals end too", lambda: not (shown & ctx.visuals("client", copy)), 3)
    finally:
        ctx.console_on("", f"{VILKAS:X}.dispelallspells")
        ctx.release_host_npc(VILKAS)

def scenario_copy_flashes_one_shot_effects(ctx):
    """An effect whose visuals play once (no FX Persist) flashes on the copy as on its owner: the host's staged
    Vilkas takes Gift of Charity (a beggar's blessing, an hour long). Its shader shows on the host's Vilkas and on
    the client's copy, then ends on both by itself while the blessing still runs (the owner sends the flash when its
    engine starts it)."""
    try:
        ctx.stage_host_npc(VILKAS)
        copy = ctx.copy_of("client", ctx.host_npc_state(VILKAS)["session_id"])
        ctx.console_on("", f"{VILKAS:X}.cast {GIFT_OF_CHARITY:08X} {VILKAS:X} left")
        ctx.wait_for("[host] Vilkas flashes the blessing", lambda: CHARITY_SHADER in ctx.shown_visuals("", VILKAS), 3)
        ctx.wait_for("[client] the copy flashes it too", lambda: CHARITY_SHADER in ctx.shown_visuals("client", copy), 3)
        ctx.wait_for("[host] the flash ends", lambda: CHARITY_SHADER not in ctx.shown_visuals("", VILKAS), 15)
        ctx.wait_for("[client] the copy's flash ends by itself", lambda: CHARITY_SHADER not in ctx.shown_visuals("client", copy), 15)
        if GIFT_OF_CHARITY not in ctx.active_spells("", VILKAS):
            raise ctx.failure(f"[host] the blessing ended with its flash: {ctx.active_spells('', VILKAS)}")
    finally:
        ctx.console_on("", f"{VILKAS:X}.dispelallspells")
        ctx.release_host_npc(VILKAS)

def scenario_copy_flashes_instant_effects(ctx):
    """A shrine's instant FX Persist cure effect flashes on the existing copy, then fades on both games."""
    try:
        ctx.stage_host_npc(VILKAS)
        copy = ctx.copy_of("client", ctx.host_npc_state(VILKAS)["session_id"])
        ctx.console_on("", f"{VILKAS:X}.cast FB995 {VILKAS:X} left")
        ctx.wait_for("[host] Vilkas flashes the shrine's cure effect",
                 lambda: CHARITY_SHADER in ctx.shown_visuals("", VILKAS), 3)
        ctx.wait_for("[client] the existing copy flashes the cure effect too",
                 lambda: CHARITY_SHADER in ctx.shown_visuals("client", copy), 3)
        for instance, actor in (("", VILKAS), ("client", copy)):
            ctx.wait_for(f"[{instance or 'host'}] the instant flash ends by itself",
                     lambda: CHARITY_SHADER not in ctx.shown_visuals(instance, actor), 15)
    finally:
        ctx.console_on("", f"{VILKAS:X}.dispelallspells")
        ctx.release_host_npc(VILKAS)

def scenario_door_opens_for_both(ctx):
    """A player opens a door: the other game's door opens too (the bridge relays the activation and
    the other game's copy of the player opens it there). Then the host closes it for both."""
    door = ctx.inner_door()
    try:
        for name in ("", "client"):
            if ctx.door_state(name, door) != DOOR_CLOSED:
                raise ctx.failure(f"[{name or 'host'}] door {door:X} is not closed to begin with")
        ctx.activate_on("client", door)
        for name in ("client", ""):
            ctx.wait_for(f"[{name or 'host'}] door {door:X} is open", lambda name=name: ctx.door_state(name, door) == DOOR_OPEN, 10)
        ctx.activate_on("", door)
        for name in ("", "client"):
            ctx.wait_for(f"[{name or 'host'}] door {door:X} is closed again", lambda name=name: ctx.door_state(name, door) == DOOR_CLOSED, 10)
    finally:
        for name in ("", "client"):
            if ctx.door_state(name, door) == DOOR_OPEN:
                ctx.activate_on(name, door)

def scenario_client_beast_form_stays_client(ctx):
    """The client turns into a werewolf with Beast Form (the Shout key): its own character changes, the host
    shows it changed, and the host's own player does not (upstream's bug: the change effect's
    `Target == Game.GetPlayer()` passed for the client and the transformation went to the host's player).
    The change effect's script runs on the client's game only (the target's owner applies effects), so the
    host's PlayerWerewolfQuest stays stopped. Then the client changes back."""
    _game = ctx.game()
    host_race = ctx.race_of("", 0x14)
    if ctx.werewolf_running("client"):
        raise ctx.failure("[client] PlayerWerewolfQuest already runs: an earlier change never ended")
    # The Companions' setup marks the first change as the rampage's: the power would only clear the mark.
    if ctx.script_var("client", COMPANIONS_HOUSEKEEPING, "CompanionsHousekeepingScript", "PlayerIsWerewolfVirgin", 0):
        raise ctx.failure("[client] C00's PlayerIsWerewolfVirgin stays set")
    ctx.prepare_beast_form("client")
    ctx.console_on("client", f"player.addspell {BEAST_FORM:X}")
    ctx.console_on("client", f"player.equipspell {BEAST_FORM:X} voice")
    since = 0

    def seen(kind, **fields):
        return lambda: any(e["kind"] == kind and all(e.get(k) == v for k, v in fields.items())
                           for e in ctx.engine_events("client", since)[0])

    def werewolf_stage():
        _game = ctx.game('client')
        return (_game.rpc({"cmd": "quest", "quest": PLAYER_WEREWOLF_QUEST}) or {}).get("current_stage")

    try:
        ctx.wait_for("[client] Beast Form is the selected power", lambda: ctx.local_player("client").get("selected_power") == BEAST_FORM, 5, _game=_game)
        _, since = ctx.engine_events("client")
        ctx.tap("client", "shout")  # a power goes out on the release
        # The chain: the change effect starts PlayerWerewolfQuest (stage 0 preps the shift) and casts the
        # transform visual, which plays the change and sets stage 1 (SetRace) on its SetRace event or after 10s.
        ctx.wait_for("[client] the change effect starts on the client's character",
                 seen("magic_effect_apply", effect=WEREWOLF_CHANGE_EFFECT, target=0x14), 5, _game=_game)
        ctx.wait_for("[client] PlayerWerewolfQuest starts", seen("quest_start", quest=PLAYER_WEREWOLF_QUEST), 5, _game=_game)
        ctx.wait_for("[client] the transform visual starts", seen("magic_effect_apply", effect=WEREWOLF_TRANSFORM_VISUAL, target=0x14), 5, _game=_game)
        ctx.wait_for("[client] PlayerWerewolfQuest reaches the shift (stage 1)", lambda: (werewolf_stage() or 0) >= 1, 15, _game=_game)
        ctx.wait_for("[client] the client is a werewolf", lambda: ctx.race_of("client", 0x14) == WEREWOLF_BEAST_RACE, 10, _game=_game)
        ctx.wait_for("[host] shows the client as a werewolf", lambda: ctx.copy_race_on_host() == WEREWOLF_BEAST_RACE, 20, _game=_game)
        if ctx.race_of("", 0x14) != host_race:
            raise ctx.failure(f"[host] the host's player changed race ({ctx.race_of('', 0x14):X}): the client's change went to it")
        if ctx.werewolf_running(""):
            raise ctx.failure("[host] PlayerWerewolfQuest runs on the host: the client's change reached the host's scripts")
    except TestFailure:
        if since:
            log("[client] engine events since the press: " + "; ".join(
                " ".join(f"{k}={v:X}" if isinstance(v, int) and k != "seq" else f"{k}={v}" for k, v in e.items())
                for e in ctx.engine_events("client", since)[0]))
        raise
    finally:
        if ctx.werewolf_running("client"):
            # The transform visual's OnEffectStart ends after its Utility.Wait(10) with SetStage(1) when the
            # player is not a werewolf: changed back before that, the client would change again.
            ctx.wait_for("[client] the transform visual's script is done",
                     lambda: not ctx.script_running("client", "WerewolfTransformVisual"), 20, _game=_game)
            ctx.console_on("client", f"setstage {PLAYER_WEREWOLF_QUEST:X} 100")  # the quest's change back
            ctx.wait_for("[client] human again", lambda: ctx.race_of("client", 0x14) != WEREWOLF_BEAST_RACE, 30, _game=_game)
            ctx.wait_for("[client] PlayerWerewolfQuest shuts down", lambda: not ctx.werewolf_running("client"), 30, _game=_game)
            ctx.wait_for("[host] shows the client human again", lambda: ctx.copy_race_on_host() != WEREWOLF_BEAST_RACE, 30, _game=_game)
        ctx.console_on("client", f"player.removespell {BEAST_FORM:X}")

def scenario_host_beast_form_stays_host(ctx):
    """The host turns into a werewolf with Beast Form (the Shout key). Its body's quest (PlayerWerewolfQuest, started by
    the change effect on its player) adds it to the werewolf's factions as that quest's stage: a player's own moment,
    not the story (dragonborn.md, "a quest a player's body started"), so the client's player stays out of them."""
    if ctx.werewolf_running(""):
        raise ctx.failure("[host] PlayerWerewolfQuest already runs: an earlier change never ended")
    if ctx.script_var("", COMPANIONS_HOUSEKEEPING, "CompanionsHousekeepingScript", "PlayerIsWerewolfVirgin", 0):
        raise ctx.failure("[host] C00's PlayerIsWerewolfVirgin stays set")
    host_race = ctx.race_of("", 0x14)
    ctx.prepare_beast_form("")
    ctx.console_on("", f"player.addspell {BEAST_FORM:X}")
    ctx.console_on("", f"player.equipspell {BEAST_FORM:X} voice")
    try:
        ctx.wait_for("[host] Beast Form is the selected power", lambda: ctx.local_player("").get("selected_power") == BEAST_FORM, 5)
        ctx.tap("", "shout")
        ctx.wait_for("[host] the host is a werewolf", lambda: ctx.race_of("", 0x14) == WEREWOLF_BEAST_RACE, 30)
        ctx.wait_for("[client] shows the host as a werewolf",
                 lambda: ctx.race_of("client", ctx.other_player(ctx.actors_on("client"))) == WEREWOLF_BEAST_RACE, 20)
        ctx.wait_for("[host] its player is in the werewolf's faction",
                 lambda: ctx.faction_rank("", 0x14, PLAYER_WEREWOLF_FACTION) >= 0, 10)
        frame = ctx.game_frame("")
        ctx.wait_for("[client] two seconds for any share to arrive", lambda: ctx.game_frame("") > frame + 120, 10)
        if ctx.faction_rank("client", 0x14, PLAYER_WEREWOLF_FACTION) >= 0:
            raise ctx.failure("[client] its player joined the host's beast form's faction: the body's quest was the story")
    finally:
        if ctx.werewolf_running(""):
            ctx.wait_for("[host] the transform visual's script is done",
                     lambda: not ctx.script_running("", "WerewolfTransformVisual"), 20)
            ctx.console_on("", f"setstage {PLAYER_WEREWOLF_QUEST:X} 100")  # the quest's change back
            ctx.wait_for("[host] human again", lambda: ctx.race_of("", 0x14) != WEREWOLF_BEAST_RACE, 30)
            ctx.wait_for("[host] PlayerWerewolfQuest shuts down", lambda: not ctx.werewolf_running(""), 30)
            ctx.wait_for("[client] shows the host's original human race again",
                     lambda: ctx.race_of("client", ctx.other_player(ctx.actors_on("client"))) == host_race, 30)
            frames = {instance: ctx.game_frame(instance) for instance in ("", "client")}
            ctx.wait_for("both games keep updating after the race/graph transition",
                     lambda: all(ctx.game_frame(instance) > frame + 120 for instance, frame in frames.items()), 10)
        ctx.console_on("", f"player.removespell {BEAST_FORM:X}")
        ctx.console_on("client", f"player.removefromfaction {PLAYER_WEREWOLF_FACTION:X}")

def scenario_load_door_not_replayed(ctx):
    """In the inn, the client walks out through its door: the host's game gets the activation and
    does not replay it (the door would take the client's copy, or anyone, out of the host's cell);
    only the client's own game takes it through, and the host follows by the party tether."""
    try:
        ctx.both_go(INN)
        doors = [o for o in ctx.cell_objects("").values() if o["load_door"]]
        if not doors:
            raise ctx.failure(f"no load door in {INN}")
        door = doors[0]["id"]
        log_file = ctx.plugin_log("")
        start = log_file.stat().st_size
        ctx.activate_on("client", door)
        ctx.wait_for(f"[host] load door {door:X} not replayed",
                 lambda: f"Load door {door:X} not replayed" in log_file.read_bytes()[start:].decode(errors="replace"), 10)
        ctx.wait_for("[client] the client went out through the door", lambda: door not in ctx.cell_objects("client"), 30)
        ctx.wait_for("[host] follows the client by the party tether",
                 lambda: "Party travel: following" in log_file.read_bytes()[start:].decode(errors="replace"), 30)
        ctx.players_together("outside the inn")
    finally:
        ctx.both_go(HOME_CELL)
