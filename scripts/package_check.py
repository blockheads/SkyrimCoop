#!/usr/bin/env python3
"""Installs the player package (scripts/package.sh) the way docs/install.md says, extracting it into a clean game
folder with a new Proton prefix, then presses Play (SkyrimSELauncher.exe, ours) and checks that the launcher enabled
the bundled start and the plugin loads with its UI from the installed files. A player build has no test harness, so
this is a boot check read from the plugin's log, not a joined session.

The clean game folder (build/clean-install/game) links the Steam install's files, without SKSE, its plugins or
anything of ours: the package brings them. Its own display (:105), so it can run beside the duo.
  scripts/package_check.py [ZIP]
"""
import os
import shutil
import subprocess
import sys
import time
import zipfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
os.environ["COOP_DUO_USER"] = ""  # a player's install: this user's Steam, not the duo's (common.DUO_USER)
from coop_harness import Session
from coop_harness.game import Game
from coop_harness.common import ROOT, STEAM_GAME, RUNTIME, PROTON
from coop_harness.errors import TestFailure
from coop_harness.utilities import log

ct = Session()

HOME = ROOT / "build/clean-install"
GAME = HOME / "game"
# What the package brings: SKSE (its loader, runtime and scripts), its plugins, ours, and Steam's launcher name.
OURS = ("SKSE", "Scripts", "SkyrimCoop", "SkyrimCoopStart.esp", "SkyrimCoopLauncher.exe", "SkyrimSELauncher.exe",
        "unofficial skyrim special edition patch", "alternate start - live another life")


def clean_game():
    """The Steam install's files as links, without SKSE, its plugins, anything of ours or Steam's launcher."""
    if GAME.exists():
        shutil.rmtree(GAME)
    (GAME / "Data").mkdir(parents=True)
    for source in STEAM_GAME.iterdir():
        if source.name != "Data" and not source.name.startswith(OURS) and not source.name.startswith("skse64"):
            (GAME / source.name).symlink_to(source)
    for source in (STEAM_GAME / "Data").iterdir():
        if not source.name.startswith(OURS):
            (GAME / "Data" / source.name).symlink_to(source)


def install(package):
    """docs/install.md: extract into the game folder; nothing else (the launcher enables the start)."""
    with zipfile.ZipFile(package) as archive:
        archive.extractall(GAME)
    ct.load_order_file().unlink(missing_ok=True)


def main():
    package = Path(sys.argv[1]) if len(sys.argv) > 1 else max((ROOT / "build/package").glob("*.zip"),
                                                                 key=lambda path: path.stat().st_mtime)
    ct.host = Game("", install=GAME, prefix=HOME / "prefix", display=":105")
    ct.games = (ct.host, ct.guest)
    if ct.prefix_processes():
        ct.stop_game()
    shutil.rmtree(ct.host.prefix, ignore_errors=True)
    clean_game()
    ct.prepare_prefix(bundled_start=False)  # the package's launcher enables the start
    install(package)
    ct.ensure_virtual_display()
    env = {key: value for key, value in ct.game_env().items()
           if key not in ("SKYRIMCOOP_UI_DIR", "SKYRIMCOOP_LOG_LEVEL", "SKYRIMCOOP_NO_CRASH_REPORTS")}
    plugin_log = GAME / "Data/SKSE/Plugins/logs/tp_client.log"
    log(f"Booting {package.name} from {GAME} through SkyrimSELauncher.exe (SkyrimCoopLauncher)")
    output = open(HOME / "proton.log", "w")
    launcher = subprocess.Popen([str(RUNTIME / "run"), "--", str(PROTON / "proton"), "waitforexitandrun",
                                 str(GAME / "SkyrimSELauncher.exe")], env=env, cwd=GAME, stdout=output,
                                stderr=subprocess.STDOUT, start_new_session=True)
    try:
        ui = ct.wait_for("the plugin's UI is ready", lambda: plugin_log.exists() and next(
            (line for line in plugin_log.read_text(errors="replace").splitlines() if "UI: RmlUi" in line), None), 240, launcher)
        log(ui)
        installed = str(GAME / "Data/SKSE/Plugins/SkyrimCoop/ui")
        if not ui.rstrip().replace("\\", "/").endswith(installed):
            raise TestFailure(f"the UI does not come from the installed files ({installed})")
        order = ct.load_order_file().read_text(errors="replace").splitlines() if ct.load_order_file().exists() else []
        wanted = ["*unofficial skyrim special edition patch.esp", "*alternate start - live another life.esp", "*SkyrimCoopStart.esp"]
        if order[-3:] != wanted:
            raise TestFailure(f"the launcher did not enable the package's plugins in order: Plugins.txt is {order!r}")
        time.sleep(30)  # the main menu, and any late failure
        problems = [line for line in plugin_log.read_text(errors="replace").splitlines()
                    if "[error]" in line or "[critical]" in line]
        if launcher.poll() is not None or not ct.prefix_processes():
            raise TestFailure("the game exited")
        shot = HOME / "main-menu.png"
        subprocess.run(["import", "-display", ct.host.display, "-window", "root", str(shot)], check=False)
        print(f"PASS: plugin loaded, plugins enabled, UI from the package; {len(problems)} error lines; screenshot {shot}")
        for line in problems[:20]:
            print("  ", line)
    finally:
        ct.stop_game()


if __name__ == "__main__":
    main()
