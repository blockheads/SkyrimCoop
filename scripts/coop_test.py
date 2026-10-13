#!/usr/bin/env python3
"""One-command co-op tests.

  coop_test.py net    [--bots N --seconds S --hz R]   no Skyrim, ~5s: N bots must see each other
  coop_test.py launch [--cell CELL | --save NAME]     fast-boot Skyrim straight into a cell, leave it running
  coop_test.py game   [--cell CELL --seconds S]       fast-boot, host, connect a bot, verify the bot's
                                                      actor exists in the engine and moves; then quit
  coop_test.py duo    [--keep-running --visible]      two real games: host + client, each must show the other
  coop_test.py scenario [names] [--tag a,b] [--shuffle --repeat N] sync scenarios against a running duo; each
                                                      starts from the same baseline and is cleaned up after
  coop_test.py reload                                 swap the current build into a running duo and reconnect
  coop_test.py stop                                   kill the test Skyrim instance(s)
  coop_test.py screenshot [out.png]                   capture the off-screen game display
  coop_test.py console <command...>                   run a console command in the test game
  coop_test.py status [-f]                            both games side by side: health, death, weapon, casting
  coop_test.py crash [--instance client]             newest crash per game: engine state (CrashLoggerSSE) + our last engine changes
  coop_test.py repro NAME [--heap-history] [--follow] cause a known crash (REPROS) and print its report;
                                                      --vanilla: the same recipe without our plugin (is it us?);
                                                      --heap-history: who allocated/freed the memory involved;
                                                      --follow: a second run watching the crash's addresses from boot;
                                                      --quarantine: poison freed memory, a use after free faults at once;
                                                      --guard 0x130: page-heap that block size, any access after free faults;
                                                      --probe 13408,36559: engine functions' calls into the breadcrumbs
  coop_test.py watch                                  live view of the off-screen game in a window
  coop_test.py setup-display                          one-time: install the off-screen X server (sudo)

The game runs on an off-screen X display (COOP_DISPLAY, default :99; the GPU still renders)
so it never covers the desktop; pass --visible to launch/game/duo to show it instead. `duo` runs a second game ("client") on
display :100 with its own prefix; watch/screenshot/console/stop take --instance client.

The games are driven through the plugin's test harness (Code/client/TestHarness: a loopback
command socket and control files in Data/SKSE/Plugins/SkyrimCoopHotReload<-instance>), which only
test builds have: `scripts/xmake-msvc.sh f -m dev` configures one (--test_harness=y), then
`scripts/xmake-msvc.sh build -j16 SkyrimTogetherClientDLL` and `scripts/xmake-msvc.sh build
SkyrimCoopLoader`. A player build has neither the harness nor the hot reload.

Skyrim runs in its own Proton prefix under build/coop-test (never your normal saves or
settings): no intro, windowed, low preset, and `coc <cell>` from the main menu. Paths
default to this machine; override with SKYRIM_GAME_DIR, SKYRIM_PROTON_DIR,
SKYRIM_RUNTIME_DIR. Every game run keeps its logs in build/coop-test/runs/<time>/.
"""
import argparse
import json
import math
import os
import random
from pathlib import Path
import re
import shutil
import signal
import socket
import subprocess
import sys
import time
import uuid

ROOT = Path(__file__).resolve().parents[1]
STEAM_LIBRARY = Path("/media/bighass/4d2c4781-34de-41a5-8939-e35551bc9a5d1/SteamLibrary/steamapps/common")
GAME = Path(os.environ.get("SKYRIM_GAME_DIR",
                           "/media/bighass/1f640250-798d-4f2f-87f2-c442109fc5d0/SteamLibrary/steamapps/common/Skyrim Special Edition"))
PROTON = Path(os.environ.get("SKYRIM_PROTON_DIR", STEAM_LIBRARY / "Proton 10.0"))
RUNTIME = Path(os.environ.get("SKYRIM_RUNTIME_DIR", STEAM_LIBRARY / "SteamLinuxRuntime_sniper"))
STEAM = Path(os.environ.get("STEAM_COMPAT_CLIENT_INSTALL_PATH", Path.home() / ".steam/steam"))
TEST_HOME = ROOT / "build/coop-test"
PREFIX = TEST_HOME / "prefix-host"
DLL = Path(os.environ.get("COOP_DLL", ROOT / "build/windows/x64/dev/SkyrimTogetherClient.dll"))
# Resident SKSE plugin that loads the client from a copy, so `reload` can swap in a rebuild
# (Code/hotreload/Loader.cpp). Without it the client is deployed as the SKSE plugin itself.
LOADER = DLL.with_name("SkyrimCoopLoader.dll")
# The harness without SkyrimCoop (Code/vanilla_harness): `repro --vanilla` and `launch --vanilla`.
VANILLA_HARNESS = DLL.with_name("SkyrimCoopVanillaHarness.dll")
BOT = ROOT / "build/linux/x86_64/release/fake_player"
PLUGINS = GAME / "Data/SKSE/Plugins"
CONTROL = PLUGINS / "SkyrimCoopHotReload"
MY_GAMES = PREFIX / "pfx/drive_c/users/steamuser/Documents/My Games/Skyrim Special Edition"
START = time.monotonic()
INSTANCE = ""  # "" = host (defaults above); "client" = second game for `duo`
VIRTUAL_DISPLAY = os.environ.get("COOP_DISPLAY", ":99")


def select_instance(name):
    """Points the module paths at one game instance. Both share the game install, so the plugin
    keeps their control files and logs apart via SKYRIMCOOP_INSTANCE."""
    global INSTANCE, PREFIX, CONTROL, MY_GAMES, VIRTUAL_DISPLAY
    INSTANCE = name
    PREFIX = TEST_HOME / f"prefix-{name or 'host'}"
    CONTROL = PLUGINS / ("SkyrimCoopHotReload" + (f"-{name}" if name else ""))
    MY_GAMES = PREFIX / "pfx/drive_c/users/steamuser/Documents/My Games/Skyrim Special Edition"
    base = int(os.environ.get("COOP_DISPLAY", ":99").lstrip(":"))
    VIRTUAL_DISPLAY = f":{base + (1 if name else 0)}"


class TestFailure(RuntimeError):
    pass


def log(message):
    print(f"[{time.monotonic() - START:6.1f}s] {message}", flush=True)


def atomic_write(path, text):
    temporary = path.with_name(path.name + ".tmp")
    temporary.write_text(text)
    os.replace(temporary, path)


def set_ini(path, values):
    """Set [section] key=value pairs, keeping everything else in the file."""
    lines = path.read_text(errors="replace").splitlines() if path.exists() else []
    for section, entries in values.items():
        header = f"[{section}]".lower()
        start = next((i for i, line in enumerate(lines) if line.strip().lower() == header), None)
        if start is None:
            lines += ["", f"[{section}]"]
            start = len(lines) - 1
        end = next((i for i in range(start + 1, len(lines)) if lines[i].strip().startswith("[")), len(lines))
        for key, value in entries.items():
            found = next((i for i in range(start + 1, end)
                          if lines[i].split("=", 1)[0].strip().lower() == key.lower()), None)
            if found is None:
                lines.insert(end, f"{key}={value}")
                end += 1
            else:
                lines[found] = f"{key}={value}"
    path.write_text("\r\n".join(lines) + "\r\n")


VISIBLE = False  # --visible: show the game on the desktop instead of the virtual display


def game_env():
    env = dict(os.environ)
    env.update(SteamAppId="489830", SteamGameId="489830", STEAM_COMPAT_DATA_PATH=str(PREFIX),
               STEAM_COMPAT_CLIENT_INSTALL_PATH=str(STEAM), WINEDEBUG=env.get("WINEDEBUG", "-all"),
               SKYRIMCOOP_LOG_LEVEL=env.get("SKYRIMCOOP_LOG_LEVEL", "debug"),
               # The UI straight from the repo (Wine's Z: is /): edits show up in game within a second.
               SKYRIMCOOP_UI_DIR=env.get("SKYRIMCOOP_UI_DIR", "Z:" + str(UI_SOURCE).replace("/", "\\")))
    if INSTANCE:
        env["SKYRIMCOOP_INSTANCE"] = INSTANCE
    if not VISIBLE:
        env["DISPLAY"] = VIRTUAL_DISPLAY
    return env


def prefix_processes():
    """PIDs started for our test prefix (matched by environment, so nothing else is touched)."""
    marker = f"STEAM_COMPAT_DATA_PATH={PREFIX}".encode()
    pids = []
    for entry in Path("/proc").iterdir():
        if entry.name.isdigit() and int(entry.name) != os.getpid():
            try:
                if marker in (entry / "environ").read_bytes().split(b"\0"):
                    pids.append(int(entry.name))
            except OSError:
                pass
    return pids


def stop_game():
    subprocess.run(["pkill", "-x", BOT.name[:15]], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)  # a --keep-running bot
    pids = prefix_processes()
    for sig in (signal.SIGTERM, signal.SIGKILL):
        for pid in pids:
            try:
                os.kill(pid, sig)
            except ProcessLookupError:
                pass
        deadline = time.monotonic() + 5
        while time.monotonic() < deadline and (pids := prefix_processes()):
            time.sleep(0.2)
        if not pids:
            return True
    return not prefix_processes()


def check_prerequisites(need_bot):
    required = [GAME / "SkyrimSE.exe", GAME / "skse64_loader.exe", PROTON / "proton", RUNTIME / "run", DLL]
    if need_bot:
        required.append(BOT)
    missing = [str(path) for path in required if not path.exists()]
    if missing:
        hint = ("\nBuild the DLL: scripts/xmake-msvc.sh f -m dev && scripts/xmake-msvc.sh build -j16 SkyrimTogetherClientDLL && scripts/xmake-msvc.sh build SkyrimCoopLoader"
                "\nBuild the bot: XMAKE_CONFIGDIR=build/hotreload-native-config xmake build -j16 fake_player")
        raise TestFailure("Missing:\n  " + "\n  ".join(missing) + hint)
    if subprocess.run(["pgrep", "-x", "steam"], capture_output=True).returncode != 0:
        raise TestFailure("Steam must be running (Skyrim's DRM needs it).")
    if (PLUGINS / "skyrim_coop_hooks.dll").exists():
        raise TestFailure(f"Remove the old relay plugin first: {PLUGINS / 'skyrim_coop_hooks.dll'}")


def copy_if_changed(source, target):
    """Replaces target atomically (a running game keeps the file it mapped); True if it changed."""
    target.parent.mkdir(parents=True, exist_ok=True)
    stat = source.stat()
    if target.exists() and target.stat().st_size == stat.st_size and target.stat().st_mtime >= stat.st_mtime:
        return False
    temporary = target.with_name(target.name + ".tmp")
    shutil.copy2(source, temporary)
    os.replace(temporary, target)
    log(f"Deployed {source.relative_to(ROOT)} ({stat.st_size // 1024} KB)")
    return True


UI_SOURCE = ROOT / "Code/client/Ui/assets"


def deploy_ui():
    """The in-game UI's documents, styles and fonts: Plugins/SkyrimCoop/ui (UiService)."""
    for source in sorted(UI_SOURCE.rglob("*")):
        if source.is_file():
            copy_if_changed(source, PLUGINS / "SkyrimCoop/ui" / source.relative_to(UI_SOURCE))


def require_test_harness():
    """These scripts drive the plugin through its test harness (Code/client/TestHarness), which only
    builds configured with --test_harness=y have; a player build would never answer."""
    if b"SkyrimCoopHotReload" not in DLL.read_bytes():
        raise TestFailure(f"{DLL} has no test harness. Configure with scripts/xmake-msvc.sh f -m dev "
                          "(it turns on --test_harness=y) and build again.")


def deploy_dll():
    """With the loader built: Plugins/SkyrimCoopLoader.dll loads Plugins/SkyrimCoop/<client>.
    Otherwise the client is the SKSE plugin. Never both: the client would load twice."""
    require_test_harness()
    deploy_ui()
    direct = PLUGINS / "SkyrimTogetherClient.dll"
    if LOADER.exists():
        direct.unlink(missing_ok=True)
        copy_if_changed(LOADER, PLUGINS / LOADER.name)
        copy_if_changed(DLL, PLUGINS / "SkyrimCoop" / DLL.name)
    else:
        (PLUGINS / LOADER.name).unlink(missing_ok=True)
        copy_if_changed(DLL, direct)


# CrashLoggerSSE (github.com/alandtse/CrashLoggerSSE, GPL-3 with a modding exception): on a crash it
# writes My Games/Skyrim Special Edition/SKSE/crash-*.log in the game's prefix with the call stack
# symbolized (our DLL from its PDB), every register and stack slot decoded into the object it
# points to (form IDs and names, NiAVObject names, RTTI types) and the loaded modules. Pinned and
# checked; cached under build/third_party.
CRASH_LOGGER_VERSION = "1.25.0"
CRASH_LOGGER_SHA256 = "ce8592d60a2394bc05874d4cc759d3513686c3e26f841fe831a98c9d66fa979c"
CRASH_LOGGER_FILES = ("CrashLogger.dll", "CrashLogger.pdb", "msdia140.dll")
CRASH_LOGGER_INI = """[Debug]
Auto Open Crash Log = false
Auto Upload Crash Log = false
Crash Log Write Minidump = false
Max Crash Logs = 50
"""


def deploy_crash_logger():
    cache = ROOT / f"build/third_party/crashlogger-{CRASH_LOGGER_VERSION}"
    if not all((cache / name).exists() for name in CRASH_LOGGER_FILES):
        archive = cache.with_suffix(".7z")
        cache.mkdir(parents=True, exist_ok=True)
        url = (f"https://github.com/alandtse/CrashLoggerSSE/releases/download/v{CRASH_LOGGER_VERSION}/"
               f"CrashLogger_{CRASH_LOGGER_VERSION}.7z")
        log(f"Downloading CrashLoggerSSE {CRASH_LOGGER_VERSION}")
        subprocess.run(["curl", "-sSfL", "-o", str(archive), url], check=True)
        import hashlib
        digest = hashlib.sha256(archive.read_bytes()).hexdigest()
        if digest != CRASH_LOGGER_SHA256:
            archive.unlink()
            raise TestFailure(f"CrashLoggerSSE archive checksum mismatch: {digest}")
        subprocess.run(["7z", "e", "-y", f"-o{cache}", str(archive), "SKSE/Plugins/*"], check=True,
                       stdout=subprocess.DEVNULL)
    for name in CRASH_LOGGER_FILES:
        copy_if_changed(cache / name, PLUGINS / name)
    # The plugin fills in every other key (and its comment) on start.
    (PLUGINS / "CrashLogger.ini").write_text(CRASH_LOGGER_INI)


def crash_logs(name):
    """CrashLoggerSSE's logs for a game instance, oldest first."""
    directory = TEST_HOME / f"prefix-{name or 'host'}/pfx/drive_c/users/steamuser/Documents/My Games/Skyrim Special Edition/SKSE"
    return sorted(directory.glob("crash-*.log"))


def plugin_log(name):
    return PLUGINS / f"logs/tp_client{'-' + name if name else ''}.log"


def settle_crash_log(crash_log, timeout=30):
    """Waits for CrashLoggerSSE to finish writing (the file stops growing for 2s, or the game is
    gone): reporting or stopping the game earlier cuts the log short."""
    deadline = time.monotonic() + timeout
    size, stable_since = -1, time.monotonic()
    while time.monotonic() < deadline:
        current = crash_log.stat().st_size if crash_log.exists() else 0
        if current != size:
            size, stable_since = current, time.monotonic()
        elif time.monotonic() - stable_since >= 2 or not prefix_processes():
            return
        time.sleep(0.25)


def crash_report(name, crash_log, ours=True):
    """The crash's report; with `ours`, our plugin's log adds the breadcrumbs and heap history."""
    settle_crash_log(crash_log)
    sys.path.insert(0, str(ROOT / "scripts/re"))
    import crash_report as report
    return report.report(crash_log, plugin_log(name) if ours else None)


# Crashes seen before this run: wait_for fails at once on a new one, with its report.
KNOWN_CRASHES = {name: set(crash_logs(name)) for name in ("", "client")}
_next_crash_check = 0.0


def check_for_crash():
    global _next_crash_check
    if time.monotonic() < _next_crash_check:
        return
    _next_crash_check = time.monotonic() + 1.0
    for name in ("", "client"):
        new = [path for path in crash_logs(name) if path not in KNOWN_CRASHES[name]]
        if new:
            KNOWN_CRASHES[name].update(new)
            raise TestFailure(f"{name or 'host'} crashed:\n" + crash_report(name, new[-1]))


def command_crash(args):
    """The newest crash of each game (or --instance), as one report."""
    names = [INSTANCE] if args.instance else ["", "client"]
    for name in names:
        logs = crash_logs(name)
        if not logs:
            print(f"{name or 'host'}: no crash logs")
            continue
        print(f"===== {name or 'host'} ({len(logs)} crash logs) =====")
        print(crash_report(name, logs[-1]))
        print()


def remove_plugin():
    (PLUGINS / "SkyrimTogetherClient.dll").unlink(missing_ok=True)
    (PLUGINS / LOADER.name).unlink(missing_ok=True)
    (PLUGINS / VANILLA_HARNESS.name).unlink(missing_ok=True)


VIRTUAL_DISPLAY = os.environ.get("COOP_DISPLAY", ":99")


DUMMY_XORG_CONFIG = "xorg-coop-dummy.conf"  # scripts/coop/<this>, installed to /etc/X11 by setup-display


def ensure_virtual_display():
    """Starts an off-screen X server for the game unless one is already running. The GPU still
    renders (Vulkan presents to it); only the window is invisible. Prefers Xorg's dummy driver,
    which reports a real 60 Hz mode: on Xvfb (0 Hz) DXVK divides by zero in Skyrim's renderer init."""
    socket = Path("/tmp/.X11-unix") / f"X{VIRTUAL_DISPLAY.lstrip(':')}"
    if socket.exists():
        return
    log_file = ROOT / "build/coop-test/xorg-display.log"
    log_file.parent.mkdir(parents=True, exist_ok=True)
    if (Path("/etc/X11") / DUMMY_XORG_CONFIG).exists() and Path("/usr/lib/xorg/Xorg").exists():
        # The real binary: the Xorg wrapper only lets console users start a server.
        command = ["/usr/lib/xorg/Xorg", VIRTUAL_DISPLAY, "-config", DUMMY_XORG_CONFIG, "-noreset",
                   "-nolisten", "tcp", "-logfile", str(log_file)]
    else:
        log("Using Xvfb (0 Hz, Skyrim will crash); run `coop_test.py setup-display` once for the dummy Xorg")
        command = ["Xvfb", VIRTUAL_DISPLAY, "-screen", "0", "1920x1080x24", "-nolisten", "tcp"]
    subprocess.Popen(command, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, start_new_session=True)
    deadline = time.monotonic() + 10
    while not socket.exists():
        if time.monotonic() > deadline:
            raise TestFailure(f"Off-screen X server did not start on {VIRTUAL_DISPLAY} (see {log_file})")
        time.sleep(0.1)


def setup_display():
    """One-time: the dummy video driver and its config (needs sudo)."""
    if not Path("/usr/lib/xorg/modules/drivers/dummy_drv.so").exists():
        subprocess.run(["sudo", "apt-get", "install", "-y", "xserver-xorg-video-dummy"], check=True)
    subprocess.run(["sudo", "cp", str(ROOT / "scripts/coop" / DUMMY_XORG_CONFIG), "/etc/X11/"], check=True)
    log(f"Installed /etc/X11/{DUMMY_XORG_CONFIG}")


