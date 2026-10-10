#!/usr/bin/env python3
"""One-command co-op tests.

  coop_test.py net    [--bots N --seconds S --hz R]   no Skyrim, ~5s: N bots must see each other
  coop_test.py launch [--cell CELL | --save NAME]     fast-boot Skyrim straight into a cell, leave it running
  coop_test.py game   [--cell CELL --seconds S]       fast-boot, host, connect a bot, verify the bot's
                                                      actor exists in the engine and moves; then quit
  coop_test.py duo    [--keep-running --visible]      two real games: host + client, each must show the other
  coop_test.py scenario [names]                       sync scenarios (equip, ...) against a running duo
  coop_test.py stop                                   kill the test Skyrim instance(s)
  coop_test.py screenshot [out.png]                   capture the off-screen game display
  coop_test.py console <command...>                   run a console command in the test game
  coop_test.py status [-f]                            both games side by side: health, death, weapon, casting
  coop_test.py watch                                  live view of the off-screen game in a window
  coop_test.py setup-display                          one-time: install the off-screen X server (sudo)

The game runs on an off-screen X display (COOP_DISPLAY, default :99; the GPU still renders)
so it never covers the desktop; pass --visible to launch/game/duo to show it instead. `duo` runs a second game ("client") on
display :100 with its own prefix; watch/screenshot/console/stop take --instance client.

Skyrim runs in its own Proton prefix under build/coop-test (never your normal saves or
settings): no intro, windowed, low preset, and `coc <cell>` from the main menu. Paths
default to this machine; override with SKYRIM_GAME_DIR, SKYRIM_PROTON_DIR,
SKYRIM_RUNTIME_DIR. Every game run keeps its logs in build/coop-test/runs/<time>/.
"""
import argparse
import json
import math
import os
from pathlib import Path
import shutil
import signal
import subprocess
import sys
import time
import uuid

ROOT = Path(__file__).resolve().parents[1]
STEAM_LIBRARY = Path("/media/bighass/4d2c4781-34de-41a5-8939-e35551bc9a5d1/SteamLibrary/steamapps/common")
GAME = Path(os.environ.get("SKYRIM_GAME_DIR",
                           "/media/bighass/BlackHole/SteamLibrary/steamapps/common/Skyrim Special Edition"))
PROTON = Path(os.environ.get("SKYRIM_PROTON_DIR", STEAM_LIBRARY / "Proton 10.0"))
RUNTIME = Path(os.environ.get("SKYRIM_RUNTIME_DIR", STEAM_LIBRARY / "SteamLinuxRuntime_sniper"))
STEAM = Path(os.environ.get("STEAM_COMPAT_CLIENT_INSTALL_PATH", Path.home() / ".steam/steam"))
TEST_HOME = ROOT / "build/coop-test"
PREFIX = TEST_HOME / "prefix-host"
DLL = Path(os.environ.get("COOP_DLL", ROOT / "build/windows/x64/dev/SkyrimTogetherClient.dll"))
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


class TestSkipped(Exception):
    """A scenario could not set up its situation; says nothing about sync."""


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
               SKYRIMCOOP_LOG_LEVEL=env.get("SKYRIMCOOP_LOG_LEVEL", "debug"))
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
        hint = ("\nBuild the DLL: scripts/xmake-msvc.sh f -m dev && scripts/xmake-msvc.sh build -j16 SkyrimTogetherClientDLL"
                "\nBuild the bot: XMAKE_CONFIGDIR=build/hotreload-native-config xmake build -j16 fake_player")
        raise TestFailure("Missing:\n  " + "\n  ".join(missing) + hint)
    if subprocess.run(["pgrep", "-x", "steam"], capture_output=True).returncode != 0:
        raise TestFailure("Steam must be running (Skyrim's DRM needs it).")
    if (PLUGINS / "skyrim_coop_hooks.dll").exists():
        raise TestFailure(f"Remove the old relay plugin first: {PLUGINS / 'skyrim_coop_hooks.dll'}")


def deploy_dll():
    PLUGINS.mkdir(parents=True, exist_ok=True)
    target = PLUGINS / "SkyrimTogetherClient.dll"
    source = DLL.stat()
    if not target.exists() or target.stat().st_size != source.st_size or target.stat().st_mtime < source.st_mtime:
        temporary = target.with_name(target.name + ".tmp")
        shutil.copy2(DLL, temporary)
        os.replace(temporary, target)
        log(f"Deployed {DLL.relative_to(ROOT)} ({source.st_size // 1048576} MB)")


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
    }})
    set_ini(prefs_ini, {"Display": {"bFull Screen": "0", "bBorderless": "0",
                                    "iSize W": "1280", "iSize H": "720",
                                    "iLocation X": "0", "iLocation Y": "0"},
                            # Silent test games. (Muting via PulseAudio would be remembered per app
                            # name and silence normal Skyrim too.)
                            "AudioMenu": {"fAudioMasterVolume": "0.0000"},
                            # Tutorial popups (e.g. "Magicka Low") swallow all input until closed.
                            "Interface": {"bShowTutorials": "0"}})


