#!/usr/bin/env python3
"""Publish, sync, load, boot and unload the package through sms (docs/design/launcher.md). Run it with
`scripts/duo.py run -- python3 scripts/package_check.py [ZIP]`; SKYRIM_MOD_SYNC names the sms binary."""
import hashlib
import json
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
from local_paths import setting

ct = Session()

HOME = ROOT / "build/clean-install"
GAME = HOME / "game"
LIST = HOME / "list"
SMS = Path(setting("SKYRIM_MOD_SYNC", shutil.which("sms") or ROOT.parent / "SkyrimModSync/target/release/sms"))
# What a clean game must not have: SKSE, its plugins, ours, the vendor mods, our old launcher.
OURS = ("Debug.log", "Logs", "SKSE", "Scripts", "SkyrimCoop", "SkyrimCoopStart.esp", "SkyrimCoopLauncher.exe",
        "SkyrimSELauncher.bethesda.exe", "unofficial skyrim special edition patch", "alternate start - live another life",
        ".skyrim-mod-sync")
PLUGINS = ["*unofficial skyrim special edition patch.esp", "*alternate start - live another life.esp", "*SkyrimCoopStart.esp"]


def hardlink(source, dest):
    os.link(os.path.realpath(source), dest)  # the file a link names: sms refuses links in a game folder


def link_tree(source, dest):
    """Hardlinks (the Steam library and build/ share a drive)."""
    if source.is_dir():
        shutil.copytree(source, dest, copy_function=hardlink, symlinks=False)
    else:
        hardlink(source, dest)


def clean_game():
    """The Steam install's files, without SKSE, its plugins, anything of ours; Bethesda's launcher kept."""
    if GAME.exists():
        shutil.rmtree(GAME)
    (GAME / "Data").mkdir(parents=True)
    for source in STEAM_GAME.iterdir():
        if source.name != "Data" and not source.name.startswith(OURS) and not source.name.startswith("skse64"):
            link_tree(source, GAME / source.name)
    bethesda = STEAM_GAME / "SkyrimSELauncher.bethesda.exe"
    if bethesda.exists():  # the old packages renamed Bethesda's launcher there
        (GAME / "SkyrimSELauncher.exe").unlink(missing_ok=True)
        hardlink(bethesda, GAME / "SkyrimSELauncher.exe")
    for source in (STEAM_GAME / "Data").iterdir():
        if not source.name.startswith(OURS):
            link_tree(source, GAME / "Data" / source.name)


def mod_list(package):
    """A mod list whose two mods are local folders: the vendor files (SKSE at the root, Data/) and our zip."""
    if LIST.exists():
        shutil.rmtree(LIST)
    with zipfile.ZipFile(package) as archive:
        archive.extractall(LIST / "SkyrimCoop")
    vendor = LIST / "Vendor"
    vendor.mkdir(parents=True)
    root = []
    for source in RUNTIME_VENDOR.iterdir():
        if source.name == "Data":
            link_tree(source, vendor / "Data")
        elif source.name.startswith("skse64_") and source.suffix in (".dll", ".exe"):
            hardlink(source, vendor / source.name)
            root.append(source.name)
    (LIST / "modlist.json").write_text(json.dumps({
        "name": "SkyrimCoop playtest",
        "mods": [{"name": "Vendor", "local": "Vendor", "root": root},
                 {"name": "SkyrimCoop", "local": "SkyrimCoop"}],
        "plugins": [p.lstrip("*") for p in PLUGINS],
    }, indent=1))


def sms(*args):
    env = dict(os.environ, SMS_CONFIG_DIR=str(HOME / "sms"))
    result = subprocess.run([str(SMS), "--json", *args], env=env, capture_output=True, text=True)
    out = json.loads(result.stdout.strip().splitlines()[-1]) if result.stdout.strip() else {}
    if result.returncode != 0:
        raise TestFailure(f"sms {' '.join(args)}: {out.get('message') or result.stderr.strip()}")
    return out


def tree(root):
    """Every file under root: size, inode and content (a same-size rewrite in place must show)."""
    def digest(path):
        value = hashlib.sha256()
        with open(path, "rb") as file:
            while block := file.read(1 << 20):
                value.update(block)
        return value.hexdigest()
    return {str(p.relative_to(root)): (p.stat().st_size, p.stat().st_ino, digest(p)) for p in root.rglob("*") if p.is_file()}


