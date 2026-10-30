"""Scenarios objects for the co-op harness."""
from ..common import (
    CLAW,
    CLAW_KEYHOLE,
    CLAW_RINGS,
    DOOR_CLOSED,
    DOOR_OPEN,
    HALLDIR_STAFF,
    HOME_CELL,
    KOLBJORN_CELL,
    KOLBJORN_PLATE,
    REBELS_CAIRN_CELL,
    RED_EAGLE_SWORD,
    SECRET_DOOR,
    SOLJUND_CELL,
    SOLJUND_FIRE_DAMAGE,
    SOLJUND_TRIGGER,
    SWORD_BOX,
    SWORD_IN_STONE,
)


def scenario_plate_counts_client(ctx):
    """The client walks across a pressure plate whose unpatched vanilla script only reacts to the player:
    on the host its character counts as the player (docs/design/party-model.md, "A client's actions on
    the host", rule 1), so the host's plate goes down under it and comes up again behind it."""
    try:
        ctx.both_go(KOLBJORN_CELL)
        for name in ("", "client"):
            if ctx.plate_state(name) != "waiting":
                raise ctx.failure(f"[{name or 'host'}] plate {KOLBJORN_PLATE:X} is '{ctx.plate_state(name)}', not Waiting, to begin with")
        ctx.place_before("client", KOLBJORN_PLATE, 150)
        ctx.press("client", "forward")
        try:
            ctx.wait_for("[host] the plate is down under the client's character", lambda: ctx.plate_state("") == "intrigger", 10)
            ctx.wait_for("[host] the plate is up again behind it", lambda: ctx.plate_state("") == "waiting", 10)
        finally:
            ctx.release("client", "forward")
    finally:
        ctx.both_go(HOME_CELL)

def scenario_sword_box_asks_client(ctx):
    """The client puts Red Eagle's sword in the stone and takes it back. On the host the box's check
    counts its character as the player (rule 1), and the script the box starts asks that character for
    the sword and hands it back to it (rule 2, docs/design/party-model.md): the host's player, who has
    none, opens the door all the same and never gets the sword. The client's own box runs nothing
    (world-state sync step 5): its sword goes once and comes back once, its door opens from the host's, and
    it sees the host's script swap the box and the sword in the stone (step 6, enabled state). The client carries
    a staff worth more than its axe: when the script takes the sword from the host's copy, the engine picks a
    weapon for the copy (an NPC losing an item), which a copy refuses: it keeps its owner's hands."""
    ctx.console_on("client", f"player.additem {HALLDIR_STAFF:X} 1")
    try:
        ctx.both_go(REBELS_CAIRN_CELL)
        copy = ctx.client_on_host()
        _, since = ctx.log_trail("")
        for name in ("", "client"):
            if ctx.door_state(name, SECRET_DOOR) != DOOR_CLOSED:
                raise ctx.failure(f"[{name or 'host'}] secret door {SECRET_DOOR:X} is not closed to begin with")
        if ctx.item_count("", 0x14, RED_EAGLE_SWORD):
            raise ctx.failure("[host] the host's player holds Red Eagle's sword to begin with")
        for name in ("", "client"):
            ctx.place_before(name, SECRET_DOOR, 400)
        ctx.console_on("client", f"player.additem {RED_EAGLE_SWORD:08X} 1")
        ctx.wait_for("[host] the client's character holds the sword", lambda: ctx.item_count("", copy, RED_EAGLE_SWORD) == 1, 5)
        ctx.activate_on("client", SWORD_BOX)
        for name in ("", "client"):
            ctx.wait_for(f"[{name or 'host'}] secret door {SECRET_DOOR:X} opens", lambda name=name: ctx.door_state(name, SECRET_DOOR) == DOOR_OPEN, 15)
        ctx.wait_for("[client] the player gave up the sword", lambda: ctx.item_count("client", 0x14, RED_EAGLE_SWORD) == 0, 5)
        if ctx.item_count("", 0x14, RED_EAGLE_SWORD):
            raise ctx.failure("[host] the host's player holds a sword it never had")
        ctx.wait_for("[host] the copy gave up the sword", lambda: ctx.item_count("", copy, RED_EAGLE_SWORD) == 0, 5)
        if HALLDIR_STAFF in ctx.copy_equips("", copy, since):
            raise ctx.failure(f"[host] the copy equipped the staff its owner never held when the sword was taken from it")
        # The host's script swaps the box for the sword in the stone; the client sees it from the host (step 6).
        for name in ("", "client"):
            ctx.wait_for(f"[{name or 'host'}] the sword stands in the stone, the box is gone",
                     lambda name=name: ctx.ref_enabled(name, SWORD_IN_STONE) and ctx.ref_enabled(name, SWORD_BOX) is False, 5)
        ctx.activate_on("client", SWORD_IN_STONE)
        for name in ("", "client"):
            ctx.wait_for(f"[{name or 'host'}] secret door {SECRET_DOOR:X} closes", lambda name=name: ctx.door_state(name, SECRET_DOOR) == DOOR_CLOSED, 15)
        ctx.wait_for("[client] the player has the sword back", lambda: ctx.item_count("client", 0x14, RED_EAGLE_SWORD) == 1, 5)
        for name in ("", "client"):
            ctx.wait_for(f"[{name or 'host'}] the box is back, the stone empty",
                     lambda name=name: ctx.ref_enabled(name, SWORD_BOX) and ctx.ref_enabled(name, SWORD_IN_STONE) is False, 5)
        if ctx.item_count("", 0x14, RED_EAGLE_SWORD):
            raise ctx.failure("[host] the sword went to the host's player, not the client's character")
        if (swords := ctx.item_count("client", 0x14, RED_EAGLE_SWORD)) != 1:
            raise ctx.failure(f"[client] the player holds {swords} swords: the box's script ran more than once")
    finally:
        if swords := ctx.item_count("client", 0x14, RED_EAGLE_SWORD):
            ctx.console_on("client", f"player.removeitem {RED_EAGLE_SWORD:08X} {swords}")
        ctx.console_on("client", f"player.removeitem {HALLDIR_STAFF:X} 1")
        ctx.both_go(HOME_CELL)

