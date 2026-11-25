#!/usr/bin/env python3
"""Installs the package (scripts/package.sh) with its requirements (build/runtime-vendor: SKSE, Address Library,
USSEP, Alternate Start) into a clean game folder with a new Proton prefix, as docs/install.md describes, and boots it
through SKSE. A player build has no test harness, so this is a boot check read from the plugin's log.
Run it with `scripts/duo.py run -- python3 scripts/package_check.py [ZIP]` (the shared duo display)."""
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
from coop_harness.common import ROOT, STEAM_GAME, RUNTIME, PROTON, RUNTIME_VENDOR
from coop_harness.errors import TestFailure
from coop_harness.utilities import log

ct = Session()

HOME = ROOT / "build/clean-install"
GAME = HOME / "game"
# What a clean game must not have: SKSE, its plugins, ours, the vendor mods, our old launcher.
OURS = ("Debug.log", "Logs", "SKSE", "Scripts", "SkyrimCoop", "SkyrimCoopStart.esp", "SkyrimCoopLauncher.exe",
        "SkyrimSELauncher.bethesda.exe", "unofficial skyrim special edition patch", "alternate start - live another life",
        ".skyrim-mod-sync")
PLUGINS = ["*unofficial skyrim special edition patch.esp", "*alternate start - live another life.esp", "*SkyrimCoopStart.esp"]


def hardlink(source, dest):
    os.link(os.path.realpath(source), dest)  # the file a link names, not the link


def link_tree(source, dest):
    """Hardlinks (the Steam library and build/ share a drive)."""
    if source.is_dir():
        shutil.copytree(source, dest, copy_function=hardlink, symlinks=False, dirs_exist_ok=True)
    else:
        hardlink(source, dest)


def clean_game():
    """The Steam install's files, without SKSE, its plugins or anything of ours."""
    if GAME.exists():
        shutil.rmtree(GAME)
    (GAME / "Data").mkdir(parents=True)
    for source in STEAM_GAME.iterdir():
        if source.name != "Data" and not source.name.startswith(OURS) and not source.name.startswith("skse64"):
            link_tree(source, GAME / source.name)
    for source in (STEAM_GAME / "Data").iterdir():
        if not source.name.startswith(OURS):
            link_tree(source, GAME / "Data" / source.name)


def install(package):
    """docs/install.md by hand: the requirements and our archive into Data/, SKSE beside the game, the plugins on."""
    for source in RUNTIME_VENDOR.iterdir():
        if source.name == "Data":
            link_tree(source, GAME / "Data")
        elif source.name.startswith("skse64_") and source.suffix in (".dll", ".exe"):
            hardlink(source, GAME / source.name)
    with zipfile.ZipFile(package) as archive:
        archive.extractall(GAME / "Data")
    ct.load_order_file().parent.mkdir(parents=True, exist_ok=True)
    ct.load_order_file().write_text("\n".join(PLUGINS) + "\n")


def main():
    package = Path(sys.argv[1]) if len(sys.argv) > 1 else max(
        (p for p in (ROOT / "build/package").glob("SkyrimCoop-*.zip") if not p.stem.endswith("-full")),
        key=lambda path: path.stat().st_mtime)
    ct.host = Game("", install=GAME, prefix=HOME / "prefix", display=":99")
    ct.games = (ct.host, ct.guest)
    if ct.prefix_processes():
        ct.stop_game()
    shutil.rmtree(ct.host.prefix, ignore_errors=True)
    clean_game()
    ct.prepare_prefix(bundled_start=False)
    install(package)

    ct.ensure_virtual_display()
    env = {key: value for key, value in ct.game_env().items()
           if key not in ("SKYRIMCOOP_UI_DIR", "SKYRIMCOOP_LOG_LEVEL", "SKYRIMCOOP_NO_CRASH_REPORTS")}
    plugin_log = GAME / "Data/SKSE/Plugins/logs/tp_client.log"
    log(f"Booting {package.name} from {GAME} through skse64_loader.exe")
    output = open(HOME / "proton.log", "w")
    loader = subprocess.Popen([str(RUNTIME / "run"), "--", str(PROTON / "proton"), "waitforexitandrun",
                               str(GAME / "skse64_loader.exe")], env=env, cwd=GAME, stdout=output,
                              stderr=subprocess.STDOUT, start_new_session=True)
    try:
        ui = ct.wait_for("the plugin's UI is ready", lambda: plugin_log.exists() and next(
            (line for line in plugin_log.read_text(errors="replace").splitlines() if "UI: RmlUi" in line), None), 240, loader)
        log(ui)
        installed = str(GAME / "Data/SKSE/Plugins/SkyrimCoop/ui")
        if not ui.rstrip().replace("\\", "/").endswith(installed):
            raise TestFailure(f"the UI does not come from the installed files ({installed})")
        time.sleep(30)  # the main menu, and any late failure
        problems = [line for line in plugin_log.read_text(errors="replace").splitlines()
                    if "[error]" in line or "[critical]" in line]
        if loader.poll() is not None or not ct.prefix_processes():
            raise TestFailure("the game exited")
        shot = HOME / "main-menu.png"
        subprocess.run(["import", "-display", ct.host.display, "-window", "root", str(shot)], check=False)
    finally:
        ct.stop_game()
    print(f"PASS: installed and booted (UI from the package); {len(problems)} error lines; screenshot {shot}")
    for line in problems[:20]:
        print("  ", line)


if __name__ == "__main__":
    main()
