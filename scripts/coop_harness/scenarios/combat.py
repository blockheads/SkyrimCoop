"""Scenarios combat for the co-op harness."""
from ..common import (
    BLOW_TOLERANCE,
    CAMERA_STATES,
    FIREBOLT,
    FLAMES,
    IRON_SWORD,
    KILL_MOVE_LAG,
    KILL_MOVE_RANDOM,
    LYDIA,
    MELEE_GAP,
    PAIR_TOLERANCE,
    SPELL_GAP,
    WEAPON_DRAWN,
    WOLF,
)
from ..errors import TestFailure
from ..comparisons import (
    POSITION_TOLERANCE,
    body_problems,
    body_report,
    distance,
)
from ..objects import (
    AnimTrace,
    GodMode,
    RagdollTrace,
    log,
    math,
    time,
)


def scenario_health_host_npc(ctx):
    """Host's NPC loses/regains health; the client's copy must match."""
    npc, _, copy = ctx.spawn_host_npc(LYDIA, restrained=True)
    ctx.health_roundtrip("", f"{npc:08X}", lambda: ctx.host_npc_health(npc), "client",
                     lambda actors: copy if str(copy) in actors else None, f"NPC copy {copy:08X}")

def scenario_death_spectate_host(ctx):
    """The host is downed by an enemy, spectates and respawns beside the guest after ten clear seconds."""
    ctx.death_spectate_respawn("")

def scenario_death_spectate_guest(ctx):
    """The guest is downed by an enemy, spectates and respawns beside the host after ten clear seconds."""
    ctx.death_spectate_respawn("client")

def scenario_party_travel_dead_guest(ctx):
    """A killed guest follows the living leader through a load and retains its death and spectator state."""
    ctx.death_spectate_respawn("client", travel_down=True)

def scenario_death_wipe_checkpoint(ctx):
    """Enemy blows down both players; both games load the previous host save and keep their saved characters."""
    _game = ctx.game()
    names = {instance: ctx.player_record(instance)["name"] for instance in ("", "client")}
    for instance in ("", "client"):
        ctx.console_on(instance, "player.restoreav health 10000")
    ctx.console_on("", "set gamehour to 12")
    checkpoint = ctx.save_game("", f"DeathSpectateCheckpoint_{int(time.time())}")
    ctx.wait_for("the checkpoint and co-save are written", ctx.saves_written(checkpoint), 15)
    ctx.console_on("", "set gamehour to 17")
    npc, _, copy = ctx.spawn_host_npc(WOLF)
    for instance in ("", "client"):
        health = (ctx.player_snapshot(instance).get("values") or {}).get("health", 100)
        if health > 1:
            ctx.console_on(instance, f"player.damageav health {health - 1:.6f}")
        ctx.place_before(instance, npc if instance == "" else copy, MELEE_GAP)
    ctx.console_on("", f"{npc:X}.startcombat player")
    ctx.wait_for("the host is downed by the enemy", lambda: ctx.death_state("").get("down"), 12)
    ctx.console_on("", f"{npc:X}.startcombat {ctx.client_on_host():X}")
    def restored_party():
        return all(ctx.game_state(instance).get("online") and not ctx.death_state(instance).get("down")
                   for instance in ("", "client")) and abs(ctx.game_state("").get("time", {}).get("hour", 24) - 12) < 0.3
    ctx.wait_for("a wipe reloads the checkpoint on both games", restored_party, 120)
    ctx.wait_for("the restored party shows both players", ctx.both_see_each_other, 30)
    for instance in ("", "client"):
        if ctx.player_record(instance)["name"] != names[instance]:
            raise ctx.failure(f"[{instance or 'host'}] the wipe restored another player's character")
    if str(npc) in ctx.local_player("").get("local_actors", {}):
        raise ctx.failure("the wolf created after the checkpoint survived the world rollback")
    rested = [ctx.comes_to_rest(lambda instance=instance: ctx.my_position(instance), still=1)
              for instance in ("", "client")]
    start = {instance: ctx.my_position(instance) for instance in ("", "client")}
    try:
        ctx.wait_for("the restored players finish landing before cleanup", lambda: all([test() for test in rested]), 10)
    except TestFailure as failure:
        moved = {instance or "host": (round(distance(start[instance], ctx.my_position(instance))),
                                      (ctx.player_snapshot(instance).get("animation") or {}).get("event"))
                 for instance in ("", "client")}
        at = {instance or "host": [round(x) for x in ctx.my_position(instance)] for instance in ("", "client")}
        raise ctx.failure(f"{failure} (moved in 10 s, latest action: {moved}; now at {at}, "
                          f"{distance(ctx.my_position(''), ctx.my_position('client')):.0f} units apart)") from None