def scenario_statue_fires_at_client(ctx):
    """The client walks into Soljund's boss trigger: on the host its character counts as the player
    (rule 1), and the updates the trigger registers for run for it (rule 2), so the host's fire statue
    shoots at the client's character, not at the host's player, who stands out of the way. Checked by the
    host's own hits; the client's own trigger runs nothing (world-state sync step 5), and the host's hit
    burns the client's player through its game. No god mode: it keeps the hit from landing at all."""
    try:
        ctx.both_go(SOLJUND_CELL)
        copy = ctx.client_on_host()
        _, since = ctx.engine_events("")
        _, client_since = ctx.engine_events("client")
        ctx.place_before("client", SOLJUND_TRIGGER, 200)
        ctx.press("client", "forward")
        try:
            ctx.wait_for("[host] the trigger is polling", lambda: ctx.boss_trigger_state("") == "polling", 10)
        finally:
            ctx.release("client", "forward")
        ctx.wait_for("[host] the statue's fire hits the client's character", lambda: copy in ctx.fire_hits(since), 15)
        if 0x14 in ctx.fire_hits(since):
            raise ctx.failure("[host] the statue's fire hit the host's player too")
        if ctx.boss_trigger_state("client") == "polling":
            raise ctx.failure("[client] its own trigger ran (world-state sync step 5): its statue would fire too")
        # Its owner's game applies the host's hit (MagicTarget: the host's world on a guest's character).
        ctx.wait_for("[client] the host's fire burns the player", lambda: any(
            e.get("kind") == "magic_effect_apply" and e.get("target") == 0x14 and e.get("effect") == SOLJUND_FIRE_DAMAGE
            for e in ctx.engine_events("client", client_since)[0]), 5)
    finally:
        for name in ("", "client"):
            ctx.console_on(name, "player.restoreav health 1000")
        ctx.both_go(HOME_CELL)