def read_status():
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
        time.sleep(0.25)
    raise TestFailure(f"Timed out after {timeout:.0f}s: {description}")


def launch(args, run_dir):
    if prefix_processes():
        raise TestFailure("The test Skyrim is already running. Use `coop_test.py stop` first.")
    if not VISIBLE:
        ensure_virtual_display()
    vanilla = getattr(args, "vanilla", False)
    if vanilla:  # A/B baseline: same prefix and settings, SKSE without our plugin
        (PLUGINS / "SkyrimTogetherClient.dll").unlink(missing_ok=True)
    else:
        deploy_dll()
    command = f"load {args.save}" if args.save else f"coc {args.cell}"
    prepare_prefix()
    CONTROL.mkdir(parents=True, exist_ok=True)
    for stale in ("status.json", "session.txt", "console.txt"):
        (CONTROL / stale).unlink(missing_ok=True)
    atomic_write(CONTROL / "boot.txt", command + "\n")  # run by the plugin at the main menu
    atomic_write(CONTROL / "request.txt", "builtin\n")  # turns on the plugin's status heartbeat
    log(f"Launching Skyrim (start command: {command!r})")
    output = open(run_dir / f"proton{'-' + INSTANCE if INSTANCE else ''}.log", "w")
    launcher = subprocess.Popen(
        [str(RUNTIME / "run"), "--", str(PROTON / "proton"), "waitforexitandrun",
         str(GAME / "skse64_loader.exe")],
        env=game_env(), cwd=GAME, stdout=output, stderr=subprocess.STDOUT, start_new_session=True)
    if vanilla:
        log(f"Vanilla run (no plugin): at the main menu, open the console (~) and type `{command}`")
        return launcher
    status = wait_for("Waiting for the plugin heartbeat (SKSE loaded our DLL)", read_status,
                      args.boot_timeout, launcher)
    log(f"Plugin alive in Skyrim (Windows PID {status['pid']})")
    wait_for(f"Waiting for the player to be in the world ({command})",
             lambda: (read_status() or {}).get("session", {}).get("world_ready"), args.world_timeout, launcher)
    log("Player is in the world")
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
    request_id = uuid.uuid4().hex[:12]
    atomic_write(CONTROL / "console.txt", f"{request_id} {command}\n")
    wait_for(f"Console: {command}", lambda: (read_status() or {}).get("console_id") == request_id, timeout)


def face_actor(actor_id):
    """Turns the player toward an actor so it is on screen (for screenshots / `watch`)."""
    status = read_status() or {}
    player = (status.get("player") or {}).get("position")
    actor = (status.get("actors", {}).get(str(actor_id), {}).get("actual")
             or ((status.get("world") or {}).get(str(actor_id)) or {}).get("placement", {}).get("position"))
    if not player or not actor:
        return
    # Skyrim heading: degrees clockwise from north (+Y).
    heading = math.degrees(math.atan2(actor[0] - player[0], actor[1] - player[1])) % 360
    run_console(f"player.setangle z {heading:.0f}")


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

        def both_see_each_other():
            seen = {}
            for name in ("", "client"):
                select_instance(name)
                players = remote_players()
                if not players:
                    return None
                seen[name or "host"] = players
            return seen
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
IRON_SWORD = 0x00012EB7
HOLD = 0.0  # scenario --hold: seconds to pause after each synced step, for watching


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


def remote_player(actors):
    return next((int(a) for a, s in actors.items() if s.get("remote_player") and s.get("has_3d")), None)


def remote_npcs(actors):
    return {int(a) for a, s in actors.items() if not s.get("remote_player") and s.get("has_3d")}


def show(observer, observed):
    """With --hold, turns the observer's camera toward the actor under test (and a player under
    test back toward the observer)."""
    if HOLD:
        actors = actors_on(observer)
        target = observed(actors)
        if target is not None:
            select_instance(observer)
            face_actor(target)
            if actors.get(str(target), {}).get("remote_player"):
                # The observed player turns around too, so the camera sees their front.
                other = "client" if observer == "" else ""
                select_instance(other)
                face_actor(remote_player(actors_on(other)))