def scenario_death_host_npc(ctx):
    """The host resurrects an NPC; its existing copy draws and swings on a running graph afterward."""
    npc, _, copy = ctx.spawn_host_npc("00039CF4", restrained=True)
    try:
        ctx.console_on("", f"{npc:X}.additem {IRON_SWORD:X} 1")
        ctx.console_on("", f"{npc:X}.equipitem {IRON_SWORD:X} 1")
        for command, dead in ((f"{npc:X}.kill", True), (f"{npc:X}.resurrect", False)):
            ctx.console_on("", command)
            ctx.wait_for(f"[host] NPC {'dies' if dead else 'is alive again'}",
                     lambda: ctx.host_npc_state(npc).get("dead") == dead, 10)
            ctx.wait_on("client", f"NPC copy {copy:X} {'dies' if dead else 'is alive again'}",
                    lambda actors: actors.get(str(copy), {}).get("dead") == dead)
        ctx.console_on("", f"{npc:X}.setrestrained 1")
        ctx.console_on("", f"{npc:X}.removeallitems")
        ctx.console_on("", f"{npc:X}.additem {IRON_SWORD:X} 1")
        ctx.console_on("", f"{npc:X}.equipitem {IRON_SWORD:X} 1")
        ctx.wait_on("client", "the revived copy holds the sword",
                lambda actors: IRON_SWORD in actors.get(str(copy), {}).get("equipped", []))
        ctx.place_before("", npc, MELEE_GAP)
        with AnimTrace(ctx, "resurrect-npc", {"": {npc: "npc"}, "client": {copy: "npc"}}, video=False) as trace:
            ctx.console_on("", f"{npc:X}.setrestrained 0")
            ctx.console_on("", f"{npc:X}.startcombat player")
            trace.wait("", "npc", "weaponSwing", 15)
            trace.wait("client", "npc", "weaponSwing", 10)
            trace.wait_state("client", "npc", "weapon", WEAPON_DRAWN, 5)
    finally:
        ctx.remove_host_npc(npc)

def scenario_friendly_fire_guest_sword(ctx):
    """The guest's sword on the host's player: the host's game applies it."""
    ctx.friendly_fire("client", "sword")

def scenario_friendly_fire_host_sword(ctx):
    """The host's sword on the guest's player: the guest's game applies it."""
    ctx.friendly_fire("", "sword")

def scenario_friendly_fire_guest_firebolt(ctx):
    """The guest's Firebolt on the host's player: the host's game applies it."""
    ctx.friendly_fire("client", "firebolt")

def scenario_friendly_fire_host_firebolt(ctx):
    """The host's Firebolt on the guest's player: the guest's game applies it."""
    ctx.friendly_fire("", "firebolt")

def scenario_fus_ro_dah_host_hits_guest(ctx):
    """The host's real three-word shout knocks the guest down on its own game and the host's copy."""
    ctx.fus_ro_dah_player("")

def scenario_fus_ro_dah_guest_hits_host(ctx):
    """The guest's real three-word shout knocks the host down on its own game and the guest's copy."""
    ctx.fus_ro_dah_player("client")

