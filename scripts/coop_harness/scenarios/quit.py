"""Solo process-lifecycle scenarios; no living duo is needed after Quit."""
import shutil
from pathlib import Path
from types import SimpleNamespace

from ..common import ACTIVATE_KEY, DOWN_KEY, JOURNAL_SYSTEM
from ..utilities import log
from .v1 import open_quit_panel, quit_to_menu


def quit_to_desktop(ctx, instance, *, main_menu):
    if main_menu:
        if not ctx.game_state(instance).get("main_menu"):
            quit_to_menu(ctx, instance)
        menu = "Main Menu"
        roots = ("_root.MenuHolder.Menu_mc.", "_root.Menu_mc.", "_root.StartMenuObj.")
        root = ctx.wait_for("the main menu's list is ready", lambda: next(
            (path for path in roots if ctx.menu_var(instance, menu, path + "MainListHolder.List_mc.selectedIndex") is not None), None), 10)
        listing = root + "MainListHolder.List_mc."
        state = lambda: ctx.menu_var(instance, menu, root + "currentState")
        selected = lambda: ctx.menu_var(instance, menu, listing + "selectedIndex")
        for _ in range(12):
            index = selected()
            text = ctx.menu_var(instance, menu, listing + f"entryList.{int(index)}.text") or ""
            if text.lstrip("$").casefold() == "quit":
                break
            ctx.tap_key(instance, DOWN_KEY)
            ctx.wait_for("the main menu selection moves", lambda: selected() != index, 5)
        else:
            raise ctx.failure("Quit is absent from the main menu")
        ctx.tap_key(instance, ACTIVATE_KEY)
        ctx.wait_for("the desktop quit confirmation finishes opening", lambda: state() == "MainConfirm", 5)
    else:
        ctx.wait_for("the loaded game takes menu input",
                     lambda: "HUD Menu" in ctx.menus(instance) and ctx.takes_input(instance), 15)
        # The journal's Quit panel is the same one used by the main-menu session scenarios.
        open_quit_panel(ctx, instance)
        ctx.move_cursor(instance, (700, 150))
        if ctx.menu_var(instance, "Journal Menu", JOURNAL_SYSTEM + "PCQuitList.selectedIndex") != 1:
            ctx.tap_key(instance, DOWN_KEY)
        ctx.wait_for("Desktop is selected", lambda: ctx.menu_var(instance, "Journal Menu", JOURNAL_SYSTEM + "PCQuitList.selectedIndex") == 1, 5)
        ctx.tap_key(instance, ACTIVATE_KEY)
        ctx.wait_for("the desktop quit confirmation opens", lambda: ctx.menu_var(instance, "Journal Menu", JOURNAL_SYSTEM + "iCurrentState") == 9, 5)
    # The movie must observe the preceding Accept release before another Accept press.
    ctx.held_a_frame(instance)
    # Confirmation can stop drawing frames immediately; don't require a subsequent frame/key release.
    if main_menu:
        ctx.key(instance, ACTIVATE_KEY, True)
    else:
        ctx.move_cursor(instance, (700, 560))
        ctx.player_command(instance, {"cmd": "mouse", "button": 0, "down": True})
        # The button acts on release, which may close the socket before its reply.
        response = ctx.game(instance).rpc({"cmd": "mouse", "button": 0, "down": False})
        if response and not response.get("ok"):
            raise ctx.failure(f"Quit confirmation refused: {response.get('error')}")


def reproduce(ctx, args, *, main_menu, game):
    ctx.check_prerequisites()
    for run in range(args.runs):
        directory = ctx.new_run_dir("quit-main-menu" if main_menu else "quit-in-game")
        boot = SimpleNamespace(**vars(args))
        boot.main_menu = main_menu
        boot.save = None
        before = set(ctx.crash_logs(game.name))
        try:
            ctx.launch(boot, directory, _game=game)
            pid = ctx.game_process(game.name)
            quit_to_desktop(ctx, game.name, main_menu=main_menu)
            ctx.wait_for("Skyrim exits after confirming Quit", lambda: not Path(f"/proc/{pid}").exists(), 30, _game=game)
            if crashes := set(ctx.crash_logs(game.name)) - before:
                raise ctx.failure(f"Quit crashed: {sorted(crashes)[-1]}")
            log(f"PASS quit {'from the main menu' if main_menu else 'from in-game'} ({run + 1}/{args.runs})")
        finally:
            ctx.stop_game(_game=game)
            ctx.collect_logs(directory, _game=game)
            for crash in set(ctx.crash_logs(game.name)) - before:
                shutil.copy2(crash, directory / crash.name)
