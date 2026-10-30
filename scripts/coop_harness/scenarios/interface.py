"""Scenarios interface for the co-op harness."""
from ..common import (
    ESCAPE_KEY,
    HOME_CELL,
    LEAVE_IT_BUTTON,
    OUTDOOR_CELL,
)
from ..comparisons import distance


def scenario_chat_reaches_everyone(ctx):
    """The client types a line (Enter, the text, Enter): both games show it under the client's name, and
    the client's character did not walk while it typed ("w" is forward). The host answers. A line
    abandoned with Escape reaches nobody."""
    client_name = next(n for n, host in ctx.session_players("").items() if not host)
    host_name = next(n for n, host in ctx.session_players("client").items() if host)
    before = ctx.player_snapshot("client")["placement"]["position"]

    ctx.say("client", "Hello there, wow!")
    for name in ("", "client"):
        ctx.wait_for(f"[{name or 'host'}] shows the client's line", lambda name=name: ctx.chat_has(name, client_name, "Hello there, wow!"), 5)
    moved = distance(ctx.player_snapshot("client")["placement"]["position"], before)
    if moved > 5:
        raise ctx.failure(f"the client's character moved {moved:.0f} units while it typed")

    ctx.say("", "Welcome!")
    ctx.wait_for("[client] shows the host's line", lambda: ctx.chat_has("client", host_name, "Welcome!"), 5)

    ctx.open_chat("client")
    ctx.type_text("client", "never mind")
    ctx.tap_key("client", ESCAPE_KEY)
    ctx.wait_for("[client] the chat line is closed", lambda: not ctx.chat("client").get("open"), 5)
    ctx.hold()
    if any(l["text"] == "never mind" for l in ctx.chat("").get("lines", [])):
        raise ctx.failure("a line abandoned with Escape reached the host")
    if not ctx.takes_input("client"):
        raise ctx.failure("[client] Escape in the chat opened a menu (the game got the key)")

def scenario_map_marker_reaches_everyone(ctx):
    """Outdoors, the client places a marker on its map: the host's map shows it at the same spot. Then the
    host places one: the client's moves there. The client removes it (P on it, "Remove It"): the host's
    goes too. (Each placement starts without a marker: one already there turns P into that box.)"""
    def clear_markers():
        for name in ("", "client"):
            if "MessageBoxMenu" in ctx.menus(name):   # a failure left the box up: it blocks the game until answered
                ctx.answer_marker_box(name, LEAVE_IT_BUTTON)
            ctx.player_command(name, {"cmd": "clear_map_marker"})
            ctx.wait_for(f"[{name or 'host'}] has no map marker", lambda name=name: not ctx.map_marker(name), 5)
    try:
        ctx.both_go(OUTDOOR_CELL)
        clear_markers()
        placed = ctx.place_map_marker("client")
        ctx.wait_for("[host] shows the client's marker", lambda: ctx.same_marker(ctx.map_marker(""), placed), 5)

        clear_markers()
        placed = ctx.place_map_marker("")
        ctx.wait_for("[client] shows the host's marker", lambda: ctx.same_marker(ctx.map_marker("client"), placed), 5)

        ctx.remove_map_marker("client")
        ctx.wait_for("[host] the marker is gone", lambda: not ctx.map_marker(""), 5)
    finally:
        clear_markers()
        ctx.both_go(HOME_CELL)