def scenario_ring_turns_for_client(ctx):
    """The client turns a ring of a claw door three times round. On the host the ring's check counts
    its character as the player (rule 1, docs/design/party-model.md), so the host's ring turns with it.
    The client runs no world script (it never ran the ring's: its script has no state); its ring turns as
    the host's does, from the host's animations."""
    ring = next(iter(CLAW_RINGS))
    try:
        ctx.before_claw_door()
        start = ctx.claw_door_state("", ring)
        if not start.startswith("position"):
            raise ctx.failure(f"[host] ring {ring:X} is '{start}' to begin with")
        ctx.wait_for(f"[client] ring {ring:X} looks as the host's to begin with", lambda: ctx.ring_looks_alike(ring), 5)
        position = int(start[-1])
        for _ in range(3):
            position = position % 3 + 1
            ctx.activate_on("client", ring)
            ctx.wait_for(f"[host] ring {ring:X} turns to position {position}",
                     lambda: ctx.claw_door_state("", ring) == f"position0{position}", 10)
            ctx.wait_for(f"[client] ring {ring:X} turned as the host's", lambda: ctx.ring_looks_alike(ring), 10)
            if state := ctx.claw_door_state("client", ring):
                raise ctx.failure(f"[client] ring {ring:X}'s own script ran: it is '{state}'")
    finally:
        ctx.both_go(HOME_CELL)

def scenario_late_join_gets_ring_pose(ctx):
    """The host turns a claw door's ring while the client is away; the client rejoins at the door. Its ring was
    never animated there, so only the cell's state (the ring's graph, kGraph) can show the host's turn."""
    ring = next(iter(CLAW_RINGS))
    try:
        ctx.before_claw_door()
        ctx.wait_for(f"[client] ring {ring:X} looks as the host's to begin with", lambda: ctx.ring_looks_alike(ring), 5)
        start = ctx.claw_door_state("", ring)
        ctx.disconnect_client()
        ctx.console_on("", f"{ring:X}.activate player")
        turned = f"position0{int(start[-1]) % 3 + 1}"
        ctx.wait_for(f"[host] ring {ring:X} turns to {turned}", lambda: ctx.claw_door_state("", ring) == turned, 10)
        ctx.rejoin_client()
        ctx.wait_for(f"[client] ring {ring:X} looks as the host's turned one", lambda: ctx.ring_looks_alike(ring), 15)
    finally:
        # Back to where it was: two more turns.
        for _ in range(2):
            if ctx.claw_door_state("", ring) != start:
                before = ctx.claw_door_state("", ring)
                ctx.console_on("", f"{ring:X}.activate player")
                ctx.wait_for(f"[host] ring {ring:X} turns on", lambda: ctx.claw_door_state("", ring) != before, 10)
        ctx.both_go(HOME_CELL)

def scenario_keyhole_asks_client(ctx):
    """The client, holding the claw, puts it in a claw door whose rings are wrong. On the host the
    keyhole counts its character as the player (rule 1) and asks it for the claw (rule 2): the host's
    player has none, yet the keyhole takes the try (busy, then back) and the door stays shut."""
    try:
        ctx.before_claw_door()
        for name in ("", "client"):
            states = {ring: ctx.claw_door_state(name, ring) for ring in CLAW_RINGS}
            if all(states[ring] == solved for ring, solved in CLAW_RINGS.items()):
                raise ctx.failure(f"[{name or 'host'}] every ring is solved to begin with: the door would open for good")
            if ctx.claw_door_state(name, CLAW_KEYHOLE) != "mainstate":
                raise ctx.failure(f"[{name or 'host'}] keyhole {CLAW_KEYHOLE:X} is '{ctx.claw_door_state(name, CLAW_KEYHOLE)}' to begin with")
        if ctx.item_count("", 0x14, CLAW):
            raise ctx.failure("[host] the host's player holds the claw to begin with")
        copy = ctx.client_on_host()
        ctx.console_on("client", f"player.additem {CLAW:08X} 1")
        ctx.wait_for("[host] the client's character holds the claw", lambda: ctx.item_count("", copy, CLAW) == 1, 5)
        ctx.activate_on("client", CLAW_KEYHOLE)
        ctx.wait_for("[host] the keyhole takes the claw", lambda: ctx.claw_door_state("", CLAW_KEYHOLE) == "busy", 5)
        ctx.wait_for("[host] the keyhole gives up: a ring is wrong", lambda: ctx.claw_door_state("", CLAW_KEYHOLE) == "mainstate", 10)
    finally:
        if claws := ctx.item_count("client", 0x14, CLAW):
            ctx.console_on("client", f"player.removeitem {CLAW:08X} {claws}")
        ctx.both_go(HOME_CELL)