def scenario_friendly_fire_kill(ctx):
    """The host's sword kills the guest: a real death on the guest's game (its copy dies with it), the guest spectates,
    and both chats name the killer."""
    attacker, victim = "", "client"
    names = {instance: ctx.player_record(instance)["name"] for instance in ("", "client")}
    for instance in ("", "client"):
        ctx.console_on(instance, "player.restoreav health 10000")
    copy = ctx.other_player(ctx.actors_on(attacker))
    try:
        ctx.console_on(attacker, "set KillMoveRandom to 0")
        ctx.give_sword(attacker)
        ctx.give_sword(victim)
        health = ctx.player_health(victim) or 100
        if health > 3:
            ctx.console_on(victim, f"player.damageav health {health - 3:.6f}")
        ctx.wait_on(attacker, "the attacker's copy shows the victim nearly dead",
                lambda actors: (actors.get(str(copy), {}).get("health") or 100) <= 3.5, 5)
        ctx.place_before(attacker, copy, MELEE_GAP)
        ctx.draw(attacker)
        ctx.attack(attacker)
        ctx.wait_for("[client] the host's blow kills the guest", lambda: ctx.death_state(victim).get("down"), 5)
        if not ctx.is_dead(ctx.player_snapshot(victim)):
            raise ctx.failure("the downed guest is not dead (essential, or bleeding out)")
        ctx.camera_sees_followed(victim, "the death camera")
        ctx.wait_for("the host's copy of the guest dies with it", lambda: ctx.is_dead(ctx.actor_snapshot(attacker, copy)), 5)
        line = f"{names[victim]} was killed by {names[attacker]}."
        for instance in ("", "client"):
            ctx.wait_for(f"[{instance or 'host'}] the chat names the killer", lambda: line in ctx.system_chat(instance), 5)
        killer = (ctx.player_snapshot(victim).get("ai") or {}).get("killer")
        if killer != ctx.other_player(ctx.actors_on(victim)):
            raise ctx.failure(f"[client] the engine's killer is {killer or 0:08X}, not the host's copy")
        ctx.wait_for("[client] the dead guest spectates the host", lambda: ctx.death_state(victim).get("spectating"), 10)
        ctx.camera_sees_followed(victim, "spectating")
        ctx.spectating_stays_third_person(victim)
        ctx.sheathe(attacker)
        ctx.wait_for("[client] the guest respawns beside the host once the party is clear",
                 lambda: not ctx.death_state(victim).get("down"), 20)
        ctx.wait_for("[client] the respawned guest is alive", lambda: not ctx.is_dead(ctx.player_snapshot(victim))
                 and (ctx.player_health(victim) or 0) > 0, 8)
        ctx.camera_sees_followed(victim, "after the respawn")
    finally:
        ctx.console_on(attacker, f"set KillMoveRandom to {KILL_MOVE_RANDOM}")
        ctx.console_on(victim, "player.restoreav health 10000")

def scenario_kill_move_kills_guest(ctx):
    """The host's kill move kills the guest: one pair on both games, a real death named in both chats, the guest's
    ragdoll on its game and its copy's on the host's move as bodies do (no back and forth, no flickers), and the guest
    stands on its feet after the respawn."""
    attacker, victim = "", "client"
    names = {instance: ctx.player_record(instance)["name"] for instance in ("", "client")}
    for instance in ("", "client"):
        ctx.console_on(instance, "player.restoreav health 10000")
    copy = ctx.other_player(ctx.actors_on(attacker))
    try:
        ctx.console_on(attacker, "set KillMoveRandom to 100")
        ctx.give_sword(attacker)
        health = ctx.player_health(victim) or 100
        if health > 3:
            ctx.console_on(victim, f"player.damageav health {health - 3:.6f}")
        ctx.wait_on(attacker, "the attacker's copy shows the victim nearly dead",
                lambda actors: (actors.get(str(copy), {}).get("health") or 100) <= 3.5, 5)
        ctx.place_before(attacker, copy, MELEE_GAP)
        ctx.draw(attacker)
        with RagdollTrace(ctx, "kill-move-kills-guest", {victim: {0x14: "the guest's body"}, attacker: {copy: "the host's copy of the guest"}}) as trace:
            ctx.attack(attacker)
            ctx.wait_for("[host] the kill move pairs the host with the guest's copy",
                     lambda: ctx.graph_bools(attacker, "bIsSynced")["bIsSynced"], 3)
            ctx.wait_for("[client] the guest dies in the kill move", lambda: ctx.death_state(victim).get("down") and ctx.is_dead(ctx.player_snapshot(victim)), 8)
            def at_rest():
                trace.take()
                bodies = ctx.ragdoll_of(victim, 0x14) + ctx.ragdoll_of(attacker, copy)
                return bodies and not any(b["awake"] for b in bodies)
            try:
                ctx.wait_for("both games' bodies of the guest come to rest", at_rest, 15)
            except TestFailure:
                pass  # the trace says what kept moving
            rested = time.monotonic()
            ctx.wait_for("two seconds at rest", lambda: time.monotonic() - rested >= 2, 3)
        log(trace.report())
        line = f"{names[victim]} was killed by {names[attacker]}."
        for instance in ("", "client"):
            ctx.wait_for(f"[{instance or 'host'}] the chat names the killer", lambda: line in ctx.system_chat(instance), 5)
        ctx.sheathe(attacker)
        ctx.wait_for("[client] the guest respawns beside the host", lambda: not ctx.death_state(victim).get("down"), 25)
        settled = time.monotonic()
        ctx.wait_for("three seconds standing", lambda: time.monotonic() - settled >= 3, 4)
        problems = []
        for instance, actor in ((victim, 0x14), (attacker, ctx.other_player(ctx.actors_on(attacker)))):
            snapshot = ctx.actor_snapshot(instance, actor)
            if body_problems(snapshot):
                problems.append(f"[{instance or 'host'}] the respawned guest's body: {body_report(snapshot)}")
        try:
            trace.check()
        except TestFailure as failure:
            problems.append(str(failure))
        if problems:
            raise ctx.failure(" | ".join(problems))
    finally:
        ctx.console_on(attacker, f"set KillMoveRandom to {KILL_MOVE_RANDOM}")
        ctx.console_on(victim, "player.restoreav health 10000")