def hold():
    if HOLD:
        log(f"(holding {HOLD:.0f}s so you can look)")
        time.sleep(HOLD)


def equip_roundtrip(actor_instance, ref, observer, observed, label):
    """Equips then unequips an Iron Sword on `ref` in one game; the other game's copy must follow
    both ways. Ending unequipped keeps reruns honest."""
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


def hold_key(instance, key, seconds):
    """Holds a key in an instance's game for `seconds` (e.g. walking)."""
    select_instance(instance)
    env = dict(os.environ, DISPLAY=VIRTUAL_DISPLAY)
    windows = subprocess.run(["xdotool", "search", "--name", "^Skyrim"], env=env,
                             capture_output=True, text=True).stdout.split()
    if not windows:
        raise TestFailure(f"[{instance or 'host'}] game window not found on {VIRTUAL_DISPLAY}")
    for command in (["windowfocus", windows[0]], ["keydown", key]):
        subprocess.run(["xdotool", *command], env=env, check=True, stderr=subprocess.DEVNULL)
    time.sleep(seconds)
    subprocess.run(["xdotool", "keyup", key], env=env, check=True, stderr=subprocess.DEVNULL)


def approach(instance, target, back_seconds):
    """Puts the instance's player in front of `target` (form id in that game), facing it: teleport
    onto it (the game finds a valid spot) and walk backwards. Raw setpos can drop the player
    through the floor."""
    console_on(instance, f"player.moveto {target:08X}")
    time.sleep(1.0)
    select_instance(instance)
    face_actor(target)
    hold_key(instance, "s", back_seconds)
    time.sleep(0.4)
    select_instance(instance)
    face_actor(target)


def key_on(instance, key):
    """Presses a key in an instance's game window (off-screen display), like a player would."""
    select_instance(instance)
    env = dict(os.environ, DISPLAY=VIRTUAL_DISPLAY)
    windows = subprocess.run(["xdotool", "search", "--name", "^Skyrim"], env=env,
                             capture_output=True, text=True).stdout.split()
    if not windows:
        raise TestFailure(f"[{instance or 'host'}] game window not found on {VIRTUAL_DISPLAY}")
    # Real (XTEST) input held for 100ms: the game polls the keyboard once per frame, and a quick
    # synthetic press (~12ms) is missed whenever a frame takes longer than that. Only the
    # off-screen display is touched. (stderr: harmless XGetInputFocus warning on the dummy X server)
    for command in (["windowfocus", windows[0]], ["keydown", key]):
        subprocess.run(["xdotool", *command], env=env, check=True, stderr=subprocess.DEVNULL)
    time.sleep(0.1)
    subprocess.run(["xdotool", "keyup", key], env=env, check=True, stderr=subprocess.DEVNULL)


def draw_roundtrip(actor_instance, observer, label):
    """Player readies then sheathes a sword (R key); the other game's copy must follow."""
    def drawn(expected):
        def check(actors):
            target = remote_player(actors)
            return target is not None and actors[str(target)].get("weapon_drawn") == expected
        return check
    console_on(actor_instance, f"player.additem {IRON_SWORD:08X} 1")
    console_on(actor_instance, f"player.equipitem {IRON_SWORD:08X}")
    show(observer, remote_player)
    def local_drawn():
        select_instance(actor_instance)
        return ((read_status() or {}).get("player") or {}).get("weapon_drawn")
    for expected, verb in ((True, "draws"), (False, "sheathes")):
        # Skyrim ignores R while a draw/sheathe animation is still playing: press until it reacts.
        for _ in range(5):
            key_on(actor_instance, "r")
            deadline = time.monotonic() + 2.5
            while time.monotonic() < deadline and local_drawn() != expected:
                time.sleep(0.1)
            if local_drawn() == expected:
                break
        else:
            raise TestFailure(f"[{actor_instance or 'host'}] player never {verb} the sword (input not reaching the game?)")
        wait_on(observer, f"{label} {verb} the sword", drawn(expected))
        hold()
    console_on(actor_instance, f"player.unequipitem {IRON_SWORD:08X}")


def scenario_draw_host_player():
    """Host readies/sheathes a weapon; the client's copy of the host must follow."""
    draw_roundtrip("", "client", "host's character")


def scenario_draw_client_player():
    """Client readies/sheathes a weapon; the host's copy of the client must follow."""
    draw_roundtrip("client", "", "client's character")


FLAMES = 0x00012FCD  # concentration spell: synced through spell cast + interrupt messages
FIREBOLT = 0x00012FD0  # fire-and-forget spell
RIGHT_HAND = 1