def settings():
    """The settings files load rewrites: their bytes, None for one that does not exist."""
    files = [ct.load_order_file(), ct.load_order_file().with_name("loadorder.txt")]
    return {str(path): path.read_bytes() if path.exists() else None for path in files}


def main():
    if not SMS.is_file():
        raise TestFailure(f"no sms binary at {SMS}: build SkyrimModSync (cargo build --release) or set SKYRIM_MOD_SYNC")
    package = Path(sys.argv[1]) if len(sys.argv) > 1 else max(
        (p for p in (ROOT / "build/package").glob("SkyrimCoop-*.zip") if not p.stem.endswith("-full")),
        key=lambda path: path.stat().st_mtime)
    ct.host = Game("", install=GAME, prefix=HOME / "prefix", display=":99")
    ct.games = (ct.host, ct.guest)
    if ct.prefix_processes():
        ct.stop_game()
    shutil.rmtree(ct.host.prefix, ignore_errors=True)
    shutil.rmtree(HOME / "sms", ignore_errors=True)
    shutil.rmtree(HOME / "store", ignore_errors=True)
    clean_game()
    ct.prepare_prefix(bundled_start=False)
    mod_list(package)

    local = ct.load_order_file().parent
    sms("setup", "--game", str(GAME), "--store", str(HOME / "store"), "--local", str(local),
        "--documents", str(ct.host.my_games))
    published = sms("host", "publish", str(LIST / "modlist.json"))
    log(f"Published {package.name}: {published['files']} files, {published['from_host']} from the host")
    env = dict(os.environ, SMS_CONFIG_DIR=str(HOME / "sms"))
    serve = subprocess.Popen([str(SMS), "--json", "host", "serve", "--bind", "127.0.0.1:41894"], env=env,
                             stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, text=True)
    try:
        serving = json.loads(serve.stdout.readline())
        synced = sms("sync", serving["code"], "--peer-address", "127.0.0.1:41894")
        log(f"Synced: {synced['files_fetched']} files")
    finally:
        serve.terminate()
        serve.wait()
    before = tree(GAME)
    settings_before = settings()
    loaded = sms("load")
    log(f"Loaded: {loaded['placed']} files placed, {loaded['set_aside']} set aside")
    if (GAME / "SkyrimSELauncher.exe").read_bytes() != (RUNTIME_VENDOR / "skse64_loader.exe").read_bytes():
        raise TestFailure("Steam's launcher slot does not hold SKSE's loader")

    ct.ensure_virtual_display()
    env = {key: value for key, value in ct.game_env().items()
           if key not in ("SKYRIMCOOP_UI_DIR", "SKYRIMCOOP_LOG_LEVEL", "SKYRIMCOOP_NO_CRASH_REPORTS")}
    plugin_log = GAME / "Data/SKSE/Plugins/logs/tp_client.log"
    log(f"Booting {package.name} from {GAME} through SkyrimSELauncher.exe (SKSE's loader while loaded)")
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
        order = ct.load_order_file().read_text(errors="replace").splitlines()
        if order != PLUGINS:
            raise TestFailure(f"the load did not write the pack's plugins: Plugins.txt is {order!r}")
        time.sleep(30)  # the main menu, and any late failure
        problems = [line for line in plugin_log.read_text(errors="replace").splitlines()
                    if "[error]" in line or "[critical]" in line]
        if launcher.poll() is not None or not ct.prefix_processes():
            raise TestFailure("the game exited")
        shot = HOME / "main-menu.png"
        subprocess.run(["import", "-display", ct.host.display, "-window", "root", str(shot)], check=False)
    finally:
        ct.stop_game()
    # The plugin's own logs are the game's output, not part of the folder sms restores.
    shutil.rmtree(GAME / "Data/SKSE/Plugins/logs", ignore_errors=True)
    sms("unload")
    after = tree(GAME)
    if after != before:
        changed = sorted(set(after.items()) ^ set(before.items()))[:10]
        raise TestFailure(f"unload left the game folder changed: {changed}")
    if settings() != settings_before:
        raise TestFailure("unload did not restore Plugins.txt and loadorder.txt")
    print(f"PASS: published, synced, loaded, booted (UI from the package), unloaded to the clean folder; "
          f"{len(problems)} error lines; screenshot {shot}")
    for line in problems[:20]:
        print("  ", line)


if __name__ == "__main__":
    main()