def prepare_prefix():
    if not (PREFIX / "pfx/drive_c/users/steamuser").exists():
        log("Creating the test Proton prefix (first run only, ~30s)...")
        PREFIX.mkdir(parents=True, exist_ok=True)
        subprocess.run([str(RUNTIME / "run"), "--", str(PROTON / "proton"), "run", "wineboot", "-u"],
                       env=game_env(), cwd=GAME, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                       timeout=300)
    MY_GAMES.mkdir(parents=True, exist_ok=True)
    skyrim_ini, prefs_ini = MY_GAMES / "Skyrim.ini", MY_GAMES / "SkyrimPrefs.ini"
    if not skyrim_ini.exists():
        shutil.copy(GAME / "Skyrim_Default.ini", skyrim_ini)
    if not prefs_ini.exists():
        shutil.copy(GAME / "Low.ini", prefs_ini)
    set_ini(skyrim_ini, {"General": {
        "sIntroSequence": "",                     # no Bethesda logo videos
        "sStartingConsoleCommand": "",  # 1.6.1170 only runs this at shutdown; the plugin runs boot.txt instead
        "bAlwaysActive": "1",                      # keep simulating without focus
    }, "Camera": {"fAutoVanityModeDelay": "1000000"},  # no idle camera orbiting the player
        # Tutorial popups (the map's, "Magicka Low") swallow all input until closed. A Skyrim.ini
        # setting (gIni_bShowTutorials_Interface): SkyrimPrefs.ini ignores it.
        "Interface": {"bShowTutorials": "0"}})
    set_ini(prefs_ini, {"Display": {"bFull Screen": "0", "bBorderless": "0",
                                    "iSize W": "1280", "iSize H": "720",
                                    "iLocation X": "0", "iLocation Y": "0"},
                            # Silent test games. (Muting via PulseAudio would be remembered per app
                            # name and silence normal Skyrim too.)
                            "AudioMenu": {"fAudioMasterVolume": "0.0000"},
                            # Subtitles on, as players have them (Low.ini turns them off).
                            "Interface": {"bDialogueSubtitles": "1", "bGeneralSubtitles": "1"}})


# The plugin's command socket (CommandServer.cpp): one JSON line per request, answered within a
# frame. Port and pid are in <control dir>/rpc.json. Falls back to the control files (250ms
# poll) for an older DLL or when the socket does not answer.
_rpc_connections = {}


def rpc(request, timeout=3.0):
    """Sends one request to the selected game; returns the decoded response, or None."""
    key = str(CONTROL)
    try:
        info = json.loads((CONTROL / "rpc.json").read_text())
    except (OSError, ValueError):
        return None
    connection = _rpc_connections.get(key)
    if connection and connection[0] != (info.get("pid"), info.get("port")):
        connection[1].close()
        connection = None
    try:
        if not connection:
            sock = socket.create_connection(("127.0.0.1", info["port"]), timeout=1.0)
            connection = _rpc_connections[key] = [(info.get("pid"), info.get("port")), sock, sock.makefile("rb")]
        connection[1].settimeout(timeout)
        connection[1].sendall((json.dumps(request) + "\n").encode())
        line = connection[2].readline()
        if not line:
            raise OSError("closed")
        return json.loads(line)
    except (OSError, ValueError, KeyError):
        if connection:
            connection[1].close()
        _rpc_connections.pop(key, None)
        return None


def read_status():
    response = rpc({"cmd": "status"})
    if response and response.get("ok"):
        return response["status"]
    try:
        status = json.loads((CONTROL / "status.json").read_text())
    except (OSError, ValueError):
        return None
    heartbeat = status.get("heartbeat_ms", 0) if isinstance(status, dict) else 0
    if not isinstance(heartbeat, (int, float)) or abs(time.time() * 1000 - heartbeat) > 3000:
        return None  # stale file from an earlier game process
    return status


def wait_for(description, predicate, timeout, launcher=None):
    log(description + "...")
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        result = predicate()
        if result:
            return result
        if launcher is not None and launcher.poll() is not None and not prefix_processes():
            raise TestFailure(f"Skyrim exited while waiting: {description}")
        check_for_crash()
        time.sleep(0.05)
    raise TestFailure(f"Timed out after {timeout:.0f}s: {description}")


def launch(args, run_dir):
    if prefix_processes():
        raise TestFailure("The test Skyrim is already running. Use `coop_test.py stop` first.")
    if not VISIBLE:
        ensure_virtual_display()
    vanilla = getattr(args, "vanilla", False)
    if vanilla:  # A/B baseline: same prefix and settings, SKSE without our plugin
        remove_plugin()
        if VANILLA_HARNESS.exists():  # runs the harness's commands, nothing of ours
            copy_if_changed(VANILLA_HARNESS, PLUGINS / VANILLA_HARNESS.name)
    else:
        (PLUGINS / VANILLA_HARNESS.name).unlink(missing_ok=True)
        deploy_dll()
    deploy_crash_logger()
    command = f"load {args.save}" if args.save else f"coc {args.cell}"
    prepare_prefix()
    CONTROL.mkdir(parents=True, exist_ok=True)
    for stale in ("status.json", "session.txt", "console.txt", "rpc.json", "request.txt"):
        (CONTROL / stale).unlink(missing_ok=True)
    atomic_write(CONTROL / "boot.txt", command + "\n")  # run by the plugin at the main menu
    log(f"Launching Skyrim (start command: {command!r})")
    output = open(run_dir / f"proton{'-' + INSTANCE if INSTANCE else ''}.log", "w")
    launcher = subprocess.Popen(
        [str(RUNTIME / "run"), "--", str(PROTON / "proton"), "waitforexitandrun",
         str(GAME / "skse64_loader.exe")],
        env=game_env(), cwd=GAME, stdout=output, stderr=subprocess.STDOUT, start_new_session=True)
    if vanilla and not VANILLA_HARNESS.exists():
        log(f"Vanilla run (no plugin): at the main menu, open the console (~) and type `{command}`")
        return launcher
    status = wait_for("Waiting for the plugin heartbeat (SKSE loaded our DLL)", read_status,
                      args.boot_timeout, launcher)
    log(f"Plugin alive in Skyrim (Windows PID {status['pid']})")
    wait_for(f"Waiting for the player to be in the world ({command})",
             lambda: (read_status() or {}).get("session", {}).get("world_ready"), args.world_timeout, launcher)
    log("Player is in the world")
    # The Survival Mode prompt opens on the first exterior and pauses the game until answered.
    run_console("set Survival_PlayerHasBeenPrompted to 1")
    return launcher


def collect_logs(run_dir):
    sources = {
        "status.json": CONTROL / "status.json",
        "tp_client.log": GAME / f"Data/SKSE/Plugins/logs/tp_client{'-' + INSTANCE if INSTANCE else ''}.log",
        "skse64.log": MY_GAMES / "SKSE/skse64.log",
        "skse64_loader.log": MY_GAMES / "SKSE/skse64_loader.log",
    }
    for name, path in sources.items():
        if path.exists():
            shutil.copy(path, run_dir / (f"{INSTANCE}-{name}" if INSTANCE else name))


def verify_bot_movement(bot_started_ms, seconds, tolerance):
    """Some remote actor must show fresh engine samples with real displacement."""
    deadline = time.monotonic() + seconds
    tracks = {}
    while time.monotonic() < deadline:
        status = read_status()
        if not status:
            raise TestFailure("Game heartbeat stopped while the bot was moving (crash or hang?).")
        for actor_id, sample in status.get("actors", {}).items():
            if sample.get("observed_ms", 0) < bot_started_ms or not sample.get("has_3d"):
                continue
            requested, actual = sample.get("requested", []), sample.get("actual", [])
            if len(actual) != 3 or not all(math.isfinite(v) for v in actual):
                continue
            track = tracks.setdefault(actor_id, {"first": actual, "samples": 0, "max_move": 0.0, "max_error": 0.0})
            track["samples"] += 1
            track["max_move"] = max(track["max_move"], math.dist(track["first"], actual))
            if len(requested) == 3:
                track["max_error"] = max(track["max_error"], math.dist(requested, actual))
        for actor_id, track in tracks.items():
            if track["samples"] >= 5 and track["max_move"] > 20 and track["max_error"] <= tolerance:
                return int(actor_id), track
        time.sleep(0.25)
    if not tracks:
        raise TestFailure("No remote actor with loaded 3D appeared in the engine. The bot connected but its "
                          "character was not spawned in-game (see tp_client.log).")
    detail = ", ".join(f"{int(k):08X}: {v['samples']} samples, moved {v['max_move']:.0f}u, "
                       f"engine error {v['max_error']:.1f}u" for k, v in tracks.items())
    raise TestFailure(f"Remote actor spawned but did not move as sent ({detail}).")


def command_net(args):
    if not BOT.exists():
        raise TestFailure(f"Bot not built: {BOT}")
    result = subprocess.run([str(BOT), "--net-test", "--bots", str(args.bots), "--duration", str(args.seconds),
                             "--tick-rate", str(args.hz)], capture_output=True, text=True)
    for line in (result.stdout + result.stderr).splitlines():
        if "net-test" in line or "[error]" in line:
            print(line)
    if result.returncode != 0:
        raise TestFailure("Network test failed (see above).")


def new_run_dir(kind):
    run_dir = TEST_HOME / "runs" / (time.strftime("%Y%m%d-%H%M%S") + "-" + kind)
    run_dir.mkdir(parents=True, exist_ok=True)
    return run_dir


def command_launch(args):
    check_prerequisites(need_bot=False)
    run_dir = new_run_dir("launch")
    try:
        launch(args, run_dir)
    finally:
        collect_logs(run_dir)
    log(f"Ready. Skyrim keeps running; stop it with: {sys.argv[0]} stop  (logs: {run_dir})")


def run_console(command, timeout=10):
    """Runs a console command in the test game (after the boot command) and waits for it."""
    response = rpc({"cmd": "console", "line": command}, timeout=timeout)
    if response and response.get("ok"):
        log(f"Console: {command}")
        return
    request_id = uuid.uuid4().hex[:12]
    atomic_write(CONTROL / "console.txt", f"{request_id} {command}\n")
    wait_for(f"Console: {command}", lambda: (read_status() or {}).get("console_id") == request_id, timeout)


def face_actor(actor_id):
    """Turns the player toward an actor so it is on screen (for screenshots / `watch`)."""
    status = read_status() or {}
    player = (status.get("player") or {}).get("position")
    actor = (status.get("actors", {}).get(str(actor_id), {}).get("actual")
             or ((status.get("world") or {}).get(str(actor_id)) or {}).get("placement", {}).get("position"))
    if not player:
        return
    if not actor:
        # Aiming at nothing casts wherever the player happens to face: say so instead.
        raise TestFailure(f"cannot face {actor_id:X}: not in this game's status (gone, or respawned under a new id?)")
    # Skyrim heading: degrees clockwise from north (+Y).
    heading = math.degrees(math.atan2(actor[0] - player[0], actor[1] - player[1])) % 360
    run_console(f"player.setangle z {heading:.0f}")
    # Level, not looking down after walking: aimed spells fly where the player looks.
    run_console("player.setangle x 0")


def command_game(args):
    check_prerequisites(need_bot=True)
    run_dir = new_run_dir("game")
    bot = None
    try:
        launch(args, run_dir)
        request = uuid.uuid4().hex
        atomic_write(CONTROL / "session.txt", request + "\n")

        def host_ready():
            session = (read_status() or {}).get("session", {})
            if session.get("request") != request:
                return None
            if session.get("state") == "failed":
                raise TestFailure(f"Hosting failed: {session.get('error')}")
            return session if session.get("state") == "ready" else None
        session = wait_for("Hosting (embedded server + host authentication)", host_ready, 60)
        log(f"Hosting on port {session['port']}")

        bot_started_ms = time.time() * 1000
        bot_log = open(run_dir / "bot.log", "w")
        bot = subprocess.Popen([str(BOT), "--fixture-dir", str(CONTROL / session["snapshot"]),
                                "--port", str(session["port"]), "--duration", str(3600 if args.keep_running else args.seconds + 10)],
                               cwd=ROOT, stdout=bot_log, stderr=subprocess.STDOUT)
        log("Bot connected; checking its actor spawns and moves in the engine...")
        actor_id, track = verify_bot_movement(bot_started_ms, args.seconds, args.tolerance)
        log(f"Remote actor {actor_id:08X}: {track['samples']} engine samples, moved {track['max_move']:.0f} units, "
            f"engine/requested error {track['max_error']:.1f} units")
        print(f"\nPASS: the bot joined the host, its character spawned in Skyrim and moved. Logs: {run_dir}")
        if args.keep_running:
            face_actor(actor_id)
    except TestFailure as error:
        print(f"\nFAIL: {error}\nLogs: {run_dir}", file=sys.stderr)
        if bot and bot.poll() is not None:
            print("Bot output (tail):\n" + "".join((run_dir / "bot.log").read_text().splitlines(True)[-15:]),
                  file=sys.stderr)
        raise SystemExit(1)
    finally:
        if bot and bot.poll() is None and not args.keep_running:
            bot.terminate()
        collect_logs(run_dir)
        if args.keep_running:
            log(f"Skyrim and the bot keep running (bot PID {bot.pid if bot else '-'}); "
                f"watch with `{sys.argv[0]} watch`, end with `{sys.argv[0]} stop`")
        else:
            stop_game()
            log("Skyrim stopped")

def start_session(command, timeout=60):
    """Writes a session request (`<id>` hosts, `<id> join <port>` joins) and waits until ready."""
    request = uuid.uuid4().hex
    atomic_write(CONTROL / "session.txt", f"{request} {command}".strip() + "\n")

    def ready():
        session = (read_status() or {}).get("session", {})
        if session.get("request") != f"{request} {command}".strip():
            return None
        if session.get("state") == "failed":
            raise TestFailure(f"Session failed: {session.get('error')}")
        return session if session.get("state") == "ready" else None
    return wait_for(f"{INSTANCE or 'host'}: {'joining' if command else 'hosting'}", ready, timeout)


def remote_players():
    """Remote actors this instance shows in the engine with 3D: {form id: position}."""
    status = read_status() or {}
    return {int(actor): sample["actual"] for actor, sample in status.get("actors", {}).items() if sample.get("has_3d")}


def both_see_each_other():
    """{"host"/"client": remote players} once each game shows the other's player, else None."""
    seen = {}
    for name in ("", "client"):
        select_instance(name)
        players = remote_players()
        if not players:
            return None
        seen[name or "host"] = players
    return seen


def command_reload(args):
    """Swaps the current build into both running games (the loader unloads the old client and
    loads the new one between two frames), then hosts and joins again. No game restart."""
    started = time.monotonic()
    reload_games(args.seconds)
    print(f"\nRELOADED: both games run the new build and are connected again "
          f"({time.monotonic() - started:.1f}s)")
    report_game_memory()


# Each reload leaves the old client copy mapped (Code/hotreload/Loader.cpp) with whatever its
# singletons held: ~60MB a reload. Past this, restart the games.
GAME_MEMORY_LIMIT_MB = 6000