def mouse_on(instance, action, button=1):
    """mousedown/mouseup in an instance's game window (button 1 = attack/cast with the right hand)."""
    select_instance(instance)
    env = dict(os.environ, DISPLAY=VIRTUAL_DISPLAY)
    window = subprocess.run(["xdotool", "search", "--name", "^Skyrim"], env=env,
                            capture_output=True, text=True).stdout.split()[0]
    # Wine's DirectInput mouse ignores synthetic per-window events: use real (XTEST) input. It only
    # touches the off-screen display, never the desktop.
    for command in (["windowfocus", window], ["mousemove", "--window", window, "640", "360"],
                    [action, str(button)]):
        subprocess.run(["xdotool", *command], env=env, check=True, stderr=subprocess.DEVNULL)


def local_player(instance):
    select_instance(instance)
    return (read_status() or {}).get("player") or {}


def press_until(instance, key, condition, what, attempts=5):
    """Presses `key` until `condition(local player status)` holds (Skyrim drops input mid-animation)."""
    for _ in range(attempts):
        key_on(instance, key)
        deadline = time.monotonic() + 2.5
        while time.monotonic() < deadline:
            if condition(local_player(instance)):
                return
            time.sleep(0.1)
    raise TestFailure(f"[{instance or 'host'}] {what} never happened (input not reaching the game?)")


def cast_roundtrip(actor_instance, observer, label):
    """Player holds a Flames cast then releases; the other game's copy must cast, then stop."""
    console_on(actor_instance, f"player.addspell {FLAMES:08X}")
    console_on(actor_instance, f"player.equipspell {FLAMES:08X} right")
    console_on(actor_instance, "player.restoreav magicka 1000")  # repeated casts drain it
    show(observer, remote_player)
    press_until(actor_instance, "r", lambda me: me.get("weapon_drawn"), "readying the spell")

    def replica_casting(expected):
        def check(actors):
            target = remote_player(actors)
            return target is not None and (actors[str(target)]["casting"][RIGHT_HAND] == FLAMES) == expected
        return check
    try:
        mouse_on(actor_instance, "mousedown")
        wait_for(f"[{actor_instance or 'host'}] player casts Flames",
                 lambda: local_player(actor_instance).get("casting", [0, 0])[RIGHT_HAND] == FLAMES, 10)
        wait_on(observer, f"{label} casts Flames", replica_casting(True))
        hold()
    finally:
        mouse_on(actor_instance, "mouseup")
    wait_on(observer, f"{label} stops casting", replica_casting(False))
    press_until(actor_instance, "r", lambda me: not me.get("weapon_drawn"), "lowering the hands")


def scenario_cast_host_player():
    """Host casts Flames (held) and stops; the client's copy of the host must follow."""
    cast_roundtrip("", "client", "host's character")


def scenario_cast_client_player():
    """Client casts Flames (held) and stops; the host's copy of the client must follow."""
    cast_roundtrip("client", "", "client's character")


def ensure_host_npc(base="000A2C8E"):
    """Returns (host ref id, client replica id) of a host-owned NPC, spawning one if needed."""
    select_instance("")
    host_player = int(remote_player(actors_on("")) or 0)
    host_npcs = [a for a in (read_status() or {}).get("player", {}).get("loaded_actors", []) if a != host_player]
    if not host_npcs:
        console_on("", f"player.placeatme {base} 1")
        host_npcs = wait_for("[host] NPC loaded", lambda: [
            a for a in ((read_status() or {}).get("player") or {}).get("loaded_actors", []) if a != host_player], 15)
    replicas = wait_on("client", "host NPC replicated", remote_npcs)
    return host_npcs[0], next(iter(replicas))


def scenario_equip_host_npc():
    """Host equips/unequips a sword on its NPC; the client's replica must follow."""
    npc, replica = ensure_host_npc()
    equip_roundtrip("", f"{npc:08X}", "client", lambda actors: replica if str(replica) in actors else None,
                    f"NPC replica {replica:08X}")


def close(a, b, tolerance=1.0):
    return a is not None and b is not None and abs(a - b) <= tolerance


def health_roundtrip(actor_instance, ref, real_health, observer, observed, label):
    """Damages then restores health on `ref` in its owner's game; the other game's copy must
    show the owner's value each time (owners broadcast actor values once a second)."""
    def matches():
        def check(actors):
            target = observed(actors)
            real = real_health()
            return target is not None and close(actors[str(target)].get("health"), real) and real
        return check
    show(observer, observed)
    for command, what in ((f"{ref}.damageav health 40", "damaged"), (f"{ref}.restoreav health 40", "restored")):
        console_on(actor_instance, command)
        time.sleep(0.3)
        health = wait_on(observer, f"{label} health matches after being {what}", matches())
        log(f"{label}: health {health:.0f} on both sides")
        hold()