def scenario_spectator_watches_kill_move(ctx):
    """The guest is dead and spectates the host while the host kills a wolf with a kill move: the guest's view stays
    third person behind the host, with nothing between them, through the whole kill move."""
    npc, _, copy = ctx.spawn_host_npc(WOLF, restrained=True)
    third_person = CAMERA_STATES.index("third person")
    try:
        ctx.console_on("", "set KillMoveRandom to 100")
        ctx.give_sword("")
        ctx.draw("")
        ctx.console_on("client", "player.kill")
        ctx.wait_for("[client] the dead guest spectates the host", lambda: ctx.death_state("client").get("spectating"), 15)
        ctx.console_on("", f"{npc:08X}.damageav health {ctx.host_world(npc)['values']['health'] - 3:.0f}")
        front = math.degrees(ctx.host_world(npc)["placement"]["rotation"][2])
        ctx.place_before("", npc, MELEE_GAP, front)
        ctx.attack("")
        ctx.wait_for("[host] the kill move pairs the host with the wolf", lambda: ctx.graph_bools("", "bIsSynced")["bIsSynced"], 3)
        while ctx.graph_bools("", "bIsSynced")["bIsSynced"]:
            view = ctx.camera_sees_followed("client", "spectating a kill move")
            if view["state"] != third_person:
                raise ctx.failure(f"[client] the spectator's view left third person during the kill move: {ctx.camera_report('client', view)}")
        ctx.wait_for("[host] the wolf dies", lambda: ctx.host_world(npc).get("state", {}).get("dead"), 8)
        ctx.sheathe("")
        ctx.wait_for("[client] the guest respawns beside the host", lambda: not ctx.death_state("client").get("down"), 30)
    finally:
        ctx.console_on("", f"set KillMoveRandom to {KILL_MOVE_RANDOM}")
        ctx.console_on("client", "player.restoreav health 10000")
        ctx.remove_host_npc(npc)

def scenario_melee_client_hits_host_npc(ctx):
    """Client hits the host's NPC with a sword: the host's real NPC loses health, the client's copy
    shows the host's value and the client's HUD its health bar."""
    npc, _, copy = ctx.spawn_host_npc(LYDIA, restrained=True)
    ctx.console_on("", f"{npc:08X}.unequipall")  # her armor takes an iron sword's hit down to ~1
    ctx.give_sword("client")
    ctx.place_before("client", copy, MELEE_GAP)
    ctx.draw("client")
    before = ctx.host_npc_health(npc)
    ctx.attack("client")
    after = ctx.hit_lands(npc, before, "the client's swing")
    log(f"host's NPC health {before:.0f} -> {after:.0f}")
    ctx.copy_health_matches(npc, copy)
    ctx.health_bar_shows("client", copy)
    ctx.hold()

def scenario_firebolt_client_hits_host_npc(ctx):
    """Client's Firebolt hits the host's NPC: only the host applies it."""
    ctx.spell_hits_host_npc(FIREBOLT, "Firebolt")