def report_game_memory():
    """Prints each running game's memory (resident + swapped) and warns when a restart is due."""
    sizes = []
    for status in Path("/proc").glob("[0-9]*/status"):
        try:
            if "SkyrimSE.exe" not in (status.parent / "cmdline").read_bytes().decode(errors="replace"):
                continue
            fields = dict(line.split(":", 1) for line in status.read_text().splitlines() if ":" in line)
        except OSError:
            continue
        kb = sum(int(fields.get(key, "0 kB").split()[0]) for key in ("VmRSS", "VmSwap"))
        if kb > 100 * 1024:  # not the launcher stubs that share the name
            sizes.append(kb // 1024)
    if not sizes:
        return
    print("Game memory: " + ", ".join(f"{mb}MB" for mb in sorted(sizes)))
    if max(sizes) > GAME_MEMORY_LIMIT_MB:
        print(f"WARNING: a game is over {GAME_MEMORY_LIMIT_MB}MB from hot reloads; restart them "
              f"(`stop`, then `duo --keep-running`)")


def reload_games(seconds=60):
    """Hot reloads the client in both games (the deployed build, rebuilt or not) and reconnects them."""
    if not LOADER.exists():
        raise TestFailure(f"Build the loader first: scripts/xmake-msvc.sh build SkyrimCoopLoader ({LOADER})")
    deploy_dll()
    started = time.monotonic()
    names = ("", "client")
    for name in names:
        # The new client would act on the old session request (and join a stale port).
        select_instance(name)
        (CONTROL / "session.txt").unlink(missing_ok=True)
    before = {}
    for name in names:
        select_instance(name)
        response = rpc({"cmd": "reload"})
        if not response or not response.get("ok"):
            raise TestFailure(f"{name or 'host'}: the reload request failed ({response}); was the game "
                              f"launched with the loader? Relaunch with `duo --keep-running`.")
        before[name] = response["reload"]["generation"]
    for name in names:
        select_instance(name)

        def reloaded():
            reload = (read_status() or {}).get("reload") or {}
            if reload.get("generation", -1) > before[name]:
                if reload.get("error"):
                    raise TestFailure(f"{name or 'host'}: reloaded but {reload['error']}")
                return reload
            if reload.get("error"):
                raise TestFailure(f"{name or 'host'}: reload refused: {reload['error']}")
            return None
        wait_for(f"{name or 'host'}: reloading", reloaded, 30)
    log(f"Both games run the new build ({time.monotonic() - started:.1f}s)")

    select_instance("")
    port = start_session("")["port"]
    select_instance("client")
    start_session(f"join {port}")
    seen = wait_for("Each game shows the other player", both_see_each_other, seconds)
    for name in names:
        select_instance(name)
        face_actor(next(iter(seen[name or "host"])))


def command_duo(args):
    """Two real Skyrim instances: host + client on one machine, each must show the other."""
    check_prerequisites(need_bot=False)
    run_dir = new_run_dir("duo")
    passed = False
    try:
        for name in ("", "client"):
            select_instance(name)
            log(f"--- {name or 'host'} ---")
            launch(args, run_dir)
        select_instance("")
        port = start_session("")["port"]
        log(f"Host is hosting on port {port}")
        select_instance("client")
        start_session(f"join {port}")
        log("Client joined the host")
        # Test players never run low on magicka: the "Magicka Low" help popup blocks all input
        # (bShowTutorials does not suppress it).
        for name in ("", "client"):
            select_instance(name)
            run_console("player.setav magicka 5000")
        # Both players spawn on the same spot: step the client aside so each can see the other.
        select_instance("")
        host_position = ((read_status() or {}).get("player") or {}).get("position")
        if host_position:
            select_instance("client")
            run_console(f"player.setpos x {host_position[0] + 200:.0f}")
            run_console(f"player.setpos y {host_position[1]:.0f}")

        seen = wait_for("Each game shows the other player", both_see_each_other, args.seconds)
        for name, players in seen.items():
            for actor, position in players.items():
                log(f"{name} shows remote player {actor:08X} at ({position[0]:.0f}, {position[1]:.0f}, {position[2]:.0f})")
        if args.npc:
            # Host-owned NPC: spawned in the host's game, must be replicated to the client.
            select_instance("client")
            before = set(remote_players())
            select_instance("")
            run_console(f"player.placeatme {args.npc} 1")
            select_instance("client")
            new_actors = wait_for(f"Client shows the host's new NPC (base {args.npc})",
                                  lambda: set(remote_players()) - before, args.seconds)
            for actor in new_actors:
                log(f"client shows host NPC {actor:08X}")
        passed = True
        print(f"\nPASS: host and client are connected and each shows the other's character"
              f"{' and the host NPC' if args.npc else ''}. Logs: {run_dir}")
        if args.keep_running:
            for name in ("", "client"):
                select_instance(name)
                face_actor(next(iter(seen[name or "host"])))
            time.sleep(0.3)
            save_home()
    except TestFailure as error:
        print(f"\nFAIL: {error}\nLogs: {run_dir}", file=sys.stderr)
        raise SystemExit(1)
    finally:
        for name in ("", "client"):
            select_instance(name)
            collect_logs(run_dir)
        if args.keep_running:
            log(f"Both games keep running. Watch: `{sys.argv[0]} watch` (host) / `{sys.argv[0]} watch --instance client`; "
                f"end with `{sys.argv[0]} stop`")
        else:
            for name in ("", "client"):
                select_instance(name)
                stop_game()
            log("Both games stopped" + ("" if passed else " (failed run)"))

# --- Sync scenarios: run against a `duo --keep-running` session, a few seconds each ------------
# Scenarios drive both games through the plugin (its test harness, Code/client/TestHarness): the player draws, attacks,
# casts and stands where the scenario puts it, through the same engine calls its input makes, so
# the same sync runs without input timing. Each step waits for a stated condition and fails at the
# first one that does not hold; nothing is retried to hide a miss. Real keyboard and mouse input is
# exercised only by the `input` group (not in a default run).
IRON_SWORD = 0x00012EB7
FLAMES = 0x00012FCD    # concentration spell: synced through spell cast + interrupt messages
FIREBOLT = 0x00012FD0  # fire-and-forget spell
RIGHT_HAND = 1
LYDIA = "000A2C8E"
WOLF = "00023ABE"
CHICKEN = "000FA6D9"   # a plain non-essential creature (Lydia is essential: `kill` won't kill her)
MELEE_GAP = 90         # units from a target: in sword reach
SPELL_GAP = 250        # units: far enough that a bolt does not spawn inside the target
HOLD = 0.0             # scenario --hold: seconds to pause after each synced step, for watching
WEAPON_SHEATHED, WEAPON_DRAWN = 0, 3  # ActorState weapon state (1-2 drawing, 4-5 sheathing)


def actors_on(instance):
    select_instance(instance)
    return (read_status() or {}).get("actors", {})


def wait_on(instance, description, predicate, timeout=15):
    """Polls `predicate(actors)` against an instance's status until it returns a truthy value."""
    def check():
        return predicate(actors_on(instance))
    return wait_for(f"[{instance or 'host'}] {description}", check, timeout)


def console_on(instance, command):
    select_instance(instance)
    run_console(command)


def local_player(instance):
    select_instance(instance)
    return (read_status() or {}).get("player") or {}


def player_snapshot(instance):
    select_instance(instance)
    return ((read_status() or {}).get("world") or {}).get("20") or {}


def remote_player(actors):
    return next((int(a) for a, s in actors.items() if s.get("remote_player") and s.get("has_3d")), None)


def remote_npcs(actors):
    return {int(a) for a, s in actors.items() if not s.get("remote_player") and s.get("has_3d")}


def close(a, b, tolerance=1.0):
    return a is not None and b is not None and abs(a - b) <= tolerance


def distance(a, b):
    return sum((x - y) ** 2 for x, y in zip(a, b)) ** 0.5


def show(observer, observed):
    """With --hold, turns the observer's camera toward the actor under test."""
    if HOLD and (target := observed(actors_on(observer))) is not None:
        face(observer, target)


def hold():
    if HOLD:
        log(f"(holding {HOLD:.0f}s so you can look)")
        time.sleep(HOLD)


# --- The players' actions, through the plugin -----------------------------------------------

def player_command(instance, request):
    """A plugin command in that game; fails the scenario if the game refuses it."""
    select_instance(instance)
    response = rpc(request) or {}
    if not response.get("ok"):
        raise TestFailure(f"[{instance or 'host'}] {request} refused: {response.get('error', 'no answer')}")
    return response


def weapon_state(instance):
    return (player_snapshot(instance).get("state") or {}).get("weapon_state")


# Buttons a scenario holds down, per game: the baseline lets them go if a scenario failed mid-press.
HELD_BUTTONS = {}

# MagicCaster::State: 3 once the spell is charged (a release casts it).
CASTER_CHARGED = 3


def takes_input(instance):
    """No menu but the HUD is open: a fade (a cell change's Fader Menu), a loading screen or the console
    swallow a player's buttons."""
    select_instance(instance)
    return not set((read_status() or {}).get("menus") or []) - {"HUD Menu"}


def press(instance, button):
    """The player presses a button ("right", "left" or "ready") through the game's input dispatch,
    as its device would: everything that listens to input sees a player's press. Once the game takes
    input, as a player would wait for a fade to end."""
    wait_for(f"[{instance or 'host'}] the game takes input", lambda: takes_input(instance), 10)
    player_command(instance, {"cmd": "input", "button": button, "down": True})
    HELD_BUTTONS.setdefault(instance, set()).add(button)


def release(instance, button):
    """The player lets go of the button; the game is told how long it was held."""
    player_command(instance, {"cmd": "input", "button": button, "down": False})
    HELD_BUTTONS.get(instance, set()).discard(button)


def tap(instance, button):
    press(instance, button)
    release(instance, button)


def hands_settled(instance):
    """No draw or sheathe animation playing: until it ends, the game ignores Ready Weapon (a player's
    press as much as ours)."""
    me = local_player(instance)
    return not me.get("equipping") and not me.get("unequipping")


def ready_weapon(instance, state, what):
    """Presses Ready Weapon and returns once its animation has played out: the weapon state alone
    is not enough (a spell's reads drawn on the press, while its animation still plays)."""
    who = f"[{instance or 'host'}]"
    wait_for(f"{who} the last draw or sheathe has played out", lambda: hands_settled(instance), 5)
    animations = local_player(instance).get("equip_animations", 0)
    tap(instance, "ready")
    wait_for(f"{who} {what}", lambda: local_player(instance).get("equip_animations", 0) > animations
             and weapon_state(instance) == state and hands_settled(instance), 5)


def draw(instance):
    """The player readies what it holds (the Ready Weapon button); returns once the hands are ready."""
    if weapon_state(instance) != WEAPON_DRAWN:
        ready_weapon(instance, WEAPON_DRAWN, "hands ready")
    wait_for(f"[{instance or 'host'}] hands settled", lambda: hands_settled(instance), 5)


def sheathe(instance):
    """The player lowers its hands (the Ready Weapon button); returns once they are down."""
    if weapon_state(instance) != WEAPON_SHEATHED:
        ready_weapon(instance, WEAPON_SHEATHED, "hands lowered")
    wait_for(f"[{instance or 'host'}] hands settled", lambda: hands_settled(instance), 5)


def equip_in_hand(instance, commands, item, what):
    """Runs the console commands that put `item` in the player's hand. With the hands up the game
    plays the equip animation (MagicForceEquip...), during which a press is cut short: waited for."""
    animations = local_player(instance).get("equip_animations", 0)
    hands = lambda: (player_snapshot(instance).get("state") or {}).get("hands", [])
    # A new item in raised hands plays the equip animation; the same item again plays nothing.
    animated = weapon_state(instance) == WEAPON_DRAWN and item not in hands()
    for command in commands:
        console_on(instance, command)
    who = f"[{instance or 'host'}]"
    wait_for(f"{who} {what}", lambda: item in hands(), 5)
    if animated:
        wait_for(f"{who} the equip animation has played out",
                 lambda: local_player(instance).get("equip_animations", 0) > animations and hands_settled(instance), 5)


def attack(instance):
    """One right-hand attack: a click of the attack button, held until the swing starts (the game
    starts it from the held button's updates, so an instant click does nothing). A swing still going
    would pass for the new one, so it waits for that to end first."""
    wait_for(f"[{instance or 'host'}] the last swing is over",
             lambda: not graph_bools(instance, "IsAttacking")["IsAttacking"], 3)
    press(instance, "right")
    try:
        wait_for(f"[{instance or 'host'}] swings", lambda: graph_bools(instance, "IsAttacking")["IsAttacking"], 2)
    finally:
        release(instance, "right")


def face(instance, target):
    player_command(instance, {"cmd": "face", "target": target})


# hkpCharacterStateType of the player's character controller.
ON_GROUND = 0


def game_frame(instance):
    select_instance(instance)
    return ((read_status() or {}).get("frames") or {}).get("frames", 0)


def place_before(instance, target, gap, bearing=None):
    """The player stands `gap` units from `target`, facing it (yaw and pitch, in the same frame).
    Returns once it stands on the ground: the move leaves its character controller in the air for
    a moment, and a draw or a cast started then is cut short when it lands. The controller notices
    the move a frame later, so the ground is checked from a few frames on."""
    moved = game_frame(instance)
    request = {"cmd": "place", "target": target, "distance": gap}
    if bearing is not None:
        request["bearing"] = bearing  # its side of the target (0 north, 90 east): two players apart
    player_command(instance, request)
    wait_for(f"[{instance or 'host'}] stands on the ground", lambda: game_frame(instance) >= moved + 3
             and local_player(instance).get("character_state") == ON_GROUND, 5)


def give_sword(instance):
    equip_in_hand(instance, [f"player.additem {IRON_SWORD:08X} 1", f"player.equipitem {IRON_SWORD:08X}"],
                  IRON_SWORD, "holds the sword")


def give_spell(instance, spell):
    equip_in_hand(instance, [f"player.addspell {spell:08X}", f"player.equipspell {spell:08X} right",
                             "player.restoreav magicka 1000"], spell, "holds the spell")


# Spells that stream while the button is held (the rest charge, then fire on the release).
CONCENTRATION_SPELLS = {FLAMES}


def graph_bools(instance, *names):
    """The player's behavior graph booleans by name."""
    return player_command(instance, {"cmd": "graph_bool", "names": list(names)})["values"]


def cast_spell(instance, target, spell):
    """The player casts the right hand's `spell` with the attack button, facing `target` first (aimed
    spells fly where the camera looks). A charged spell fires once the game has charged it; a
    concentration spell streams until stop_casting."""
    if target:
        face(instance, target)
    # A press while the last cast is still going out (its release animation lasts about a second) is
    # ignored, as a player's would be.
    wait_for(f"[{instance or 'host'}] the last cast has gone out",
             lambda: (local_player(instance).get("caster_states") or [0, 0])[RIGHT_HAND] == 0
             and not graph_bools(instance, "IsCastingRight")["IsCastingRight"], 5)
    press(instance, "right")
    if spell in CONCENTRATION_SPELLS:
        return
    wait_for(f"[{instance or 'host'}] {spell:08X} charged",
             lambda: (local_player(instance).get("caster_states") or [0, 0])[RIGHT_HAND] == CASTER_CHARGED, 5)
    release(instance, "right")


def stop_casting(instance):
    """The player lets go of the attack button: a concentration spell stops."""
    release(instance, "right")


# --- The host's NPCs and their copies, by server id -----------------------------------------

def host_npc_state(npc):
    return local_player("").get("local_actors", {}).get(str(npc), {})


def host_world(form_id):
    select_instance("")
    return ((read_status() or {}).get("world") or {}).get(str(form_id)) or {}


def copy_of(instance, server_id, timeout=15):
    """The form id of the copy, in that game, of the actor with this server id."""
    return wait_on(instance, f"shows server actor {server_id:X}",
                   lambda actors: next((int(a) for a, s in actors.items()
                                        if s.get("server_id") == server_id and s.get("has_3d")), None), timeout)


STAGE_DEPTH = 450  # units past the host's home spot, away from the client's


def stage_spot():
    """Where scenarios put their NPC: the middle of the hall behind the host (the two home spots are
    in an archway, walls on both sides). The client then stands between the host and the NPC, so
    nothing is in its line of fire. (`placeatme` drops an NPC wherever the navmesh lets it, in
    QASmoke well out of sight.)"""
    host, client = load_home("")["position"], load_home("client")["position"]
    dx, dy = host[0] - client[0], host[1] - client[1]
    length = math.hypot(dx, dy) or 1.0
    return [host[0] + dx / length * STAGE_DEPTH, host[1] + dy / length * STAGE_DEPTH, host[2]]


def spawn_host_npc(base, restrained=False):
    """The host spawns an NPC on the stage: (host ref id, server id, the client's copy).
    Restrained, it stays there (an NPC otherwise walks up to greet the players)."""
    before = set(local_player("").get("local_actors", {}))
    console_on("", f"player.placeatme {base} 1")
    npc = int(next(iter(wait_for("[host] new NPC loaded",
                                 lambda: set(local_player("").get("local_actors", {})) - before, 15))))
    # Unassigned, it reads -1 (server id 0 is valid: the host's player).
    server_id = wait_for("[host] NPC has a server id",
                         lambda: (sid := host_npc_state(npc).get("server_id")) is not None and sid >= 0 and (sid,), 10)[0]
    if restrained:
        console_on("", f"{npc:08X}.setrestrained 1")
    goal = stage_spot()
    place("", f"{npc:08X}", goal, host_npc_state(npc)["position"])
    wait_for("[host] NPC on the stage", lambda: distance(host_npc_state(npc)["position"][:2], goal[:2]) < 40
             and abs(host_npc_state(npc)["position"][2] - goal[2]) < 60, 10)
    copy = copy_of("client", server_id)
    wait_on("client", "its copy on the stage",
            lambda actors: distance((actors.get(str(copy), {}).get("actual") or [1e9, 1e9])[:2], goal[:2]) < 60, 10)
    return npc, server_id, copy


def remove_host_npc(npc):
    """Takes a spawned NPC out of the scene for good."""
    console_on("", f"{npc:08X}.disable")
    console_on("", f"{npc:08X}.markfordelete")


def host_npc_health(npc):
    return host_npc_state(npc).get("health")


def hit_lands(npc, before, what, timeout=2.0):
    """The host's real NPC loses health within `timeout`: the hit landed and its owner applied it."""
    return wait_for(f"[host] {what} lands", lambda: (h := host_npc_health(npc)) is not None and h < before - 0.5 and h,
                    timeout)


def copy_health_matches(npc, copy):
    wait_on("client", "the copy shows the host NPC's health",
            lambda actors: close(actors.get(str(copy), {}).get("health"), host_npc_health(npc)), 5)


def health_bar_shows(instance, target):
    """The attacker's HUD shows its target's health bar (hit feedback is the attacker's own; the
    owner applies the hit)."""
    wait_for(f"[{instance or 'host'}] HUD shows {target:X}'s health bar",
             lambda: local_player(instance).get("enemy_health_target") == target, 5)


def visuals(instance, form_id):
    """The effect shaders and art the engine plays on that actor in that game."""
    select_instance(instance)
    return set((((read_status() or {}).get("world") or {}).get(str(form_id)) or {}).get("visuals") or [])


class GodMode:
    """Both players invulnerable for the scenario (toggled back afterwards)."""
    def __enter__(self):
        for instance in ("", "client"):
            console_on(instance, "tgm")
    def __exit__(self, *exc):
        for instance in ("", "client"):
            console_on(instance, "tgm")


# --- Session --------------------------------------------------------------------------------

def game_state(instance):
    select_instance(instance)
    return (read_status() or {}).get("game") or {}


def session_players(instance):
    """{player name: is host} for the other players this game lists."""
    return {p["name"]: p["host"] for p in game_state(instance).get("players", [])}


def disconnect_client():
    """The client leaves the session like a player quitting."""
    player_command("client", {"cmd": "disconnect"})
    wait_for("[client] disconnected", lambda: game_state("client").get("online") is False, 10)


def rejoin_client():
    """The client joins the host's running session again."""
    select_instance("")
    port = ((read_status() or {}).get("session") or {}).get("port")
    if not port:
        raise TestFailure("[host] no session port to rejoin")
    select_instance("client")
    start_session(f"join {port}")
    wait_for("Each game shows the other player again", both_see_each_other, 30)


def scenario_session_roster():
    """Everyone connected is in the session (there are no parties): each game lists the other
    player, and both agree the host hosts."""
    wait_for("[host] lists the client, not as the host",
             lambda: len(session_players("")) == 1 and not any(session_players("").values()), 10)
    wait_for("[client] lists the host, as the host", lambda: list(session_players("client").values()) == [True], 10)
    if not game_state("").get("hosting") or game_state("client").get("hosting"):
        raise TestFailure("the host's game must host and the client's must not")


def party_panel_lists_the_other(when):
    """Each game's party panel shows the other player, with its character's health: the client's
    lists the host (as the host), the host's lists the client."""
    def panel(instance):
        return game_state(instance).get("party_panel")
    for instance, host in (("client", True), ("", False)):
        wait_for(f"[{instance or 'host'}] the party panel lists the {'host' if host else 'client'} {when}",
                 lambda: (rows := panel(instance)) is not None and len(rows) == 1 and rows[0]["host"] == host
                 and rows[0]["health"] >= 0, 10)


def scenario_party_panel():
    """The party panel (the in-game UI) lists the other player in each game, with its health, and
    still does after both games hot reload."""
    party_panel_lists_the_other("now")
    reload_games()
    party_panel_lists_the_other("after a hot reload")


def party_panel_shows_level(instance, level, when):
    """The game's party panel shows the other player at aLevel."""
    wait_for(f"[{instance or 'host'}] the party panel shows the other player at level {level} {when}",
             lambda: (rows := game_state(instance).get("party_panel")) and len(rows) == 1 and rows[0]["level"] == level, 5)


def scenario_party_shows_levels():
    """Each party panel shows the other player's level, and a level-up in either game (the console's
    advlevel, as a level-up raises it) reaches the other's panel. The baseline puts the levels back."""
    levels = {instance: game_state(instance)["level"] for instance in ("", "client")}
    for instance, other in (("", "client"), ("client", "")):
        party_panel_shows_level(instance, levels[other], "now")
    for instance, other in (("client", ""), ("", "client")):
        console_on(instance, "player.advlevel")
        wait_for(f"[{instance or 'host'}] the player is level {levels[instance] + 1}",
                 lambda: game_state(instance)["level"] == levels[instance] + 1, 5)
        party_panel_shows_level(other, levels[instance] + 1, f"after the {instance or 'host'}'s level-up")


def scenario_late_join_client():
    """The client leaves and joins again while the host's world changed: it must see the world as it
    is now (the host's player and NPC as they are), and the host must drop the client's character
    while it is away."""
    npc, server_id, _ = spawn_host_npc(LYDIA)
    console_on("", f"{npc:08X}.additem {IRON_SWORD:08X} 1")
    console_on("", f"{npc:08X}.equipitem {IRON_SWORD:08X}")
    give_sword("")
    disconnect_client()
    wait_on("", "the client's character leaves the host's game", lambda actors: remote_player(actors) is None)
    # While the client is away.
    draw("")
    console_on("", f"{npc:08X}.damageav health 50")
    rejoin_client()
    party_panel_lists_the_other("after the client joined again")
    copy = copy_of("client", server_id)
    wait_on("client", "the NPC's copy holds the sword", lambda actors: IRON_SWORD in actors.get(str(copy), {}).get("equipped", []))
    copy_health_matches(npc, copy)
    wait_on("client", "the host's character has the sword drawn",
            lambda actors: remote_player(actors) is not None and actors[str(remote_player(actors))].get("weapon_drawn"))
    hold()


def scenario_host_rejoins():
    """The host leaves (which ends the session for everyone: the host is the session) and hosts
    again, and the client joins again: the host must own the NPCs it spawns, not the client."""
    player_command("", {"cmd": "disconnect"})
    wait_for("[host] stopped hosting", lambda: not game_state("").get("hosting") and game_state("").get("online") is False, 15)
    wait_for("[client] the session ended", lambda: game_state("client").get("online") is False, 15)
    select_instance("")
    start_session("")
    rejoin_client()
    npc, server_id, copy = spawn_host_npc(LYDIA)
    log(f"host's NPC {npc:08X} (server id {server_id:X}) reaches the client as {copy:08X}")


def scenario_host_kicks_client():
    """The host removes the client from the session: the client is disconnected, its character
    leaves the host's game and the host no longer lists it. The client can join again."""
    players = game_state("").get("players", [])
    if len(players) != 1:
        raise TestFailure(f"[host] expected the client in the session, got {players}")
    player_command("", {"cmd": "kick", "player": players[0]["id"]})
    wait_for("[client] disconnected", lambda: game_state("client").get("online") is False, 10)
    wait_for("[host] no longer lists the client", lambda: not game_state("").get("players"), 10)
    wait_on("", "the client's character leaves the host's game", lambda actors: remote_player(actors) is None)
    rejoin_client()


# --- Characters: spawning, moving, removing, cells ------------------------------------------

def scenario_steady_state():
    """Desync checker: every actor's full state matches between owner and copy."""
    if problems := check_desync():
        raise TestFailure("desync: " + " | ".join(problems))


def place(instance, ref, goal, current):
    """Moves `ref` to `goal` in an instance's game by lifting it 100 units, sliding it over and
    letting it drop onto the floor there. Separate setpos x/y/z at floor height can slide it into
    furniture or under the floor, and it then falls through the world."""
    console_on(instance, f"{ref}.setpos z {max(current[2], goal[2]) + 100:.0f}")
    console_on(instance, f"{ref}.setpos x {goal[0]:.0f}")
    console_on(instance, f"{ref}.setpos y {goal[1]:.0f}")


def scenario_move_host_npc():
    """Host moves its NPC to a player; the client's copy must end up where the host's NPC is."""
    npc, _, copy = spawn_host_npc(LYDIA, restrained=True)
    show("client", lambda actors: copy)
    host_actors = actors_on("")
    players = {"player": local_player("")["position"]}
    if client_on_host := remote_player(host_actors):
        players[f"{client_on_host:08X}"] = host_actors[str(client_on_host)]["actual"]
    start = host_npc_state(npc)["position"]
    target = max(players, key=lambda ref: distance(players[ref], start))  # the farther player
    goal = players[target]
    place("", f"{npc:08X}", goal, start)
    wait_for("[host] NPC moved and landed", lambda: distance(host_npc_state(npc)["position"], start) > 100
             and abs(host_npc_state(npc)["position"][2] - goal[2]) < 60, 10)
    last = {}
    def copy_matches(actors):
        last.update(copy=actors.get(str(copy), {}).get("actual"), owned=host_npc_state(npc)["position"])
        return last["copy"] and distance(last["copy"], last["owned"]) < 30
    try:
        wait_on("client", f"NPC copy {copy:08X} at the host NPC's position", copy_matches, 10)
    except TestFailure:
        raise TestFailure(f"copy at {[round(v) for v in last['copy'] or []]}, host's NPC at {[round(v) for v in last['owned']]}")
    hold()


def voice(instance, form_id):
    """The actor's voice line in that game (mod.voice): callback state, seconds left, sound id."""
    select_instance(instance)
    snapshot = (((read_status() or {}).get("world") or {}).get(str(form_id)) or {})
    return (snapshot.get("mod") or {}).get("voice") or {}


DO_NOTHING_PACKAGE = 27  # TESPackage::packType of Actor::InitiateDoNothingPackage's package


def package_type(instance, form_id):
    """The type of the actor's current package in that game (mod.package_type), -1 without one."""
    select_instance(instance)
    snapshot = (((read_status() or {}).get("world") or {}).get(str(form_id)) or {})
    return (snapshot.get("mod") or {}).get("package_type")


def subtitle(instance, form_id):
    """The subtitle that game shows over the actor (mod.subtitle), "" if none."""
    select_instance(instance)
    snapshot = (((read_status() or {}).get("world") or {}).get(str(form_id)) or {})
    return (snapshot.get("mod") or {}).get("subtitle") or ""


def line_playing(instance, form_id):
    """Started (UpdateSoundCallback set its timer) and not over."""
    line = voice(instance, form_id)
    return line.get("callback") == -1 and (line.get("seconds") or -1) > 0


def line_over(instance, form_id):
    """No time left, and nothing queued with a sound behind it."""
    line = voice(instance, form_id)
    return (line.get("seconds") or -1) <= 0 and not (line.get("callback") == 3 and line.get("sound", -1) != -1)


def scenario_copy_ends_voice_line():
    """The host player talks to its NPC: she greets him, and the client's copy plays the same line
    (NotifyDialogue) with the same subtitle (NotifySubtitle) and ends it, as the host's NPC does. SpeakSound only queues a line; the engine's
    voice upkeep in Character::UpdateAI starts and ends it (Actor::UpdateSoundCallback,
    Character::UpdateVoiceTimer), which copies run from HookUpdateAI. A copy holds the engine's do-nothing
    package (HookCheckForNewPackage): an actor without one gets no UpdateAI once its 3D is far enough from
    the camera to fade (Character::ShouldOnlyCheckPackage)."""
    npc, _, copy = spawn_host_npc(LYDIA, restrained=True)
    try:
        wait_for(f"[client] copy {copy:08X} holds the do-nothing package",
                 lambda: package_type("client", copy) == DO_NOTHING_PACKAGE, 5)
        place_before("", npc, 100)
        activate_on("", npc)
        wait_for("[host] NPC's line plays", lambda: line_playing("", npc), 10)
        wait_for(f"[client] copy {copy:08X}'s line plays", lambda: line_playing("client", copy), 5)
        text = wait_for("[host] NPC's subtitle shows", lambda: subtitle("", npc), 5)
        wait_for(f"[client] copy {copy:08X} shows the same subtitle", lambda: subtitle("client", copy) == text, 5)
        wait_for("[host] NPC's line ends", lambda: line_over("", npc), 30)
        wait_for(f"[client] copy {copy:08X}'s line ends", lambda: line_over("client", copy), 5)
    finally:
        select_instance("")
        rpc({"cmd": "menu", "name": "Dialogue Menu", "show": False})
    hold()


def scenario_remove_host_npc():
    """Host disables an NPC it owns; the client's copy must disappear."""
    npc, _, copy = spawn_host_npc(CHICKEN)
    console_on("", f"{npc:08X}.disable")
    wait_for("[host] NPC unloaded", lambda: str(npc) not in local_player("").get("local_actors", {}), 10)
    wait_on("client", f"NPC copy {copy:08X} removed", lambda actors: str(copy) not in actors)
    hold()


def scenario_teleport_keeps_copies():
    """The client teleports within the cell (moveto): its game keeps the copies it shows (the host's
    character and NPC stay the same actors). A respawned copy loses its state and id."""
    _, server_id, copy = spawn_host_npc(LYDIA, restrained=True)
    host_copy = remote_player(actors_on("client"))
    log_file = plugin_log("client")
    start = log_file.stat().st_size
    console_on("client", f"player.moveto {copy:08X}")
    wait_for("[client] the teleport arrives (cell arrival)",
             lambda: "Arrived in cell" in log_file.read_bytes()[start:].decode(errors="replace"), 10)
    end = time.monotonic() + 1.5
    while time.monotonic() < end:
        actors = actors_on("client")
        if remote_player(actors) != host_copy:
            raise TestFailure(f"the host's character was respawned: copy {host_copy:08X} is now {remote_player(actors)}")
        now = next((int(a) for a, s in actors.items() if s.get("server_id") == server_id), None)
        if now != copy:
            raise TestFailure(f"the host's NPC was respawned: copy {copy:08X} is now {now}")
        time.sleep(0.1)


# SkyrimClear, and a weather Riverwood itself shows (0x10A23 is refused by `fw` there).
CLEAR_WEATHER, OTHER_WEATHER = 0x81A, 0x10E1F1
OUTDOOR_CELL, HOME_CELL = "Riverwood", "QASmoke"   # the suite's home is an interior: no sky
INN = "RiverwoodSleepingGiantInn"


def player_cell(instance):
    """The cell the player is in, by that game's snapshot."""
    select_instance(instance)
    return ((((read_status() or {}).get("world") or {}).get("20") or {}).get("placement") or {}).get("cell")


def both_go(cell, command=None):
    """Both players go to `cell` with coc (or `command`, e.g. "cow Tamriel 7 -8"). Each game's arrival is
    waited for first: until then both still show the other player in the old cell, and that check alone
    passed at once."""
    before = {name: player_cell(name) for name in ("", "client")}
    for name in ("", "client"):
        console_on(name, command or f"coc {cell}")
    for name in ("", "client"):
        wait_for(f"[{name or 'host'}] arrived in {cell}",
                 lambda name=name: player_cell(name) not in (None, before[name]), 60)
    wait_for(f"In {cell}, each game shows the other player", both_see_each_other, 60)
    # The arrival's fade: until it ends the game takes no input and resets the player's heading.
    for name in ("", "client"):
        wait_for(f"[{name or 'host'}] the arrival's fade is over", lambda name=name: takes_input(name), 10)


# The White River west of Whiterun, Tamriel cell (7, -8): water at -1280 over a bed at about -1536,
# deepest south of the spot (Skyrim.esm XCLW and LAND), deep enough for a head to go under.
RIVER_CELL, RIVER_SPOT, RIVER_BANK = "cow Tamriel 7 -8", (30592, -29696, -1300), (30592, -29200, -1100)
DIVE_SECONDS = 6           # fActorLeaveWaterBreathTimer is 4: surfacing after that gasps
SURFACE_PACKAGE = 29       # TESPackage::packType of the package Actor::Update gives a drowning NPC


def water(instance, form_id=None):
    """The actor's mod.water in that game (submerged, swimming, head_under, seconds_under); the player's
    own without `form_id`."""
    if form_id is None:
        return (player_snapshot(instance).get("mod") or {}).get("water") or {}
    select_instance(instance)
    return ((((read_status() or {}).get("world") or {}).get(str(form_id)) or {}).get("mod") or {}).get("water") or {}


def dive_roundtrip(diver, observer, label):
    """The diver swims to the river bed, stays under, then surfaces: the owner gasps (Actor::Update
    queues a comment once a head was under for 4 s). The copy runs Actor::Update too; its own decisions
    there (gasp, Surface package, drowning damage) must not act. It plays the owner's line only, once,
    and keeps the do-nothing package and its owner's health."""
    both_go("river", RIVER_CELL)
    # The river's wildlife (wolves within 3000 units) would join in: a fight, grunts on the voice line.
    # The host runs every NPC's AI: its combat AI off (tcai, a toggle) keeps them out until the end.
    console_on("", "tcai")
    try:
        for name, spot in ((diver, RIVER_SPOT), (observer, RIVER_BANK)):
            for axis, value in zip("xyz", spot):
                console_on(name, f"player.setpos {axis} {value}")
        wait_for(f"[{diver or 'host'}] swims", lambda: water(diver).get("swimming"), 10)
        show(observer, remote_player)
        select_instance(observer)
        copy = remote_player((read_status() or {}).get("actors", {}))
        lines = {"owner": [], "copy": []}
        packages, healths = set(), set()
        def watch():
            owner_line = (player_snapshot(diver).get("mod") or {}).get("voice") or {}
            select_instance(observer)
            snap = ((read_status() or {}).get("world") or {}).get(str(copy)) or {}
            copy_line = (snap.get("mod") or {}).get("voice") or {}
            # A line counts once it plays (time left, UpdateSoundCallback started it): the voice slot can
            # still hold an old sound that never played.
            for who, line in (("owner", owner_line), ("copy", copy_line)):
                if (line.get("seconds") or -1) > 0 and line.get("sound", -1) != -1:
                    if line["sound"] not in [l["sound"] for l in lines[who]]:
                        lines[who].append({"sound": line["sound"], "duration_ms": line.get("duration_ms", 0)})
                    else:
                        lines[who][-1]["duration_ms"] = max(lines[who][-1]["duration_ms"], line.get("duration_ms", 0))
            packages.add((snap.get("mod") or {}).get("package_type"))
            healths.add(round((snap.get("values") or {}).get("health", 0)))
        # Dive: look down and swim forward, as a player does; south, where the bed is deepest (the bank
        # where the observer stands is north).
        console_on(diver, "player.setangle z 180")
        console_on(diver, "player.setangle x 80")
        press(diver, "forward")
        try:
            wait_for(f"[{diver or 'host'}] head under water", lambda: water(diver).get("head_under"), 5)
            wait_for(f"[{observer or 'host'}] {label}'s head under water", lambda: water(observer, copy).get("head_under"), 5)
            wait_for(f"[{diver or 'host'}] under for {DIVE_SECONDS}s",
                     lambda: (watch(), water(diver).get("seconds_under", 0) >= DIVE_SECONDS)[1], DIVE_SECONDS + 5)
        finally:
            release(diver, "forward")
        console_on(diver, "player.setangle x -80")
        press(diver, "forward")
        try:
            wait_for(f"[{diver or 'host'}] surfaces and gasps",
                     lambda: (watch(), not water(diver).get("head_under") and lines["owner"])[1], 10)
        finally:
            release(diver, "forward")
        console_on(diver, "player.setangle x 0")
        # Until both lines are over: a gasp of the copy's own would start after it surfaced.
        wait_for(f"[{observer or 'host'}] {label} surfaced and its line is over",
                 lambda: (watch(), not water(observer, copy).get("head_under")
                          and line_over(observer, copy) and line_over(diver, 0x14))[1], 10)
        if len(lines["owner"]) != 1:
            raise TestFailure(f"owner's lines {lines['owner']}: expected its one gasp")
        if len(lines["copy"]) != 1 or lines["copy"][0]["duration_ms"] != lines["owner"][0]["duration_ms"]:
            raise TestFailure(f"copy's lines {lines['copy']}, owner's {lines['owner']}: the copy should play the owner's gasp only")
        if packages != {DO_NOTHING_PACKAGE}:
            raise TestFailure(f"copy's packages while under water: {sorted(packages, key=str)} (a Surface package is {SURFACE_PACKAGE})")
        owner_health = round((player_snapshot(diver).get("values") or {}).get("health", 0))
        if healths != {owner_health}:
            raise TestFailure(f"copy's health while under water {sorted(healths)}, owner's {owner_health}")
        hold()
    finally:
        console_on("", "tcai")
        both_go(HOME_CELL)


def scenario_dive_host_player():
    """Host dives, stays under, surfaces; the client's copy of the host follows without its own decisions."""
    dive_roundtrip("", "client", "host's character")


def scenario_dive_client_player():
    """Client dives, stays under, surfaces; the host's copy of the client follows without its own decisions."""
    dive_roundtrip("client", "", "client's character")


def scenario_party_changes_cells():
    """The party goes to another cell and back together (the party tether): each arrival drops the
    copies the old cell took, and the host sends what is there. In each cell both games show the
    other player again, and the client shows the host's NPCs there."""
    try:
        both_go(OUTDOOR_CELL)
        wait_on("client", f"shows the host's NPCs in {OUTDOOR_CELL}", lambda actors: remote_npcs(actors), 30)
    finally:
        both_go(HOME_CELL)


def scenario_party_spreads_on_arrival():
    """The party arrives on one spot (a coc puts everyone on the cell's marker): the host stays, the
    client steps aside onto floor, so neither stands in the other."""
    try:
        both_go(INN)
        def apart():
            host, client = (player_snapshot(n).get("placement") or {} for n in ("", "client"))
            if not host.get("position") or not client.get("position") or host.get("cell") != client.get("cell"):
                return None
            gap = distance(host["position"], client["position"])
            return gap >= 70 and gap
        gap = wait_for("the players stand apart in the inn", apart, 5)
        log(f"players {gap:.0f} units apart")
        hold()
    finally:
        both_go(HOME_CELL)



# Every way a player travels ends in one engine call that sends PositionPlayerEvent (docs/re/systems/player-travel.md):
# doors, map and carriage fast travel, scripted moves, coc. These cover what differs after it.
INN_DOOR_INSIDE, INN_DOOR_OUTSIDE = 0x13419, 0x13424   # the Sleeping Giant Inn's load doors (Skyrim.esm)


def travel(instance, how, cell_before):
    """One player travels (`how` runs it) and arrives: its game is in another cell and takes input again."""
    how()
    wait_for(f"[{instance or 'host'}] arrived somewhere new",
             lambda: player_cell(instance) not in (None, cell_before), 60)
    wait_for(f"[{instance or 'host'}] the arrival's fade is over", lambda: takes_input(instance), 15)


def players_together(where):
    """Both games show the other player, with 3D, in the same cell (a copy left behind would not count)."""
    def check():
        cells = {player_cell(n) for n in ("", "client")}
        return len(cells) == 1 and all(remote_player(actors_on(n)) for n in ("", "client"))
    wait_for(f"{where}: both in one cell, each game shows the other player", check, 30)


def scenario_party_uses_door():
    """The party walks out of the inn through its door, one after the other (the host first), and back in
    (the client first, ahead of the host). Each time both games end up showing the other player, and
    outside the client shows the host's NPCs."""
    try:
        both_go(INN)
        travel("", lambda: activate_on("", INN_DOOR_INSIDE), player_cell(""))
        travel("client", lambda: activate_on("client", INN_DOOR_INSIDE), player_cell("client"))
        players_together("outside the inn")
        wait_on("client", "shows the host's NPCs outside", lambda actors: remote_npcs(actors), 30)
        travel("client", lambda: activate_on("client", INN_DOOR_OUTSIDE), player_cell("client"))
        travel("", lambda: activate_on("", INN_DOOR_OUTSIDE), player_cell(""))
        players_together("back in the inn")
        hold()
    finally:
        both_go(HOME_CELL)


def scenario_party_fast_travels():
    """The party fast travels from the river to Riverwood, the host first (the client still at the river,
    in the same worldspace), then the client. The trip passes time on the host and the client keeps the
    host's time; once both are there, each game shows the other player. Fast travel here is the
    console's FastTravel: it queues the same move, with the marker, as the map's."""
    def hour(instance):
        return game_state(instance).get("time", {}).get("hour")
    try:
        both_go("river", RIVER_CELL)
        before = hour("")
        travel("", lambda: console_on("", f"fasttravel {INN_DOOR_OUTSIDE:X}"), player_cell(""))
        passed = (hour("") - before) % 24
        if passed < 0.5:
            raise TestFailure(f"the host's trip passed {passed:.2f} h: fast travel passes time")
        wait_for("[client] keeps the host's time", lambda: close(hour("client"), hour(""), 0.1), 10)
        travel("client", lambda: console_on("client", f"fasttravel {INN_DOOR_OUTSIDE:X}"), player_cell("client"))
        players_together("in Riverwood")
        if not close(hour("client"), hour(""), 0.1):
            raise TestFailure(f"after its own trip the client's hour is {hour('client'):.2f}, the host's {hour(''):.2f}")
        hold()
    finally:
        both_go(HOME_CELL)

# --- Inventory ------------------------------------------------------------------------------

def equip_roundtrip(actor_instance, ref, observer, observed, label):
    """Equips then unequips an Iron Sword on `ref` in one game; the other game's copy must follow
    both ways."""
    def wears(expected):
        def check(actors):
            target = observed(actors)
            return target is not None and (IRON_SWORD in actors[str(target)]["equipped"]) == expected
        return check
    show(observer, observed)
    console_on(actor_instance, f"{ref}.additem {IRON_SWORD:08X} 1")
    console_on(actor_instance, f"{ref}.equipitem {IRON_SWORD:08X}")
    wait_on(observer, f"{label} wears the Iron Sword", wears(True))
    hold()
    console_on(actor_instance, f"{ref}.unequipitem {IRON_SWORD:08X}")
    wait_on(observer, f"{label} no longer wears it", wears(False))
    hold()


def scenario_equip_host_player():
    """Host equips/unequips a sword; the client's copy of the host must follow."""
    equip_roundtrip("", "player", "client", remote_player, "host's character")


def scenario_equip_client_player():
    """Client equips/unequips a sword; the host's copy of the client must follow."""
    equip_roundtrip("client", "player", "", remote_player, "client's character")


def scenario_equip_host_npc():
    """Host equips/unequips a sword on its NPC; the client's copy must follow."""
    npc, _, copy = spawn_host_npc(LYDIA, restrained=True)
    equip_roundtrip("", f"{npc:08X}", "client", lambda actors: copy if str(copy) in actors else None,
                    f"NPC copy {copy:08X}")


def scenario_repair_equipment_drift():
    """A copy's equipment drifts from its owner's (stripped in its game only): the owner's periodic
    snapshot brings it back."""
    npc, _, copy = spawn_host_npc(LYDIA, restrained=True)
    def equipment(instance, form_id):
        select_instance(instance)
        return set((((read_status() or {}).get("world") or {}).get(str(form_id)) or {}).get("equipment") or [])
    owner = equipment("", npc)
    if not owner:
        raise TestFailure("the host's NPC wears nothing: nothing to drift")
    player_command("client", {"cmd": "strip", "actor": copy})
    wait_for("[client] the copy's equipment drifted", lambda: equipment("client", copy) != owner, 5)
    wait_for("[client] the copy wears the owner's equipment again", lambda: equipment("client", copy) == owner, 10)


# --- Hands: draw, cast, sheathe -------------------------------------------------------------

def copy_drawn(expected):
    return lambda actors: remote_player(actors) is not None and actors[str(remote_player(actors))].get("weapon_drawn") == expected


def draw_roundtrip(actor_instance, observer, label):
    """Player readies then sheathes a sword; the other game's copy must follow."""
    give_sword(actor_instance)
    show(observer, remote_player)
    draw(actor_instance)
    wait_on(observer, f"{label} draws the sword", copy_drawn(True), 5)
    hold()
    sheathe(actor_instance)
    wait_on(observer, f"{label} sheathes the sword", copy_drawn(False), 5)


def scenario_draw_host_player():
    """Host readies/sheathes a weapon; the client's copy of the host must follow."""
    draw_roundtrip("", "client", "host's character")


def sneaking(instance):
    return bool((player_snapshot(instance).get("state") or {}).get("sneaking"))


def copy_sneaking(observer):
    """The observer's copy of the other player: its sneaking flag (what detection reads), or None."""
    select_instance(observer)
    status = read_status() or {}
    target = remote_player(status.get("actors", {}))
    if target is None:
        return None
    return bool((((status.get("world") or {}).get(str(target)) or {}).get("state") or {}).get("sneaking"))


def sneak(instance, down):
    """The player presses Sneak (a toggle) until it is sneaking (`down`) or standing."""
    if sneaking(instance) != down:
        tap(instance, "sneak")
    wait_for(f"[{instance or 'host'}] {'sneaks' if down else 'stands up'}", lambda: sneaking(instance) == down, 5)


def sneak_roundtrip(actor_instance, observer, label):
    """Player sneaks then stands up; the other game's copy must follow and stay there. A copy runs
    Actor::Update, which decides whether its actor should sneak (package, combat, teammate rules) and
    toggles it if not: on a copy that toggle is an action our PerformAction hook refuses, so it keeps
    its owner's state (docs/re/systems/actor-ai-and-copies.md, "Actor::Update")."""
    show(observer, remote_player)
    sneak(actor_instance, True)
    wait_for(f"[{observer or 'host'}] {label} sneaks", lambda: copy_sneaking(observer) is True, 5)
    # Held for a while: Actor::Update runs every frame on the copy.
    stable_until = time.time() + 1.0
    wait_for(f"[{observer or 'host'}] {label} still sneaks after 1s",
             lambda: copy_sneaking(observer) is True and time.time() >= stable_until, 3)
    hold()
    sneak(actor_instance, False)
    wait_for(f"[{observer or 'host'}] {label} stands up", lambda: copy_sneaking(observer) is False, 5)


# QASmoke's cooking spit: furniture with no menu on entering (its crafting menu opens on an activation).
COOKING_SPIT = 0x101973
IN_FURNITURE = 3  # ActorState1::sitSleepState kIsSitting


def sit_state(instance, form_id):
    select_instance(instance)
    world = (read_status() or {}).get("world") or {}
    return (((world.get(str(form_id)) or {}).get("state")) or {}).get("sit_sleep")


def furniture_roundtrip(actor_instance, observer, label):
    """The player gets into furniture and out again; the other game's copy must follow both ways by
    replay, while loaded (a copy spawned later starts in its owner's graph state instead)."""
    show(observer, remote_player)
    copy = remote_player(actors_on(observer))
    console_on(actor_instance, f"player.moveto {COOKING_SPIT:08X}")
    wait_for(f"[{actor_instance or 'host'}] player is at the cooking spit", lambda: sit_state(actor_instance, 0x14) == IN_FURNITURE, 5)
    wait_for(f"[{observer or 'host'}] {label} is at the cooking spit", lambda: sit_state(observer, copy) == IN_FURNITURE, 5)
    hold()
    activate_on(actor_instance, COOKING_SPIT)
    wait_for(f"[{actor_instance or 'host'}] player leaves it", lambda: sit_state(actor_instance, 0x14) == 0, 5)
    wait_for(f"[{observer or 'host'}] {label} leaves it", lambda: sit_state(observer, copy) == 0, 5)
    if remote_player(actors_on(observer)) != copy:
        raise TestFailure(f"{label} was respawned on the way: the state was not replayed")


def scenario_furniture_host_player():
    """Host uses the cooking spit and leaves it; the client's copy must follow both ways."""
    furniture_roundtrip("", "client", "host's character")


def scenario_furniture_client_player():
    """Client uses the cooking spit and leaves it; the host's copy must follow both ways."""
    furniture_roundtrip("client", "", "client's character")


def scenario_sneak_host_player():
    """Host sneaks and stands up; the client's copy of the host must follow."""
    sneak_roundtrip("", "client", "host's character")


def scenario_sneak_client_player():
    """Client sneaks and stands up; the host's copy of the client must follow."""
    sneak_roundtrip("client", "", "client's character")


def scenario_draw_client_player():
    """Client readies/sheathes a weapon; the host's copy of the client must follow."""
    draw_roundtrip("client", "", "client's character")


def copy_casting(spell, expected):
    def check(actors):
        target = remote_player(actors)
        return target is not None and (actors[str(target)]["casting"][RIGHT_HAND] == spell) == expected
    return check


def cast_roundtrip(actor_instance, observer, label):
    """Player casts Flames (a held stream) by holding the attack button, and lets go; the other
    game's copy must cast, then stop."""
    give_spell(actor_instance, FLAMES)
    show(observer, remote_player)
    draw(actor_instance)
    cast_spell(actor_instance, None, FLAMES)
    try:
        wait_for(f"[{actor_instance or 'host'}] player casts Flames",
                 lambda: local_player(actor_instance).get("casting", [0, 0])[RIGHT_HAND] == FLAMES, 5)
        wait_on(observer, f"{label} casts Flames", copy_casting(FLAMES, True), 5)
        hold()
    finally:
        stop_casting(actor_instance)
    wait_on(observer, f"{label} stops casting", copy_casting(FLAMES, False), 5)
    sheathe(actor_instance)


def scenario_cast_host_player():
    """Host casts Flames (held) and stops; the client's copy of the host must follow."""
    cast_roundtrip("", "client", "host's character")


def scenario_cast_client_player():
    """Client casts Flames (held) and stops; the host's copy of the client must follow."""
    cast_roundtrip("client", "", "client's character")


def quick_sheathe(actor_instance, observer, label):
    """Player raises a spell and lowers it as soon as the game lets it, a few times. The owner's
    engine fires a late 'MRh_Equipped_Event' it refuses itself: sent anyway, the copy took it after
    the sheathe and kept showing the spell ready."""
    give_spell(actor_instance, FLAMES)
    for _ in range(3):
        draw(actor_instance)
        sheathe(actor_instance)
    wait_on(observer, f"{label}'s copy has the hands down", copy_drawn(False), 5)
    # The late event reaches the copy a few hundred ms after the sheathe: still down after it.
    end = time.monotonic() + 1.5
    while time.monotonic() < end:
        if not copy_drawn(False)(actors_on(observer)):
            raise TestFailure(f"{label}'s copy raised the hands again after the sheathe")
        time.sleep(0.1)


def scenario_quick_sheathe_host_player():
    """Host raises and at once lowers a spell; the client's copy must end with the hands down."""
    quick_sheathe("", "client", "host's character")


def scenario_quick_sheathe_client_player():
    """Client raises and at once lowers a spell; the host's copy must end with the hands down."""
    quick_sheathe("client", "", "client's character")


def hands_survive_reload(actor_instance, observer, label):
    """The player keeps its hands up through a hot reload, then lowers them and reloads again: each
    time the other game respawns its copy, which must show the hands as they are."""
    give_spell(actor_instance, FLAMES)
    for up, verb in ((True, "raised"), (False, "lowered")):
        draw(actor_instance) if up else sheathe(actor_instance)
        wait_on(observer, f"{label}'s copy has the hands {verb}", copy_drawn(up), 5)
        reload_games()
        if bool(local_player(actor_instance).get("weapon_drawn")) != up:
            raise TestFailure(f"the reload changed the {actor_instance or 'host'} player's hands")
        wait_on(observer, f"{label}'s respawned copy has the hands {verb}", copy_drawn(up))


def scenario_hands_survive_reload_host_player():
    """Host's hands up/down across hot reloads; the client's respawned copy must match."""
    hands_survive_reload("", "client", "host's character")


def scenario_hands_survive_reload_client_player():
    """Client's hands up/down across hot reloads; the host's respawned copy must match."""
    hands_survive_reload("client", "", "client's character")


# --- Actor values ---------------------------------------------------------------------------

def health_roundtrip(actor_instance, ref, real_health, observer, observed, label):
    """Damages then restores health on `ref` in its owner's game; the other game's copy must show
    the owner's value each time."""
    def matches(actors):
        target = observed(actors)
        real = real_health()
        return target is not None and close(actors[str(target)].get("health"), real) and real
    show(observer, observed)
    for command, what in ((f"{ref}.damageav health 40", "damaged"), (f"{ref}.restoreav health 40", "restored")):
        before = real_health()
        console_on(actor_instance, command)
        wait_for(f"[{actor_instance or 'host'}] health {what}", lambda: not close(real_health(), before), 5)
        health = wait_on(observer, f"{label} health matches after being {what}", matches, 5)
        log(f"{label}: health {health:.0f} on both sides")
        hold()


def scenario_health_host_player():
    """Host player loses/regains health; the client's copy of the host must match."""
    health_roundtrip("", "player", lambda: local_player("").get("health"), "client", remote_player, "host's character")


def scenario_health_client_player():
    """Client player loses/regains health; the host's copy of the client must match."""
    health_roundtrip("client", "player", lambda: local_player("client").get("health"), "", remote_player,
                     "client's character")


def scenario_health_host_npc():
    """Host's NPC loses/regains health; the client's copy must match."""
    npc, _, copy = spawn_host_npc(LYDIA, restrained=True)
    health_roundtrip("", f"{npc:08X}", lambda: host_npc_health(npc), "client",
                     lambda actors: copy if str(copy) in actors else None, f"NPC copy {copy:08X}")


def scenario_death_host_npc():
    """Host kills then resurrects an NPC it owns; the client's copy must die and come back."""
    npc, _, copy = spawn_host_npc(CHICKEN)
    for command, dead in ((f"{npc:08X}.kill", True), (f"{npc:08X}.resurrect", False)):
        console_on("", command)
        wait_for(f"[host] NPC {'dies' if dead else 'is alive again'}", lambda: host_npc_state(npc).get("dead") == dead, 10)
        wait_on("client", f"NPC copy {copy:08X} {'dies' if dead else 'is alive again'}",
                lambda actors: actors.get(str(copy), {}).get("dead") == dead)
        hold()


# --- Combat: the client hits the host's NPC; its owner applies it ---------------------------

def scenario_melee_client_hits_host_npc():
    """Client hits the host's NPC with a sword: the host's real NPC loses health, the client's copy
    shows the host's value and the client's HUD its health bar."""
    npc, _, copy = spawn_host_npc(LYDIA, restrained=True)
    console_on("", f"{npc:08X}.unequipall")  # her armor takes an iron sword's hit down to ~1
    give_sword("client")
    place_before("client", copy, MELEE_GAP)
    draw("client")
    before = host_npc_health(npc)
    attack("client")
    after = hit_lands(npc, before, "the client's swing")
    log(f"host's NPC health {before:.0f} -> {after:.0f}")
    copy_health_matches(npc, copy)
    health_bar_shows("client", copy)
    hold()


def spell_hits_host_npc(spell, name):
    """Client casts `spell` at the host's NPC. The host's real NPC must take the damage, and the
    client's copy must only ever show the host's value: a copy that drops below its owner applied
    the effect itself (two games simulating one actor)."""
    npc, _, copy = spawn_host_npc(LYDIA, restrained=True)
    give_spell("client", spell)
    place_before("client", copy, SPELL_GAP)
    draw("client")
    before = host_npc_health(npc)
    worst_gap = 0.0
    def landed():
        nonlocal worst_gap
        owner, mirror = host_npc_health(npc), actors_on("client").get(str(copy), {}).get("health")
        if owner is not None and mirror is not None:
            worst_gap = max(worst_gap, owner - mirror)
        return owner is not None and owner < before - 1 and owner
    try:
        cast_spell("client", copy, spell)
        after = wait_for(f"[host] the client's {name} lands", landed, 3)
    finally:
        stop_casting("client")
    log(f"host's NPC health {before:.0f} -> {after:.0f}")
    if worst_gap > 5:
        raise TestFailure(f"the client's copy dropped {worst_gap:.0f} below the host's real NPC: the client applied the spell itself")
    copy_health_matches(npc, copy)
    health_bar_shows("client", copy)
    hold()


def scenario_firebolt_client_hits_host_npc():
    """Client's Firebolt hits the host's NPC: only the host applies it."""
    spell_hits_host_npc(FIREBOLT, "Firebolt")


def scenario_flames_client_hits_host_npc():
    """Client's Flames stream hits the host's NPC: only the host applies it."""
    spell_hits_host_npc(FLAMES, "Flames")


def scenario_client_angers_host_ally():
    """Client keeps hitting an ally NPC of the host with Firebolts: after the friendly-hit tolerance
    it fights the client."""
    npc, _, copy = spawn_host_npc(LYDIA)
    client_on_host = remote_player(actors_on(""))
    with GodMode():
        console_on("", f"{npc:08X}.setav health 3000")
        give_spell("client", FIREBOLT)
        draw("client")
        turned = lambda: host_world(npc).get("ai", {}).get("combat_target") == client_on_host
        for hit in range(1, 11):  # Lydia takes 6-7 before she turns
            place_before("client", copy, SPELL_GAP)  # a hit staggers her back
            before = host_npc_health(npc)
            cast_spell("client", copy, FIREBOLT)
            hit_lands(npc, before, f"Firebolt {hit}", 3)
            if turned():
                break
        wait_for(f"[host] the NPC fights the client's character {client_on_host:08X}", turned, 3)
        log(f"host's NPC turned on the client after {hit} hits")
        hold()


def kill_credit(weapon):
    npc, _, copy = spawn_host_npc(WOLF, restrained=True)
    client_on_host = remote_player(actors_on(""))
    dead = lambda: host_world(npc).get("state", {}).get("dead")
    with GodMode():
        if weapon == "spell":
            give_spell("client", FIREBOLT)
            place_before("client", copy, SPELL_GAP)
        else:
            give_sword("client")
            place_before("client", copy, MELEE_GAP)
        draw("client")
        for hit in range(1, 16):
            before = host_npc_health(npc)
            if weapon == "spell":
                cast_spell("client", copy, FIREBOLT)
            else:
                attack("client")
            hit_lands(npc, before, f"hit {hit}", 3)
            if dead():
                break
        wait_for("[host] the wolf dies", dead, 3)
    killer = wait_for("[host] killer recorded", lambda: host_world(npc).get("ai", {}).get("killer"), 5)
    if killer != client_on_host:
        raise TestFailure(f"host records killer {killer:08X}, expected the client's character {client_on_host:08X}")
    log(f"host's wolf killed by the client's character {killer:08X} after {hit} hits")
    wait_on("client", "the wolf's copy dies too", lambda actors: actors.get(str(copy), {}).get("dead"), 5)
    hold()


def combat_xp(instance):
    """The player's last used combat skill (an actor value, -1 none) and its experience."""
    skill = local_player(instance).get("combat_skill") or {}
    return skill.get("skill", -1), skill.get("xp", 0.0)


def scenario_shared_xp():
    """Everyone shares combat experience: what the client earns hitting the host's NPC, the host's
    player gains in its own last used combat skill (sent once a second)."""
    npc, _, copy = spawn_host_npc(LYDIA, restrained=True)
    for instance in ("", "client"):
        give_sword(instance)
        draw(instance)
    # A hit each first: One-Handed becomes both players' last combat skill (where shared experience goes).
    # On opposite sides of her: two players on one spot push each other out of reach.
    targets, sides = {"": npc, "client": copy}, {"": 0, "client": 180}
    for instance in ("", "client"):
        place_before(instance, targets[instance], MELEE_GAP, sides[instance])  # a hit staggers her back
        before = host_npc_health(npc)
        attack(instance)
        hit_lands(npc, before, f"the {instance or 'host'}'s first swing")
        wait_for(f"[{instance or 'host'}] has a last combat skill", lambda: combat_xp(instance)[0] != -1, 3)
    skill = combat_xp("")[0]
    # Experience goes out once a second: both players' must have settled before measuring.
    seen = {"since": time.monotonic(), "xp": None}
    def settled():
        xp = (combat_xp("")[1], combat_xp("client")[1])
        if xp != seen["xp"]:
            seen.update(since=time.monotonic(), xp=xp)
        return time.monotonic() - seen["since"] >= 1.5 and xp
    host_before, client_before = wait_for("both players' experience settles", settled, 6)
    place_before("client", copy, MELEE_GAP, sides["client"])
    before = host_npc_health(npc)
    attack("client")
    hit_lands(npc, before, "the client's swing")
    earned = wait_for("[client] earns combat experience", lambda: (d := combat_xp("client")[1] - client_before) > 0 and d, 3)
    gained = wait_for("[host] gains the client's experience", lambda: (d := combat_xp("")[1] - host_before) > 0 and d, 3)
    log(f"client earned {earned:.2f}, host gained {gained:.2f} in skill {skill}")
    if abs(gained - earned) > 0.01:
        raise TestFailure(f"host gained {gained:.2f}, the client earned {earned:.2f}")
    hold()


def scenario_client_spell_kill_credit():
    """Client kills a host NPC with a spell: the host records the client's character as the killer."""
    kill_credit("spell")


def scenario_client_melee_kill_credit():
    """Client kills a host NPC with a sword: the host records the client's character as the killer."""
    kill_credit("melee")


# --- Magic effects --------------------------------------------------------------------------

def scenario_burning_shows_on_copy():
    """Client's Flames set the host's NPC burning: the host's game applies the effect, and the
    client's copy shows the same fire (its owner's active effects, played as visuals only). When
    the burning ends there, it ends on the copy too."""
    npc, _, copy = spawn_host_npc(LYDIA, restrained=True)
    give_spell("client", FLAMES)
    place_before("client", copy, SPELL_GAP)
    draw("client")
    try:
        cast_spell("client", copy, FLAMES)
        burning = wait_for("[host] the NPC burns", lambda: visuals("", npc), 3)
        log(f"host's NPC burning: {', '.join(f'{v:X}' for v in sorted(burning))}")
        wait_for("[client] its copy shows the same fire", lambda: burning <= visuals("client", copy), 3)
    finally:
        stop_casting("client")
    wait_for("[host] the burning ends", lambda: not (burning & visuals("", npc)), 10)
    wait_for("[client] the copy's fire ends too", lambda: not (burning & visuals("client", copy)), 3)
    hold()


# --- Objects: doors, locks ------------------------------------------------------------------

def cell_objects(instance):
    """The doors and containers in that game's cell, by reference id ({"cmd": "objects"})."""
    select_instance(instance)
    response = rpc({"cmd": "objects"}) or {}
    return {o["id"]: o for o in response.get("objects", [])}


def inner_door():
    """A door in the host's cell that is not a load door (QASmoke has two), closed."""
    doors = [o for o in cell_objects("").values() if o["door"] and not o["load_door"]]
    if not doors:
        raise TestFailure("no door in the host's cell that stays in it")
    return min(doors, key=lambda o: o["id"])["id"]


DOOR_OPEN, DOOR_CLOSED = 1, 3  # TESObjectREFR::OpenState


def object_command(instance, request):
    """{"cmd": "activate"/"lock", ...} in that game; the player does it, as by hand."""
    player_command(instance, request)
    log(f"[{instance or 'host'}] {request['cmd']} {request['object']:X}")


def activate_on(instance, door):
    object_command(instance, {"cmd": "activate", "object": door})


def lock_on(instance, door, locked, level=0):
    object_command(instance, {"cmd": "lock", "object": door, "locked": locked, "level": level})


def door_state(instance, door):
    return cell_objects(instance).get(door, {}).get("open_state")


def door_lock(instance, door):
    o = cell_objects(instance).get(door, {})
    return (o.get("locked"), o.get("lock_level")) if o.get("has_lock") else (False, None)


def scenario_door_opens_for_both():
    """A player opens a door: the other game's door opens too (the bridge relays the activation and
    the other game's copy of the player opens it there). Then the host closes it for both."""
    door = inner_door()
    try:
        for name in ("", "client"):
            if door_state(name, door) != DOOR_CLOSED:
                raise TestFailure(f"[{name or 'host'}] door {door:X} is not closed to begin with")
        activate_on("client", door)
        for name in ("client", ""):
            wait_for(f"[{name or 'host'}] door {door:X} is open", lambda name=name: door_state(name, door) == DOOR_OPEN, 10)
        activate_on("", door)
        for name in ("", "client"):
            wait_for(f"[{name or 'host'}] door {door:X} is closed again", lambda name=name: door_state(name, door) == DOOR_CLOSED, 10)
    finally:
        for name in ("", "client"):
            if door_state(name, door) == DOOR_OPEN:
                activate_on(name, door)


def scenario_lock_reaches_others():
    """The host locks a door: the client's door is locked too, at the same level. The client
    unlocks it: the host's is unlocked."""
    door = inner_door()
    try:
        lock_on("", door, True, 50)
        wait_for(f"[client] door {door:X} is locked at 50", lambda: door_lock("client", door) == (True, 50), 10)
        lock_on("client", door, False, 50)
        wait_for(f"[host] door {door:X} is unlocked", lambda: not door_lock("", door)[0], 10)
    finally:
        for name in ("", "client"):
            if door_lock(name, door)[0]:
                lock_on(name, door, False)


def scenario_load_door_not_replayed():
    """In the inn, the client walks out through its door: the host's game gets the activation and
    does not replay it (the door would take the client's copy, or anyone, out of the host's cell);
    only the client's own game takes it through."""
    try:
        both_go(INN)
        doors = [o for o in cell_objects("").values() if o["load_door"]]
        if not doors:
            raise TestFailure(f"no load door in {INN}")
        door = doors[0]["id"]
        log_file = plugin_log("")
        start = log_file.stat().st_size
        activate_on("client", door)
        wait_for(f"[host] load door {door:X} not replayed",
                 lambda: f"Load door {door:X} not replayed" in log_file.read_bytes()[start:].decode(errors="replace"), 10)
        wait_for("[client] the client went out through the door", lambda: door not in cell_objects("client"), 30)
        if door not in cell_objects(""):
            raise TestFailure("the host left the inn: the activation took its player through")
    finally:
        both_go(HOME_CELL)



# --- Chat -----------------------------------------------------------------------------------

# DirectInput scan codes for typing on a US layout (the test prefix's): (code, shift).
ENTER_KEY, ESCAPE_KEY, SHIFT_KEY = 0x1C, 0x01, 0x2A
TYPED_KEYS = {**{c: (code, False) for c, code in zip("qwertyuiop", range(0x10, 0x1A))},
              **{c: (code, False) for c, code in zip("asdfghjkl", range(0x1E, 0x27))},
              **{c: (code, False) for c, code in zip("zxcvbnm", range(0x2C, 0x33))},
              **{c: (code, False) for c, code in zip("1234567890", range(0x02, 0x0C))},
              " ": (0x39, False), ",": (0x33, False), ".": (0x34, False), "'": (0x28, False),
              "!": (0x02, True), "?": (0x35, True)}


def key(instance, code, down):
    player_command(instance, {"cmd": "key", "scan": code, "down": down})


def tap_key(instance, code, shift=False):
    """A key pressed and let go, as the keyboard sends it (through the game's input dispatch)."""
    if shift:
        key(instance, SHIFT_KEY, True)
    key(instance, code, True)
    key(instance, code, False)
    if shift:
        key(instance, SHIFT_KEY, False)


def type_text(instance, text):
    for char in text:
        code, shift = TYPED_KEYS[char.lower()] if char.isalpha() else TYPED_KEYS[char]
        tap_key(instance, code, shift or char.isupper())


def chat(instance):
    return game_state(instance).get("chat") or {}


def open_chat(instance):
    """The player opens the chat line with Enter, once the game takes input (as a player waits)."""
    wait_for(f"[{instance or 'host'}] the game takes input", lambda: takes_input(instance), 10)
    tap_key(instance, ENTER_KEY)
    wait_for(f"[{instance or 'host'}] the chat line is open", lambda: chat(instance).get("open"), 5)


def say(instance, text):
    """The player types a line into the chat and sends it with Enter."""
    open_chat(instance)
    type_text(instance, text)
    wait_for(f"[{instance or 'host'}] the chat line reads {text!r}", lambda: chat(instance).get("draft") == text, 5)
    tap_key(instance, ENTER_KEY)
    wait_for(f"[{instance or 'host'}] the chat line is closed", lambda: not chat(instance).get("open"), 5)


def chat_has(instance, name, text):
    return any(l["name"] == name and l["text"] == text for l in chat(instance).get("lines", []))


def scenario_chat_reaches_everyone():
    """The client types a line (Enter, the text, Enter): both games show it under the client's name, and
    the client's character did not walk while it typed ("w" is forward). The host answers. A line
    abandoned with Escape reaches nobody."""
    client_name = next(n for n, host in session_players("").items() if not host)
    host_name = next(n for n, host in session_players("client").items() if host)
    before = player_snapshot("client")["placement"]["position"]

    say("client", "Hello there, wow!")
    for name in ("", "client"):
        wait_for(f"[{name or 'host'}] shows the client's line", lambda name=name: chat_has(name, client_name, "Hello there, wow!"), 5)
    moved = distance(player_snapshot("client")["placement"]["position"], before)
    if moved > 5:
        raise TestFailure(f"the client's character moved {moved:.0f} units while it typed")

    say("", "Welcome!")
    wait_for("[client] shows the host's line", lambda: chat_has("client", host_name, "Welcome!"), 5)

    open_chat("client")
    type_text("client", "never mind")
    tap_key("client", ESCAPE_KEY)
    wait_for("[client] the chat line is closed", lambda: not chat("client").get("open"), 5)
    hold()
    if any(l["text"] == "never mind" for l in chat("").get("lines", [])):
        raise TestFailure("a line abandoned with Escape reached the host")
    if not takes_input("client"):
        raise TestFailure("[client] Escape in the chat opened a menu (the game got the key)")

# --- Map markers ----------------------------------------------------------------------------

QUICK_MAP_KEY, PLACE_MARKER_KEY, TWEEN_MENU_KEY = 0x32, 0x19, 0x0F   # M, P, Tab (the game's controlmap.txt)


def map_marker(instance):
    return game_state(instance).get("map_marker")


def map_takes_input(instance):
    """The map is open and ready (MapMenu's controlsReady), and nothing covers it (its fade-in)."""
    select_instance(instance)
    status = read_status() or {}
    return (status.get("game") or {}).get("map_ready") and set(status.get("menus") or []) <= {"HUD Menu", "MapMenu", "Cursor Menu"}


def place_map_marker(instance):
    """The player opens the map (M), places a marker where it is centred (P) and closes it (Tab)."""
    who = f"[{instance or 'host'}]"
    wait_for(f"{who} the game takes input", lambda: takes_input(instance), 10)
    tap_key(instance, QUICK_MAP_KEY)
    wait_for(f"{who} the map is open and takes input", lambda: map_takes_input(instance), 10)
    tap_key(instance, PLACE_MARKER_KEY)
    wait_for(f"{who} its marker is on the map", lambda: map_marker(instance), 5)
    tap_key(instance, TWEEN_MENU_KEY)
    wait_for(f"{who} the map is closed", lambda: takes_input(instance), 10)
    return map_marker(instance)


# The "move or remove your marker?" box's buttons, in window pixels at the test games' 1280x720. Vanilla
# message boxes take only the mouse (docs/re/systems/map-markers.md).
LEAVE_IT_BUTTON, REMOVE_IT_BUTTON = (630, 393), (746, 393)


def menus(instance):
    select_instance(instance)
    return set((read_status() or {}).get("menus") or [])


def move_cursor(instance, target):
    """The player moves the mouse until the menu cursor is on target (window pixels)."""
    who = f"[{instance or 'host'}]"
    for _ in range(5):
        position = game_state(instance).get("cursor")
        if not position:
            raise TestFailure(f"{who} no menu cursor")
        delta = (round(target[0] - position[0]), round(target[1] - position[1]))
        if max(abs(delta[0]), abs(delta[1])) <= 2:
            return
        player_command(instance, {"cmd": "mouse", "dx": delta[0], "dy": delta[1]})
        wait_for(f"{who} the cursor moves", lambda: game_state(instance).get("cursor") != position, 2)
    raise TestFailure(f"{who} the cursor did not reach {target}: at {game_state(instance).get('cursor')}")


def click(instance):
    player_command(instance, {"cmd": "mouse", "button": 0, "down": True})
    player_command(instance, {"cmd": "mouse", "button": 0, "down": False})


def answer_marker_box(instance, button):
    """The player clicks a button of the "move or remove" box."""
    move_cursor(instance, button)
    click(instance)
    wait_for(f"[{instance or 'host'}] the box is closed", lambda: "MessageBoxMenu" not in menus(instance), 5)


def remove_map_marker(instance):
    """The player opens the map (M), presses P on its marker and clicks "Remove It", then closes the map."""
    who = f"[{instance or 'host'}]"
    wait_for(f"{who} the game takes input", lambda: takes_input(instance), 10)
    tap_key(instance, QUICK_MAP_KEY)
    wait_for(f"{who} the map is open and takes input", lambda: map_takes_input(instance), 10)
    tap_key(instance, PLACE_MARKER_KEY)
    wait_for(f"{who} asks to move or remove the marker", lambda: "MessageBoxMenu" in menus(instance), 5)
    answer_marker_box(instance, REMOVE_IT_BUTTON)
    wait_for(f"{who} its marker is gone", lambda: not map_marker(instance), 5)
    wait_for(f"{who} the map takes input", lambda: map_takes_input(instance), 5)
    tap_key(instance, TWEEN_MENU_KEY)
    wait_for(f"{who} the map is closed", lambda: takes_input(instance), 10)


def same_marker(marker, placed):
    return bool(marker) and marker["world"] == placed["world"] and distance(marker["position"], placed["position"]) < 2


def scenario_map_marker_reaches_everyone():
    """Outdoors, the client places a marker on its map: the host's map shows it at the same spot. Then the
    host places one: the client's moves there. The client removes it (P on it, "Remove It"): the host's
    goes too. (Each placement starts without a marker: one already there turns P into that box.)"""
    def clear_markers():
        for name in ("", "client"):
            if "MessageBoxMenu" in menus(name):   # a failure left the box up: it blocks the game until answered
                answer_marker_box(name, LEAVE_IT_BUTTON)
            player_command(name, {"cmd": "clear_map_marker"})
            wait_for(f"[{name or 'host'}] has no map marker", lambda name=name: not map_marker(name), 5)
    try:
        both_go(OUTDOOR_CELL)
        clear_markers()
        placed = place_map_marker("client")
        wait_for("[host] shows the client's marker", lambda: same_marker(map_marker(""), placed), 5)

        clear_markers()
        placed = place_map_marker("")
        wait_for("[client] shows the host's marker", lambda: same_marker(map_marker("client"), placed), 5)

        remove_map_marker("client")
        wait_for("[host] the marker is gone", lambda: not map_marker(""), 5)
    finally:
        clear_markers()
        both_go(HOME_CELL)


# --- World: time, weather -------------------------------------------------------------------

def scenario_host_time_reaches_client():
    """The host sets the hour: it stays set on the host (the host's clock is the session's) and the
    client's clock follows."""
    hour = (game_state("").get("time", {}).get("hour", 0) + 6) % 24
    console_on("", f"set gamehour to {hour:.2f}")
    def near(value, target, tolerance=0.2):
        return value is not None and min(abs(value - target), 24 - abs(value - target)) < tolerance
    wait_for("[client] the client's clock follows the host's",
             lambda: near(game_state("client").get("time", {}).get("hour"), hour), 10)
    # And it holds on both: nothing else drives the host's clock back.
    end = time.monotonic() + 2.0
    while time.monotonic() < end:
        for name in ("", "client"):
            if not near(game_state(name).get("time", {}).get("hour"), hour, 0.3):
                raise TestFailure(f"[{name or 'host'}] the hour moved off {hour:.2f}: something else drives the clock")
        time.sleep(0.2)


def scenario_weather_follows_host():
    """Both players outdoors; the host's weather changes: the client's game takes it (the host's
    weather is the session's), and keeps it while its own weather system would move on."""
    try:
        both_go(OUTDOOR_CELL)
        for name in ("", "client"):
            wait_for(f"[{name or 'host'}] outdoors, under a sky", lambda name=name: game_state(name).get("weather"), 30)
        weather = OTHER_WEATHER if game_state("").get("weather") == CLEAR_WEATHER else CLEAR_WEATHER
        console_on("", f"fw {weather:x}")
        wait_for("[host] shows the forced weather", lambda: game_state("").get("weather") == weather, 10)
        wait_for("[client] follows the host's weather", lambda: game_state("client").get("weather") == weather, 10)
        end = time.monotonic() + 3.0  # long enough for the client's own weather system to move on
        while time.monotonic() < end:
            if (held := game_state("client").get("weather")) != weather:
                raise TestFailure(f"the client lost the host's weather: {held:X}, host's {weather:X}")
            time.sleep(0.2)
    finally:
        both_go(HOME_CELL)


# --- Input: real keyboard and mouse (not in a default run) ----------------------------------

def game_window(instance):
    select_instance(instance)
    env = dict(os.environ, DISPLAY=VIRTUAL_DISPLAY)
    windows = subprocess.run(["xdotool", "search", "--name", "^Skyrim"], env=env, capture_output=True, text=True).stdout.split()
    if not windows:
        raise TestFailure(f"[{instance or 'host'}] game window not found on {VIRTUAL_DISPLAY}")
    return env, windows[0]


def hold_key(instance, key, seconds):
    """Holds a key in an instance's game for `seconds` (real XTEST input on the off-screen display:
    the game polls the keyboard once a frame, so a quick synthetic press can fall between two)."""
    env, window = game_window(instance)
    for command in (["windowfocus", window], ["keydown", key]):
        subprocess.run(["xdotool", *command], env=env, check=True, stderr=subprocess.DEVNULL)
    time.sleep(seconds)
    subprocess.run(["xdotool", "keyup", key], env=env, check=True, stderr=subprocess.DEVNULL)


def key_on(instance, key):
    hold_key(instance, key, 0.1)


def mouse_on(instance, action, button=1):
    """mousedown/mouseup in an instance's game window (button 1 = attack/cast with the right hand).
    Wine's DirectInput mouse ignores synthetic per-window events: real (XTEST) input."""
    env, window = game_window(instance)
    for command in (["windowfocus", window], ["mousemove", "--window", window, "640", "360"], [action, str(button)]):
        subprocess.run(["xdotool", *command], env=env, check=True, stderr=subprocess.DEVNULL)


def scenario_input_keyboard_draw():
    """The host presses R to ready a sword, then again to sheathe it: the game reacts to the key,
    and the client's copy of the host follows."""
    give_sword("")
    for state, verb in ((WEAPON_DRAWN, "draws"), (WEAPON_SHEATHED, "sheathes")):
        # The game ignores Ready Weapon while a draw or sheathe still plays, a player's key as well.
        wait_for("[host] the last draw or sheathe has played out", lambda: hands_settled(""), 5)
        key_on("", "r")
        wait_for(f"[host] the R key {verb} the sword", lambda: weapon_state("") == state, 3)
        wait_on("client", f"host's character {verb} the sword", copy_drawn(state == WEAPON_DRAWN), 5)


def scenario_input_mouse_cast():
    """The client holds the mouse button with Flames ready, then lets go: the game casts while it is
    held, and the host's copy of the client casts, then stops."""
    give_spell("client", FLAMES)
    draw("client")
    mouse_on("client", "mousedown")
    try:
        wait_for("[client] holding the button casts Flames",
                 lambda: local_player("client").get("casting", [0, 0])[RIGHT_HAND] == FLAMES, 3)
        wait_on("", "client's character casts Flames", copy_casting(FLAMES, True), 5)
    finally:
        mouse_on("client", "mouseup")
    wait_on("", "client's character stops casting", copy_casting(FLAMES, False), 5)



# --- Isolation: every scenario starts from the same baseline and leaves nothing behind ----------
# reset_baseline() puts both players back (casts, hands, equipment, values, combat, place) and
# removes every NPC; scenarios spawn their own NPCs fresh. check_baseline() lists what still
# differs. The runner resets and checks after every scenario: one that leaves something the reset
# cannot undo (usually a copy stuck out of sync) fails, instead of the innocent one after it.
HOME_FILE = "home.json"     # in each game's control dir, written by `duo --keep-running`
TEST_ITEMS = {IRON_SWORD}   # given out by scenarios, never part of the baseline
HOME_TOLERANCE = 40.0       # units from the recorded spot
PLAYER_VALUES = ("health", "magicka", "stamina")


def save_home():
    """Records each player's spot, heading, clothes and level as the baseline the scenarios start from."""
    for name in ("", "client"):
        me = player_snapshot(name)
        placement = me.get("placement") or {}
        if not placement.get("position"):
            raise TestFailure(f"{name or 'host'}: no player snapshot to record the baseline from")
        home = {"position": placement["position"], "heading": placement["rotation"][2],
                "equipment": sorted(i for i in me.get("equipment", []) if i not in TEST_ITEMS),
                "level": game_state(name).get("level")}
        atomic_write(CONTROL / HOME_FILE, json.dumps(home) + "\n")
    log("Recorded both players' baseline (home.json)")


def load_home(instance):
    select_instance(instance)
    try:
        return json.loads((CONTROL / HOME_FILE).read_text())
    except (OSError, ValueError):
        return None


def owned_npcs(instance):
    return [int(a) for a in local_player(instance).get("local_actors", {})]


def foreign_in_hands(state, home):
    """Something in the hands that the baseline does not wear (a test's sword or spell)."""
    return any(item and item not in home["equipment"] for item in state.get("hands") or [])


def reset_player(instance, home):
    me = player_snapshot(instance)
    state = me.get("state") or {}
    select_instance(instance)
    # First person, and no idle camera orbiting the player: what `watch` shows is the player's view.
    run_console('setini "fAutoVanityModeDelay:Camera" 1000000')
    # Subtitles on in a game started before the prefs had them (prepare_prefix).
    run_console('setini "bDialogueSubtitles:Interface" 1')
    run_console('setini "bGeneralSubtitles:Interface" 1')
    rpc({"cmd": "first_person"})
    for button in sorted(HELD_BUTTONS.get(instance, set())):
        release(instance, button)
    for source in (0, 1):
        rpc({"cmd": "interrupt", "caster": 0x14, "source": source})
    if state.get("sneaking"):
        sneak(instance, False)
    if state.get("weapon_drawn") or state.get("weapon_state"):
        # The sheathe action, as a player's: that is what copies replay (setting the engine flag
        # directly leaves them drawn).
        sheathe(instance)
    if sorted(me.get("equipment", [])) != home["equipment"] or foreign_in_hands(state, home):
        run_console("player.unequipall")
        for item in home["equipment"]:
            run_console(f"player.equipitem {item:08X}")
    if home.get("level") and game_state(instance).get("level") != home["level"]:
        run_console(f"player.setlevel {home['level']}")
    values = me.get("values") or {}
    for value in PLAYER_VALUES:
        if values.get(value, 0) < values.get(f"max_{value}", 0) - 0.5:
            run_console(f"player.restoreav {value} 100000")
    if (me.get("ai") or {}).get("combat_target"):
        run_console("player.stopcombat")
    position = (me.get("placement") or {}).get("position")
    goal = home["position"]
    if not position or distance(position, goal) > HOME_TOLERANCE:
        # Lifted a little and dropped onto the floor there (see place()).
        run_console(f"player.setpos z {goal[2] + 20:.0f}")
        run_console(f"player.setpos x {goal[0]:.0f}")
        run_console(f"player.setpos y {goal[1]:.0f}")
    run_console(f"player.setangle z {math.degrees(home['heading']) % 360:.0f}")
    run_console("player.setangle x 0")


def reset_baseline():
    if game_state("client").get("online") is False:
        rejoin_client()
    for name in ("", "client"):
        for npc in owned_npcs(name):
            console_on(name, f"{npc:08X}.disable")
            console_on(name, f"{npc:08X}.markfordelete")
    for name in ("", "client"):
        reset_player(name, load_home(name))


def check_baseline():
    """What differs from the baseline in either game (empty: a scenario can start)."""
    problems = []
    for name in ("", "client"):
        game = name or "host"
        home = load_home(name)
        me = player_snapshot(name)
        state = me.get("state") or {}
        values = me.get("values") or {}
        if any(state.get("casting") or []):
            problems.append(f"{game}: player still casting {state['casting']}")
        if foreign_in_hands(state, home):
            problems.append(f"{game}: player holds {state['hands']} in the hands")
        if state.get("weapon_drawn"):
            problems.append(f"{game}: player's hands are raised")
        if sorted(me.get("equipment", [])) != home["equipment"]:
            problems.append(f"{game}: player wears {sorted(me.get('equipment', []))}, baseline {home['equipment']}")
        for value in PLAYER_VALUES:
            if values.get(value, 0) < values.get(f"max_{value}", 0) - 1:
                problems.append(f"{game}: player {value} {values.get(value):.0f} of {values.get(f'max_{value}'):.0f}")
        if (me.get("ai") or {}).get("combat_target"):
            problems.append(f"{game}: player in combat with {me['ai']['combat_target']:08X}")
        if home.get("level") and game_state(name).get("level") != home["level"]:
            problems.append(f"{game}: player level {game_state(name).get('level')}, baseline {home['level']}")
        position = (me.get("placement") or {}).get("position")
        if not position or distance(position, home["position"]) > HOME_TOLERANCE:
            problems.append(f"{game}: player at {[round(v) for v in position or []]}, "
                            f"baseline {[round(v) for v in home['position']]}")
        if npcs := owned_npcs(name):
            problems.append(f"{game}: still owns NPCs {[f'{n:08X}' for n in npcs]}")
        actors = actors_on(name)
        if remote_player(actors) is None:
            problems.append(f"{game}: does not show the other player")
        if copies := remote_npcs(actors):
            problems.append(f"{game}: still shows NPC copies {[f'{n:08X}' for n in copies]}")
    if not problems:
        problems = check_desync(samples=1)
    return problems


def settle_baseline(timeout=8.0):
    """Resets both games and waits until they match the baseline; returns what still differs.
    A failed scenario can leave a player's copy on an animation its owner moved past: the player
    then draws and sheathes (a fresh action for the copy to replay). If even that does not settle
    it, a hot reload respawns every copy. The scenario that left it has already failed; this only
    gives the next one a clean start."""
    def settle(seconds):
        deadline = time.monotonic() + seconds
        while True:
            problems = check_baseline()
            if not problems or time.monotonic() > deadline:
                return problems
            time.sleep(0.1)
    try:
        reset_baseline()
        if not (problems := settle(timeout / 2)):
            return []
        for name, role in (("", "host"), ("client", "client")):
            if any(f"[{role} 00000014" in problem and "animation." in problem for problem in problems):
                log(f"baseline: the {role}'s copy shows a stale animation; the {role} draws and sheathes")
                draw(name)
                sheathe(name)
        reset_baseline()
        if not (problems := settle(timeout / 2)):
            return []
        log("baseline: still not reached (" + " | ".join(problems) + "); hot reload respawns the copies")
        reload_games()
        reset_baseline()
        return settle(timeout)
    except TestFailure as error:  # e.g. a game that crashed: the scenario before it is to blame
        return [f"reset failed: {error}"]


SCENARIOS = {
    # session
    "session-roster": scenario_session_roster,
    "party-panel": scenario_party_panel,
    "party-shows-levels": scenario_party_shows_levels,
    "late-join-client": scenario_late_join_client,
    "host-rejoins": scenario_host_rejoins,
    "host-kicks-client": scenario_host_kicks_client,
    # characters
    "steady-state": scenario_steady_state,
    "move-host-npc": scenario_move_host_npc,
    "remove-host-npc": scenario_remove_host_npc,
    "copy-ends-voice-line": scenario_copy_ends_voice_line,
    "teleport-keeps-copies": scenario_teleport_keeps_copies,
    "party-changes-cells": scenario_party_changes_cells,
    "party-spreads-on-arrival": scenario_party_spreads_on_arrival,
    "party-uses-door": scenario_party_uses_door,
    "party-fast-travels": scenario_party_fast_travels,
    # inventory
    "equip-host-player": scenario_equip_host_player,
    "equip-client-player": scenario_equip_client_player,
    "equip-host-npc": scenario_equip_host_npc,
    "repair-equipment-drift": scenario_repair_equipment_drift,
    # hands
    "draw-host-player": scenario_draw_host_player,
    "draw-client-player": scenario_draw_client_player,
    "sneak-host-player": scenario_sneak_host_player,
    "furniture-host-player": scenario_furniture_host_player,
    "furniture-client-player": scenario_furniture_client_player,
    "sneak-client-player": scenario_sneak_client_player,
    "dive-host-player": scenario_dive_host_player,
    "dive-client-player": scenario_dive_client_player,
    "cast-host-player": scenario_cast_host_player,
    "cast-client-player": scenario_cast_client_player,
    "quick-sheathe-host-player": scenario_quick_sheathe_host_player,
    "quick-sheathe-client-player": scenario_quick_sheathe_client_player,
    "hands-survive-reload-host-player": scenario_hands_survive_reload_host_player,
    "hands-survive-reload-client-player": scenario_hands_survive_reload_client_player,
    # values
    "health-host-player": scenario_health_host_player,
    "health-client-player": scenario_health_client_player,
    "health-host-npc": scenario_health_host_npc,
    "death-host-npc": scenario_death_host_npc,
    # combat
    "melee-client-hits-host-npc": scenario_melee_client_hits_host_npc,
    "firebolt-client-hits-host-npc": scenario_firebolt_client_hits_host_npc,
    "flames-client-hits-host-npc": scenario_flames_client_hits_host_npc,
    "client-angers-host-ally": scenario_client_angers_host_ally,
    "client-spell-kill-credit": scenario_client_spell_kill_credit,
    "shared-xp": scenario_shared_xp,
    "client-melee-kill-credit": scenario_client_melee_kill_credit,
    # magic
    "burning-shows-on-copy": scenario_burning_shows_on_copy,
    # objects
    "door-opens-for-both": scenario_door_opens_for_both,
    "lock-reaches-others": scenario_lock_reaches_others,
    "load-door-not-replayed": scenario_load_door_not_replayed,
    # chat
    "chat-reaches-everyone": scenario_chat_reaches_everyone,
    "map-marker-reaches-everyone": scenario_map_marker_reaches_everyone,
    # world
    "host-time-reaches-client": scenario_host_time_reaches_client,
    "weather-follows-host": scenario_weather_follows_host,
    # input (real keyboard and mouse; only when named or tagged)
    "input-keyboard-draw": scenario_input_keyboard_draw,
    "input-mouse-cast": scenario_input_mouse_cast,
}

# What each scenario exercises, for running only the ones a change can affect:
# `scenario --tag movement,spawn`. The full suite still runs before a commit. Scenarios tagged
# `input` drive the real keyboard and mouse and run only when named or tagged.
TAGS = {
    "session-roster": {'session'},
    "party-panel": {'session', 'ui', 'reload'},
    "party-shows-levels": {'session', 'ui'},
    "late-join-client": {'session', 'spawn', 'inventory', 'hands', 'values'},
    "host-rejoins": {'session', 'spawn'},
    "host-kicks-client": {'session', 'spawn'},
    "steady-state": {'session', 'movement', 'spawn'},
    "move-host-npc": {'movement'},
    "remove-host-npc": {'spawn'},
    "copy-ends-voice-line": {'dialogue', 'ai'},
    "teleport-keeps-copies": {'spawn', 'movement', 'cells'},
    "party-changes-cells": {'spawn', 'cells'},
    "party-spreads-on-arrival": {'cells'},
    "party-uses-door": {'spawn', 'cells', 'objects'},
    "party-fast-travels": {'spawn', 'cells', 'world'},
    "equip-host-player": {'inventory'},
    "equip-client-player": {'inventory'},
    "equip-host-npc": {'inventory'},
    "repair-equipment-drift": {'inventory'},
    "draw-host-player": {'hands'},
    "draw-client-player": {'hands'},
    "sneak-host-player": {'movement', 'ai'},
    "sneak-client-player": {'movement', 'ai'},
    "dive-host-player": {'movement', 'ai', 'dialogue', 'cells'},
    "dive-client-player": {'movement', 'ai', 'dialogue', 'cells'},
    "furniture-host-player": {'movement'},
    "furniture-client-player": {'movement'},
    "cast-host-player": {'hands', 'magic'},
    "cast-client-player": {'hands', 'magic'},
    "quick-sheathe-host-player": {'hands', 'magic'},
    "quick-sheathe-client-player": {'hands', 'magic'},
    "hands-survive-reload-host-player": {'hands', 'spawn', 'reload'},
    "hands-survive-reload-client-player": {'hands', 'spawn', 'reload'},
    "health-host-player": {'values'},
    "health-client-player": {'values'},
    "health-host-npc": {'values'},
    "death-host-npc": {'values', 'spawn'},
    "melee-client-hits-host-npc": {'combat', 'values'},
    "firebolt-client-hits-host-npc": {'combat', 'magic', 'values'},
    "flames-client-hits-host-npc": {'combat', 'magic', 'values'},
    "client-angers-host-ally": {'combat', 'magic'},
    "client-spell-kill-credit": {'combat', 'magic'},
    "shared-xp": {'combat'},
    "client-melee-kill-credit": {'combat'},
    "burning-shows-on-copy": {'combat', 'magic'},
    "door-opens-for-both": {'objects'},
    "lock-reaches-others": {'objects'},
    "load-door-not-replayed": {'objects', 'cells'},
    "chat-reaches-everyone": {'ui', 'session'},
    "map-marker-reaches-everyone": {'ui', 'world'},
    "host-time-reaches-client": {'world'},
    "weather-follows-host": {'world', 'cells'},
    "input-keyboard-draw": {'input', 'hands'},
    "input-mouse-cast": {'input', 'hands', 'magic'},
}


def status_table():
    """One row per character, as each game sees it: owner's values next to the copies'."""
    games = {}
    for name in ("", "client"):
        select_instance(name)
        games[name or "host"] = read_status() or {}
    host, client = games["host"], games["client"]
    rows = []

    def describe(entry):
        if not entry:
            return "-"
        hp = entry.get("health")
        parts = [f"hp {hp:5.0f}" if isinstance(hp, (int, float)) else "hp     ?"]
        if entry.get("dead"):
            parts.append("DEAD")
        if entry.get("weapon_drawn"):
            parts.append("drawn")
        if any(entry.get("casting") or []):
            parts.append("casting")
        return " ".join(parts)

    host_player = host.get("player") or {}
    client_player = client.get("player") or {}
    host_on_client = (client.get("actors") or {}).get(str(remote_player(client.get("actors") or {})))
    client_on_host = (host.get("actors") or {}).get(str(remote_player(host.get("actors") or {})))
    rows.append(("host's character", describe(host_player), describe(host_on_client)))
    rows.append(("client's character", describe(client_on_host), describe(client_player)))
    # Pair each host NPC with the client copy linked to the same server actor; anything left
    # unpaired on either side is a desync (a copy the host no longer has, or a missing copy).
    copies = {v.get("server_id"): v for v in (client.get("actors") or {}).values() if not v.get("remote_player")}
    for npc, owned in sorted((host_player.get("local_actors") or {}).items()):
        copy = copies.pop(owned.get("server_id"), None)
        gap = f"  {distance(owned['position'], copy['actual']):.0f} apart" if copy and owned.get("position") else ""
        mine, theirs = owned.get("identity") or {}, (copy or {}).get("identity") or {}
        # Same name and race, or the copy is a different-looking actor (built from the wrong record).
        wrong = "" if not copy or (mine.get("name"), mine.get("race")) == (theirs.get("name"), theirs.get("race")) \
            else f" WRONG-ACTOR({theirs.get('name') or '?'})"
        label = f"NPC {mine.get('name') or ''} {int(npc):08X}".replace("  ", " ")[:20]
        rows.append((label, describe(owned), (describe(copy) + wrong if copy else "MISSING") + gap))
    for server_id, copy in copies.items():
        rows.append((f"NPC server {server_id}", "GONE on host", describe(copy)))
    lines = [f"{'':20s} {'HOST game':24s} {'CLIENT game':24s}"]
    lines += [f"{who:20s} {h:24s} {c:24s}" for who, h, c in rows]
    return "\n".join(lines)


# --- Desync checker -------------------------------------------------------------------------
# Each game's status.json carries "world": a snapshot of every loaded actor (TestHarness/Status.cpp
# Snapshot()). Every actor has exactly one owner game; the other game holds a copy linked by the
# same server id. The checker pairs them and compares every snapshot field, so a field added to
# Snapshot() is checked without touching this code. Rules below only say how to compare a field
# (tolerances) or why a field is expected to differ.

POSITION_TOLERANCE = 50.0   # units; copies trail the owner by the interpolation delay
ANGLE_TOLERANCE = 0.35      # radians
VALUE_TOLERANCE = 1.0       # actor values


def flatten(tree, prefix=""):
    if isinstance(tree, dict):
        out = {}
        for key, value in tree.items():
            out.update(flatten(value, f"{prefix}{key}."))
        return out
    return {prefix[:-1]: tree}


def compare_field(field, owner, copy, owner_snapshot):
    """Returns None when the copy agrees with the owner, else a short reason."""
    if field.startswith(("mod.", "ai.", "graph")) or field in ("role", "identity.template_base", "identity.temporary_base"):
        return None  # bookkeeping (Local vs Remote) and AI (owner only) differ by design; the live
                     # graph changes every frame (compared over time by `coop_test.py graph`)
    if field == "effects":
        return None  # copies never hold effects (their owner applies them); what shows is "visuals"
    if field == "visuals":
        missing, extra = set(owner or []) - set(copy or []), set(copy or []) - set(owner or [])
        if not missing and not extra:
            return None
        return " ".join(filter(None, [missing and "copy lacks " + ",".join(f"{v:08X}" for v in sorted(missing)),
                                      extra and "copy also shows " + ",".join(f"{v:08X}" for v in sorted(extra))]))
    if field == "identity.base" and (owner_snapshot["player"] or owner_snapshot["identity"]["temporary_base"]):
        return None  # players and leveled spawns are rebuilt on a new base: name and race are compared
    if field == "identity.name" and owner_snapshot["player"]:
        return None  # a player's copy shows the player's name, its own base is unnamed
    if field == "placement.position":
        gap = distance(owner, copy)
        return None if gap <= POSITION_TOLERANCE else f"{gap:.0f} units apart"
    if field == "placement.rotation":
        gap = abs((owner[2] - copy[2] + math.pi) % (2 * math.pi) - math.pi)
        return None if gap <= ANGLE_TOLERANCE else f"facing differs by {math.degrees(gap):.0f} deg"
    if field == "equipment":
        missing, extra = set(owner) - set(copy), set(copy) - set(owner)
        if not missing and not extra:
            return None
        return " ".join(filter(None, [missing and "copy lacks " + ",".join(f"{i:08X}" for i in sorted(missing)),
                                      extra and "copy also wears " + ",".join(f"{i:08X}" for i in sorted(extra))]))
    if isinstance(owner, (int, float)) and isinstance(copy, (int, float)) and not isinstance(owner, bool):
        return None if abs(owner - copy) <= VALUE_TOLERANCE else "differs"
    return None if owner == copy else "differs"


def mod_problems(snapshot, role):
    """The mod's bookkeeping must match the actor's role."""
    components = set((snapshot.get("mod") or {}).get("components") or [])
    need = {"Local"} if role == "own" else {"Remote", "Interpolation"}
    problems = [f"mod: missing {name}Component" for name in sorted(need - components)]
    if role == "copy" and "Local" in components:
        problems.append("mod: copy also has LocalComponent (two owners)")
    return problems


def describe_actor(snapshot, form_id, game):
    name = snapshot["identity"].get("name") or ("player" if snapshot["player"] else "?")
    return f"{name} [{game} {int(form_id):08X}, sid {snapshot['mod']['server_id']}]"


def find_desyncs():
    """Compares both games once. Returns a list of (key, message)."""
    worlds = {}
    for instance, game in (("", "host"), ("client", "client")):
        select_instance(instance)
        status = read_status() or {}
        if "world" not in status:
            raise TestFailure(f"{game} game reports no world snapshot (old DLL or not running)")
        worlds[game] = status["world"]
    problems = []
    copies = {game: {s["mod"]["server_id"]: (fid, s) for fid, s in world.items() if s["role"] == "copy"}
              for game, world in worlds.items()}
    paired = {game: set() for game in worlds}
    for game, world in worlds.items():
        other = "client" if game == "host" else "host"
        for form_id, owned in world.items():
            if owned["role"] != "own":
                continue
            who = describe_actor(owned, form_id, game)
            for problem in mod_problems(owned, "own"):
                problems.append((f"{who} {problem}", f"{who} (owned by {game}): {problem}"))
            sid = owned["mod"]["server_id"]
            if sid < 0:
                problems.append((f"{who} unlinked", f"{who}: owned by {game} but never registered with the server"))
                continue
            if sid not in copies[other]:
                problems.append((f"{who} missing", f"{who}: owned by {game}, but {other} has no copy"))
                continue
            copy_id, copy = copies[other][sid]
            paired[other].add(sid)
            for problem in mod_problems(copy, "copy"):
                problems.append((f"{who} {problem}", f"{who}: {other}'s copy {int(copy_id):08X} {problem}"))
            mine, theirs = flatten(owned), flatten(copy)
            for field in sorted(set(mine) | set(theirs)):
                reason = compare_field(field, mine.get(field), theirs.get(field), owned)
                if reason:
                    problems.append((f"{who} {field}",
                                     f"{who} {field}: {game} (owner) {mine.get(field)!r} vs {other} copy "
                                     f"{theirs.get(field)!r} - {reason}"))
    for game, by_sid in copies.items():
        for sid, (form_id, copy) in by_sid.items():
            if sid not in paired[game]:
                who = describe_actor(copy, form_id, game)
                problems.append((f"{who} orphan", f"{who}: copy in {game} with no owner in the other game"))
    return problems


# A copy legitimately trails its owner by the interpolation and replay delay (~0.3-0.5s): only a
# difference that lasts the whole window is a desync. Most checks pass on the first sample.
DESYNC_WINDOW = 0.8
DESYNC_INTERVAL = 0.1


def check_desync(samples=int(DESYNC_WINDOW / DESYNC_INTERVAL) + 1, interval=DESYNC_INTERVAL):
    """Only differences seen in every sample count (copies legitimately lag for a moment)."""
    persistent = None
    for index in range(samples):
        current = dict(find_desyncs())
        persistent = current if persistent is None else {k: v for k, v in current.items() if k in persistent}
        if not persistent:
            return []
        if index + 1 < samples:
            time.sleep(interval)
    return list(persistent.values())


def command_desync(args):
    while True:
        problems = check_desync(args.samples, args.interval)
        report = "\n".join(problems) if problems else "in sync: every copy matches its owner"
        if not args.follow:
            print(report)
            if problems:
                raise SystemExit(1)
            return
        print("\033[2J\033[H" + time.strftime("%H:%M:%S ") + report + "\n\n(Ctrl+C to stop)", flush=True)


def command_status(args):
    while True:
        table = status_table()
        if not args.follow:
            print(table)
            return
        print("\033[2J\033[H" + table + "\n\n(refreshing every 0.5s, Ctrl+C to stop)", flush=True)
        time.sleep(0.5)


# Each game's status.json carries "lock_order": the plugin's lock-order checker (LockOrder.cpp)
# reports engine-lock patterns that can deadlock, the first time they occur. Vanilla Skyrim has
# latent ones too, so a scenario fails only on a new pattern with our code on the reporting
# thread's stack ("[ours]") that is not in scripts/lock_order_known.txt; others are warnings.
LOCK_PATTERN = re.compile(r"^(\[ours\] )?(lock order inversion|self-deadlock) \[([^\]]*)\]")
KNOWN_LOCK_PATTERNS = Path(__file__).with_name("lock_order_known.txt")


def known_lock_patterns():
    try:
        lines = KNOWN_LOCK_PATTERNS.read_text().splitlines()
    except OSError:
        return set()
    patterns = {line.split("#")[0].strip() for line in lines} - {""}
    # "A vs B" and "B vs A" are one pair: the checker names whichever thread it met first.
    return patterns | {" vs ".join(reversed(p.split(" vs "))) for p in patterns if " vs " in p}


def lock_order_reports():
    """{sites: (game, report, ours)} over both games, without reviewed patterns."""
    known = known_lock_patterns()
    found = {}
    for name in ("", "client"):
        select_instance(name)
        status = read_status() or {}
        for report in status.get("lock_order", {}).get("reports", []):
            match = LOCK_PATTERN.match(report)
            sites = match.group(3) if match else report
            if sites not in known:
                found.setdefault(sites, (name or "host", report, bool(match and match.group(1))))
    return found


def command_locks(args):
    for name in ("", "client"):
        select_instance(name)
        status = read_status()
        if not status:
            print(f"{name or 'host'}: not running")
            continue
        lock_order = status.get("lock_order", {})
        print(f"{name or 'host'}: " + ", ".join(f"{k} {v}" for k, v in lock_order.items() if k != "reports"))
        for report in lock_order.get("reports", []):
            print("  " + report)


# Crashes we can cause on demand: a cell to boot into and console commands that crash the game
# after it (3s apart). `repro NAME` runs one; docs/re/crashes/ has what each one taught us.
# "@show Menu" / "@hide Menu" open and close a menu instead (the console for typed commands).
REPROS = {
    "qasmoke-exit": ("QASmoke", ["player.placeatme 000A2C8E", "coc Riverwood"]),
    # The same with the console open, as when typed (vanilla does not crash that way).
    "qasmoke-exit-console": ("QASmoke", ["player.placeatme 000A2C8E", "@show Console", "coc Riverwood"]),
}


def run_step(step):
    """A repro step: a console command, or @show / @hide a menu."""
    if step.startswith(("@show ", "@hide ")):
        rpc({"cmd": "menu", "name": step[6:], "show": step.startswith("@show")})
        log(f"Menu: {step}")
    else:
        run_console(step)


def reproduce_once(args, cell, commands):
    """Fresh game in `cell`, runs the commands, waits for a crash; its report, or None."""
    stop_game()
    if args.heap_history:
        os.environ["SKYRIMCOOP_HEAP_HISTORY"] = str(args.heap_history)
    if args.watch:
        os.environ["SKYRIMCOOP_HEAP_WATCH"] = args.watch
    if args.quarantine:
        os.environ["SKYRIMCOOP_HEAP_QUARANTINE"] = str(args.quarantine)
    if args.guard:
        os.environ["SKYRIMCOOP_HEAP_GUARD"] = args.guard
    if args.probe:
        os.environ["SKYRIMCOOP_PROBE"] = args.probe
    args.cell, args.save = cell, None
    run_dir = new_run_dir("repro")
    before = set(crash_logs(""))
    started = time.monotonic()
    try:
        launch(args, run_dir)
        try:
            for command in commands:
                run_step(command)
                time.sleep(3)
        except TestFailure:
            if not set(crash_logs("")) - before:  # a crash mid-command is the expected outcome
                raise
        deadline = time.monotonic() + args.wait
        while time.monotonic() < deadline and not set(crash_logs("")) - before:
            time.sleep(0.5)
        new = sorted(set(crash_logs("")) - before)
        KNOWN_CRASHES[""].update(new)
        # The crash handler's part of the plugin log is complete once the game is gone.
        if new:
            wait_for("Waiting for the crashed game to exit", lambda: not prefix_processes(), 30)
    finally:
        stop_game()
        collect_logs(run_dir)
        os.environ.pop("SKYRIMCOOP_HEAP_HISTORY", None)
        os.environ.pop("SKYRIMCOOP_HEAP_WATCH", None)
        os.environ.pop("SKYRIMCOOP_HEAP_QUARANTINE", None)
        os.environ.pop("SKYRIMCOOP_HEAP_GUARD", None)
        os.environ.pop("SKYRIMCOOP_PROBE", None)
    KNOWN_CRASHES[""].update(crash_logs(""))
    if not new:
        return None
    log(f"Crashed {time.monotonic() - started:.0f}s after launch (logs in {run_dir})")
    return new[-1]


CRASH_REGISTER = re.compile(r"^\t(R[A-Z0-9]+)\s+0x([0-9A-F]+)\s")


def crash_registers(crash_log):
    """The crash's registers that hold heap-like addresses (not the game's image, not small numbers)."""
    registers = {}
    for line in crash_log.read_text(errors="replace").splitlines():
        match = CRASH_REGISTER.match(line)
        value = int(match.group(2), 16) if match else 0
        if match and match.group(1) not in ("RSP", "R11") and 0x100000 < value < 0x7FF000000000 \
                and not 0x140000000 <= value < 0x150000000:
            registers[match.group(1)] = value
    return registers


def command_repro(args):
    """Runs a crash recipe (REPROS, or --cell and console commands) and prints each crash's report."""
    if args.name in REPROS:
        cell, commands = REPROS[args.name]
    else:  # console commands of its own
        cell, commands = args.cell, ([args.name] if args.name else []) + args.commands
    if not commands:
        raise TestFailure("nothing to run: name a repro or give console commands")
    crashes = 0
    for run in range(1, args.runs + 1):
        log(f"Run {run}/{args.runs}: {cell}, then {' / '.join(commands)}")
        crash_log = reproduce_once(args, cell, commands)
        if not crash_log:
            log(f"No crash within {args.wait:.0f}s")
            continue
        crashes += 1
        print(crash_report("", crash_log, ours=not args.vanilla))
        if args.follow:
            # Heap addresses repeat when the game boots the same way: watch the crash's from boot.
            registers = crash_registers(crash_log)
            args.watch = ",".join(f"{value:#x}" for value in dict.fromkeys(registers.values()))
            log(f"Following: a second run watching {', '.join(f'{name} {value:#x}' for name, value in registers.items())}")
            follow_log = reproduce_once(args, cell, commands)
            if not follow_log:
                log("The second run did not crash: nothing to follow")
                continue
            again = crash_registers(follow_log)
            same = [name for name, value in registers.items() if again.get(name) == value]
            log(f"Registers the same as the first crash: {', '.join(same) or 'none'}"
                + ("" if len(same) == len(registers) else " (the others' watches are about different memory)"))
            print(crash_report("", follow_log, ours=not args.vanilla))
    log(f"Crashed {crashes} of {args.runs} runs")


def command_scenario(args):
    global HOLD
    HOLD = args.hold
    names = args.names or [name for name in SCENARIOS if "input" not in TAGS.get(name, set()) or args.tag]
    if args.tag:
        wanted = set(args.tag.split(","))
        known = set().union(*TAGS.values())
        if wanted - known:
            raise TestFailure(f"unknown tag(s) {sorted(wanted - known)}; known: {', '.join(sorted(known))}")
        names = [name for name in names if TAGS.get(name, set()) & wanted]
    unknown = [name for name in names if name not in SCENARIOS]
    if unknown:
        raise TestFailure(f"unknown scenario(s) {unknown}; known: {', '.join(SCENARIOS)}")
    for name in ("", "client"):
        select_instance(name)
        if not read_status():
            raise TestFailure(f"{name or 'host'} game is not running: start `{sys.argv[0]} duo --keep-running` first")
    if not all(load_home(name) for name in ("", "client")):
        log("warning: no baseline recorded by `duo --keep-running`: using where the players stand now")
        for name in ("", "client"):
            reset_player(name, {"position": player_snapshot(name)["placement"]["position"],
                                "heading": player_snapshot(name)["placement"]["rotation"][2],
                                "equipment": sorted(i for i in player_snapshot(name).get("equipment", [])
                                                    if i not in TEST_ITEMS)})
        save_home()
    order = [name for _ in range(args.repeat) for name in names]
    if args.shuffle:
        seed = args.seed if args.seed is not None else random.randrange(1 << 30)
        random.Random(seed).shuffle(order)
        log(f"Shuffled order (--seed {seed} repeats it): {' '.join(order)}")

    suite_start = time.monotonic()
    log("=== baseline: resetting both games")
    if problems := settle_baseline():
        raise TestFailure("cannot reach the baseline before the first scenario: " + " | ".join(problems))
    runs = []  # (name, seconds, result)
    for index, name in enumerate(order):
        log(f"=== {name}: {SCENARIOS[name].__doc__}")
        scenario_start = time.monotonic()
        try:
            known_locks = lock_order_reports()
            SCENARIOS[name]()
            # Whatever the scenario did, both games must agree afterwards.
            problems = check_desync()
            if problems:
                raise TestFailure("desync afterwards: " + " | ".join(problems))
            new_locks = [v for k, v in lock_order_reports().items() if k not in known_locks]
            for game, report, ours in new_locks:
                if not ours:
                    log(f"warning: engine-only lock-order pattern on {game}: {report}")
            if ours_locks := [f"{game}: {report}" for game, report, ours in new_locks if ours]:
                raise TestFailure("lock order (our code on the stack): " + " | ".join(ours_locks))
            result = "PASS"
        except TestFailure as error:
            result = f"FAIL ({error})"
        # Back to the baseline for the next one; what the reset cannot undo is this scenario's.
        leftovers = settle_baseline()
        if leftovers:
            left = "left behind: " + " | ".join(leftovers)
            result = f"FAIL ({left})" if result == "PASS" else f"{result} [{left}]"
        runs.append((name, time.monotonic() - scenario_start, result))
        log(f"=== {name}: {result}")
        if args.fail_fast and result != "PASS":
            for skipped in order[index + 1:]:
                runs.append((skipped, 0.0, "NOT RUN (--fail-fast)"))
            break
        if leftovers:
            for skipped in order[index + 1:]:
                runs.append((skipped, 0.0, "NOT RUN (the games could not be reset to the baseline)"))
            break
    print()
    for name, seconds, result in runs:
        print(f"{name:28s} {seconds:5.1f}s  {result}")
    if args.repeat > 1 or args.shuffle:
        print()
        for name in names:
            mine = [result for run, _, result in runs if run == name]
            print(f"{name:28s} {sum(r == 'PASS' for r in mine)}/{len(mine)} passed")
    print(f"{'total':28s} {time.monotonic() - suite_start:5.1f}s")
    if any(r != "PASS" for _, _, r in runs):
        raise SystemExit(1)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    commands = parser.add_subparsers(dest="command", required=True)
    net = commands.add_parser("net", help="headless multi-bot network test")
    net.add_argument("--bots", type=int, default=4)
    net.add_argument("--seconds", type=int, default=3)
    net.add_argument("--hz", type=int, default=5)
    for name in ("launch", "game", "duo", "repro"):
        sub = commands.add_parser(name)
        sub.add_argument("--cell", default="QASmoke", help="cell for `coc` from the main menu (default QASmoke)")
        sub.add_argument("--save", help="load this save instead of coc")
        sub.add_argument("--boot-timeout", type=float, default=240)
        sub.add_argument("--world-timeout", type=float, default=180)
        sub.add_argument("--visible", action="store_true",
                         help="show the game on the desktop (default: off-screen Xvfb display; see `screenshot`)")
        if name in ("launch", "repro"):
            sub.add_argument("--vanilla", action="store_true",
                             help="without our plugin (A/B baseline): the stand-in harness plugin "
                                  "(Code/vanilla_harness) runs the commands; the next normal launch redeploys ours")
        if name in ("game", "duo"):
            sub.add_argument("--seconds", type=float, default=30, help="how long to wait for bot movement")
            sub.add_argument("--tolerance", type=float, default=5, help="max engine vs requested distance")
            sub.add_argument("--keep-running", action="store_true", help="leave Skyrim open afterwards")
        if name == "repro":
            sub.add_argument("name", nargs="?", help=f"a known crash: {', '.join(REPROS)}")
            sub.add_argument("commands", nargs="*", help="or: console commands after booting into --cell (3s apart)")
            sub.add_argument("--wait", type=float, default=25, help="seconds to wait for the crash after them")
            sub.add_argument("--runs", type=int, default=1, help="repeat N times (how reliably it crashes)")
            sub.add_argument("--heap-history", nargs="?", type=int, const=2, default=0, metavar="MILLIONS",
                             help="record engine allocations and frees with stacks (Code/client/HeapHistory.h)")
            sub.add_argument("--watch", default="", metavar="ADDR,ADDR",
                             help="keep every allocation and free of the blocks at these addresses since boot")
            sub.add_argument("--quarantine", nargs="?", type=int, const=256, default=0, metavar="MB",
                             help="poison freed blocks and hold them back from reuse: a use after free faults at "
                                  "its first read (with --heap-history: on the freed block's story)")
            sub.add_argument("--guard", default="", metavar="SIZE,SIZE",
                             help="page-heap these block sizes (hex, from a heap history): any access to one after "
                                  "its free faults right there, even a read that poison would not catch")
            sub.add_argument("--probe", default="", metavar="ID,ID",
                             help="engine functions (Address Library IDs) whose calls go into the breadcrumbs: "
                                  "whether, when and on what the engine ran them before the crash")
            sub.add_argument("--follow", action="store_true",
                             help="after a crash, run again watching the addresses in its registers from boot")
        if name == "duo":
            sub.add_argument("--npc", default="000A2C8E",
                             help="NPC base form (8 hex digits) the host spawns; the client must show it (default Lydia, '' to skip)")
    reload = commands.add_parser("reload", help="swap the current build into a running duo and reconnect")
    reload.add_argument("--seconds", type=float, default=30, help="how long to wait for the players to reappear")
    scenario = commands.add_parser("scenario", help="sync scenarios against a running `duo --keep-running`")
    scenario.add_argument("--hold", type=float, default=0,
                          help="pause N seconds after each synced step and face the camera at it (for watching)")
    scenario.add_argument("names", nargs="*", help=f"default: all of {', '.join(SCENARIOS)}")
    scenario.add_argument("--shuffle", action="store_true", help="random order (scenarios must not depend on each other)")
    scenario.add_argument("--seed", type=int, help="with --shuffle: repeat a shuffled order")
    scenario.add_argument("--repeat", type=int, default=1, help="run the list N times")
    scenario.add_argument("--tag", help="only scenarios with one of these comma-separated tags (see TAGS)")
    scenario.add_argument("--fail-fast", action="store_true", help="stop at the first scenario that fails")
    stop = commands.add_parser("stop", help="stop the test game(s) (both instances unless --instance)")
    console = commands.add_parser("console", help="run a console command in the running test game")
    console.add_argument("text", nargs="+")
    status = commands.add_parser("status", help="both games side by side: health, death, weapon, casting")
    status.add_argument("--follow", "-f", action="store_true", help="keep refreshing")
    commands.add_parser("locks", help="both games' lock-order checker reports (deadlock patterns)")
    crash = commands.add_parser("crash", help="the newest crash of each game: engine state (CrashLoggerSSE) and our last engine changes")
    crash.add_argument("--instance", choices=["client"], help="only this game (default: both)")
    desync = commands.add_parser("desync", help="compare every actor's full state between the two games")
    desync.add_argument("--follow", "-f", action="store_true", help="keep checking")
    desync.add_argument("--samples", type=int, default=3, help="a difference must persist this many samples")
    desync.add_argument("--interval", type=float, default=1.0, help="seconds between samples")
    watch = commands.add_parser("watch", help="live view of the off-screen game display in a window (ffplay)")
    commands.add_parser("setup-display", help="one-time: install the off-screen dummy X server (sudo)")
    shot = commands.add_parser("screenshot", help="save a PNG of the off-screen game display")
    shot.add_argument("output", nargs="?", type=Path)
    for sub in (stop, console, watch, shot):
        sub.add_argument("--instance", choices=("host", "client", "both") if sub is watch else ("host", "client"),
                         help="which `duo` game (default: host)")
    args = parser.parse_args()
    global VISIBLE
    VISIBLE = getattr(args, "visible", False)
    select_instance("client" if getattr(args, "instance", None) == "client" else "")
    try:
        if args.command == "net":
            command_net(args)
        elif args.command == "launch":
            command_launch(args)
        elif args.command == "game":
            command_game(args)
        elif args.command == "duo":
            command_duo(args)
        elif args.command == "scenario":
            command_scenario(args)
        elif args.command == "reload":
            command_reload(args)
        elif args.command == "console":
            run_console(" ".join(args.text))
            log("Done")
        elif args.command == "locks":
            command_locks(args)
        elif args.command == "crash":
            command_crash(args)
        elif args.command == "repro":
            command_repro(args)
        elif args.command == "desync":
            command_desync(args)
        elif args.command == "status":
            command_status(args)
        elif args.command == "watch":
            names = ["", "client"] if args.instance == "both" else [INSTANCE]
            viewers = []
            for index, name in enumerate(names):
                select_instance(name)
                viewers.append(subprocess.Popen(
                    ["ffplay", "-loglevel", "error", "-window_title", f"Skyrim {name or 'host'} (coop test)",
                     "-left", str(40 + index * 660), "-top", "40", "-x", "640", "-y", "360",
                     "-f", "x11grab", "-framerate", "30", "-video_size", "1280x720", "-i", f"{VIRTUAL_DISPLAY}+4,30"]))
            for viewer in viewers:
                viewer.wait()
        elif args.command == "setup-display":
            setup_display()
        elif args.command == "screenshot":
            args.output = args.output or ROOT / f"build/coop-test/screenshot-{INSTANCE or 'host'}.png"
            args.output.parent.mkdir(parents=True, exist_ok=True)
            subprocess.run(["import", "-display", VIRTUAL_DISPLAY, "-window", "root", str(args.output)], check=True)
            log(f"Saved {args.output}")
        else:
            names = [INSTANCE] if args.instance else ["", "client"]
            stopped = True
            for name in names:
                select_instance(name)
                stopped = stop_game() and stopped
            log("Stopped" if stopped else "Some processes would not exit")
    except TestFailure as error:
        print(f"FAIL: {error}", file=sys.stderr)
        sys.exit(1)
    except KeyboardInterrupt:
        sys.exit(130)


if __name__ == "__main__":
    main()