def scenario_health_host_player():
    """Host player loses/regains health; the client's copy of the host must match."""
    health_roundtrip("", "player", lambda: local_player("").get("health"), "client", remote_player, "host's character")


def scenario_health_client_player():
    """Client player loses/regains health; the host's copy of the client must match."""
    health_roundtrip("client", "player", lambda: local_player("client").get("health"), "", remote_player,
                     "client's character")


def host_npc_state(npc):
    return local_player("").get("local_actors", {}).get(str(npc), {})


def scenario_health_host_npc():
    """Host's NPC loses/regains health; the client's replica must match."""
    npc, replica = ensure_host_npc()
    health_roundtrip("", f"{npc:08X}", lambda: host_npc_state(npc).get("health"), "client",
                     lambda actors: replica if str(replica) in actors else None, f"NPC replica {replica:08X}")


CHICKEN = "000FA6D9"  # a plain non-essential creature (Lydia is essential: `kill` won't kill her)


def spawn_host_npc(base):
    """Host spawns a new NPC; returns (host ref id, client replica id) of the new one."""
    host_before = set(local_player("").get("local_actors", {}))
    client_before = remote_npcs(actors_on("client"))
    console_on("", f"player.placeatme {base} 1")
    npc = int(next(iter(wait_for("[host] new NPC loaded",
                                 lambda: set(local_player("").get("local_actors", {})) - host_before, 15))))
    replica = next(iter(wait_on("client", "new NPC replicated",
                                lambda actors: remote_npcs(actors) - client_before)))
    return npc, replica


def scenario_death_host_npc():
    """Host kills then resurrects an NPC it owns; the client's replica must die and come back."""
    npc, replica = spawn_host_npc(CHICKEN)
    show("client", lambda actors: replica if str(replica) in actors else None)
    try:
        for command, dead in ((f"{npc:08X}.kill", True), (f"{npc:08X}.resurrect", False)):
            console_on("", command)
            wait_for(f"[host] NPC {'dies' if dead else 'is alive again'}",
                     lambda: host_npc_state(npc).get("dead") == dead, 10)
            wait_on("client", f"NPC replica {replica:08X} {'dies' if dead else 'is alive again'}",
                    lambda actors: actors.get(str(replica), {}).get("dead") == dead)
            hold()
    finally:
        console_on("", f"{npc:08X}.disable")  # keep the scene to the players + Lydia for reruns


def scenario_remove_host_npc():
    """Host disables an NPC it owns; the client's copy must disappear."""
    npc, replica = spawn_host_npc(CHICKEN)
    show("client", lambda actors: replica if str(replica) in actors else None)
    console_on("", f"{npc:08X}.disable")
    wait_for("[host] NPC unloaded", lambda: str(npc) not in local_player("").get("local_actors", {}), 10)
    wait_on("client", f"NPC copy {replica:08X} removed", lambda actors: str(replica) not in actors)
    hold()


def distance(a, b):
    return sum((x - y) ** 2 for x, y in zip(a, b)) ** 0.5


def place(instance, ref, goal, current):
    """Moves `ref` to `goal` in an instance's game by lifting it 100 units, sliding it over and
    letting it drop onto the floor there. Separate setpos x/y/z at floor height can slide it into
    furniture or under the floor, and it then falls through the world."""
    console_on(instance, f"{ref}.setpos z {max(current[2], goal[2]) + 100:.0f}")
    console_on(instance, f"{ref}.setpos x {goal[0]:.0f}")
    console_on(instance, f"{ref}.setpos y {goal[1]:.0f}")


def scenario_move_host_npc():
    """Host moves its NPC to a player; the client's copy must end up where the host's NPC is."""
    npc, replica = ensure_host_npc()
    show("client", lambda actors: replica if str(replica) in actors else None)
    host_actors = actors_on("")
    # Move to whichever player is farther away (both stand on valid floor).
    players = {"player": local_player("")["position"]}
    client_on_host = remote_player(host_actors)
    if client_on_host:
        players[f"{client_on_host:08X}"] = host_actors[str(client_on_host)]["actual"]
    start = host_npc_state(npc)["position"]
    target = max(players, key=lambda ref: distance(players[ref], start))
    goal = players[target]
    place("", f"{npc:08X}", goal, start)
    wait_for("[host] NPC moved and landed", lambda: distance(host_npc_state(npc)["position"], start) > 100
             and abs(host_npc_state(npc)["position"][2] - goal[2]) < 60, 10)
    last = {}
    def copy_matches(actors):
        copy = actors.get(str(replica), {}).get("actual")
        last.update(copy=copy, owned=host_npc_state(npc)["position"])
        return copy and distance(copy, last["owned"]) < 30
    try:
        wait_on("client", f"NPC copy {replica:08X} at the host NPC's position", copy_matches)
    except TestFailure:
        raise TestFailure(f"copy at {[round(v) for v in last['copy'] or []]}, "
                          f"host's NPC at {[round(v) for v in last['owned']]}")
    hold()