def scenario_flames_client_hits_host_npc(ctx):
    """Client's Flames stream hits the host's NPC: only the host applies it."""
    ctx.spell_hits_host_npc(FLAMES, "Flames")

def scenario_client_angers_host_ally(ctx):
    """Client keeps hitting an ally NPC of the host with Firebolts: after the friendly-hit tolerance
    it fights the client."""
    npc, _, copy = ctx.spawn_host_npc(LYDIA)
    client_on_host = ctx.other_player(ctx.actors_on(""))
    with GodMode(ctx):
        ctx.console_on("", f"{npc:08X}.setav health 3000")
        ctx.give_spell("client", FIREBOLT)
        ctx.draw("client")
        turned = lambda: ctx.host_world(npc).get("ai", {}).get("combat_target") == client_on_host
        for hit in range(1, 11):  # Lydia takes 6-7 before she turns
            ctx.place_before("client", copy, SPELL_GAP)  # a hit staggers her back
            before = ctx.host_npc_health(npc)
            ctx.cast_spell("client", copy, FIREBOLT)
            ctx.hit_lands(npc, before, f"Firebolt {hit}", 3)
            if turned():
                break
        ctx.wait_for(f"[host] the NPC fights the client's character {client_on_host:08X}", turned, 3)
        log(f"host's NPC turned on the client after {hit} hits")
        ctx.hold()

