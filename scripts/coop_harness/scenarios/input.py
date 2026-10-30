"""Scenarios input for the co-op harness."""
from ..common import (
    FLAMES,
    RIGHT_HAND,
    WEAPON_DRAWN,
    WEAPON_SHEATHED,
)


def scenario_input_keyboard_draw(ctx):
    """The host presses R to ready a sword, then again to sheathe it: the game reacts to the key,
    and the client's copy of the host follows."""
    ctx.give_sword("")
    for state, verb in ((WEAPON_DRAWN, "draws"), (WEAPON_SHEATHED, "sheathes")):
        # The game ignores Ready Weapon while a draw or sheathe still plays, a player's key as well.
        ctx.wait_for("[host] the last draw or sheathe has played out", lambda: ctx.hands_settled(""), 5)
        ctx.key_on("", "r")
        ctx.wait_for(f"[host] the R key {verb} the sword", lambda: ctx.weapon_state("") == state, 3)
        ctx.wait_on("client", f"host's character {verb} the sword", ctx.copy_drawn(state == WEAPON_DRAWN), 5)

def scenario_input_mouse_cast(ctx):
    """The client holds the mouse button with Flames ready, then lets go: the game casts while it is
    held, and the host's copy of the client casts, then stops."""
    ctx.give_spell("client", FLAMES)
    ctx.draw("client")
    ctx.mouse_on("client", "mousedown")
    try:
        ctx.wait_for("[client] holding the button casts Flames",
                 lambda: ctx.local_player("client").get("casting", [0, 0])[RIGHT_HAND] == FLAMES, 3)
        ctx.wait_on("", "client's character casts Flames", ctx.copy_casting(FLAMES, True), 5)
    finally:
        ctx.mouse_on("client", "mouseup")
    ctx.wait_on("", "client's character stops casting", ctx.copy_casting(FLAMES, False), 5)