def scenario_steady_state():
    """Desync checker: every actor's full state matches between owner and copy."""
    problems = check_desync(samples=4)
    if problems:
        raise TestFailure("desync: " + " | ".join(problems))


def scenario_repair_equipment_drift():
    """A copy's equipment is broken behind the owner's back; it must converge back to the owner's."""
    npc, replica = ensure_host_npc()
    def world(instance):
        select_instance(instance)
        return (read_status() or {}).get("world", {})
    owner_equipment = set(world("")[str(npc)]["equipment"])
    def broken():
        return set(world("client").get(str(replica), {}).get("equipment") or []) != owner_equipment
    for _ in range(3):  # the strip only takes effect on reload, and not always: retry it
        for command in ("unequipall", "disable", "enable"):
            console_on("client", f"{replica:08X}.{command}")
            time.sleep(0.5)
        deadline = time.time() + 4
        while not broken() and time.time() < deadline:
            time.sleep(0.1)
        if broken():
            log(f"copy now wears {len(world('client')[str(replica)]['equipment'])} of the owner's "
                f"{len(owner_equipment)} items")
            break
    else:
        raise TestSkipped("could not break the copy's equipment to test the repair")
    deadline = time.time() + 15
    while (problems := check_desync(samples=1)) and time.time() < deadline:
        time.sleep(1)
    if problems:
        raise TestFailure("never converged: " + " | ".join(problems))


def scenario_melee_client_hits_host_npc():
    """Client hits the host's NPC with a sword; the host's real NPC must lose health."""
    npc, replica = ensure_host_npc()
    approach("client", replica, 0.25)
    console_on("client", f"player.additem {IRON_SWORD:08X} 1")
    console_on("client", f"player.equipitem {IRON_SWORD:08X}")
    press_until("client", "r", lambda me: me.get("weapon_drawn"), "drawing the sword")
    before = host_npc_state(npc).get("health")
    log(f"host's real NPC health before: {before:.0f}")
    try:
        for _ in range(8):  # swing until something lands
            mouse_on("client", "mousedown"); time.sleep(0.15); mouse_on("client", "mouseup")
            time.sleep(0.9)
            now = host_npc_state(npc).get("health")
            if now is not None and before is not None and now < before - 1:
                log(f"host's real NPC health after the client's hits: {now:.0f}")
                break
        else:
            raise TestFailure(f"host's NPC health stayed at {host_npc_state(npc).get('health')} (before {before})")
        # The client's copy must show the host's value (no locally applied damage of its own).
        wait_on("client", "NPC replica health matches the host's",
                lambda actors: close(actors.get(str(replica), {}).get("health"), host_npc_state(npc).get("health")))
        hold()
    finally:
        console_on("", f"{npc:08X}.resurrect")  # also restores health and calms it for reruns
        press_until("client", "r", lambda me: not me.get("weapon_drawn"), "sheathing the sword")


def cast_at(instance, target, spell, hold_seconds, until, attempts=4):
    """In an instance's game: stand 150 units from `target` (form id in that game), face it, ready
    `spell` and cast (hold, release) until `until()` is true. Calls `during()` while waiting."""
    approach(instance, target, 0.8)
    console_on(instance, f"player.addspell {spell:08X}")
    console_on(instance, f"player.equipspell {spell:08X} right")
    console_on(instance, "player.restoreav magicka 1000")
    press_until(instance, "r", lambda me: me.get("weapon_drawn"), "readying the spell")
    try:
        for _ in range(attempts):
            mouse_on(instance, "mousedown")
            time.sleep(hold_seconds)
            mouse_on(instance, "mouseup")
            end = time.time() + 2.0
            while time.time() < end:
                if until():
                    return True
                time.sleep(0.1)
        return False
    finally:
        press_until(instance, "r", lambda me: not me.get("weapon_drawn"), "lowering the hands")