def scenario_host_kill_move(ctx):
    """The host's player kills a wolf with a kill move: on the client the host's character and the wolf's
    copy play it as one pair (the engine pairs them, laid out as on the host), a moment later and at the
    same pace, and the wolf dies with its owner. Leaves an animation trace (timeline, side-by-side frames)."""
    npc, _, copy = ctx.spawn_host_npc(WOLF, restrained=True)
    host_on_client = ctx.other_player(ctx.actors_on("client"))
    trace = AnimTrace(ctx, "host-kill-move", {"": {npc: "wolf", 0x14: "player"},
                                         "client": {copy: "wolf", host_on_client: "player", 0x14: "watcher"}})
    with GodMode(ctx):
        ctx.console_on("", f"set KillMoveRandom to 100")
        try:
            ctx.give_sword("")
            ctx.draw("")
            ctx.console_on("", f"{npc:08X}.setrestrained 0")
            ctx.console_on("", f"{npc:08X}.startcombat player")
            ctx.wait_for("[host] the wolf fights the host's player", lambda: ctx.host_world(npc).get("ai", {}).get("combat_target") == 0x14, 8)
            # One hit from death, the host in front of its head; the client a few steps to the side, watching.
            ctx.console_on("", f"{npc:08X}.damageav health {ctx.host_world(npc)['values']['health'] - 3:.0f}")
            front = math.degrees(ctx.host_world(npc)["placement"]["rotation"][2])
            ctx.place_before("", npc, MELEE_GAP, front)
            ctx.place_before("client", copy, 300, front + 90)
            with trace:
                trace.mark("before the swing")
                ctx.press("", "right")
                try:
                    started = trace.wait_state("", "player", "kill_move", True, 3)
                finally:
                    ctx.release("", "right")
                if not (killer := trace.action_before(started)):
                    raise ctx.failure("[host] the player is in a kill move with no action that started it")
                # The client: the host's character's copy takes the paired idle and the engine pairs the wolf's copy
                # with it (each in a kill move, together).
                pair = {who: trace.wait_state("client", who, "kill_move", True, 3) for who in ("player", "wolf")}
                halves = {"player": trace.action_before(pair["player"])}
                if not halves["player"] or halves["player"]["idle"] != killer["idle"] or abs(pair["player"]["f"] - pair["wolf"]["f"]) > 1:
                    raise ctx.failure(f"the client's halves are not one pair: {halves['player']}, {pair}")
                lag = halves["player"]["time"] - killer["time"]
                trace.mark("the kill move starts", started["time"], halves["player"]["time"])
                owner_kill = trace.wait("", "wolf", "KillActor", 5)
                copy_kill = trace.wait("client", "wolf", "KillActor", 5)
                # Mid-animation, settled: the pair laid out on the client as on the host.
                host_layout = trace.layout("", "player", "wolf", owner_kill["time"] - 0.3)
                client_layout = trace.layout("client", "player", "wolf", copy_kill["time"] - 0.3)
                trace.mark("the blow lands", owner_kill["time"], copy_kill["time"])
                died = trace.wait_state("", "wolf", "life", 1, 3)
                copy_died = trace.wait_state("client", "wolf", "life", 1, 3)
                trace.mark("the wolf dies", died["time"], copy_died["time"])
                over = trace.wait_state("", "player", "kill_move", False, 5, after=started["time"])
                copy_over = trace.wait_state("client", "player", "kill_move", False, 5, after=pair["player"]["time"])
                trace.mark("the kill move is over", over["time"], copy_over["time"])
                rest = ctx.wait_for("[host] the wolf's body comes to rest",
                                ctx.comes_to_rest(lambda: ctx.host_world(npc)["placement"]["position"]), 10)
                ctx.wait_on("client", "the copy's body lies with it",
                        lambda actors: distance((actors.get(str(copy)) or {}).get("actual") or [1e9] * 3, rest) <= POSITION_TOLERANCE, 3)
                trace.mark("after")
        finally:
            ctx.console_on("", f"set KillMoveRandom to {KILL_MOVE_RANDOM}")
    log("\n" + trace.timeline())
    host_blow, client_blow = owner_kill["time"] - killer["time"], copy_kill["time"] - halves["player"]["time"]
    stood = copy_died["time"] - copy_kill["time"]
    log(f"the blow {host_blow * 1000:.0f}ms into the host's kill move, {client_blow * 1000:.0f}ms into the client's; "
        f"the wolf's copy dies {stood * 1000:.0f}ms after its blow")
    log(f"client's pair {lag * 1000:.0f}ms after the host's; layout (distance, bearing, heading) "
        f"host {tuple(round(v) for v in host_layout)}, client {tuple(round(v) for v in client_layout)}")
    # A kill cam that slows the host's clock slows the client's too (one clock for everyone).
    slowest = {game: min((e["state"]["time"] for e in trace.entries if e["kind"] == "game" and e["game"] == game), default=1.0)
               for game in ("", "client")}
    log(f"slowest clock: host {slowest['']:.2f}, client {slowest['client']:.2f}")
    if abs(slowest[""] - slowest["client"]) > 0.05:
        raise ctx.failure(f"the host's clock slowed to {slowest['']:.2f}, the client's to {slowest['client']:.2f}")
    if abs(host_blow - client_blow) > BLOW_TOLERANCE or stood > BLOW_TOLERANCE:
        raise ctx.failure(f"the client's kill move ran ahead of the host's: the blow {client_blow * 1000:.0f}ms in against "
                          f"{host_blow * 1000:.0f}ms, its wolf standing {stood * 1000:.0f}ms until its owner died")
    if lag > KILL_MOVE_LAG:
        raise ctx.failure(f"the client's kill move started {lag * 1000:.0f}ms after the host's")
    if any(abs((a - b + 180) % 360 - 180) > PAIR_TOLERANCE for a, b in zip(host_layout, client_layout)):
        raise ctx.failure(f"the client's pair is laid out {client_layout}, the host's {host_layout}")
    if pulled := [e for e in trace.entries if e["who"] == "watcher" and e["kind"] == "state"
                  and (e["state"].get("kill_move") or e["state"].get("bIsSynced"))]:
        raise ctx.failure(f"the client's own player was pulled into the pair: {pulled[0]}")
    ctx.hold()

