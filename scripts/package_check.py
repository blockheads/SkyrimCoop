#!/usr/bin/env python3
"""Installs the player package (scripts/package.sh) the way docs/install.md says into a clean game folder and a new
Proton prefix, then boots it through SkyrimCoopLauncher.exe and checks that the plugin loads and its UI comes from the
installed files. A player build has no test harness, so this is a boot check read from the plugin's log, not a
joined session.

The clean game folder (build/clean-install/game) links the Steam install's files but has its own SKSE and Address
Library (copied, as a player downloads them) and nothing of ours but the package. Its own display (:105), so it can
run beside the duo.
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
from coop_harness import Session
from coop_harness.game import Game
from coop_harness.common import ROOT, GAME as STEAM_GAME, RUNTIME, PROTON
from coop_harness.errors import TestFailure
from coop_harness.utilities import log

ct = Session()

HOME = ROOT / "build/clean-install"
GAME = HOME / "game"
# What a player installs before us (docs/install.md, step 1): SKSE's loader and runtime, Address Library.
REQUIREMENTS = ["skse64_loader.exe", "skse64_1_6_1170.dll", "Data/SKSE/Plugins/versionlib-1-6-1170-0.bin"]
OURS = ("SKSE", "SkyrimCoop", "SkyrimCoopStart.esp", "SkyrimCoopLauncher.exe", "SkyrimSELauncher.exe")


def clean_game():
    """The Steam install's files as links, but no SKSE plugins, nothing of ours and its own launcher name."""
    if GAME.exists():
        shutil.rmtree(GAME)
    (GAME / "Data").mkdir(parents=True)
    for source in STEAM_GAME.iterdir():
        if source.name != "Data" and not source.name.startswith(OURS) and not source.name.startswith("skse64"):
            (GAME / source.name).symlink_to(source)
    for source in (STEAM_GAME / "Data").iterdir():
        if not source.name.startswith(OURS):
            (GAME / "Data" / source.name).symlink_to(source)
    (GAME / "SkyrimSELauncher.exe.vanilla").symlink_to(STEAM_GAME / "SkyrimSELauncher.exe")
    for name in REQUIREMENTS:
        (GAME / name).parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(STEAM_GAME / name, GAME / name)


def install(package):
    """docs/install.md steps 2 and 3: extract into the game folder, enable the start, the launcher under Steam's name."""
    with zipfile.ZipFile(package) as archive:
        archive.extractall(GAME)
    shutil.copy2(GAME / "SkyrimCoopLauncher.exe", GAME / "SkyrimSELauncher.exe")
    order = ct.load_order_file()
    order.parent.mkdir(parents=True, exist_ok=True)
    order.write_text("*SkyrimCoopStart.esp\n")


def main():
    package = Path(sys.argv[1]) if len(sys.argv) > 1 else max((ROOT / "build/package").glob("*.zip"),
                                                                 key=lambda path: path.stat().st_mtime)
    ct.host = Game("", install=GAME, prefix=HOME / "prefix", display=":105")
    ct.games = (ct.host, ct.guest)
    if ct.prefix_processes():
        ct.stop_game()
    shutil.rmtree(ct.host.prefix, ignore_errors=True)
    clean_game()
    ct.prepare_prefix(bundled_start=False)  # package start is enabled by hand below, as a player does
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
        time.sleep(30)  # the main menu, and any late failure
        problems = [line for line in plugin_log.read_text(errors="replace").splitlines()
                    if "[error]" in line or "[critical]" in line]
        if launcher.poll() is not None or not ct.prefix_processes():
            raise TestFailure("the game exited")
        shot = HOME / "main-menu.png"
        subprocess.run(["import", "-display", ct.host.display, "-window", "root", str(shot)], check=False)
        print(f"PASS: plugin loaded, UI from the package; {len(problems)} error lines; screenshot {shot}")
        for line in problems[:20]:
            print("  ", line)
    finally:
        ct.stop_game()


if __name__ == "__main__":
    main()