def spell_hits_host_npc(spell, hold_seconds):
    """Client casts `spell` at the host's NPC. The host's real NPC must take the damage, and the
    client's copy must only ever show the host's value: a copy that drops below its owner applied
    the effect itself (two games simulating one actor)."""
    npc, replica = ensure_host_npc()
    approach("client", replica, 0.8)
    console_on("client", f"player.addspell {spell:08X}")
    console_on("client", f"player.equipspell {spell:08X} right")
    console_on("client", "player.restoreav magicka 1000")
    press_until("client", "r", lambda me: me.get("weapon_drawn"), "readying the spell")
    before = host_npc_state(npc).get("health")
    log(f"host's real NPC health before: {before:.0f}")
    worst_gap = 0.0
    def sample():
        nonlocal worst_gap
        owner = host_npc_state(npc).get("health")
        copy = actors_on("client").get(str(replica), {}).get("health")
        if owner is not None and copy is not None:
            worst_gap = max(worst_gap, owner - copy)
        return owner
    try:
        for _ in range(4):  # cast until something lands
            mouse_on("client", "mousedown")
            end = time.time() + hold_seconds
            while time.time() < end:
                sample(); time.sleep(0.1)
            mouse_on("client", "mouseup")
            end = time.time() + 2.0
            while time.time() < end:
                sample(); time.sleep(0.1)
            now = sample()
            if now is not None and now < before - 1:
                log(f"host's real NPC health after the client's spell: {now:.0f}")
                break
        else:
            raise TestFailure(f"host's NPC health stayed at {host_npc_state(npc).get('health')} (before {before}); "
                              f"client's copy went down to {before - worst_gap:.0f}" if worst_gap > 1 else
                              f"host's NPC health stayed at {host_npc_state(npc).get('health')} (before {before})")
        if worst_gap > 5:
            raise TestFailure(f"the client's copy dropped {worst_gap:.0f} below the host's real NPC: "
                              "the client applied the spell to its copy itself")
        wait_on("client", "NPC copy health matches the host's",
                lambda actors: close(actors.get(str(replica), {}).get("health"), host_npc_state(npc).get("health")))
        hold()
    finally:
        console_on("", f"{npc:08X}.resurrect")
        press_until("client", "r", lambda me: not me.get("weapon_drawn"), "lowering the hands")


def scenario_firebolt_client_hits_host_npc():
    """Client's Firebolt hits the host's NPC: only the host applies it."""
    spell_hits_host_npc(FIREBOLT, 1.0)  # charge, then release to fire


def scenario_flames_client_hits_host_npc():
    """Client's Flames stream hits the host's NPC: only the host applies it."""
    spell_hits_host_npc(FLAMES, 2.0)