def scenario_client_kill_move(ctx):
    """The client's player kills the host's wolf with a kill move: on the host the client's character and the real
    wolf play it as one pair, a moment later and at the same pace, and the host records the client's character as
    the killer. The wolf's copy on the client dies with its owner. Leaves an animation trace."""
    npc, _, copy = ctx.spawn_host_npc(WOLF, restrained=True)
    client_on_host = ctx.other_player(ctx.actors_on(""))
    trace = AnimTrace(ctx, "client-kill-move", {"": {npc: "wolf", client_on_host: "player", 0x14: "watcher"},
                                           "client": {copy: "wolf", 0x14: "player"}})
    with GodMode(ctx):
        ctx.console_on("client", "set KillMoveRandom to 100")
        try:
            ctx.give_sword("client")
            ctx.draw("client")
            # One hit from death, the client in front of its head; the host a few steps to the side, watching.
            ctx.console_on("", f"{npc:08X}.damageav health {ctx.host_world(npc)['values']['health'] - 3:.0f}")
            ctx.wait_on("client", "the wolf's copy shows it one hit from death",
                    lambda actors: (actors.get(str(copy), {}).get("health") or 100) < 5, 3)
            front = math.degrees(ctx.actors_on("client")[str(copy)]["rotation"][2])
            ctx.place_before("client", copy, MELEE_GAP, front)
            ctx.place_before("", npc, 300, front + 90)
            ctx.wait_for("[client] a blow now is a kill move", lambda: (check := ctx.player_command("client", {"cmd": "kill_move_check", "victim": copy}))
                     ["should_attack_kill"] and check["attack_target"] == copy, 3)
            with trace:
                trace.mark("before the swing")
                ctx.press("client", "right")
                try:
                    started = trace.wait_state("client", "player", "kill_move", True, 3)
                finally:
                    ctx.release("client", "right")
                if not (killer := trace.action_before(started)):
                    raise ctx.failure("[client] the player is in a kill move with no action that started it")
                # The host: the client's character's copy takes the paired idle, the real wolf its half (each in a
                # kill move, together). The copy's action is recorded the frame after it plays.
                pair = {who: trace.wait_state("", who, "kill_move", True, 3) for who in ("player", "wolf")}
                halves = {"player": trace.wait("", "player", killer["event"], 3, kind="action", after=started["time"])}
                if halves["player"]["idle"] != killer["idle"] or abs(pair["player"]["f"] - pair["wolf"]["f"]) > 1:
                    raise ctx.failure(f"the host's halves are not one pair: {halves['player']}, {pair}")
                lag = halves["player"]["time"] - killer["time"]
                trace.mark("the kill move starts", halves["player"]["time"], started["time"])
                copy_kill = trace.wait("client", "wolf", "KillActor", 5)
                owner_kill = trace.wait("", "wolf", "KillActor", 5)
                host_layout = trace.layout("", "player", "wolf", owner_kill["time"] - 0.3)
                client_layout = trace.layout("client", "player", "wolf", copy_kill["time"] - 0.3)
                trace.mark("the blow lands", owner_kill["time"], copy_kill["time"])
                died = trace.wait_state("", "wolf", "life", 1, 3)
                copy_died = trace.wait_state("client", "wolf", "life", 1, 3)
                trace.mark("the wolf dies", died["time"], copy_died["time"])
                rest = ctx.wait_for("[host] the wolf's body comes to rest",
                                ctx.comes_to_rest(lambda: ctx.host_world(npc)["placement"]["position"]), 10)
                ctx.wait_on("client", "the copy's body lies with it",
                        lambda actors: distance((actors.get(str(copy)) or {}).get("actual") or [1e9] * 3, rest) <= POSITION_TOLERANCE, 3)
                trace.mark("after")
        finally:
            ctx.console_on("client", f"set KillMoveRandom to {KILL_MOVE_RANDOM}")
    log("\n" + trace.timeline())
    recorded = ctx.host_world(npc).get("ai", {}).get("killer")
    client_blow, host_blow = copy_kill["time"] - killer["time"], owner_kill["time"] - halves["player"]["time"]
    stood = copy_died["time"] - copy_kill["time"]
    log(f"the blow {client_blow * 1000:.0f}ms into the client's kill move, {host_blow * 1000:.0f}ms into the host's; "
        f"the wolf's copy dies {stood * 1000:.0f}ms after its blow (its owner's death a network delay later)")
    log(f"host's pair {lag * 1000:.0f}ms after the client's; layout (distance, bearing, heading) "
        f"client {tuple(round(v) for v in client_layout)}, host {tuple(round(v) for v in host_layout)}")
    if recorded != client_on_host:
        raise ctx.failure(f"host records killer {recorded or 0:08X}, expected the client's character {client_on_host:08X}")
    if abs(host_blow - client_blow) > BLOW_TOLERANCE:
        raise ctx.failure(f"the host's kill move ran at another pace: the blow {host_blow * 1000:.0f}ms in against "
                          f"the client's {client_blow * 1000:.0f}ms")
    if lag > KILL_MOVE_LAG:
        raise ctx.failure(f"the host's kill move started {lag * 1000:.0f}ms after the client's")
    if stood > KILL_MOVE_LAG + BLOW_TOLERANCE:
        raise ctx.failure(f"the wolf's copy stood {stood * 1000:.0f}ms after its blow")
    if any(abs((a - b + 180) % 360 - 180) > PAIR_TOLERANCE for a, b in zip(host_layout, client_layout)):
        raise ctx.failure(f"the host's pair is laid out {host_layout}, the client's {client_layout}")
    if pulled := [e for e in trace.entries if e["who"] == "watcher" and e["kind"] == "state"
                  and (e["state"].get("kill_move") or e["state"].get("bIsSynced"))]:
        raise ctx.failure(f"the host's own player was pulled into the pair: {pulled[0]}")
    ctx.hold()