SCENARIOS = {
    "steady-state": scenario_steady_state,
    "equip-host-player": scenario_equip_host_player,
    "equip-client-player": scenario_equip_client_player,
    "equip-host-npc": scenario_equip_host_npc,
    "draw-host-player": scenario_draw_host_player,
    "draw-client-player": scenario_draw_client_player,
    "cast-host-player": scenario_cast_host_player,
    "cast-client-player": scenario_cast_client_player,
    "health-host-player": scenario_health_host_player,
    "health-client-player": scenario_health_client_player,
    "health-host-npc": scenario_health_host_npc,
    "death-host-npc": scenario_death_host_npc,
    "remove-host-npc": scenario_remove_host_npc,
    "move-host-npc": scenario_move_host_npc,
    "melee-client-hits-host-npc": scenario_melee_client_hits_host_npc,
    "repair-equipment-drift": scenario_repair_equipment_drift,
    "firebolt-client-hits-host-npc": scenario_firebolt_client_hits_host_npc,
    "flames-client-hits-host-npc": scenario_flames_client_hits_host_npc,
    "steady-state-after": scenario_steady_state,
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
# Each game's status.json carries "world": a snapshot of every loaded actor (Runtime.cpp
# Snapshot()). Every actor has exactly one owner game; the other game holds a copy linked by the
# same server id. The checker pairs them and compares every snapshot field, so a field added to
# Snapshot() is checked without touching this code. Rules below only say how to compare a field
# (tolerances) or why a field is expected to differ.

# Differences the design does not handle yet: reported by `desync`, but they do not fail scenarios.
KNOWN_GAP = "known gap (copies do not show magic effects yet): "
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
    if field.startswith(("mod.", "ai.")) or field in ("role", "identity.template_base", "identity.temporary_base"):
        return None  # bookkeeping (Local vs Remote) and AI (owner only) differ by design
    if field == "effects":  # copies carry no abilities or worn enchantments: compare timed effects
        timed = lambda effects: sorted((e["effect"], e["spell"], round(e["magnitude"])) for e in effects or []
                                       if e["duration"] > 0 and not e["flags"] & 0x48000)
        missing = set(timed(owner)) - set(timed(copy))
        extra = set(timed(copy)) - set(timed(owner))
        if not missing and not extra:
            return None
        fmt = lambda items: ",".join(f"{e:08X}/{s:08X}" for e, s, _ in sorted(items))
        return KNOWN_GAP + " ".join(filter(None, [missing and "copy lacks " + fmt(missing),
                                                  extra and "copy also has " + fmt(extra)]))
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


def check_desync(samples=3, interval=1.0, include_known=False):
    """Only differences seen in every sample count (copies legitimately lag for a moment)."""
    persistent = None
    for index in range(samples):
        current = {k: v for k, v in find_desyncs() if include_known or KNOWN_GAP not in v}
        persistent = current if persistent is None else {k: v for k, v in current.items() if k in persistent}
        if not persistent:
            return []
        if index + 1 < samples:
            time.sleep(interval)
    return list(persistent.values())


def command_desync(args):
    while True:
        problems = check_desync(args.samples, args.interval, include_known=True)
        real = [p for p in problems if KNOWN_GAP not in p]
        report = "\n".join(problems) if problems else "in sync: every copy matches its owner"
        if problems and not real:
            report += "\n(only known gaps: in sync otherwise)"
        if not args.follow:
            print(report)
            if real:
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


def command_scenario(args):
    global HOLD
    HOLD = args.hold
    names = args.names or list(SCENARIOS)
    unknown = [name for name in names if name not in SCENARIOS]
    if unknown:
        raise TestFailure(f"unknown scenario(s) {unknown}; known: {', '.join(SCENARIOS)}")
    for name in ("", "client"):
        select_instance(name)
        if not read_status():
            raise TestFailure(f"{name or 'host'} game is not running: start `{sys.argv[0]} duo --keep-running` first")
    results = {}
    for name in names:
        log(f"=== {name}: {SCENARIOS[name].__doc__}")
        try:
            SCENARIOS[name]()
            # Whatever the scenario did, both games must agree afterwards.
            problems = check_desync(samples=4)
            if problems:
                raise TestFailure("desync afterwards: " + " | ".join(problems))
            results[name] = "PASS"
        except TestSkipped as error:
            results[name] = f"SKIP ({error})"
        except TestFailure as error:
            results[name] = f"FAIL ({error})"
        log(f"=== {name}: {results[name]}")
    print()
    for name, result in results.items():
        print(f"{name:24s} {result}")
    if any(r.startswith("FAIL") for r in results.values()):
        raise SystemExit(1)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    commands = parser.add_subparsers(dest="command", required=True)
    net = commands.add_parser("net", help="headless multi-bot network test")
    net.add_argument("--bots", type=int, default=4)
    net.add_argument("--seconds", type=int, default=3)
    net.add_argument("--hz", type=int, default=5)
    for name in ("launch", "game", "duo"):
        sub = commands.add_parser(name)
        sub.add_argument("--cell", default="QASmoke", help="cell for `coc` from the main menu (default QASmoke)")
        sub.add_argument("--save", help="load this save instead of coc")
        sub.add_argument("--boot-timeout", type=float, default=240)
        sub.add_argument("--world-timeout", type=float, default=180)
        sub.add_argument("--visible", action="store_true",
                         help="show the game on the desktop (default: off-screen Xvfb display; see `screenshot`)")
        if name == "launch":
            sub.add_argument("--vanilla", action="store_true",
                             help="launch without our plugin (A/B baseline; the next normal launch redeploys it)")
        if name in ("game", "duo"):
            sub.add_argument("--seconds", type=float, default=30, help="how long to wait for bot movement")
            sub.add_argument("--tolerance", type=float, default=5, help="max engine vs requested distance")
            sub.add_argument("--keep-running", action="store_true", help="leave Skyrim open afterwards")
        if name == "duo":
            sub.add_argument("--npc", default="000A2C8E",
                             help="NPC base form (8 hex digits) the host spawns; the client must show it (default Lydia, '' to skip)")
    scenario = commands.add_parser("scenario", help="sync scenarios against a running `duo --keep-running`")
    scenario.add_argument("--hold", type=float, default=0,
                          help="pause N seconds after each synced step and face the camera at it (for watching)")
    scenario.add_argument("names", nargs="*", help=f"default: all of {', '.join(SCENARIOS)}")
    stop = commands.add_parser("stop", help="stop the test game(s) (both instances unless --instance)")
    console = commands.add_parser("console", help="run a console command in the running test game")
    console.add_argument("text", nargs="+")
    status = commands.add_parser("status", help="both games side by side: health, death, weapon, casting")
    status.add_argument("--follow", "-f", action="store_true", help="keep refreshing")
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
        elif args.command == "console":
            run_console(" ".join(args.text))
            log("Done")
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