def scenario_xp_stays_personal(ctx):
    """The guest's actual melee hit raises its own One-Handed; the host gains no experience from it."""
    npc, _, copy = ctx.spawn_host_npc(LYDIA, restrained=True)
    try:
        ctx.give_sword("client")
        ctx.draw("client")
        ctx.place_before("client", copy, MELEE_GAP)
        before = {g: ctx.skill(g, 6) for g in ("", "client")}
        health = ctx.host_npc_health(npc)
        ctx.attack("client")
        ctx.hit_lands(npc, health, "the guest's swing")
        ctx.wait_for("the guest's One-Handed gains experience", lambda: ctx.skill("client", 6) > before["client"], 5)
        ctx.screen_marker("client", "")
        if ctx.skill("", 6) != before[""]:
            raise ctx.failure(f"the host gained XP from its guest's hit: {before['']} -> {ctx.skill('', 6)}")
    finally:
        ctx.sheathe("client")
        ctx.remove_host_npc(npc)

def scenario_client_spell_kill_credit(ctx):
    """Client kills a host NPC with a spell: the host records the client's character as the killer."""
    ctx.kill_credit("spell")

def scenario_client_melee_kill_credit(ctx):
    """Client kills a host NPC with a sword: the host records the client's character as the killer."""
    ctx.kill_credit("melee")


from .physics import scenario_guest_knockdown_lies_on_host
"""Scenarios combat for the co-op harness."""
from ..common import MELEE_GAP


def melee_bash(ctx, attacker):
    """A real shield bash on the other player's copy staggers its owner and sends damage only once."""
    victim = "" if attacker else "client"
    shield = 0x12EB6
    held = ctx.item_count(attacker, 0x14, shield)
    try:
        for game in ("", "client"):
            ctx.console_on(game, "player.restoreav health 10000")
        ctx.give_sword(attacker)
        if not held:
            ctx.console_on(attacker, f"player.additem {shield:X} 1")
        ctx.console_on(attacker, f"player.equipitem {shield:X}")
        ctx.draw(attacker)
        copy = ctx.player_copy(attacker)
        ctx.place_before(attacker, copy, MELEE_GAP)
        before = ctx.player_health(victim)
        ctx.press(attacker, "left")
        ctx.wait_for("the attacker blocks with its shield", lambda:
                    ctx.graph_bools(attacker, "IsBlocking")["IsBlocking"], 5)
        ctx.press(attacker, "right")
        ctx.wait_for("the victim's own game plays the bash stagger", lambda:
                    ctx.graph_bools(victim, "IsStaggering")["IsStaggering"], 5)
        ctx.release(attacker, "right")
        ctx.release(attacker, "left")
        ctx.wait_for("the victim's own game takes the bash damage", lambda:
                    ctx.player_health(victim) < before, 5)
        ctx.wait_for("the stagger finishes on its owner", lambda:
                    not ctx.graph_bools(victim, "IsStaggering")["IsStaggering"], 10)
    finally:
        ctx.release(attacker, "right")
        ctx.release(attacker, "left")
        ctx.sheathe(attacker)
        ctx.console_on(victim, "player.restoreav health 10000")
        if not held:
            ctx.console_on(attacker, f"player.removeitem {shield:X} 1")

def revived_player_knockdown(ctx):
    """The guest can be knocked down before and after its real death and party revival."""
    scenario_guest_knockdown_lies_on_host(ctx)
    scenario_spectator_watches_kill_move(ctx)
    scenario_guest_knockdown_lies_on_host(ctx)
