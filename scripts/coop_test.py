#!/usr/bin/env python3
"""One-command co-op tests.

  coop_test.py launch [--cell CELL | --save NAME]     fast-boot Skyrim straight into a cell, leave it running
  coop_test.py duo    [--keep-running --visible]      two real games: host + client, each must show the other
                      [--save NAME --level N]         (from a save; raise both players' level after loading)
  coop_test.py scenario [names] [--tag a,b] [--shuffle --repeat N] sync scenarios against a running duo; each
                                                      starts from the same baseline and is cleaned up after
  coop_test.py affected [--base REF] [--run] [--check] select scenarios from changed sources (default HEAD);
                                                      core changes need the full suite, never started here
  coop_test.py soak [--hours H | --rounds N] [--seed S] fresh duo each round, shuffled suite, persistent reports;
                      [--tag a,b]                     a tag limits this to a smoke test of the soak
  coop_test.py reload                                 swap the current build into a running duo and reconnect
  coop_test.py stop                                   kill the test Skyrim instance(s)
  coop_test.py screenshot [out.png]                   capture the off-screen game display
  coop_test.py console <command...>                   run a console command in the test game
  coop_test.py status [-f]                            both games side by side: health, death, weapon, casting
  coop_test.py save-diff [--label X --noise S]       save both games at once, compare the two worlds block by block
  coop_test.py change-rate [names] [--seconds S]     the engine's change records per form type and flag (idle or scenarios)
  coop_test.py quest-walk QUEST --names-from SAVE    walk the host through a quest (duo booted with --save SAVE),
                                                      the client leaving and rejoining; how its quest compares each step
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

A failed scenario explains itself: under the FAIL line, what each game saw (its open menus and its plugin log
lines, info and up, from the harness's log trail: the failing step's marked '>', backtraces symbolized), cut at
the moment of failure. World-state messages log one line each where they are sent, applied or proposed
(Services/WorldStateLog.h), so a missing step shows as a missing line; each key press and release logs as the
game dispatched it (TestHarness/InputTrace.cpp: its user event, each input sink's answer, the menus open). A
console line waits until the console ran it and fails on the console's own error (docs/re/systems/console.md).

Scenarios can record an animation trace (AnimTrace: both games' graph events, actions, life state and
positions each frame, both screens, one timeline) into build/coop-test/traces/; see host-kill-move.

Skyrim runs in its own Proton prefix under build/coop-test (never your normal saves or
settings): no intro, windowed, low preset, and `coc <cell>` from the main menu. Paths
default to this machine; override with SKYRIM_GAME_DIR, SKYRIM_PROTON_DIR,
SKYRIM_RUNTIME_DIR. Every game run keeps its logs in build/coop-test/runs/<time>/.
"""
import argparse
import collections
import contextlib
import fnmatch
import json
import math
import os
import pwd
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
# The client game runs as another OS user, whose Steam is logged into a second account: Steam invites need two
# (docs/design/join-flow.md, "Steam"). Default: the test user `coop2` when it exists; "" runs both as this user.
def user_exists(name):
    try:
        return bool(pwd.getpwnam(name))
    except KeyError:
        return False


CLIENT_USER = os.environ.get("COOP_CLIENT_USER", "coop2" if user_exists("coop2") else "")
TEST_HOME = (ROOT / "build/coop-test").resolve()  # one name for the games, from any checkout or worktree
PREFIX = TEST_HOME / "prefix-host"
DLL = Path(os.environ.get("COOP_DLL", ROOT / "build/windows/x64/dev/SkyrimTogetherClient.dll"))
# Resident SKSE plugin that loads the client from a copy, so `reload` can swap in a rebuild
# (Code/hotreload/Loader.cpp). Without it the client is deployed as the SKSE plugin itself.
LOADER = DLL.with_name("SkyrimCoopLoader.dll")
# The harness without SkyrimCoop (Code/vanilla_harness): `repro --vanilla` and `launch --vanilla`.
VANILLA_HARNESS = DLL.with_name("SkyrimCoopVanillaHarness.dll")
# A game stand-in on the network (Code/tests/peer): a host that refuses joins, or a game asking to join.
PEER = ROOT / "build/linux/x86_64/debug/coop_peer"
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
    def __init__(self, *args):
        super().__init__(*args)
        self.time = time.time()  # wall clock, as the games' log lines: the failure digest cuts here


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


# The client user's own network namespace: every Steam client serves games on the fixed 127.0.0.1:57343, so a second
# Steam on this machine needs its own loopback (docs/design/testing.md, "Two Steam accounts").
CLIENT_NETNS, HOST_SIDE_ADDRESS, CLIENT_ADDRESS = "coop2", "10.77.0.1", "10.77.0.2"


def sudo(*command):
    return subprocess.run(["sudo", "-n", *command], capture_output=True, text=True)


def ensure_client_netns():
    """The namespace, its link to this one (10.77.0.1 here, 10.77.0.2 there), NAT out and DNS; kept until reboot."""
    if CLIENT_NETNS not in sudo("ip", "netns", "list").stdout.split():
        log(f"Creating the client's network namespace {CLIENT_NETNS} (once per boot)")
        for command in (["ip", "netns", "add", CLIENT_NETNS],
                        ["ip", "link", "add", "coop2-host", "type", "veth", "peer", "name", "coop2-ns"],
                        ["ip", "link", "set", "coop2-ns", "netns", CLIENT_NETNS],
                        ["ip", "addr", "add", f"{HOST_SIDE_ADDRESS}/24", "dev", "coop2-host"],
                        ["ip", "link", "set", "coop2-host", "up"],
                        ["ip", "netns", "exec", CLIENT_NETNS, "ip", "addr", "add", f"{CLIENT_ADDRESS}/24", "dev", "coop2-ns"],
                        ["ip", "netns", "exec", CLIENT_NETNS, "ip", "link", "set", "coop2-ns", "up"],
                        ["ip", "netns", "exec", CLIENT_NETNS, "ip", "link", "set", "lo", "up"],
                        ["ip", "netns", "exec", CLIENT_NETNS, "ip", "route", "add", "default", "via", HOST_SIDE_ADDRESS]):
            if (result := sudo(*command)).returncode != 0:
                raise TestFailure(f"netns setup failed: {' '.join(command)}: {result.stderr.strip()}")
    sudo("sysctl", "-q", "-w", "net.ipv4.ip_forward=1")
    # Forwarding is dropped by default here (Docker's policy): let the namespace's traffic through, then NAT it.
    for table, chain, rule in (("filter", "FORWARD", ["-i", "coop2-host", "-j", "ACCEPT"]),
                               ("filter", "FORWARD", ["-o", "coop2-host", "-j", "ACCEPT"]),
                               ("nat", "POSTROUTING", ["-s", "10.77.0.0/24", "!", "-o", "coop2-host", "-j", "MASQUERADE"])):
        if sudo("iptables", "-t", table, "-C", chain, *rule).returncode != 0:
            sudo("iptables", "-t", table, "-I", chain, "1", *rule)
    resolv = Path(f"/etc/netns/{CLIENT_NETNS}/resolv.conf")
    if not resolv.exists():
        sudo("mkdir", "-p", str(resolv.parent))
        subprocess.run(["sudo", "-n", "tee", str(resolv)], input="nameserver 1.1.1.1\nnameserver 8.8.8.8\n",
                       text=True, capture_output=True)


def in_client_netns(pid):
    """That process runs in the client's namespace."""
    mine = sudo("readlink", f"/proc/{pid}/ns/net").stdout.strip()
    theirs = sudo("ip", "netns", "exec", CLIENT_NETNS, "readlink", "/proc/self/ns/net").stdout.strip()
    return bool(mine) and mine == theirs


def instance_runtime():
    """The Steam runtime this instance's game runs in: the client user's own copy (its container hard-links the
    runtime's files, which it may only do for files it owns), refreshed when Steam updates the runtime."""
    if not instance_user():
        return RUNTIME
    copy = TEST_HOME / f"runtime-{instance_user()}"
    if not (copy / "VERSIONS.txt").exists() or (copy / "VERSIONS.txt").read_bytes() != (RUNTIME / "VERSIONS.txt").read_bytes():
        log(f"Copying the Steam runtime for {instance_user()} (once per runtime update, ~800 MB)")
        sudo("rm", "-rf", str(copy))
        sudo("install", "-d", "-o", instance_user(), "-g", instance_user(), str(copy))
        result = sudo("-u", instance_user(), "rsync", "-a", "--exclude", "/var/", f"{RUNTIME}/", f"{copy}/")
        if result.returncode != 0:
            raise TestFailure(f"copying the Steam runtime failed: {result.stderr.strip()}")
    return copy


def instance_user():
    """The OS user this instance's game runs as, when not this one (CLIENT_USER for the client)."""
    return CLIENT_USER if INSTANCE == "client" and CLIENT_USER else None


def as_instance_user(command, env):
    """The command run as the instance's user (sudo, the environment passed explicitly, files group-writable), so
    its game talks to that user's Steam; unchanged for this user."""
    user = instance_user()
    if not user:
        return command
    personal = {"HOME", "USER", "LOGNAME", "XDG_RUNTIME_DIR", "DBUS_SESSION_BUS_ADDRESS", "XAUTHORITY", "MAIL"}
    # HOST_HOME and its kin name this user's home, where Steam's container would find this user's Steam.
    assignments = [f"{key}={value}" for key, value in env.items()
                   if key not in personal and not key.startswith("HOST_")]
    # Its own session bus (user_bus): without one, a game's D-Bus client starts one that outlives the game.
    assignments.append(f"DBUS_SESSION_BUS_ADDRESS=unix:path={user_bus(user)}")
    ensure_client_netns()
    return ["sudo", "-n", "ip", "netns", "exec", CLIENT_NETNS, "sudo", "-u", user, "-H", "--", "env", *assignments,
            "bash", "-c", 'umask 002; exec "$@"', "_", *command]


def user_bus(user):
    """A session bus for that user, started once and left running (the client user has no desktop session)."""
    path = Path(f"/tmp/skyrimcoop-{user}-bus")
    if not path.is_socket():
        subprocess.run(["sudo", "-n", "-u", user, "-H", "dbus-daemon", "--session", f"--address=unix:path={path}",
                        "--fork", "--nopidfile"], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    return path


def game_env():
    env = dict(os.environ)
    steam = Path(f"/home/{instance_user()}/.steam/steam") if instance_user() else STEAM
    env.update(SteamAppId="489830", SteamGameId="489830", STEAM_COMPAT_DATA_PATH=str(PREFIX),
               STEAM_COMPAT_CLIENT_INSTALL_PATH=str(steam), WINEDEBUG=env.get("WINEDEBUG", "-all"),
               SKYRIMCOOP_LOG_LEVEL=env.get("SKYRIMCOOP_LOG_LEVEL", "debug"),
               # The UI straight from the repo (Wine's Z: is /): edits show up in game within a second.
               SKYRIMCOOP_UI_DIR=env.get("SKYRIMCOOP_UI_DIR", "Z:" + str(UI_SOURCE).replace("/", "\\")))
    if INSTANCE:
        env["SKYRIMCOOP_INSTANCE"] = INSTANCE
    if instance_user():  # in its namespace: reachable on its link, and the host is across it
        env.update(SKYRIMCOOP_COMMAND_ADDRESS=CLIENT_ADDRESS, SKYRIMCOOP_JOIN_HOST=HOST_SIDE_ADDRESS)
    if not VISIBLE:
        env["DISPLAY"] = VIRTUAL_DISPLAY
    return env


def uses_prefix(environ, keys=("STEAM_COMPAT_DATA_PATH", "WINEPREFIX")):
    """That process environment names our test prefix, in any path spelling (another worktree's build/
    link, or Steam's rewrite of STEAM_COMPAT_DATA_PATH while WINEPREFIX keeps ours)."""
    prefix = PREFIX.resolve()
    for line in environ.split(b"\0"):
        key, _, value = line.partition(b"=")
        if key.decode(errors="replace") in keys and value:
            path = Path(value.decode(errors="replace").rstrip("/"))
            path = path.parent if path.name == "pfx" else path
            try:
                if path.resolve() == prefix:
                    return True
            except OSError:
                pass
    return False


def process_environs():
    """{pid: environment} of every process; the client user's through one sudo (its /proc environ is not ours)."""
    environs, theirs = {}, []
    uid = pwd.getpwnam(CLIENT_USER).pw_uid if CLIENT_USER else None
    for entry in Path("/proc").iterdir():
        if entry.name.isdigit() and int(entry.name) != os.getpid():
            try:
                environs[int(entry.name)] = (entry / "environ").read_bytes()
            except PermissionError:
                with contextlib.suppress(OSError):
                    if entry.stat().st_uid == uid:
                        theirs.append(entry.name)
            except OSError:
                pass
    if theirs:
        script = ("import json,sys\nout={}\nfor p in sys.argv[1:]:\n try: out[p]=open(f'/proc/{p}/environ','rb')"
                  ".read().hex()\n except OSError: pass\nprint(json.dumps(out))")
        result = subprocess.run(["sudo", "-n", "python3", "-c", script, *theirs], capture_output=True, text=True)
        with contextlib.suppress(ValueError):
            environs.update({int(pid): bytes.fromhex(data) for pid, data in json.loads(result.stdout).items()})
    return environs


def signal_process(pid, sig):
    """os.kill, through sudo for the client user's processes."""
    try:
        os.kill(pid, sig)
    except PermissionError:
        subprocess.run(["sudo", "-n", "kill", f"-{sig.name.removeprefix('SIG')}", str(pid)], capture_output=True)


def prefix_processes():
    """PIDs started for our test prefix (matched by environment, so nothing else is touched)."""
    return [pid for pid, environ in process_environs().items() if uses_prefix(environ)]


def game_process(instance):
    """The instance's SkyrimSE.exe (by its wine prefix; not the launcher stubs that share the name)."""
    select_instance(instance)
    for pid, environ in process_environs().items():
        entry = Path("/proc") / str(pid)
        try:
            if "SkyrimSE.exe" in (entry / "cmdline").read_bytes().decode(errors="replace") and \
                    uses_prefix(environ, ("WINEPREFIX",)) and \
                    int((entry / "statm").read_text().split()[1]) * 4096 > 100 << 20:
                return pid
        except (OSError, ValueError, IndexError):
            pass
    raise TestFailure(f"[{instance or 'host'}] no running SkyrimSE.exe")


@contextlib.contextmanager
def game_stalled(instance):
    """The game's process stops (as a hitch or a slow load would hold it) and runs again afterwards: what
    reaches it meanwhile waits on its socket."""
    pid = game_process(instance)
    signal_process(pid, signal.SIGSTOP)
    log(f"[{instance or 'host'}] game stalled")
    try:
        yield
    finally:
        signal_process(pid, signal.SIGCONT)
        log(f"[{instance or 'host'}] game runs again")


def stop_game():
    pids = prefix_processes()
    for sig in (signal.SIGTERM, signal.SIGKILL):
        for pid in pids:
            try:
                signal_process(pid, sig)
            except ProcessLookupError:
                pass
        deadline = time.monotonic() + 5
        while time.monotonic() < deadline and (pids := prefix_processes()):
            time.sleep(0.2)
        if not pids:
            return True
    return not prefix_processes()


def check_prerequisites():
    required = [GAME / "SkyrimSE.exe", GAME / "skse64_loader.exe", PROTON / "proton", RUNTIME / "run", DLL]
    missing = [str(path) for path in required if not path.exists()]
    if missing:
        hint = ("\nBuild the DLL: scripts/xmake-msvc.sh f -m dev && scripts/xmake-msvc.sh build -j16 SkyrimTogetherClientDLL && scripts/xmake-msvc.sh build SkyrimCoopLoader")
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
        allow_instance_user()
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
    allow_instance_user()


def allow_instance_user():
    """Lets the instance's user draw on its display (xhost)."""
    if user := instance_user():
        subprocess.run(["xhost", f"+SI:localuser:{user}"], env={**os.environ, "DISPLAY": game_env()["DISPLAY"]},
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)


def ensure_user_steam():
    """The instance user's Steam client is running (its game's Steam API needs it), started silently on the game's
    display if not. Logged in once by hand: `sudo -u coop2 -H env DISPLAY=:1 steam`."""
    user = instance_user()
    if not user:
        return
    ensure_client_netns()

    def steam_pids():
        return [int(p) for p in subprocess.run(["pgrep", "-u", user, "-x", "steam"], capture_output=True,
                                               text=True).stdout.split()]
    if (pids := steam_pids()) and all(in_client_netns(pid) for pid in pids):
        return
    if pids:  # outside the namespace it gets no game: restart it inside (its login is kept)
        log(f"[{INSTANCE}] restarting {user}'s Steam inside {CLIENT_NETNS}")
        sudo("pkill", "-u", user, "-x", "steam")
        wait_for(f"[{INSTANCE}] {user}'s old Steam exited", lambda: not steam_pids(), 60)
    allow_instance_user()
    subprocess.Popen(["sudo", "-n", "ip", "netns", "exec", CLIENT_NETNS, "sudo", "-u", user, "-H", "env",
                      f"DISPLAY={game_env()['DISPLAY']}", f"DBUS_SESSION_BUS_ADDRESS=unix:path={user_bus(user)}",
                      "/usr/games/steam", "-silent"],
                     stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, start_new_session=True)
    wait_for(f"[{INSTANCE}] {user}'s Steam is running in {CLIENT_NETNS}",
             lambda: subprocess.run(["pgrep", "-u", user, "-f", "steamwebhelper"], capture_output=True).returncode == 0,
             180)


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
        subprocess.run(as_instance_user([str(instance_runtime() / "run"), "--", str(PROTON / "proton"), "run", "wineboot", "-u"],
                                        game_env()), env=game_env(), cwd=GAME, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
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
            sock = socket.create_connection((info.get("address", "127.0.0.1"), info["port"]), timeout=1.0)
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


# The step a scenario waits on now, and when it began (wall clock, as the games' log lines): the failure digest
# marks what each game logged from then on.
CURRENT_STEP = {"description": None, "time": 0.0}


def wait_for(description, predicate, timeout, launcher=None):
    log(description + "...")
    CURRENT_STEP.update(description=description, time=time.time())
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
    ensure_user_steam()
    launcher = subprocess.Popen(
        as_instance_user([str(instance_runtime() / "run"), "--", str(PROTON / "proton"), "waitforexitandrun",
                          str(GAME / "skse64_loader.exe")], game_env()),
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


def new_run_dir(kind):
    run_dir = TEST_HOME / "runs" / (time.strftime("%Y%m%d-%H%M%S") + "-" + kind)
    run_dir.mkdir(parents=True, exist_ok=True)
    return run_dir


def command_launch(args):
    check_prerequisites()
    run_dir = new_run_dir("launch")
    try:
        launch(args, run_dir)
    finally:
        collect_logs(run_dir)
    log(f"Ready. Skyrim keeps running; stop it with: {sys.argv[0]} stop  (logs: {run_dir})")


# What the console prints for a line it could not run (its compiler's errors): the line did nothing.
CONSOLE_ERRORS = re.compile(r"not found|Syntax error|Missing parameter|Compiled script not saved|not run|cannot be moved", re.IGNORECASE)


# "<reference id>.<command>": the console takes no such line for an id starting with a digit ("1A695.getav"), so the
# harness runs the command on the reference itself, as the console's selected one (docs/re/systems/console.md).
CONSOLE_ON_REFERENCE = re.compile(r"^([0-9A-Fa-f]{1,8})\.(\S.*)$")


def run_console(command, timeout=10):
    """Runs a console command in the test game (after the boot command) and waits until the console ran it; returns
    what it printed. A line the console could not run fails the scenario with the console's own error."""
    request = {"cmd": "console", "line": command}
    if on_reference := CONSOLE_ON_REFERENCE.match(command):
        request.update(line=on_reference.group(2), target=int(on_reference.group(1), 16))
    response = rpc(request, timeout=timeout)
    if response and response.get("ok"):
        ticket, deadline = response.get("ticket"), time.monotonic() + timeout
        while ticket is not None:
            result = rpc({"cmd": "console_result", "ticket": ticket}) or {}
            if result.get("ran"):
                output = result.get("output") or []
                log(f"Console: {command}" + (f"  -> {' | '.join(output)}" if output else ""))
                if errors := [line for line in output if CONSOLE_ERRORS.search(line)]:
                    raise TestFailure(f"console could not run `{command}`: {' | '.join(errors)}")
                return output
            if time.monotonic() > deadline:
                raise TestFailure(f"console line `{command}` never ran (still queued after {timeout}s)")
            time.sleep(0.01)
        log(f"Console: {command}")
        return []
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


def other_players():
    """Remote actors this instance shows in the engine with 3D: {form id: position}."""
    status = read_status() or {}
    return {int(actor): sample["actual"] for actor, sample in status.get("actors", {}).items() if sample.get("has_3d")}


def both_see_each_other():
    """{"host"/"client": remote players} once each game shows the other's player, else None."""
    seen = {}
    for name in ("", "client"):
        select_instance(name)
        players = other_players()
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
    check_prerequisites()
    run_dir = getattr(args, "run_dir", None) or new_run_dir("duo")
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
            if args.level:  # level-gated story manager starts (DA07: 20) need it
                run_console(f"player.setlevel {args.level}")
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
            before = set(other_players())
            select_instance("")
            run_console(f"player.placeatme {args.npc} 1")
            select_instance("client")
            new_actors = wait_for(f"Client shows the host's new NPC (base {args.npc})",
                                  lambda: set(other_players()) - before, args.seconds)
            for actor in new_actors:
                log(f"client shows host NPC {actor:08X}")
        passed = True
        print(f"\nPASS: host and client are connected and each shows the other's character"
              f"{' and the host NPC' if args.npc else ''}. Logs: {run_dir}")
        if args.keep_running:
            for name in ("", "client"):
                select_instance(name)
                face_actor(next(iter(seen[name or "host"])))
            save_home(cell_name=args.cell if not args.save else None)
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
    return run_dir

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
    return run_console(command)


def local_player(instance):
    select_instance(instance)
    return (read_status() or {}).get("player") or {}


def player_snapshot(instance):
    select_instance(instance)
    return ((read_status() or {}).get("world") or {}).get("20") or {}


def other_player(actors):
    return next((int(a) for a, s in actors.items() if s.get("other_player") and s.get("has_3d")), None)


def remote_npcs(actors):
    return {int(a) for a, s in actors.items() if not s.get("other_player") and s.get("has_3d")}


def close(a, b, tolerance=1.0):
    return a is not None and b is not None and abs(a - b) <= tolerance


def distance(a, b):
    return sum((x - y) ** 2 for x, y in zip(a, b)) ** 0.5


def comes_to_rest(read, still=0.5, within=2.0):
    """A wait_for predicate: the position `read()` returns, once it has moved less than `within`
    units for `still` seconds."""
    seen = {"since": time.monotonic(), "at": None}
    def rested():
        at = read()
        if seen["at"] is None or distance(at, seen["at"]) > within:
            seen.update(since=time.monotonic(), at=at)
        return time.monotonic() - seen["since"] >= still and at
    return rested


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
    """No menu but the HUD is open and no kill cam plays: a fade (a cell change's Fader Menu), a loading
    screen, the console or a kill cam (the player's killing blow, by chance) swallow a player's buttons."""
    select_instance(instance)
    status = read_status() or {}
    return not set(status.get("menus") or []) - {"HUD Menu"} and not status.get("kill_cam")


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
        # A killing blow can start a kill move instead (a paired animation: synced, not attacking).
        wait_for(f"[{instance or 'host'}] swings", lambda: any(graph_bools(instance, "IsAttacking", "bIsSynced").values()), 2)
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
    Returns once it stands on the ground and has landed: the move leaves its character controller in
    the air for a moment, and a draw or a cast started then, or during the landing animation the
    graph plays after the controller is down (bInJumpState), goes wrong. The controller notices the
    move a frame later, so the ground is checked from a few frames on."""
    moved = game_frame(instance)
    request = {"cmd": "place", "target": target, "distance": gap}
    if bearing is not None:
        request["bearing"] = bearing  # its side of the target (0 north, 90 east): two players apart
    player_command(instance, request)
    wait_for(f"[{instance or 'host'}] stands on the ground", lambda: game_frame(instance) >= moved + 3
             and local_player(instance).get("character_state") == ON_GROUND
             and not graph_bools(instance, "bInJumpState")["bInJumpState"], 5)


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


# --- The host's NPCs and their copies, by session id -----------------------------------------

def host_npc_state(npc):
    return local_player("").get("local_actors", {}).get(str(npc), {})


def host_world(form_id):
    select_instance("")
    return ((read_status() or {}).get("world") or {}).get(str(form_id)) or {}


def copy_of(instance, session_id, timeout=15):
    """The form id of the copy, in that game, of the actor with this session id."""
    return wait_on(instance, f"shows server actor {session_id:X}",
                   lambda actors: next((int(a) for a, s in actors.items()
                                        if s.get("session_id") == session_id and s.get("has_3d")), None), timeout)


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
    """The host spawns an NPC on the stage: (host ref id, session id, the client's copy).
    Restrained, it stays there (an NPC otherwise walks up to greet the players)."""
    before = set(local_player("").get("local_actors", {}))
    console_on("", f"player.placeatme {base} 1")
    npc = int(next(iter(wait_for("[host] new NPC loaded",
                                 lambda: set(local_player("").get("local_actors", {})) - before, 15))))
    # Unassigned, it reads -1.
    session_id = wait_for("[host] NPC has a session id",
                          lambda: (sid := host_npc_state(npc).get("session_id")) is not None and sid >= 0 and (sid,), 10)[0]
    if restrained:
        console_on("", f"{npc:08X}.setrestrained 1")
    goal = stage_spot()
    place("", f"{npc:08X}", goal, host_npc_state(npc)["position"])
    wait_for("[host] NPC on the stage", lambda: distance(host_npc_state(npc)["position"][:2], goal[:2]) < 40
             and abs(host_npc_state(npc)["position"][2] - goal[2]) < 60, 10)
    copy = copy_of("client", session_id)
    wait_on("client", "its copy on the stage",
            lambda actors: distance((actors.get(str(copy), {}).get("actual") or [1e9, 1e9])[:2], goal[:2]) < 60, 10)
    return npc, session_id, copy


def freeze(instance, actor, frozen=True):
    """Freezes (or thaws) the actor in that game: no AI, not pushed, players pass through it; it still takes hits."""
    player_command(instance, {"cmd": "freeze", "actor": actor, "frozen": frozen})


def stage_host_npc(npc):
    """Brings one of the host's placed NPCs (wherever it is, in furniture or not) onto the stage and freezes it there;
    returns the client's copy. release_host_npc sends it back."""
    goal = stage_spot()
    # Its own package can bring it into the cell elsewhere (an unloaded actor travelling to the player arrives at
    # the door): it is frozen once here, then moved within the cell.
    player_command("", {"cmd": "move_actor", "actor": npc, "position": goal})
    wait_for(f"[host] {npc:X} is in the home cell", lambda: host_npc_state(npc).get("session_id", -1) >= 0, 10)
    freeze("", npc)
    player_command("", {"cmd": "move_actor", "actor": npc, "position": goal})
    try:
        wait_for(f"[host] {npc:X} stands on the stage", lambda: distance(host_npc_state(npc)["position"][:2], goal[:2]) < 40, 5)
    except TestFailure:
        raise TestFailure(f"[host] {npc:X} is not on the stage {[round(x) for x in goal]}: {host_world(npc).get('placement')}")
    freeze("", npc)  # again: its character controller exists now
    copy = copy_of("client", host_npc_state(npc)["session_id"])
    wait_on("client", f"its copy of {npc:X} on the stage",
            lambda actors: distance((actors.get(str(copy), {}).get("actual") or [1e9, 1e9])[:2], goal[:2]) < 60, 10)
    return copy


def release_host_npc(npc):
    """Sends a staged NPC back where the game placed it and thaws it; the client's (its own reference, shown as a
    copy) follows the host's there. Thawed once there: an unloaded actor's AI walks it on in jumps."""
    player_command("", {"cmd": "move_actor", "actor": npc, "home": True})
    wait_for(f"[host] {npc:X} has gone home", lambda: not host_npc_state(npc), 10)
    try:
        copy_follows_host(npc, "goes home with the host's")
    finally:
        freeze("", npc, False)


def actor_place(instance, actor):
    """Where that game has the actor, loaded or not ({} if it has none): cell, worldspace, position, has_3d,
    disabled, deleted, and whether it is a copy there."""
    select_instance(instance)
    response = rpc({"cmd": "actor_place", "actor": actor}) or {}
    return response if response.get("ok") else {}


def copy_follows_host(npc, what, timeout=10):
    """The client's reference of the host's NPC is where the host's is (its cell, within 64 units) and as enabled,
    and still a copy: never left behind as the client's own (copy-contract.md, "When the owner's actor goes away")."""
    def follows():
        host, client = actor_place("", npc), actor_place("client", npc)
        return (host and client and client["copy"] and client["cell"] == host["cell"]
                and distance(client["position"], host["position"]) < 64 and client["disabled"] == host["disabled"]
                and client)
    try:
        return wait_for(f"[client] its {npc:X} {what}", follows, timeout)
    except TestFailure:
        raise TestFailure(f"[client] its {npc:X} did not follow the host's ({what}): host {actor_place('', npc)}, "
                          f"client {actor_place('client', npc)}")


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


# --- Animation trace: both games' animations on one timeline ---------------------------------

TRACES = TEST_HOME / "traces"
# Left out of the timeline (still in the JSON): steps, sounds and the first-person graph's echo.
TRACE_NOISE = re.compile(r"^(Foot|Sound|tail|HeadTracking|PitchOverride|IdleOffset|EnableBumper|DisableBumper|CastOK)")
LOG_LINE = re.compile(r"^\[(\d{4}-\d\d-\d\d \d\d:\d\d:\d\d\.\d{3})\] \[(\w+)\] (.*)$")


class AnimTrace:
    """Records what actors' animations do in both games (the harness's anim_trace: every behavior graph
    event, action taken, life state, kill move, health and position, each frame), both screens, and the
    plugin logs' lines naming them, onto one wall-clock timeline:

        with AnimTrace("kill-move", {"": {npc: "wolf", 0x14: "host"}, "client": {copy: "wolf"}}) as trace:
            ...
            trace.wait("client", "wolf", "KillActor", 3)

    Leaves build/coop-test/traces/<time>-<name>/: timeline.txt (host left, client right), trace.json,
    host.mp4 / client.mp4, and strip-*.png (both screens side by side) at moments asked for with mark()."""

    def __init__(self, name, actors, bools=("bIsSynced",), video=True):
        self.name, self.actors, self.bools, self.video = name, actors, list(bools), video
        self.entries, self.marks, self.captures = [], [], {}

    def __enter__(self):
        self.dir = TRACES / f"{time.strftime('%Y%m%d-%H%M%S')}-{self.name}"
        self.dir.mkdir(parents=True, exist_ok=True)
        if self.video:
            for game in self.actors:
                select_instance(game)
                path = self.dir / f"{game or 'host'}.mp4"
                capture = subprocess.Popen(
                    ["ffmpeg", "-y", "-hide_banner", "-nostats", "-f", "x11grab", "-framerate", "30", "-video_size", "1280x720",
                     "-i", f"{VIRTUAL_DISPLAY}+4,30", "-c:v", "libx264", "-preset", "ultrafast", "-pix_fmt", "yuv420p", str(path)],
                    stdin=subprocess.PIPE, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE, text=True)
                self.captures[game] = [capture, path, None]
        self.start = time.time()
        for game, ids in self.actors.items():
            select_instance(game)
            if not (rpc({"cmd": "anim_trace", "actors": list(ids), "bools": self.bools}) or {}).get("ok"):
                self.__exit__(None, None, None)  # the captures and any game already recording
                raise TestFailure(f"[{game or 'host'}] the animation trace did not start")
        return self

    def who(self, game, form_id):
        return "game" if form_id == 0 else self.actors.get(game, {}).get(form_id, f"{form_id:X}")

    def _add(self, game, entries):
        for entry in entries:
            entry.update(game=game, who=self.who(game, entry.get("actor", 0)), time=entry["t"] / 1e6)
            self.entries.append(entry)

    def take(self):
        """Pulls what both games recorded so far."""
        for game in self.actors:
            select_instance(game)
            self._add(game, (rpc({"cmd": "anim_trace"}, timeout=10) or {}).get("entries", []))
        self.entries.sort(key=lambda entry: entry["time"])

    def find(self, game, who, event=None, kind="graph", after=0.0):
        """The first entry of that game's actor (by its name here) of that kind (graph event name, or action
        event), after a wall-clock time."""
        return next((e for e in self.entries if e["game"] == game and e["who"] == who and e["kind"] == kind
                     and e["time"] >= after and (event is None or e.get("event") == event)), None)

    def action_before(self, entry):
        """The last action that entry's actor took up to it (what put it in that state)."""
        return next((e for e in reversed(self.entries) if e["game"] == entry["game"] and e["who"] == entry["who"]
                     and e["kind"] == "action" and e["time"] <= entry["time"]), None)

    def wait(self, game, who, event, timeout, kind="graph", after=0.0):
        """Waits for that entry (see find); returns it."""
        def found():
            self.take()
            return self.find(game, who, event, kind, after)
        return wait_for(f"[{game or 'host'}] {who}'s graph sends {event}" if kind == "graph" else
                        f"[{game or 'host'}] {who} {kind} {event}", found, timeout)

    def wait_state(self, game, who, key, value, timeout, after=0.0):
        """Waits for that actor's state (the harness's sample: life, weapon, kill_move, health, graph
        booleans) to read value; returns the entry."""
        def found():
            self.take()
            return next((e for e in self.entries if e["game"] == game and e["who"] == who and e["kind"] == "state"
                         and e["time"] >= after and e["state"].get(key) == value), None)
        return wait_for(f"[{game or 'host'}] {who}'s {key} is {value}", found, timeout)

    def mark(self, label, host=None, client=None):
        """Both screens side by side into strip-<n>.png: each game's at its wall-clock time (now by default;
        the client's at the host's unless given, e.g. at the same moment of a replayed animation)."""
        host = host if host is not None else time.time()
        self.marks.append((label, {"": host, "client": client if client is not None else host}))

    def layout(self, game, who, other, at):
        """Where the other actor is from this one at a wall-clock time: (distance, bearing from its heading,
        heading difference), degrees."""
        def nearest(name):
            return min((e for e in self.entries if e["kind"] == "pos" and e["game"] == game and e["who"] == name),
                       key=lambda e: abs(e["time"] - at))
        mine, theirs = nearest(who), nearest(other)
        dx, dy = theirs["pos"][0] - mine["pos"][0], theirs["pos"][1] - mine["pos"][1]
        turn = lambda degrees: (degrees + 180) % 360 - 180
        return (math.hypot(dx, dy), turn(math.degrees(math.atan2(dx, dy) - mine["yaw"])),
                turn(math.degrees(theirs["yaw"] - mine["yaw"])))

    def __exit__(self, *exc):
        for game in self.actors:
            select_instance(game)
            self._add(game, (rpc({"cmd": "anim_trace", "stop": True}, timeout=10) or {}).get("entries", []))
        self.entries.sort(key=lambda entry: entry["time"])
        self.end = time.time()
        for game, capture in self.captures.items():
            process = capture[0]
            try:
                _, errors = process.communicate("q", timeout=15)
            except subprocess.TimeoutExpired:
                process.kill()
                _, errors = process.communicate()
            # x11grab stamps frames with the wall clock: the first frame's time lines the video up.
            started = re.search(r"start: (\d+\.\d+)", errors or "")
            capture[2] = float(started.group(1)) if started else None
        self._logs()
        (self.dir / "trace.json").write_text(json.dumps({"start": self.start, "actors": {g or "host": {f"{i:X}": n for i, n in a.items()}
                                                                                          for g, a in self.actors.items()},
                                                         "videos": {g or "host": c[2] for g, c in self.captures.items()},
                                                         "entries": self.entries}))
        (self.dir / "timeline.txt").write_text(self.timeline())
        for index, (label, times) in enumerate(self.marks):
            self.strip(self.dir / f"strip-{index}-{re.sub(r'[^a-z0-9]+', '-', label.lower())}.png", times, label)
        log(f"Animation trace: {self.dir}")

    def _logs(self):
        """The plugin logs' lines in the trace's window naming a traced actor (by its hex form id; the
        player's 14 is too common a number to match)."""
        for game, ids in self.actors.items():
            names = {f"{i:X}": n for i, n in ids.items() if i != 0x14}
            if not names:
                continue
            token = re.compile(r"\b(" + "|".join(names) + r")\b")
            try:
                lines = plugin_log(game).read_text(errors="replace").splitlines()[-20000:]
            except OSError:
                continue
            for line in lines:
                match = LOG_LINE.match(line)
                if not match or not (named := token.search(match.group(3))):
                    continue
                at = time.mktime(time.strptime(match.group(1)[:19], "%Y-%m-%d %H:%M:%S")) + int(match.group(1)[20:]) / 1000
                if self.start - 0.1 <= at <= self.end + 0.1:
                    self.entries.append({"game": game, "who": names[named.group(1)], "time": at, "kind": "log",
                                         "level": match.group(2), "text": match.group(3), "f": None})
        self.entries.sort(key=lambda entry: entry["time"])

    def timeline(self, everything=False):
        """One line per entry, host's on the left and the client's on the right, at ms from the start.
        Positions are left out; noise (steps, sounds, the first-person graph) unless everything."""
        width = 70
        lines = [f"{'ms':>8}  {'host':<{width}}  client"]
        for entry in self.entries:
            kind = entry["kind"]
            if kind == "pos" or (not everything and kind == "graph" and (entry.get("project") == "FirstPerson"
                                                                           or TRACE_NOISE.match(entry.get("event", "")))):
                continue
            if kind == "graph":
                text = f"{entry['event']}" + (f" ({entry['payload']})" if entry.get("payload") else "")
            elif kind == "action":
                text = f"ACTION {entry['event']} {entry['action']:X}" + (f" idle {entry['idle']:X}" if entry.get("idle") else "") \
                       + (f" -> {self.who(entry['game'], entry['target'])}" if entry.get("target") else "")
            elif kind in ("state", "game"):
                text = kind.upper() + " " + " ".join(f"{k}={v}" for k, v in entry["state"].items())
            elif kind == "log":
                text = f"LOG {entry['text']}"
            else:
                text = kind.upper() + " " + " ".join(f"{k}={v}" for k, v in entry.items()
                                                     if k not in ("t", "f", "kind", "game", "who", "time", "actor"))
            frame = f"f{entry['f']}" if entry.get("f") is not None else ""
            cell = f"{frame:>6} {entry['who']}: {text}"
            ms = (entry["time"] - self.start) * 1000
            lines.append(f"{ms:8.1f}  {cell:<{width}}" if entry["game"] == "" else f"{ms:8.1f}  {'':<{width}}  {cell}")
        return "\n".join(lines) + "\n"

    def gap(self, first, second):
        """Milliseconds from one entry to another."""
        return (second["time"] - first["time"]) * 1000

    def strip(self, path, times, label=""):
        """Both games' screens side by side, each at its wall-clock time ({game: time})."""
        frames = []
        for game, (_, video, started) in self.captures.items():
            if started is None:
                continue
            frame = self.dir / f".frame-{game or 'host'}.png"
            subprocess.run(["ffmpeg", "-y", "-loglevel", "error", "-ss", f"{max(0.0, times[game] - started):.3f}", "-i", str(video),
                            "-frames:v", "1", "-vf", "scale=640:-1", str(frame)], check=False)
            if frame.exists():
                frames.append(frame)
        if frames:
            subprocess.run(["convert", *map(str, frames), "+append", "-gravity", "north", "-background", "black",
                            "-fill", "white", "-pointsize", "18", "-splice", "0x26",
                            "-annotate", "+0+2", f"{label}  (host +{(times[''] - self.start) * 1000:.0f}ms | "
                                                 f"client +{(times['client'] - self.start) * 1000:.0f}ms)", str(path)], check=False)
            for frame in frames:
                frame.unlink(missing_ok=True)


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
    npc, session_id, _ = spawn_host_npc(LYDIA)
    console_on("", f"{npc:08X}.additem {IRON_SWORD:08X} 1")
    console_on("", f"{npc:08X}.equipitem {IRON_SWORD:08X}")
    give_sword("")
    disconnect_client()
    wait_on("", "the client's character leaves the host's game", lambda actors: other_player(actors) is None)
    # While the client is away.
    draw("")
    console_on("", f"{npc:08X}.damageav health 50")
    rejoin_client()
    party_panel_lists_the_other("after the client joined again")
    copy = copy_of("client", session_id)
    wait_on("client", "the NPC's copy holds the sword", lambda actors: IRON_SWORD in actors.get(str(copy), {}).get("equipped", []))
    copy_health_matches(npc, copy)
    wait_on("client", "the host's character has the sword drawn",
            lambda actors: other_player(actors) is not None and actors[str(other_player(actors))].get("weapon_drawn"))
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
    npc, session_id, copy = spawn_host_npc(LYDIA)
    log(f"host's NPC {npc:08X} (session id {session_id:X}) reaches the client as {copy:08X}")


def scenario_host_kicks_client():
    """The host removes the client from the session: the client is disconnected, its character
    leaves the host's game and the host no longer lists it. The client can join again."""
    players = game_state("").get("players", [])
    if len(players) != 1:
        raise TestFailure(f"[host] expected the client in the session, got {players}")
    player_command("", {"cmd": "kick", "player": players[0]["id"]})
    wait_for("[client] disconnected", lambda: game_state("client").get("online") is False, 10)
    wait_for("[host] no longer lists the client", lambda: not game_state("").get("players"), 10)
    wait_on("", "the client's character leaves the host's game", lambda actors: other_player(actors) is None)
    rejoin_client()


def join_status(instance):
    """The game's last attempt to join: {"state", "reason", "message", "details"}."""
    return game_state(instance).get("join") or {}


def system_chat(instance):
    """The system lines (session events) the game's chat received."""
    return [line["text"] for line in (game_state(instance).get("chat") or {}).get("lines", []) if line["system"]]


def free_port():
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as probe:
        probe.bind(("127.0.0.1", 0))
        return probe.getsockname()[1]


def run_peer(*args):
    if not PEER.exists():
        raise TestFailure(f"{PEER} missing: scripts/xmake-linux.sh build -j16 coop_peer")
    return [str(PEER), *map(str, args)]


class StandInHost:
    """A host on the network that refuses every game asking to join (coop_peer host); its port."""
    def __init__(self, refuse, *options):
        self.args = ("host", "--refuse", refuse, "--version", "v0.0.0-stand-in", "--game", "1.6.640", "--skse", "2.2.3", *options)

    def __enter__(self):
        port = free_port()
        self.process = subprocess.Popen(run_peer(*self.args, "--port", port), stdout=subprocess.PIPE, text=True)
        if self.process.stdout.readline().strip() != f"listening {port}":
            self.process.kill()
            raise TestFailure(f"the stand-in host did not listen on {port}")
        return port

    def __exit__(self, *exc):
        self.process.kill()
        self.process.wait()


def client_joins(port):
    """The client asks the game on port to join (its answer decides what follows)."""
    select_instance("client")
    atomic_write(CONTROL / "session.txt", f"{uuid.uuid4().hex} join {port}\n")


def scenario_join_refused_shows_why():
    """A refused game's join status says why (a reason code and a sentence), and the host's chat says
    who could not join. The client asks stand-in hosts (coop_peer) on another build, on another
    Skyrim and SKSE, and a full one; games on another build and on another Skyrim ask the real host.
    The client then joins the host again."""
    disconnect_client()
    for refuse, reason in (("wrong_version", "wrong_build"), ("wrong_game", "wrong_game"), ("full", "full")):
        with StandInHost(refuse) as port:
            client_joins(port)
            wait_for(f"[client] refused: {reason}",
                     lambda: (status := join_status("client"))["state"] == "refused" and status["reason"] == reason
                     and status["message"], 15)
        if game_state("client").get("online"):
            raise TestFailure("[client] refused, yet online")

    select_instance("")
    port = (read_status() or {}).get("session", {}).get("port")
    host_build = game_state("")["build"]
    for name, options, answer, said in (
            ("Intruder", ("--version", "v0.0.0-stand-in"), "refused wrong_version", "they run SkyrimCoop v0.0.0-stand-in"),
            ("Visitor", ("--version", host_build, "--game", "1.6.640", "--skse", "2.2.3"), "refused wrong_game",
             "they run Skyrim 1.6.640 with SKSE 2.2.3")):
        result = subprocess.run(run_peer("join", "--port", port, "--name", name, *options),
                                capture_output=True, text=True, timeout=20).stdout.strip()
        if result != answer:
            raise TestFailure(f"the host answered {name} with {result!r}, expected {answer!r}")
        wait_for(f"[host] the chat says {name} couldn't join: {said}",
                 lambda: any(line.startswith(f"{name} couldn't join: {said}") for line in system_chat("")), 10)
    if game_state("").get("players"):
        raise TestFailure(f"[host] lists players after a refusal: {game_state('').get('players')}")

    rejoin_client()
    wait_for("[client] joined", lambda: join_status("client")["state"] == "joined", 5)


# A plugin with nothing in it (a TES4 header only): a real difference in a load order, changing nothing.
TEST_PLUGIN = "SkyrimCoopTest.esp"


def empty_plugin():
    import struct
    fields = (b"HEDR" + struct.pack("<HfiI", 12, 1.71, 0, 0x800)
              + b"CNAM" + struct.pack("<H", 11) + b"SkyrimCoop\0")
    return b"TES4" + struct.pack("<IIIHHHH", len(fields), 0, 0, 0, 0, 44, 0) + fields


def load_order_file():
    """The selected game's Plugins.txt (in its own prefix: each test game has its own load order)."""
    return PREFIX / "pfx/drive_c/users/steamuser/AppData/Local/Skyrim Special Edition/Plugins.txt"


def restart_client(plugins, dropped=True):
    """Starts the client game again with `plugins` as its Plugins.txt; it joins nothing yet. A game
    killed in a session says no goodbye: the host must notice it stopped answering (enet's timeout,
    up to 30 s) and drop the player, or the next game on that connection is taken for it."""
    select_instance("client")
    stop_game()
    if dropped:
        wait_for("[host] drops the client whose game ended", lambda: not game_state("").get("players"), 40)
        select_instance("client")
    load_order_file().write_bytes(plugins)
    launch(argparse.Namespace(cell="QASmoke", save=None, boot_timeout=240, world_timeout=180), new_run_dir("restart"))
    run_console("player.setav magicka 5000")  # as `duo`: no "Magicka Low" popup


def scenario_join_refused_plugins_differ():
    """Two real games with different load orders: the client game restarts with one plugin more
    than the host, asks the host to join and is refused with that difference listed; the host's chat
    says why. The client then restarts with the host's load order and joins again."""
    select_instance("client")
    original = load_order_file().read_bytes()
    (GAME / "Data" / TEST_PLUGIN).write_bytes(empty_plugin())  # inactive unless a load order lists it
    try:
        restart_client(original.rstrip(b"\n") + f"\n*{TEST_PLUGIN}\n".encode())
        select_instance("")
        port = (read_status() or {}).get("session", {}).get("port")
        client_joins(port)
        wait_for(f"[client] refused: mods_differ, extra {TEST_PLUGIN}",
                 lambda: (status := join_status("client"))["state"] == "refused" and status["reason"] == "mods_differ"
                 and status["details"] == [f"extra {TEST_PLUGIN}"], 30)
        wait_for("[host] the chat says the client's plugins differ",
                 lambda: any(line.endswith("couldn't join: their plugins differ from yours (1 difference).")
                             for line in system_chat("")), 10)
        if game_state("").get("players"):
            raise TestFailure(f"[host] lists players after a refusal: {game_state('').get('players')}")
    finally:
        # Put the client back whatever failed: its own load order, the plugin gone.
        (GAME / "Data" / TEST_PLUGIN).unlink(missing_ok=True)
        restart_client(original, dropped=False)
    rejoin_client()
    wait_for("[client] joined", lambda: join_status("client")["state"] == "joined", 5)


def difficulty(instance):
    return game_state(instance)["difficulty"]


def set_difficulty(instance, value):
    """Sets the game's difficulty as its settings menu does; the difficulty right after."""
    return player_command(instance, {"cmd": "difficulty", "value": value})["difficulty"]


def scenario_difficulty_follows_host():
    """The session's difficulty is the host's: a joining game takes it, follows the host's changes
    and cannot change away from it; the host's own is never changed; leaving gives the client's game
    its own difficulty back."""
    start = difficulty("")
    own = (start + 1) % 6    # the client's own, before it joins
    other = (start + 3) % 6  # what the host changes to
    disconnect_client()
    set_difficulty("client", own)
    try:
        rejoin_client()
        wait_for(f"[client] takes the host's difficulty {start}", lambda: difficulty("client") == start, 5)
        set_difficulty("", other)
        wait_for(f"[client] follows the host to {other}", lambda: difficulty("client") == other, 5)
        if difficulty("") != other:
            raise TestFailure(f"[host] difficulty {difficulty('')}, set to {other}: the session changed it")
        if set_difficulty("client", start) != start:
            raise TestFailure("[client] the difficulty could not be set")
        wait_for(f"[client] is held at the host's {other}", lambda: difficulty("client") == other, 5)
        disconnect_client()
        wait_for(f"[client] has its own difficulty {own} back", lambda: difficulty("client") == own, 5)
    finally:
        set_difficulty("", start)
        if game_state("client").get("online") is False:
            rejoin_client()
        wait_for(f"[client] back at the host's {start}", lambda: difficulty("client") == start, 5)

# --- World state: the host's globals and quests on every game (world-state-sync.md, step 4) ----

# FriendsCountRift: Riften's favor count, a plain global a quest fragment raises (MQ202's walk).
FRIENDS_COUNT_RIFT = 0x87E1C


def global_value(instance, global_id):
    select_instance(instance)
    response = rpc({"cmd": "global", "global": global_id}) or {}
    if not response.get("ok"):
        raise TestFailure(f"[{instance or 'host'}] global {global_id:08X}: {response.get('error', 'no answer')}")
    return response["value"]


def scenario_global_reaches_guest():
    """A global the host's game changes (the console's `set`, as a fragment's SetValue does) takes the
    host's value on the client; the client's own change does not reach the host."""
    start = global_value("", FRIENDS_COUNT_RIFT)
    try:
        console_on("", f"set FriendsCountRift to {start + 3:g}")
        wait_for(f"[host] FriendsCountRift is {start + 3:g}", lambda: global_value("", FRIENDS_COUNT_RIFT) == start + 3, 5)
        wait_for(f"[client] FriendsCountRift follows the host to {start + 3:g}",
                 lambda: global_value("client", FRIENDS_COUNT_RIFT) == start + 3, 5)
        console_on("client", f"set FriendsCountRift to {start + 7:g}")
        wait_for("[client] FriendsCountRift is its own 7 more", lambda: global_value("client", FRIENDS_COUNT_RIFT) == start + 7, 5)
        if global_value("", FRIENDS_COUNT_RIFT) != start + 3:
            raise TestFailure("[host] FriendsCountRift took the client's value: a guest decided world state")
    finally:
        console_on("", f"set FriendsCountRift to {start:g}")
        wait_for(f"[client] FriendsCountRift back at the host's {start:g}",
                 lambda: global_value("client", FRIENDS_COUNT_RIFT) == start, 5)


def scenario_late_join_gets_globals():
    """A client that joins after the host changed a global starts with the host's value."""
    start = global_value("", FRIENDS_COUNT_RIFT)
    disconnect_client()
    try:
        console_on("", f"set FriendsCountRift to {start + 5:g}")
        wait_for(f"[host] FriendsCountRift is {start + 5:g}", lambda: global_value("", FRIENDS_COUNT_RIFT) == start + 5, 5)
        if global_value("client", FRIENDS_COUNT_RIFT) == start + 5:
            raise TestFailure("[client] has the host's value while away: the test shows nothing")
        rejoin_client()
        wait_for(f"[client] joins with the host's FriendsCountRift {start + 5:g}",
                 lambda: global_value("client", FRIENDS_COUNT_RIFT) == start + 5, 5)
    finally:
        if game_state("client").get("online") is False:
            rejoin_client()
        console_on("", f"set FriendsCountRift to {start:g}")
        wait_for(f"[client] FriendsCountRift back at the host's {start:g}",
                 lambda: global_value("client", FRIENDS_COUNT_RIFT) == start, 5)

# Finn's Lute (BardsCollegeLute): runs from the start; stages 0, 15, 20, 40, 200 with fragments, objective 20
# shown at 20, 200 completes it. CR03, a Companions radiant job: its start picks the quest giver among them.
LUTE_QUEST, CR03_QUEST = 0xD93FA, 0x25230
SYNCED_QUEST_FLAGS = 0xC3  # running, starting, completed, failed (the HUD's tracking is each player's own)


def synced_quest(instance, quest):
    """What quest sync carries of a quest on that game: flags, stage, done stages, objectives, reference fills by id
    (a created reference by its base: each game has its own id for it) and location fills."""
    state = quest_snapshot(instance, quest)
    def fill(alias, ref):
        return ("created", state["bases"].get(alias)) if ref >= CREATED_ID else ref
    return {"flags": state["flags"] & SYNCED_QUEST_FLAGS, "stage": state["stage"], "done": state["done"],
            "objectives": state["objectives"], "locations": state["locations"],
            "aliases": {alias: fill(alias, ref) for alias, (ref, _) in state["aliases"].items()}}


def quest_fragments(instance, quest):
    select_instance(instance)
    return (rpc({"cmd": "quest_fragments", "quest": quest}) or {}).get("runs")


def reset_quest_fragments():
    for instance in ("", "client"):
        select_instance(instance)
        rpc({"cmd": "quest_fragments_reset"})


def client_quest_matches(quest, label):
    def matches():
        return synced_quest("client", quest) == synced_quest("", quest)
    wait_for(f"[client] {label} as on the host", matches, 5)


BANNERED_MARE = "WhiterunBanneredMare"


def arrival_story_events(instance):
    """The location change story events this game's player raised since the last call (harness story_trace),
    as (old location, new location, event id)."""
    select_instance(instance)
    return (rpc({"cmd": "story_trace"}) or {}).get("captured", [])


def story_event_outcome(instance, event_id, since):
    """Whether this game's story manager started a quest for that event; None until it has handled it."""
    for event in engine_events(instance, since)[0]:
        if event["kind"] == "story_event_processed" and event["event"] == event_id:
            return event["started_quest"]
    return None


def scenario_guest_story_starts_no_quest():
    """Both players walk into the Bannered Mare: each game raises its own arrival story event (a location
    change). The host's story manager starts quests for it (the bard's song, the hold's radiant quests); the
    client's decides nothing, its event handled with no quest started: quests come from the host."""
    since = {}
    for name in ("", "client"):
        player_command(name, {"cmd": "story_trace", "on": True})
        arrival_story_events(name)
        since[name] = engine_events(name)[1]
    try:
        both_go(BANNERED_MARE)
        for name, wanted in (("", True), ("client", False)):
            who = f"[{name or 'host'}]"
            events = arrival_story_events(name)
            if not events:
                raise TestFailure(f"{who} raised no location change story event on arriving")
            event_id = events[-1][2]
            wait_for(f"{who} its story manager handled the arrival (event {event_id})",
                     lambda name=name, event_id=event_id: story_event_outcome(name, event_id, since[name]) is not None, 15)
            if story_event_outcome(name, event_id, since[name]) != wanted:
                raise TestFailure(f"{who} its story manager {'started no quest' if wanted else 'started quests'} for the "
                                  f"arrival: {'the test shows nothing' if wanted else 'a guest decided quests'}")
    finally:
        for name in ("", "client"):
            player_command(name, {"cmd": "story_trace", "on": False})
        both_go(HOME_CELL)


# Scripts the host's game runs alone: vanilla ones a guest used to run too (docs/research/guest-scripts-and-mods.md).
CRITTER_SPAWNER = 0x6B45C  # critterSpawnDragonflies24x7: CritterSpawn's OnLoad places 2 dragonflies (05:00-20:00)
VALDR, VALDR_QUEST = 0x411BA, 0x376EE  # his base's defaultSetStageOnDeath: OnDeath sets the quest's stage 105
STAGE_ON_DEATH = "defaultSetStageOnDeath"


def created_by_base(instance):
    """The references that game created, counted by base."""
    return collections.Counter(r["base"] for i, r in world_state(instance).items() if i >= CREATED_ID)


def scenario_guest_spawns_no_critters():
    """The host places a critter spawner: its script (CritterSpawn, on loading) places the dragonflies on the
    host's game, and the client shows those. The client's own spawner (made as the host's created object) runs
    no world script, so it places none of its own."""
    console_on("", "set gamehour to 12")
    before = {name: created_by_base(name) for name in ("", "client")}
    before_ids = {i for i in world_state("") if i >= CREATED_ID}
    console_on("", f"player.placeatme {CRITTER_SPAWNER:X} 1")
    try:
        def critters(name):
            return {base: n - before[name].get(base, 0) for base, n in created_by_base(name).items()
                    if base != CRITTER_SPAWNER and n > before[name].get(base, 0)}
        host = wait_for("[host] its spawner places the dragonflies", lambda: critters("") or None, 20)
        wait_for("[client] shows the host's dragonflies", lambda: all(critters("client").get(b, 0) >= n for b, n in host.items()), 10)
        stable_until = time.monotonic() + 4.0
        wait_for("[client] places none of its own (as many as the host's for 4 s)",
                 lambda: critters("client") == critters("") and time.monotonic() >= stable_until, 10)
    finally:
        # The spawner first (it places more while it lives), then what it placed; the client's twins go with the
        # host's (its own, if it placed any, stay: the world check names them).
        def placed():  # made since the scenario began, alive: not disabled (0x800) or deleted (0x20)
            return [(ref, r) for ref, r in world_state("").items()
                    if ref >= CREATED_ID and ref not in before_ids and not r.get("flags", 0) & 0x820]
        for ref, _ in sorted(placed(), key=lambda item: item[1]["base"] != CRITTER_SPAWNER):
            console_on("", f"{ref:X}.disable")
            console_on("", f"{ref:X}.markfordelete")
        wait_for("[host] nothing the spawner placed is left", lambda: not placed(), 10)


def steam_status(instance):
    """The game's Steam account and session (harness status `steam`)."""
    select_instance(instance)
    return (read_status() or {}).get("steam") or {}


def client_steam_opens(url):
    """The client player's Steam opens a steam:// link, as clicking Join on an invite does: with the game running,
    Steam hands it the lobby (GameLobbyJoinRequested_t)."""
    select_instance("client")
    user = instance_user()
    if not user:
        raise TestFailure("the client game shares the host's Steam account: no second account (COOP_CLIENT_USER)")
    result = sudo("ip", "netns", "exec", CLIENT_NETNS, "sudo", "-u", user, "-H", "env", f"DISPLAY={game_env()['DISPLAY']}",
                  f"DBUS_SESSION_BUS_ADDRESS=unix:path={user_bus(user)}", "/usr/games/steam", url)
    log(f"[client] Steam opened {url}" + (f" ({result.stderr.strip()[-200:]})" if result.returncode else ""))


def steam_accounts():
    """(host's SteamID64, client's): two accounts, or the scenario cannot run."""
    host_id, guest_id = steam_status("").get("id"), steam_status("client").get("id")
    if not host_id or not guest_id:
        raise TestFailure(f"a game reports no Steam account (host {host_id}, client {guest_id})")
    if host_id == guest_id:
        raise TestFailure(f"both games signed into the same Steam account {host_id}: the client needs its own "
                          "(docs/design/testing.md, \"Two Steam accounts\")")
    return host_id, guest_id


def client_accepts_steam_invite():
    """The host's game makes its lobby and invites the client's account; the client's player accepts; returns once
    the client's game is connected to the host over Steam."""
    host_id, guest_id = steam_accounts()
    select_instance("")
    if not (rpc({"cmd": "steam_host"}) or {}).get("ok"):
        raise TestFailure(f"[host] could not host on Steam: {steam_status('')}")
    wait_for("[host] its lobby is made and it listens",
             lambda: steam_status("").get("session", {}).get("state") == "hosting", 30)
    lobby = steam_status("")["session"]["lobby"]
    select_instance("")
    if not (rpc({"cmd": "steam_invite", "friend": guest_id}) or {}).get("ok"):
        raise TestFailure("[host] Steam refused the invite")
    client_steam_opens(f"steam://joinlobby/489830/{lobby}/{host_id}")
    session = lambda: steam_status("client").get("session", {})
    try:
        wait_for("[client] joins the lobby and connects to the host", lambda: session().get("state") == "connected", 60)
    except TestFailure:
        raise TestFailure(f"[client] Steam session {session().get('state')}: {session().get('error')}")
    guest_name = steam_status("client").get("name")
    wait_for(f"[host] has {guest_name}'s hello",
             lambda: any(p.get("name") == guest_name for p in steam_status("").get("session", {}).get("peers", [])), 10)
    for name, game in (("host", ""), ("client", "client")):
        log(f"[{name}] Steam peers: {steam_status(game)['session']['peers']}")


def steam_sessions_end():
    for game in ("client", ""):
        select_instance(game)
        rpc({"cmd": "steam_leave"})


def scenario_guest_joins_by_invite():
    """A friend joins the host's session from a Steam invite, no address or port (two accounts: docs/design/testing.md,
    "Two Steam accounts"): the client leaves its direct session; the host's game makes a lobby and invites the
    client's account; the client's player accepts (its Steam opens the invite's join link); its game joins the lobby,
    connects to the host through Valve's network, and joins the session through that connection (enet tunnelled over
    it). Each game shows the other player and the party panels list each other; leaving the Steam session
    drops the client's character from the host's game. Ends joined directly again."""
    disconnect_client()
    wait_on("", "the client's character leaves the host's game", lambda actors: other_player(actors) is None)
    try:
        client_accepts_steam_invite()
        wait_for("[client] is in the host's session, through Steam", lambda: game_state("client").get("online") is True, 30)
        wait_for("Each game shows the other player, joined through Steam", both_see_each_other, 30)
        party_panel_lists_the_other("joined through Steam")
        select_instance("client")
        rpc({"cmd": "steam_leave"})
        wait_on("", "the client's character leaves the host's game with the Steam session",
                lambda actors: other_player(actors) is None)
    finally:
        steam_sessions_end()
        if game_state("client").get("online") is not True:
            rejoin_client()


def scenario_loaded_save_hosts_then_joins_friend():
    """A player loads a save and is hosting on Steam by itself (TESLoadGameEvent; its chat says so); a friend's invite
    then reaches it, and its game, hosting alone, steps down and joins the friend's session through Steam. Each chat
    says who joined. Ends joined directly again."""
    host_id, _ = steam_accounts()
    host_name, guest_name = steam_status("").get("name"), steam_status("client").get("name")
    disconnect_client()
    wait_on("", "the client's character leaves the host's game", lambda actors: other_player(actors) is None)
    try:
        console_on("client", "save SkyrimCoopSteamHost")
        console_on("client", "load SkyrimCoopSteamHost")
        wait_for("[client] the save loads and its game hosts on Steam",
                 lambda: steam_status("client").get("session", {}).get("state") == "hosting"
                 and game_state("client").get("hosting") is True, 90)
        wait_for("[client] its chat says it hosts",
                 lambda: "Hosting: friends can join from Steam (Shift+Tab, Invite to Game)." in system_chat("client"), 10)
        client_accepts_steam_invite()
        wait_for("[client] stops hosting and is in the host's session, through Steam",
                 lambda: game_state("client").get("online") is True and game_state("client").get("hosting") is not True, 30)
        wait_for("Each game shows the other player, joined through Steam", both_see_each_other, 30)
        wait_for(f"[client] its chat says it joined {host_name}'s game",
                 lambda: f"Joined {host_name}'s game." in system_chat("client"), 10)
        wait_for(f"[host] its chat says {guest_name} joined", lambda: f"{guest_name} joined." in system_chat(""), 10)
    finally:
        steam_sessions_end()
        player_command("client", {"cmd": "disconnect"})
        if game_state("client").get("online") is not True:
            rejoin_client()


def scenario_wildlife_takes_hosts_pick():
    """Riverwood's placed leveled wildlife (a Rabbit reference can roll a Fox) rolls on each game: the client's
    copy takes the host's pick (its base swapped, CharacterService::ApplyPlacedBase) and stays enabled and loaded."""
    both_go(OUTDOOR_CELL)
    try:
        def leveled():
            select_instance("")
            return {s["mod"]["session_id"]: s["identity"] for s in (read_status() or {}).get("world", {}).values()
                    if s["role"] == "own" and not s["player"] and s["identity"].get("temporary_base")}
        host = wait_for("[host] has placed leveled wildlife around", lambda: leveled() or None, 20)

        def wrong():
            select_instance("client")
            copies = {s["mod"]["session_id"]: s["identity"] for s in (read_status() or {}).get("world", {}).values()
                      if s["role"] == "copy"}
            return [f"{who['name']} sid {sid}: " + (f"client's is a {copies[sid]['name']} (race {copies[sid]['race']:X})"
                                                    if sid in copies else "no loaded copy on the client")
                    for sid, who in host.items()
                    if sid not in copies or (copies[sid]["name"], copies[sid]["race"]) != (who["name"], who["race"])]
        try:
            wait_for(f"[client] {len(host)} loaded copies, each the host's pick", lambda: not wrong(), 20)
        except TestFailure:
            raise TestFailure("[client] copies of the host's leveled wildlife: " + "; ".join(wrong()))
    finally:
        both_go(HOME_CELL)


def stage_script_state(instance, actor):
    select_instance(instance)
    return ((rpc({"cmd": "script_state", "object": actor, "script": STAGE_ON_DEATH}) or {}).get("state") or "").lower()


def scenario_death_stage_runs_once():
    """The host kills an NPC whose script sets a quest stage on its death (Valdr: defaultSetStageOnDeath, which
    then goes to its "inactive" state). The script runs on the host's game; the client's copy dies too, but its
    script stays waiting: a stage it set would run the quest's fragment a second time."""
    reset_quest_fragments()
    npc, _, copy = spawn_host_npc(f"{VALDR:08X}", restrained=True)
    try:
        for name, actor in (("", npc), ("client", copy)):
            if stage_script_state(name, actor) != "waiting":
                raise TestFailure(f"[{name or 'host'}] {STAGE_ON_DEATH} on {actor:X} is "
                                  f"'{stage_script_state(name, actor)}', not waiting")
        console_on("", f"{npc:08X}.kill")
        wait_on("client", f"NPC copy {copy:08X} dies", lambda actors: actors.get(str(copy), {}).get("dead"))
        wait_for("[host] its death script ran", lambda: stage_script_state("", npc) == "inactive", 10)
        waiting_until = time.monotonic() + 3.0
        wait_for("[client] its copy's death script stays waiting (3 s)",
                 lambda: stage_script_state("client", copy) != "waiting" or time.monotonic() >= waiting_until, 10)
        if (state := stage_script_state("client", copy)) != "waiting":
            raise TestFailure(f"[client] its copy's {STAGE_ON_DEATH} ran ('{state}'): the client's own script set the stage")
        if runs := quest_fragments("client", VALDR_QUEST):
            raise TestFailure(f"[client] ran {runs} of the quest's fragments: only the host's game runs them")
    finally:
        console_on("", f"resetquest {VALDR_QUEST:X}")


def scenario_quest_stage_reaches_guest():
    """A stage the host's game sets is done on the client too, with its objective, through the client's
    own SetStage; the stage's fragment (a world script) runs on the host only."""
    console_on("", "resetquest BardsCollegeLute")
    client_quest_matches(LUTE_QUEST, "Finn's Lute is reset")
    reset_quest_fragments()
    try:
        for stage in (15, 20):
            console_on("", f"setstage BardsCollegeLute {stage}")
            wait_for(f"[host] stage {stage} is done", lambda: stage in quest_snapshot("", LUTE_QUEST)["done"], 5)
            client_quest_matches(LUTE_QUEST, f"stage {stage} is done")
        if synced_quest("client", LUTE_QUEST)["objectives"].get(20) != 1:
            raise TestFailure("[client] objective 20 is not shown")
        if not quest_fragments("", LUTE_QUEST):
            raise TestFailure("[host] ran no fragment for its stages: the test shows nothing")
        if quest_fragments("client", LUTE_QUEST):
            raise TestFailure(f"[client] ran {quest_fragments('client', LUTE_QUEST)} of the stages' fragments")
    finally:
        console_on("", "resetquest BardsCollegeLute")
        client_quest_matches(LUTE_QUEST, "Finn's Lute is reset again")


def scenario_guest_script_stage_reaches_host():
    """A script on the client's own game (an item's, as the Elder Scroll's MQ206.SetStage(20)) sets a quest's stage:
    the host's game sets it, for the client (PlayerNatives::SendToHost), and runs its fragment; the client takes the
    stage from the host, its fragment skipped."""
    console_on("", "resetquest BardsCollegeLute")
    client_quest_matches(LUTE_QUEST, "Finn's Lute is reset")
    reset_quest_fragments()
    try:
        script_call("client", "Quest", "SetCurrentStageID", [15], on=LUTE_QUEST)
        wait_for("[host] stage 15 is done", lambda: 15 in quest_snapshot("", LUTE_QUEST)["done"], 5)
        client_quest_matches(LUTE_QUEST, "stage 15 is done")
        if not quest_fragments("", LUTE_QUEST):
            raise TestFailure("[host] ran no fragment for the client's stage")
        if quest_fragments("client", LUTE_QUEST):
            raise TestFailure(f"[client] ran {quest_fragments('client', LUTE_QUEST)} of the stage's fragments")
        console_on("", "resetquest BardsCollegeLute")
        client_quest_matches(LUTE_QUEST, "Finn's Lute is reset again")
    finally:
        for name in ("", "client"):
            console_on(name, "resetquest BardsCollegeLute")


def scenario_quest_completes_for_guest():
    """The host's game completes a quest: completed on the client, its objectives as the host's."""
    console_on("", "resetquest BardsCollegeLute")
    client_quest_matches(LUTE_QUEST, "Finn's Lute is reset")
    try:
        console_on("", "setstage BardsCollegeLute 200")
        wait_for("[host] Finn's Lute is completed", lambda: quest_snapshot("", LUTE_QUEST)["flags"] & 0x2, 5)
        client_quest_matches(LUTE_QUEST, "Finn's Lute is completed")
    finally:
        console_on("", "resetquest BardsCollegeLute")
        client_quest_matches(LUTE_QUEST, "Finn's Lute is reset again")


def scenario_quest_roles_match_host():
    """A radiant quest the host's game starts runs on the client with the host's picks for its roles
    (who gives the job), not rolled by the client, and the client runs none of its start's fragments."""
    console_on("", "stopquest CR03")
    client_quest_matches(CR03_QUEST, "CR03 is stopped")
    reset_quest_fragments()
    try:
        console_on("", "startquest CR03")
        wait_for("[host] CR03 runs with a quest giver",
                 lambda: synced_quest("", CR03_QUEST)["flags"] & 1 and synced_quest("", CR03_QUEST)["aliases"], 5)
        client_quest_matches(CR03_QUEST, "CR03 runs with the host's roles")
        if quest_fragments("client", CR03_QUEST):
            raise TestFailure("[client] ran CR03's start fragments itself")
    finally:
        console_on("", "stopquest CR03")
        client_quest_matches(CR03_QUEST, "CR03 is stopped again")


# Temporary references in a cell no scenario loads: one a game read once stays in its memory (kept for the quest).
EMBERSHARD_MINE, EMBERSHARD_REFS = 0xB6BE6, range(0x10A7C2, 0x10A7CD)
CR03_SPAWN_ALIAS = 5  # SamplePeltSpawnPoint


def in_memory(instance, form_id):
    select_instance(instance)
    return bool((rpc({"cmd": "form", "form": form_id}) or {}).get("ok"))


def scenario_quest_fill_loads_on_guest():
    """The host's quest fills a role with a reference in a cell neither game loaded (as a radiant quest fills its
    target): the host's game reads it from the plugin, and so does the client's, the quest's role filled the same."""
    console_on("", "stopquest CR03")
    client_quest_matches(CR03_QUEST, "CR03 is stopped")
    try:
        console_on("", "startquest CR03")
        client_quest_matches(CR03_QUEST, "CR03 runs with the host's roles")
        ref = next((r for r in EMBERSHARD_REFS if not in_memory("client", r)), None)
        if ref is None:
            raise TestFailure("[client] every Embershard Mine reference is in memory already: restart the games")
        select_instance("")
        response = rpc({"cmd": "quest_force_alias", "quest": CR03_QUEST, "alias": CR03_SPAWN_ALIAS,
                        "ref": ref, "parent": EMBERSHARD_MINE}) or {}
        if not response.get("ok"):
            raise TestFailure(f"[host] cannot fill CR03's alias {CR03_SPAWN_ALIAS}: {response.get('error')}")
        wait_for(f"[client] {ref:X} loaded and in the role",
                 lambda: quest_snapshot("client", CR03_QUEST)["aliases"].get(CR03_SPAWN_ALIAS, (0,))[0] == ref, 10)
        client_quest_matches(CR03_QUEST, "CR03's roles are the host's")
    finally:
        console_on("", "stopquest CR03")


def scenario_late_join_gets_quests():
    """A client that joins after the host's game moved a quest on starts with the host's quest, taken as a
    load takes it: no fragment runs on the client."""
    console_on("", "resetquest BardsCollegeLute")
    client_quest_matches(LUTE_QUEST, "Finn's Lute is reset")
    disconnect_client()
    try:
        console_on("", "setstage BardsCollegeLute 20")
        wait_for("[host] stage 20 is done", lambda: 20 in quest_snapshot("", LUTE_QUEST)["done"], 5)
        if 20 in quest_snapshot("client", LUTE_QUEST)["done"]:
            raise TestFailure("[client] has stage 20 while away: the test shows nothing")
        reset_quest_fragments()
        rejoin_client()
        client_quest_matches(LUTE_QUEST, "joins with stage 20 done")
        if quest_fragments("client", LUTE_QUEST):
            raise TestFailure("[client] ran the quest's fragments taking the host's quest at join")
    finally:
        if game_state("client").get("online") is False:
            rejoin_client()
        console_on("", "resetquest BardsCollegeLute")
        client_quest_matches(LUTE_QUEST, "Finn's Lute is reset again")

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
    if client_on_host := other_player(host_actors):
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


def scenario_copy_jumps_with_owner():
    """The host puts its NPC 900 units away at once (the console's setpos, as a script's MoveTo does): the client's
    copy jumps there too, never sliding across in between (Movement::Teleported)."""
    npc, _, copy = spawn_host_npc(LYDIA, restrained=True)
    show("client", lambda actors: copy)
    start = host_npc_state(npc)["position"]
    def flat(a, b):
        return math.hypot(a[0] - b[0], a[1] - b[1])
    wait_on("client", f"copy {copy:08X} at the host's NPC", lambda actors: (p := actors.get(str(copy), {}).get("actual")) and flat(p, start) < 30, 10)
    goal = [start[0] + 900, start[1], start[2]]
    console_on("", f"{npc:08X}.setpos x {goal[0]:.0f}")
    seen = []
    def arrived(actors):
        if position := actors.get(str(copy), {}).get("actual"):
            seen.append(position)
        return position and flat(position, goal) < 30
    wait_on("client", f"copy {copy:08X} at the NPC's new place", arrived, 5)
    if between := [[round(v) for v in p] for p in seen if flat(p, start) > 30 and flat(p, goal) > 30]:
        raise TestFailure(f"[client] the copy slid across: {len(between)} of {len(seen)} reads between, {between[:3]}")
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
    Character::UpdateVoiceTimer), which copies run from HookUpdateAI. A copy holds a package, its owner's
    (AiStateService) or the engine's do-nothing one (HookCheckForNewPackage): an actor without one gets no
    UpdateAI once its 3D is far enough from the camera to fade (Character::ShouldOnlyCheckPackage)."""
    npc, _, copy = spawn_host_npc(LYDIA, restrained=True)
    try:
        wait_for(f"[client] copy {copy:08X} holds its owner's package or the do-nothing one",
                 lambda: package_type("client", copy) == DO_NOTHING_PACKAGE
                 or (package_type("client", copy) not in (None, -1)
                     and ai_state("client", copy).get("package") == ai_state("", npc).get("package")), 5)
        place_before("", npc, 100)
        activate_on("", npc)
        wait_for("[host] NPC's line plays", lambda: line_playing("", npc), 10)
        wait_for(f"[client] copy {copy:08X}'s line plays", lambda: line_playing("client", copy), 5)
        text = wait_for("[host] NPC's subtitle shows", lambda: subtitle("", npc), 5)
        wait_for(f"[client] copy {copy:08X} shows the same subtitle", lambda: subtitle("client", copy) == text, 5)
        wait_for("[host] NPC's line ends", lambda: line_over("", npc), 30)
        wait_for(f"[client] copy {copy:08X}'s line ends", lambda: line_over("client", copy), 5)
    finally:
        close_dialogues()
    hold()


TAB_KEY = 0x0F  # leaves the dialogue menu
ACTIVATE_KEY = 0x12  # E: says the selected option (the menu's Accept; Enter does not)
UP_KEY, DOWN_KEY = 0x11, 0x1F  # W, S: move the highlight (the arrows' scan codes do not, sent by the harness)
HULDA = "00013BA3"  # the Bannered Mare's innkeeper: three options (a room, rumors, magic)
BRENUIN = "00013BA7"  # Whiterun's beggar: "Here. Have a gold piece." only for a player carrying gold
GOLD = 0xF
GIVE_COIN_TOPIC = 0x6001F  # FavorJobsBeggarsGiveMoneyTopic
GIVE_COIN_INFO = 0x60022   # its line: the beggar thanks the player
RENT_ROOM_TOPIC = 0x9CC92  # its info: the player pays 10 gold for the room, or, without it, cannot afford it
PLAYER_BASE = 0x7  # the player's base (TESNPC): an owner meaning each game's own player
RENT_ROOM_NO_GOLD_INFO = 0x9CC95  # Hulda's line to a player without the gold


def dialogue(instance):
    """That game's conversation: {"menu": the Dialogue Menu shows, "speaker", "options": [...]}."""
    return player_command(instance, {"cmd": "dialogue"})


def talking_to(instance, speaker):
    """That game's dialogue menu shows, with its player talking to `speaker`."""
    state = dialogue(instance)
    return state.get("menu") and state.get("speaker") == speaker


def menu_closed(instance):
    return not dialogue(instance).get("menu")


def list_shown(instance):
    """That game's dialogue menu shows its list: the arrow keys move the highlight (eMenuState TOPIC_LIST_SHOWN)."""
    return (dialogue(instance).get("swf") or {}).get("state") == 1


def cursor(instance):
    """The topic highlighted in that game's dialogue menu (0: none)."""
    return (dialogue(instance).get("swf") or {}).get("cursor") or 0


def lists_topic(instance, topic):
    """That game's conversation lists the topic (its own option, or another player's added to it)."""
    return topic in [o["topic"] for o in dialogue(instance).get("options", [])]


def topics_shown(instance):
    """The topics that game's conversation lists, in order."""
    return [o["topic"] for o in dialogue(instance).get("options", [])]


def options_shown(instance):
    """That game's dialogue list as (topic, line, text), in menu order."""
    return [(o["topic"], o["info"], o["text"]) for o in dialogue(instance).get("options", [])]


def client_shows_hosts_list(what, timeout=10):
    """The client's menu shows exactly the host's list: its topics, lines and texts, in its order (every game's menu
    shows the host's game's list; docs/design/shared-dialogue.md). Returns it."""
    def same():
        host = options_shown("")
        return host and options_shown("client") == host and host
    try:
        return wait_for(f"[client] the menu shows the host's list {what}", same, timeout)
    except TestFailure:
        raise TestFailure(f"[client] the menu does not show the host's list {what}: client {options_shown('client')}, "
                          f"host {options_shown('')}")


def info_shown(instance, topic):
    """The line that game's conversation has for the topic (0: not listed)."""
    return next((o["info"] for o in dialogue(instance).get("options", []) if o["topic"] == topic), 0)


def menu_takes_pick(instance):
    """That game's dialogue menu shows its list and takes a pick: the SWF's bAllowProgress, set 750 ms after the
    line's voice is ready, and its list shown (DialogueMenu.as eMenuState TOPIC_LIST_SHOWN)."""
    swf = dialogue(instance).get("swf") or {}
    return swf.get("allow_progress") == 1 and swf.get("state") == 1


def close_dialogues():
    """Every open conversation ends as its players leave it (Tab): a menu hidden from outside skips the SWF's close,
    which gives the HUD its subtitles back (DialogueMenu 51511). The last one out ends the host's, which may run
    without its menu."""
    for instance in ("client", ""):
        if dialogue(instance).get("menu"):
            tap_key(instance, TAB_KEY)
            wait_for(f"[{instance or 'host'}] the menu closes", lambda instance=instance: menu_closed(instance), 10)
    wait_for("[host] its conversation ends", lambda: not dialogue("").get("speaker"), 10)


def scenario_dialogue_peer_starts():
    """The client talks to its copy of the host's NPC: the host's player is the one talking (its game runs the
    conversation with the real NPC, without its menu: the host plays on), and the client's menu opens on the copy
    with the host's list: its topics, lines and texts, in its order (docs/design/shared-dialogue.md)."""
    npc, _, copy = spawn_host_npc(LYDIA, restrained=True)
    try:
        place_before("", npc, 110)
        place_before("client", copy, 110)
        look_apart()
        activate_on("client", copy)
        wait_for(f"[host] the player talks to {npc:08X} in the background", lambda: in_conversation("", npc), 10)
        wait_for(f"[client] the menu opens on copy {copy:08X}", lambda: talking_to("client", copy), 10)
        wait_for("[host] the conversation lists options", lambda: topics_shown(""), 5)
        client_shows_hosts_list("at the open")
        if dialogue("").get("menu"):
            raise TestFailure("[host] the dialogue menu opened for a conversation it did not start")
        hold()
    finally:
        close_dialogues()
        remove_host_npc(npc)


def scenario_dialogue_last_one_out_ends_it():
    """A player leaving (Tab) leaves the conversation on for the others; the last one out ends it. The host talks
    and the client joins by talking to its copy; the client leaves, the host's stays, and the host's Tab ends it.
    Then the other way round: the host leaves first and its conversation goes on without its menu, until the
    client leaves too."""
    npc, _, copy = spawn_host_npc(LYDIA, restrained=True)
    try:
        place_before("", npc, 110)
        place_before("client", copy, 110)
        for first, last in (("client", ""), ("", "client")):
            activate_on("", npc)
            wait_for(f"[host] the player talks to {npc:08X}", lambda: talking_to("", npc), 10)
            if dialogue("client").get("menu"):
                raise TestFailure("[client] the host's conversation opened its menu: joining is by talking")
            activate_on("client", copy)
            wait_for(f"[client] talking to copy {copy:08X} joins", lambda: talking_to("client", copy), 10)
            tap_key(first, TAB_KEY)
            wait_for(f"[{first or 'host'}] Tab closes its menu", lambda: menu_closed(first), 10)
            frame = game_frame("")
            def stays():
                if not talking_to(last, copy if last else npc) or not in_conversation("", npc):
                    raise TestFailure(f"[{first or 'host'}] leaving ended the conversation for the other player")
                return game_frame("") > frame + 120
            wait_for(f"[{last or 'host'}] the conversation goes on", stays, 10)
            tap_key(last, TAB_KEY)
            wait_for(f"[{last or 'host'}] Tab closes its menu", lambda: menu_closed(last), 10)
            wait_for("[host] the last one out ends the conversation", lambda: not dialogue("").get("speaker"), 10)
            hold()
    finally:
        close_dialogues()
        remove_host_npc(npc)


def on_other_player(instance, within=30):
    """That game's player stands within `within` units of its copy of the other player."""
    select_instance(instance)
    status = read_status() or {}
    actors = status.get("actors", {})
    other = other_player(actors)
    me = (status.get("player") or {}).get("position")
    return other is not None and me and distance(me[:2], actors[str(other)]["actual"][:2]) < within


def look_apart():
    """Each player looks its own way, as players do before anything brings them together: the host up and
    north, the client down and south."""
    console_on("", "player.setangle x -30")
    console_on("", "player.setangle z 10")
    console_on("client", "player.setangle x 40")
    console_on("client", "player.setangle z 200")


def camera_view(instance):
    """That game's camera: its mode, yaw, pitch and position."""
    v = player_command(instance, {"cmd": "camera", "target": 0})
    return v["state"], v["yaw"], v["pitch"], v["position"]


def same_view(what, degrees=2, units=15):
    """Both games show the same view: the camera in the same mode, at the same spot, turned the same way. The
    spot within 15 units: a follower goes where the leader arrived, and the leader settles a few units after."""
    seen = {}
    def check():
        (state_a, yaw_a, pitch_a, at_a), (state_b, yaw_b, pitch_b, at_b) = camera_view(""), camera_view("client")
        seen["views"] = (f"host {state_a} yaw {yaw_a:.1f} pitch {pitch_a:.1f} at {[round(a) for a in at_a]}, "
                         f"client {state_b} yaw {yaw_b:.1f} pitch {pitch_b:.1f} at {[round(a) for a in at_b]}")
        turn = abs((yaw_a - yaw_b + 180) % 360 - 180)
        return state_a == state_b and turn < degrees and abs(pitch_a - pitch_b) < degrees and distance(at_a, at_b) < units
    try:
        wait_for(what, check, 5)
    except TestFailure as e:
        raise TestFailure(f"{e}: {seen.get('views')}") from None


def stacked(what):
    """Both games show the two players on one spot: nobody pushed anybody off it."""
    for instance in ("client", ""):
        wait_for(f"[{instance or 'host'}] {what}", lambda instance=instance: on_other_player(instance), 5)


def my_position(instance):
    select_instance(instance)
    return ((read_status() or {}).get("player") or {}).get("position")


def other_player_position(instance):
    """Where that game shows the other player (its copy)."""
    actors = actors_on(instance)
    other = other_player(actors)
    return actors[str(other)]["actual"] if other is not None else None


def walk_off(walker, toward, what):
    """The walker turns toward `toward` (x, y) and walks there until it is 100 units from the other player: off
    the stack (60), with a short clear way back, and stays off until its game takes it off the stack (half a second
    off the spot, TravelService::UpdateStack: one back on it sooner is still stacked). Nothing else follows from
    it: no menu opens, nobody travels."""
    _, mark = log_trail(walker, 1 << 62)
    start = my_position(walker)
    console_on(walker, f"player.setangle z {math.degrees(math.atan2(toward[0] - start[0], toward[1] - start[1])) % 360:.0f}")
    cells = {name: player_cell(name) for name in ("", "client")}
    press(walker, "forward")
    try:
        wait_for(f"[{walker or 'host'}] {what}",
                 lambda: distance(my_position(walker)[:2], other_player_position(walker)[:2]) > 100, 10)
    finally:
        release(walker, "forward")
    wait_for(f"[{walker or 'host'}] its game takes it off the stack",
             lambda: any("Stack: 14 walked off" in line["text"] for line in log_trail(walker, mark)[0]), 5)
    for name in ("", "client"):
        if player_cell(name) != cells[name]:
            raise TestFailure(f"[{name or 'host'}] walking off the stack took the party elsewhere")
        if not takes_input(name):
            raise TestFailure(f"[{name or 'host'}] walking off the stack opened {sorted(set((read_status() or {}).get('menus') or []) - {'HUD Menu'})}")


def bumps_into_other(walker):
    """Off the stack, the walker collides again: walking into the other player (Forward held), it slides around
    it or is stopped by it, never through it: the two never come closer than their bodies allow."""
    face(walker, other_player(actors_on(walker)))
    seen = {"closest": float("inf")}
    rested = comes_to_rest(lambda: my_position(walker))
    def done():
        me, other = my_position(walker), other_player_position(walker)
        seen["gap"] = gap = distance(me[:2], other[:2])
        seen["closest"] = min(seen["closest"], gap)
        return seen["closest"] < 80 and (gap > 120 or rested())
    press(walker, "forward")
    try:
        try:
            wait_for(f"[{walker or 'host'}] walks into the other player, then past it or stopped by it", done, 10)
        except TestFailure as error:
            raise TestFailure(f"{error} (closest {seen['closest']:.0f}, now {seen.get('gap', 0):.0f} units apart)")
    finally:
        release(walker, "forward")
    if seen["closest"] < 30:
        raise TestFailure(f"[{walker or 'host'}] came within {seen['closest']:.0f} units of the other player: it walked "
                          "through it (collision should be back off the stack)")
    log(f"[{walker or 'host'}] came no closer than {seen['closest']:.0f} units to the other player")


def subtitle_on_screen(instance):
    """Whose subtitle that game's HUD shows now (SubtitleManager's current speaker), 0 for none."""
    select_instance(instance)
    return (read_status() or {}).get("subtitle_on_screen") or 0


def scenario_dialogue_nobody_moves():
    """The client talks to its copy while the host stands 1500 units off: the host's player is the one talking
    (its game runs the conversation from where it stands, without its menu), and neither player is moved for it.
    The client's menu lists the options and stays open; its Tab, the last one out, ends it."""
    npc, _, copy = spawn_host_npc(LYDIA, restrained=True)
    try:
        place_before("", npc, 1500, 90)
        place_before("client", copy, 110)
        spots = {name: my_position(name) for name in ("", "client")}
        activate_on("client", copy)
        wait_for(f"[host] the player talks to {npc:08X} in the background", lambda: in_conversation("", npc), 10)
        wait_for(f"[client] the menu opens on copy {copy:08X}", lambda: talking_to("client", copy), 10)
        wait_for("[client] the menu lists options",
                 lambda: talking_to("client", copy) and dialogue("client").get("options"), 10)
        for name in ("", "client"):
            if distance(my_position(name)[:2], spots[name][:2]) > 5:
                raise TestFailure(f"[{name or 'host'}] the player was moved for the conversation")
        hold()
        tap_key("client", TAB_KEY)
        wait_for("[client] the menu closes", lambda: menu_closed("client"), 10)
        wait_for("[host] its conversation ends", lambda: not in_conversation("", npc), 10)
    finally:
        close_dialogues()
        remove_host_npc(npc)


def scenario_dialogue_join_by_talking():
    """The host talks to its NPC with the client 2000 units off: the client stays out of it (no menu) and reads
    her line on its screen (relayed with the engine's force flag, so at any distance). Coming up to her copy and
    talking to it, the client joins: its menu opens on the copy, the host's conversation goes on."""
    npc, _, copy = spawn_host_npc(LYDIA, restrained=True)
    try:
        place_before("client", copy, 2000, 90)
        place_before("", npc, 110)
        activate_on("", npc)
        wait_for(f"[host] the player talks to {npc:08X}", lambda: talking_to("", npc), 10)
        wait_for(f"[client] the HUD shows copy {copy:08X}'s line", lambda: subtitle_on_screen("client") == copy, 10)
        place_before("client", copy, 110, 90)
        if dialogue("client").get("menu"):
            raise TestFailure("[client] near her copy, the menu opened without talking to it")
        activate_on("client", copy)
        wait_for(f"[client] talking to copy {copy:08X} joins", lambda: talking_to("client", copy), 10)
        wait_for("[client] the menu lists options", lambda: talking_to("client", copy) and dialogue("client").get("options"), 10)
        if not talking_to("", npc):
            raise TestFailure("[host] the client joining ended the host's conversation")
        hold()
    finally:
        close_dialogues()
        remove_host_npc(npc)


def in_conversation(instance, speaker):
    """That game's player talks to `speaker`, with its menu open or not (MenuTopicManager's speaker)."""
    return dialogue(instance).get("speaker") == speaker


def scenario_dialogue_host_in_background():
    """The client talks to its copy with the host 800 units off: the host's game runs the conversation in the
    background (no menu, its player not turned, free to move) and shows her line on its HUD, while the client's
    menu opens. The host walks off and the conversation waits for a pick; walking back and talking to her, the
    host joins: its menu opens on the conversation under way. Then the
    host leaves it (Tab ends it for both), the client talks again, and a pick in the host's background
    conversation says her line: the follower line ends it for both."""
    npc, _, copy = spawn_host_npc(LYDIA, restrained=True)
    try:
        place_before("", npc, 800, 90)
        place_before("client", copy, 110)
        activate_on("client", copy)
        wait_for(f"[host] the player talks to {npc:08X} in the background", lambda: in_conversation("", npc), 10)
        wait_for(f"[client] the menu opens on copy {copy:08X}", lambda: talking_to("client", copy), 10)
        wait_for("[host] the conversation lists options", lambda: dialogue("").get("options"), 10)
        if dialogue("").get("menu"):
            raise TestFailure("[host] 800 units off, the dialogue menu opened")
        wait_for(f"[host] the HUD shows {npc:08X}'s line", lambda: subtitle_on_screen("") == npc, 10)
        start = my_position("")
        console_on("", "player.setangle z 90")  # away from her: the host stands east of her
        press("", "forward")  # the host takes input: no menu
        try:
            wait_for("[host] walks away from her", lambda: distance(my_position("")[:2], start[:2]) > 200, 10)
        finally:
            release("", "forward")
        # Waiting for a pick, with no menu: the conversation stays (600 frames, about 10 s).
        frame = game_frame("")
        def waited():
            if not in_conversation("", npc) or not talking_to("client", copy):
                raise TestFailure("[host] waiting for a pick without the menu, the conversation ended")
            return game_frame("") > frame + 600
        wait_for("[host] the conversation waits for a pick", waited, 30)
        face("", npc)
        press("", "forward")
        try:
            wait_for("[host] walks back to her", lambda: distance(my_position("")[:2], start[:2]) < 300, 10)
        finally:
            release("", "forward")
        if dialogue("").get("menu"):
            raise TestFailure("[host] near her, the menu opened without talking to her")
        activate_on("", npc)
        wait_for(f"[host] talking to {npc:08X} joins: the menu opens", lambda: talking_to("", npc), 10)
        wait_for("[host] the menu lists options", lambda: dialogue("").get("options"), 10)
        hold()
        close_dialogues()

        place_before("", npc, 800, 90)
        activate_on("client", copy)
        wait_for(f"[host] the player talks to {npc:08X} in the background", lambda: in_conversation("", npc), 10)
        wait_for(f"[client] the menu opens on copy {copy:08X}", lambda: talking_to("client", copy), 10)
        options = wait_for("[host] the conversation lists options", lambda: dialogue("").get("options"), 10)
        log(f"[host] options: {[o['text'] for o in options]}")
        player_command("", {"cmd": "dialogue_pick", "index": 0})
        wait_for("[host] her line (a goodbye) ends the conversation", lambda: not in_conversation("", npc), 20)
        wait_for("[client] the menu closes", lambda: menu_closed("client"), 10)
        if not takes_input(""):
            raise TestFailure(f"[host] after the conversation, a menu is open: {(read_status() or {}).get('menus')}")
    finally:
        close_dialogues()
        remove_host_npc(npc)


def scenario_dialogue_options_are_the_hosts():
    """Every menu shows the host's list, judged for the host's player: Brenuin offers "Here. Have a gold piece." only
    to a player carrying gold. The host carries gold, the client none: the client talks to him (the host's game in
    the background) and its menu lists the coin anyway. Given gold, the client picks it: the host's game says
    Brenuin's answer, its script runs for the client (party-model.md, rule 2) and takes the client's coin, not the
    host's; the chat on both games names who said it. Then the other way round (the client carrying gold, the host
    none): nobody's menu lists the coin."""
    gold = {name: item_count(name, 0x14, GOLD) for name in ("", "client")}
    npc, _, copy = spawn_host_npc(BRENUIN, restrained=True)
    def set_gold(name, count, wait=True):
        have = item_count(name, 0x14, GOLD)
        if have != count:
            console_on(name, f"player.{'additem' if count > have else 'removeitem'} f {abs(count - have)}")
        if wait:
            wait_for(f"[{name or 'host'}] carries {count} gold", lambda: item_count(name, 0x14, GOLD) == count, 5)
    try:
        # The coin's line comes back 4 hours after it is said (an earlier run said it).
        console_on("", "set gamehour to gamehour + 5")
        set_gold("", 10)
        set_gold("client", 0)
        names = [name for name, host in session_players("").items() if not host]
        if len(names) != 1:
            raise TestFailure(f"[host] lists {names} as the other players, the scenario needs the client alone")
        place_before("", npc, 110)
        place_before("client", copy, 110)
        activate_on("client", copy)
        wait_for(f"[client] the menu opens on copy {copy:08X}", lambda: talking_to("client", copy), 10)
        wait_for(f"[host] the player talks to {npc:08X} in the background", lambda: in_conversation("", npc), 10)
        wait_for("[host] its list has the coin", lambda: lists_topic("", GIVE_COIN_TOPIC), 30)
        client_shows_hosts_list("with the coin, though the client carries no gold")
        text = next(o["text"] for o in dialogue("client")["options"] if o["topic"] == GIVE_COIN_TOPIC)
        set_gold("client", 10)
        character = client_on_host()
        def copy_carries_it():
            if item_count("client", 0x14, GOLD) != 10:
                raise TestFailure(f"[client] its gold changed to {item_count('client', 0x14, GOLD)} before the pick")
            return item_count("", character, GOLD) == 10
        try:
            wait_for("[host] the client's character carries its gold", copy_carries_it, 5)
        except TestFailure as error:
            raise TestFailure(f"{error}: the copy {character:08X} carries {item_count('', character, GOLD)}") from error
        pick_with_keys("client", GIVE_COIN_TOPIC)
        wait_for(f"[host] {npc:08X} says his answer to the coin",
                 lambda: dialogue("").get("current_info") == GIVE_COIN_INFO, 10)
        wait_for(f"[client] copy {copy:08X} voices it", lambda: line_playing("client", copy), 5)
        wait_for("[client] its player gave the coin", lambda: item_count("client", 0x14, GOLD) == 9, 10)
        if item_count("", 0x14, GOLD) != 10:
            raise TestFailure(f"[host] its player paid the client's coin: {item_count('', 0x14, GOLD)} gold")
        for name in ("", "client"):
            wait_for(f"[{name or 'host'}] the chat reads {names[0]}: {text}",
                     lambda name=name: chat_has(name, names[0], text), 5)
        close_dialogues()

        console_on("", "set gamehour to gamehour + 5")
        set_gold("", 0)
        set_gold("client", 10)
        activate_on("client", copy)
        wait_for(f"[client] the menu opens on copy {copy:08X} again", lambda: talking_to("client", copy), 10)
        wait_for("[client] the menu shows its list", lambda: list_shown("client"), 30)
        client_shows_hosts_list("without the coin")
        if lists_topic("client", GIVE_COIN_TOPIC):
            raise TestFailure("[client] its menu lists the coin, which the host's player cannot give")
        hold()
    finally:
        close_dialogues()
        remove_host_npc(npc)
        for name in ("", "client"):
            console_on(name, f"player.dispel {GIFT_OF_CHARITY:X}")  # the coin's blessing on the giver
        for name in ("", "client"):
            set_gold(name, gold[name], wait=False)


# The quest of Brenuin's coin topic (FavorJobsBeggars): running in a game is what lets its engine offer the topic.
BEGGARS_QUEST = "FavorJobsBeggars"
GIFT_OF_CHARITY = 0xEEF58  # the blessing a beggar's coin gives the giver (an hour of +10 Speech)


def quest_running(instance, quest_id):
    select_instance(instance)
    return bool((rpc({"cmd": "quest", "quest": quest_id}) or {}).get("flags", 0) & 1)


def quest_stage(instance, quest_id):
    select_instance(instance)
    return (rpc({"cmd": "quest", "quest": quest_id}) or {}).get("current_stage")


def scenario_dialogue_quest_option_from_host():
    """A guest's menu lists what the host's quests offer, whatever its own quest state: the client's game stops
    Brenuin's coin quest (its own copy of it; a guest runs no quests and keeps what the save had), so its engine would
    offer no coin. The host's quest runs and its player carries gold: the client talks to Brenuin and its menu lists
    the coin, the host's list. The quest is started again on the client afterwards."""
    host_gold = item_count("", 0x14, GOLD)
    npc, _, copy = spawn_host_npc(BRENUIN, restrained=True)
    quest = 0
    try:
        console_on("", "set gamehour to gamehour + 5")
        if host_gold < 10:
            console_on("", f"player.additem f {10 - host_gold}")
        place_before("", npc, 800, 90)
        place_before("client", copy, 110)
        activate_on("client", copy)
        wait_for(f"[client] the menu opens on copy {copy:08X}", lambda: talking_to("client", copy), 10)
        wait_for("[host] its list has the coin", lambda: lists_topic("", GIVE_COIN_TOPIC), 30)
        quest = next(o["quest"] for o in dialogue("")["options"] if o["topic"] == GIVE_COIN_TOPIC)
        close_dialogues()
        if not quest or not quest_running("", quest):
            raise TestFailure(f"[host] the coin's quest {quest:X} does not run")
        console_on("client", f"stopquest {BEGGARS_QUEST}")
        wait_for(f"[client] its quest {quest:X} has stopped", lambda: not quest_running("client", quest), 5)

        activate_on("client", copy)
        wait_for(f"[client] the menu opens on copy {copy:08X} again", lambda: talking_to("client", copy), 10)
        wait_for("[client] the menu shows its list", lambda: list_shown("client"), 30)
        client_shows_hosts_list("with the coin, its own quest stopped")
        hold()
    finally:
        close_dialogues()
        remove_host_npc(npc)
        if quest and not quest_running("client", quest):
            console_on("client", f"startquest {BEGGARS_QUEST}")
        change = host_gold - item_count("", 0x14, GOLD)
        if change:
            console_on("", f"player.{'additem' if change > 0 else 'removeitem'} f {abs(change)}")


def scenario_dialogue_said_line_waits():
    """A line with reset hours is not offered again until they pass, on every game: the client gives Brenuin a
    coin (its line comes back after 4 hours), leaves, and talks to him again: its menu no longer lists the coin,
    as the host's game, which said the line, does not."""
    gold = item_count("", 0x14, GOLD)
    npc, _, copy = spawn_host_npc(BRENUIN, restrained=True)
    try:
        console_on("", "set gamehour to gamehour + 5")
        console_on("", "player.additem f 10")
        place_before("", npc, 800, 90)
        place_before("client", copy, 110)
        activate_on("client", copy)
        wait_for(f"[client] the menu opens on copy {copy:08X}", lambda: talking_to("client", copy), 10)
        wait_for("[client] its menu lists the coin", lambda: lists_topic("client", GIVE_COIN_TOPIC), 10)
        pick_with_keys("client", GIVE_COIN_TOPIC)
        wait_for(f"[host] {npc:08X} says his answer to the coin",
                 lambda: dialogue("").get("current_info") == GIVE_COIN_INFO, 10)
        close_dialogues()

        activate_on("client", copy)
        wait_for(f"[client] the menu opens on copy {copy:08X} again", lambda: talking_to("client", copy), 10)
        wait_for("[client] the menu shows its list", lambda: list_shown("client"), 30)
        if lists_topic("client", GIVE_COIN_TOPIC):
            raise TestFailure("[client] its menu offers the coin again within its reset hours")
        hold()
    finally:
        close_dialogues()
        remove_host_npc(npc)
        for name in ("", "client"):
            console_on(name, f"player.dispel {GIFT_OF_CHARITY:X}")  # the coin's blessing on the giver
        change = gold - item_count("", 0x14, GOLD)
        if change:
            console_on("", f"player.{'additem' if change > 0 else 'removeitem'} f {abs(change)}")


def pick_with_keys(instance, topic):
    """That game's player moves the highlight to the topic (W, S) and picks it (E), once the menu takes a pick."""
    wait_for(f"[{instance or 'host'}] the menu takes a pick", lambda: menu_takes_pick(instance), 10)
    while cursor(instance) != topic:
        here = cursor(instance)
        topics = [o["topic"] for o in dialogue(instance).get("options", [])]
        tap_key(instance, DOWN_KEY if topics.index(here) < topics.index(topic) else UP_KEY)
        wait_for(f"[{instance or 'host'}] the key moves the highlight", lambda: cursor(instance) != here, 5)
    log(f"[{instance or 'host'}] picks {topic:X}")
    tap_key(instance, ACTIVATE_KEY)


def scenario_dialogue_pick_says_the_hosts_line():
    """A topic with a different line per player is said with the host's: Hulda's "rent a room" is the room for 10
    gold for a player with the gold, "cannot afford it" for one without. The host carries gold, the client none:
    the client's menu shows the host's line, the client picks it, and the host's game says it: the room. Its script
    runs for the client (party-model.md, rule 2), so the host's gold is not taken; the client, judged by the host's
    list, has none to give (the conditions gap, party-model.md "Items and gold"). Then both menus show the host's
    next list."""
    gold = {name: item_count(name, 0x14, GOLD) for name in ("", "client")}
    npc, _, copy = spawn_host_npc(HULDA, restrained=True)
    try:
        if gold["client"]:
            console_on("client", f"player.removeitem f {gold['client']}")
        if gold[""] < 10:
            console_on("", f"player.additem f {10 - gold['']}")
        place_before("", npc, 110)
        place_before("client", copy, 110)
        activate_on("client", copy)
        wait_for(f"[client] the menu opens on copy {copy:08X}", lambda: talking_to("client", copy), 10)
        wait_for(f"[host] the player talks to {npc:08X} in the background", lambda: in_conversation("", npc), 10)
        host_info = wait_for("[host] its list has rent a room", lambda: info_shown("", RENT_ROOM_TOPIC), 30)
        if host_info == RENT_ROOM_NO_GOLD_INFO:
            raise TestFailure(f"[host] carrying gold, its line for rent a room is the one without ({host_info:X})")
        client_shows_hosts_list("with the host's line for rent a room")
        host_gold = item_count("", 0x14, GOLD)
        pick_with_keys("client", RENT_ROOM_TOPIC)
        wait_for(f"[host] {npc:08X} says the host's line {host_info:X}", lambda: dialogue("").get("current_info") == host_info, 10)
        wait_for(f"[client] copy {copy:08X} voices it", lambda: line_playing("client", copy), 5)
        wait_for("[client] the menu shows the next list", lambda: list_shown("client"), 30)
        client_shows_hosts_list("after the pick")
        if item_count("", 0x14, GOLD) != host_gold:
            raise TestFailure(f"[host] its player paid for the client's room: {host_gold} -> {item_count('', 0x14, GOLD)} gold")
        # The room is the party's: rented by the client's character, the bed is the player's (0x7) on the host.
        bed = script_var("", npc, "RentRoomScript", "Bed")
        owner = player_command("", {"cmd": "form", "form": bed}).get("owner")
        if owner != PLAYER_BASE:
            raise TestFailure(f"[host] the rented bed {bed:X} is owned by {owner}, not the player's base")
        hold()
    finally:
        close_dialogues()
        remove_host_npc(npc)
        for name in ("", "client"):
            change = gold[name] - item_count(name, 0x14, GOLD)
            if change:
                console_on(name, f"player.{'additem' if change > 0 else 'removeitem'} f {abs(change)}")


SPEECH_SKILL = 17  # ActorValueInfo::kSpeechcraft


def skill(instance, index):
    """That game's player's skill: (level, experience toward the next level)."""
    s = player_command(instance, {"cmd": "skill", "index": index})
    return s["level"], s["experience"]


UNGRIEN = "0001337E"  # Riften's Black-Briar Meadery: his Black-Briar topic (DialogueRiften, running) has a persuade
UNGRIEN_TOPIC = 0x369D2  # its branch's first topic; his answer's choices follow
UNGRIEN_PERSUADE_TOPIC = 0x369D1  # needs the player's Speech at SpeechEasy (25); its script persuades him


def misc_stat(instance, name):
    """That game's player's misc stat (the console's GetPCMiscStat)."""
    output = console_on(instance, f'getpcmiscstat "{name}"')
    numbers = re.findall(r"-?\d+(?:\.\d+)?", " ".join(output))  # "Player misc stat value 1.00"
    if not numbers:
        raise TestFailure(f"[{instance or 'host'}] no value for misc stat {name}: {output}")
    return round(float(numbers[-1]))


def set_misc_stat(instance, name, value):
    if change := value - misc_stat(instance, name):
        console_on(instance, f'modpcmiscstat "{name}" {change}')


def actor_value(instance, actor, index):
    """That actor's value in that game (a copy's: what the host's scripts read of it)."""
    return player_command(instance, {"cmd": "skill", "index": index, "actor": actor})["level"]


# FavorDialogueScript.Persuade's achievement: Game.QueryStat("Bribes") && Game.QueryStat("Intimidations"), then
# Game.AddAchievement(28).
PERSUADE_ACHIEVEMENT_STATS = ("Bribes", "Intimidations")


def natives_run_for_host(since, function):
    """The natives the host's script ran on that game's player since that trail number (GuestService's log)."""
    lines, _ = log_trail("client", since)
    return [line["text"] for line in lines if line["text"].startswith(f"GuestService: the host's script runs Game.{function}")]


def scenario_natives_reach_matches_ida():
    """The plugin's reading of which global natives act on this game's player (Papyrus/NativeReach, rule 2's natives
    on the player) matches IDA's over the game (scripts/re/papyrus_natives.py, docs/re/papyrus_natives.tsv): no
    native moves to the guest's game, or stays on the host, that IDA reads otherwise. Natives IDA does not list
    (SKSE's) are not compared."""
    expected = {}
    with open(ROOT / "docs/re/papyrus_natives.tsv") as table:
        header = table.readline().rstrip("\n").split("\t")
        for line in table:
            row = dict(zip(header, line.rstrip("\n").split("\t")))
            if row["self"] == "StaticFunctionTag" and row["function"] != "?":
                expected.setdefault(row["class"], {})[row["name"].lower()] = bool(row.get("reaches the player"))
    differ = []
    for klass, natives in sorted(expected.items()):
        read = {name.lower(): reaches for name, reaches in
                player_command("", {"cmd": "global_natives", "class": klass})["natives"].items()}
        for name, reaches in sorted(natives.items()):
            if name not in read:
                differ.append(f"{klass}.{name}: not in the VM")
            elif read[name] != reaches:
                differ.append(f"{klass}.{name}: the plugin reads {'the player' if read[name] else 'no player'}, IDA "
                              f"{'the player' if reaches else 'no player'}")
    if differ:
        raise TestFailure(f"[host] {len(differ)} global natives read otherwise than IDA: " + "; ".join(differ))


def scenario_dialogue_picker_gets_speech_experience():
    """A persuade's Speech experience is the picker's: the host's game says every line and runs its script for the
    player who picked it (party-model.md, rule 2), and the Game.AdvanceSkill in it runs on that player's game. The
    client talks to Ungrien's copy, asks about the Black-Briars and persuades him (FavorDialogueScript.Persuade; the
    host's list, judged for the host's Speech, set to 30): the client's Speech gains experience, the host's does
    not. The persuade sizes it from Game.GetPlayer().GetAv("Speechcraft"), the client's character on the host, which
    carries the client's Speech (45). Its Game.QueryStat asks the client's game, whose bribes and intimidations are
    counted (the host's are not): the achievement it then gives goes to the client's game. Then the host persuades
    another Ungrien itself (his topic is gone once he is persuaded): its own Speech gains it, the client's not."""
    speech = {name: skill(name, SPEECH_SKILL)[0] for name in ("", "client")}
    stats = {name: {stat: misc_stat(name, stat) for stat in PERSUADE_ACHIEVEMENT_STATS} for name in ("", "client")}
    npc, _, copy = spawn_host_npc(UNGRIEN, restrained=True)
    def persuade(name, talk_to):
        other = "client" if name == "" else ""
        place_before(name, talk_to, 110)
        activate_on(name, talk_to)
        wait_for(f"[{name or 'host'}] the menu opens on {talk_to:08X}", lambda: talking_to(name, talk_to), 10)
        wait_for(f"[{name or 'host'}] its menu lists the Black-Briars", lambda: lists_topic(name, UNGRIEN_TOPIC), 30)
        pick_with_keys(name, UNGRIEN_TOPIC)
        wait_for(f"[{name or 'host'}] its menu lists the persuade", lambda: lists_topic(name, UNGRIEN_PERSUADE_TOPIC), 30)
        before = {player: skill(player, SPEECH_SKILL) for player in ("", "client")}
        pick_with_keys(name, UNGRIEN_PERSUADE_TOPIC)
        wait_for(f"[{name or 'host'}] its Speech gains the persuade's experience", lambda: skill(name, SPEECH_SKILL) > before[name], 30)
        frame = game_frame("")
        def other_unchanged():
            if skill(other, SPEECH_SKILL) != before[other]:
                raise TestFailure(f"[{other or 'host'}] its Speech gained the persuade of {name or 'the host'}: "
                                  f"{before[other]} -> {skill(other, SPEECH_SKILL)}")
            return game_frame("") > frame + 120
        wait_for(f"[{other or 'host'}] its Speech stays as it was", other_unchanged, 10)
        close_dialogues()
    try:
        console_on("", "player.setav speechcraft 30")
        console_on("client", "player.setav speechcraft 45")
        for stat in PERSUADE_ACHIEVEMENT_STATS:
            set_misc_stat("", stat, 0)
            set_misc_stat("client", stat, max(stats["client"][stat], 1))
        character = client_on_host()
        wait_for(f"[host] the client's character {character:08X} carries the client's Speech",
                 lambda: actor_value("", character, SPEECH_SKILL) == skill("client", SPEECH_SKILL)[0], 10)
        place_before("", npc, 800, 90)
        _, since = log_trail("client")
        persuade("client", copy)
        wait_for("[client] the persuade's achievement runs here: its QueryStat answered for the client",
                 lambda: natives_run_for_host(since, "AddAchievement"), 10)
        remove_host_npc(npc)
        npc, _, copy = spawn_host_npc(UNGRIEN, restrained=True)
        place_before("client", copy, 800, 90)
        _, since = log_trail("client")
        persuade("", npc)
        if achievements := natives_run_for_host(since, "AddAchievement"):
            raise TestFailure(f"[client] the host's own persuade gave the client's game an achievement: {achievements[0]}")
        hold()
    finally:
        close_dialogues()
        remove_host_npc(npc)
        for name in ("", "client"):
            console_on(name, f"player.setav speechcraft {speech[name]:.0f}")
            for stat, value in stats[name].items():
                set_misc_stat(name, stat, value)


def scenario_dialogue_refused_start():
    """A conversation the host's game cannot start is refused to the peer who asked: the client talks to the copy
    of an NPC that is fighting, its menu does not open and its chat says why. Once the fight is over, the host
    talks to that NPC itself: the client's menu stays closed (the refused ask is forgotten), and the client can
    then join by talking to the copy as usual."""
    npc, _, copy = spawn_host_npc(LYDIA)
    foe, _, _ = spawn_host_npc(BRENUIN)
    try:
        place_before("", npc, 300)
        place_before("client", copy, 110)
        console_on("", f"{foe:08X}.setav health 100000")
        console_on("", f"{npc:08X}.startcombat {foe:08X}")
        lines = len(chat("client").get("lines", []))
        activate_on("client", copy)
        wait_for("[client] its chat says the host can't talk to them",
                 lambda: any(l["system"] and "can't talk" in l["text"] for l in chat("client").get("lines", [])[lines:]), 10)
        if dialogue("client").get("menu") or dialogue("").get("speaker"):
            raise TestFailure("[client] a conversation opened with an NPC in combat")
        console_on("", f"{npc:08X}.stopcombat")
        console_on("", f"{foe:08X}.stopcombat")
        remove_host_npc(foe)
        foe = None
        place_before("", npc, 110)
        activate_on("", npc)
        wait_for(f"[host] the player talks to {npc:08X}", lambda: talking_to("", npc), 10)
        frame = game_frame("client")
        def client_stays_out():
            if dialogue("client").get("menu"):
                raise TestFailure("[client] its menu opened for the host's conversation: the refused ask was kept")
            return game_frame("client") > frame + 60
        wait_for("[client] its menu stays closed", client_stays_out, 10)
        activate_on("client", copy)
        wait_for(f"[client] talking to copy {copy:08X} joins: the menu opens", lambda: talking_to("client", copy), 10)
        hold()
    finally:
        close_dialogues()
        if foe:
            remove_host_npc(foe)
        remove_host_npc(npc)


def scenario_dialogue_ends_on_travel():
    """Walking off does not end a conversation (the host talks from anywhere), travel does: the client talks to
    Lydia's copy (the host's game in the background) and the host takes the party to the inn. The host's
    conversation ends and the client's menu closes. Back home, the next conversation runs as usual: the host
    talks to a new Lydia, the client's HUD shows her line and the client joins by talking to her copy (its copy of
    the first Lydia was deleted mid-conversation, which left its game unable to talk to anyone)."""
    npc, _, copy = spawn_host_npc(LYDIA, restrained=True)
    try:
        place_before("", npc, 800, 90)
        place_before("client", copy, 110)
        activate_on("client", copy)
        wait_for(f"[client] the menu opens on copy {copy:08X}", lambda: talking_to("client", copy), 10)
        wait_for(f"[host] the player talks to {npc:08X} in the background", lambda: in_conversation("", npc), 10)
        both_go(INN)
        wait_for("[host] its conversation ended", lambda: not dialogue("").get("speaker"), 10)
        wait_for("[client] its menu closed", lambda: menu_closed("client"), 10)
        both_go(HOME_CELL)
        remove_host_npc(npc)
        npc, _, copy = spawn_host_npc(LYDIA, restrained=True)
        place_before("", npc, 110)
        place_before("client", copy, 300)
        activate_on("", npc)
        wait_for(f"[host] the player talks to {npc:08X}", lambda: talking_to("", npc), 10)
        wait_for(f"[client] the HUD shows copy {copy:08X}'s line", lambda: subtitle_on_screen("client") == copy, 10)
        activate_on("client", copy)
        wait_for(f"[client] talking to copy {copy:08X} joins: the menu opens", lambda: talking_to("client", copy), 10)
        hold()
    finally:
        close_dialogues()
        if player_cell("") != player_cell("client") or not player_cell(""):
            both_go(HOME_CELL)
        remove_host_npc(npc)


def scenario_dialogue_npc_removed():
    """The NPC goes away mid-conversation (the host's game deletes it): the client's menu on its copy closes, and
    the client can talk to the next NPC (a copy deleted while its game talks to it left that game unable to start
    another conversation)."""
    npc, _, copy = spawn_host_npc(LYDIA, restrained=True)
    try:
        place_before("", npc, 800, 90)
        place_before("client", copy, 110)
        activate_on("client", copy)
        wait_for(f"[client] the menu opens on copy {copy:08X}", lambda: talking_to("client", copy), 10)
        wait_for(f"[host] the player talks to {npc:08X} in the background", lambda: in_conversation("", npc), 10)
        remove_host_npc(npc)
        wait_for("[client] its menu closes", lambda: menu_closed("client"), 10)
        npc, _, copy = spawn_host_npc(LYDIA, restrained=True)
        place_before("client", copy, 110)
        activate_on("client", copy)
        wait_for(f"[client] talking to the new copy {copy:08X} opens the menu", lambda: talking_to("client", copy), 10)
        hold()
    finally:
        close_dialogues()
        remove_host_npc(npc)


def force_greet(instance, actor):
    """The actor starts a conversation with that game's player, as its ForceGreet package does."""
    return player_command(instance, {"cmd": "force_greet", "actor": actor})["started"]


def scenario_dialogue_npc_starts():
    """An NPC starts the conversation (a force greet; NPCs run their AI on the host's game, so it greets the host's
    player): it greets the party, every game's menu opens on it (the clients' on their copies), with the host's
    topics, without anyone talking to it. While the client's conversation with another NPC runs in the
    background, a force greet waits: the host's game stays in the client's conversation."""
    npc, server_id, copy = spawn_host_npc(LYDIA, restrained=True)
    other, _, other_copy = spawn_host_npc(HULDA, restrained=True)
    try:
        place_before("", npc, 110)
        place_before("client", copy, 110)
        if not force_greet("", npc):
            raise TestFailure(f"[host] {npc:08X}'s force greet did not start")
        wait_for(f"[host] the menu opens on {npc:08X}", lambda: talking_to("", npc), 10)
        host_topics = wait_for("[host] the conversation lists options", lambda: topics_shown(""), 5)
        wait_for(f"[client] greeted too: the menu opens on copy {copy:08X}", lambda: talking_to("client", copy), 10)
        wait_for("[client] the menu shows the host's topics",
                 lambda: topics_shown("client")[:len(host_topics)] == host_topics, 10)
        close_dialogues()

        place_before("client", other_copy, 110)
        activate_on("client", other_copy)
        wait_for(f"[client] the menu opens on copy {other_copy:08X}", lambda: talking_to("client", other_copy), 10)
        wait_for(f"[host] the player talks to {other:08X} in the background", lambda: in_conversation("", other), 10)
        if force_greet("", npc):
            raise TestFailure(f"[host] {npc:08X}'s force greet started during the client's conversation")
        frame = game_frame("")
        def stays():
            if not in_conversation("", other) or not talking_to("client", other_copy):
                raise TestFailure("the client's conversation did not survive the force greet")
            return game_frame("") > frame + 60
        wait_for("[host] its game stays in the client's conversation", stays, 10)
        hold()
    finally:
        close_dialogues()
        remove_host_npc(npc)
        remove_host_npc(other)


ANDURS = "00013BA8"  # Whiterun's priest of Arkay: a question, its follow-up, then the amulet's choices
ANDURS_KNOW_ARKAY = 0xD74C1  # "The god of death? I know him well." (his greeting's choices)
ANDURS_WHAT_LOST = 0x94E32  # "What did you lose?"
ANDURS_AMULET = {0xD3D5D, 0xD3D5C}  # the amulet's choices


def scenario_dialogue_several_questions_deep():
    """A conversation three questions deep, picked by turns: Andurs greets with a question; the client answers,
    the host (joined by talking) asks the follow-up, the client answers the last one. After each pick both
    menus show the host's next list (topics, lines, texts), the line's choices only, and a peer's not before the NPC
    has answered."""
    npc, _, copy = spawn_host_npc(ANDURS, restrained=True)
    def next_list(what, topics):
        def shows(name):
            shown = set(topics_shown(name)) if list_shown(name) else None
            if shown == topics and name and line_playing("", npc):
                raise TestFailure(f"[{name}] the menu offers {what} while the host's NPC still answers")
            return shown == topics
        for name in ("client", ""):
            wait_for(f"[{name or 'host'}] the menu shows {what}", lambda name=name: shows(name) and shows("client"), 60)
    try:
        place_before("", npc, 110)
        place_before("client", copy, 300)
        activate_on("client", copy)
        wait_for(f"[client] the menu opens on copy {copy:08X}", lambda: talking_to("client", copy), 10)
        wait_for(f"[host] the player talks to {npc:08X} in the background", lambda: in_conversation("", npc), 10)
        activate_on("", npc)
        wait_for(f"[host] talking to {npc:08X} joins: the menu opens", lambda: talking_to("", npc), 10)
        wait_for("[client] the menu lists Andurs' question", lambda: lists_topic("client", ANDURS_KNOW_ARKAY), 30)
        client_shows_hosts_list("at his question")
        pick_with_keys("client", ANDURS_KNOW_ARKAY)
        next_list("the follow-up", {ANDURS_WHAT_LOST})
        client_shows_hosts_list("at the follow-up")
        pick_with_keys("", ANDURS_WHAT_LOST)
        next_list("the amulet's choices", ANDURS_AMULET)
        client_shows_hosts_list("at the amulet's choices")
        pick_with_keys("client", topics_shown("client")[0])
        wait_for(f"[host] {npc:08X} says the last answer", lambda: line_playing("", npc), 10)
        hold()
    finally:
        close_dialogues()
        remove_host_npc(npc)


HULDA_COLLEGE_TOPIC = 0x1F80E  # MG01InitialBranchTopic: Hulda on the College, a say-once line
HULDA_COLLEGE_LINES = (0x8EC38, 0x8EC39)  # one for elves, one for the other races


def info_said(instance, info, said=None):
    """Whether that game marks the line said; with said, set back to it first (test setup only)."""
    return player_command(instance, {"cmd": "info_said", "info": info, **({} if said is None else {"said": said})})["said"]


def scenario_dialogue_said_once_line():
    """A say-once line is not said again once said, on any game: the client asks Hulda about the College (one of
    her two say-once lines), leaves and talks to her again: its menu, as the host's (joined by talking), offers
    the topic with her other line, not the one said. Both lines are marked unsaid on both games before and
    after (they stay said in a game otherwise)."""
    def unsay():
        for name in ("", "client"):
            for line in HULDA_COLLEGE_LINES:
                info_said(name, line, False)
    npc, _, copy = spawn_host_npc(HULDA, restrained=True)
    try:
        unsay()
        place_before("", npc, 800, 90)
        place_before("client", copy, 110)
        activate_on("client", copy)
        wait_for(f"[client] the menu opens on copy {copy:08X}", lambda: talking_to("client", copy), 10)
        wait_for("[client] its menu offers the College", lambda: lists_topic("client", HULDA_COLLEGE_TOPIC), 10)
        pick_with_keys("client", HULDA_COLLEGE_TOPIC)
        said = wait_for(f"[host] {npc:08X} says a College line",
                        lambda: (info := dialogue("").get("current_info")) in HULDA_COLLEGE_LINES and info, 10)
        close_dialogues()

        activate_on("client", copy)
        wait_for(f"[client] the menu opens on copy {copy:08X} again", lambda: talking_to("client", copy), 10)
        place_before("", npc, 110)
        activate_on("", npc)
        wait_for(f"[host] talking to {npc:08X} joins: the menu opens", lambda: talking_to("", npc), 10)
        for name in ("", "client"):
            wait_for(f"[{name or 'host'}] the menu shows its list", lambda name=name: list_shown(name), 30)
        lines = {name or "host": info_shown(name, HULDA_COLLEGE_TOPIC) for name in ("", "client")}
        if said in lines.values() or lines["host"] != lines["client"]:
            raise TestFailure(f"after line {said:X} was said, the College lines offered are {lines}: the said one "
                              f"again, or not the host's")
        hold()
    finally:
        close_dialogues()
        remove_host_npc(npc)
        unsay()


ONMUND = "0001C194"  # the College's Nord student; greets with a question
ONMUND_FEW_NORDS = 0x8B58D  # "Why aren't there many Nords at the College?" (his greeting's choices)
ONMUND_NOT_A_NORD = 0x66963  # its only answer, for a player who is not a Nord
ONMUND_NOT_A_NORD_LINE = 0x6696E
ONMUND_GREETINGS = (0x66973, 0x66974)  # his say-once greetings that ask the question (Nord, not a Nord)
NORD_RACE = 0x13746


def player_race_on_host(instance):
    """The race of that game's player, as the host's copy of it shows it."""
    select_instance(instance)
    sid = next(s["mod"]["session_id"] for s in ((read_status() or {}).get("world") or {}).values()
               if s.get("role") == "own" and s.get("mod", {}).get("session_id") is not None and (s.get("identity") or {}).get("base") == 7)
    select_instance("")
    return next(((s.get("identity") or {}).get("race") for s in ((read_status() or {}).get("world") or {}).values()
                 if s.get("role") == "copy" and s.get("mod", {}).get("session_id") == sid), None)


def scenario_dialogue_unanswered_question_ends_it():
    """A question the host's player cannot answer ends the conversation for everyone, as it does in the game: the
    list is the host's. Onmund's "Why aren't there many Nords at the College?" has one answer, for a player who is
    not a Nord; the host is a Nord, the client a Breton for the scenario. The client asks it, the host joined by
    talking: the host's engine has nothing to say, the conversation ends, and both menus close. His greetings, said
    once in a game, are marked unsaid on both games before and after."""
    def unsay():
        for name in ("", "client"):
            for line in ONMUND_GREETINGS:
                info_said(name, line, False)
    npc, _, copy = spawn_host_npc(ONMUND, restrained=True)
    try:
        unsay()
        console_on("client", "player.setrace BretonRace")
        place_before("", npc, 110, 90)
        place_before("client", copy, 110, 270)
        activate_on("client", copy)
        wait_for(f"[client] the menu opens on copy {copy:08X}", lambda: talking_to("client", copy), 10)
        wait_for("[client] his greeting is over: the menu shows his question's answers",
                 lambda: list_shown("client") and lists_topic("client", ONMUND_FEW_NORDS), 30)
        activate_on("", npc)
        wait_for(f"[host] talking to {npc:08X} joins: the menu opens", lambda: talking_to("", npc), 10)
        pick_with_keys("client", ONMUND_FEW_NORDS)
        wait_for("[host] nobody it lists can answer: the conversation ends", lambda: not dialogue("").get("speaker"), 60)
        wait_for("[client] its menu closes", lambda: menu_closed("client"), 10)
        if lists_topic("client", ONMUND_NOT_A_NORD):
            raise TestFailure("[client] its menu offered its own player's answer, not the host's list")
        hold()
    finally:
        close_dialogues()
        remove_host_npc(npc)
        unsay()
        console_on("client", "player.setrace NordRace")
        wait_for("[host] shows the client a Nord again", lambda: player_race_on_host("client") == NORD_RACE, 30)


AMREN = "00013BAA"  # Whiterun's one-handed trainer (journeyman)
TRAINING_TOPIC = 0xE3A35  # OffersTrainingTopic (DialogueTrainers): its line opens the training menu
ONE_HANDED_SKILL = 6  # ActorValueInfo::kOneHanded
TRAINING_MENU = "Training Menu"


def trainings(instance, count=None):
    """How many times that game's player trained this level (five allowed); with count, set back to it first."""
    return player_command(instance, {"cmd": "trainings", **({} if count is None else {"set": count})})["count"]


def scenario_dialogue_picker_trains():
    """A trainer's menu opens on the game of whoever picked its line: the client talks to Amren's copy and picks
    training. The host's game says the line and runs its script for the client (party-model.md, rule 2), whose
    Game.ShowTrainingMenu runs on the client's game, with the copy, over its dialogue menu as in single player; the
    host's game opens nothing. The client trains once: its One-handed goes up and its gold down, the host's stay as
    they were, and the client's character on the host carries the gold it has left (the menu pays from the inventory
    itself, InventoryChanges::RemoveItem). Leaving the training menu, the client is back in the conversation, as in
    single player."""
    gold = {name: item_count(name, 0x14, GOLD) for name in ("", "client")}
    trained = {name: (skill(name, ONE_HANDED_SKILL)[0], trainings(name)) for name in ("", "client")}
    npc, _, copy = spawn_host_npc(AMREN, restrained=True)
    try:
        console_on("client", "player.additem f 2000")
        place_before("", npc, 800, 90)
        place_before("client", copy, 110)
        activate_on("client", copy)
        wait_for(f"[client] the menu opens on copy {copy:08X}", lambda: talking_to("client", copy), 10)
        wait_for(f"[host] the player talks to {npc:08X} in the background", lambda: in_conversation("", npc), 10)
        wait_for(f"[host] {npc:08X}'s greeting is over", lambda: line_over("", npc), 30)
        wait_for("[client] its menu lists training", lambda: lists_topic("client", TRAINING_TOPIC), 5)
        before = {name: (skill(name, ONE_HANDED_SKILL), item_count(name, 0x14, GOLD)) for name in ("", "client")}
        pick_with_keys("client", TRAINING_TOPIC)
        wait_for(f"[host] {npc:08X} says the training line", lambda: line_playing("", npc), 10)
        wait_for("[client] the training menu opens", lambda: TRAINING_MENU in menus("client"), 30)
        frame = game_frame("")
        def host_has_none():
            if TRAINING_MENU in menus(""):
                raise TestFailure("[host] the training menu opened on the host's game")
            return game_frame("") > frame + 60
        wait_for("[host] no training menu", host_has_none, 10)
        tap_key("client", ACTIVATE_KEY)
        wait_for("[client] its One-handed goes up a level",
                 lambda: skill("client", ONE_HANDED_SKILL)[0] == before["client"][0][0] + 1, 10)
        if item_count("client", 0x14, GOLD) >= before["client"][1]:
            raise TestFailure(f"[client] training cost nothing: {before['client'][1]} gold before, {item_count('client', 0x14, GOLD)} after")
        if (skill("", ONE_HANDED_SKILL), item_count("", 0x14, GOLD)) != before[""]:
            raise TestFailure(f"[host] its One-handed or gold changed: {before['']} -> "
                              f"{(skill('', ONE_HANDED_SKILL), item_count('', 0x14, GOLD))}")
        character = client_on_host()
        left = item_count("client", 0x14, GOLD)
        wait_for(f"[host] the client's character carries the {left} gold it has left",
                 lambda: item_count("", character, GOLD) == left, 5)
        tap_key("client", TAB_KEY)
        wait_for("[client] the training menu closes", lambda: TRAINING_MENU not in menus("client"), 10)
        if not talking_to("client", copy):
            raise TestFailure("[client] left the conversation with the training menu")
        hold()
    finally:
        if TRAINING_MENU in menus("client"):
            tap_key("client", TAB_KEY)
            wait_for("[client] the training menu closes", lambda: TRAINING_MENU not in menus("client"), 10)
        close_dialogues()
        remove_host_npc(npc)
        for name in ("", "client"):
            change = gold[name] - item_count(name, 0x14, GOLD)
            if change:
                console_on(name, f"player.{'additem' if change > 0 else 'removeitem'} f {abs(change)}")
            level, count = trained[name]
            if skill(name, ONE_HANDED_SKILL)[0] != level:
                console_on(name, f"player.setav onehanded {level:.0f}")
            trainings(name, count)


def scenario_dialogue_pick_reaches_host():
    """The client talks to its copy (the host's game runs it in the background, the host 800 units off) and picks
    an option with its keys: the client's menu does not say it, the host's game does (her line on the host's NPC,
    voiced on the client's copy), and both games follow it: a goodbye ends the conversation for both, any other
    line leads both to the same next list."""
    npc, _, copy = spawn_host_npc(LYDIA, restrained=True)
    try:
        place_before("", npc, 800, 90)
        place_before("client", copy, 110)
        activate_on("client", copy)
        wait_for(f"[host] the player talks to {npc:08X} in the background", lambda: in_conversation("", npc), 10)
        wait_for(f"[client] the menu opens on copy {copy:08X}", lambda: talking_to("client", copy), 10)
        options = wait_for("[client] the menu lists options",
                           lambda: talking_to("client", copy) and dialogue("client").get("options"), 10)
        before = [o["topic"] for o in dialogue("").get("options", [])]
        wait_for("[host] its NPC's greeting is over", lambda: line_over("", npc), 30)
        wait_for("[client] the menu takes a pick", lambda: menu_takes_pick("client"), 10)
        log(f"[client] picks {options[0]['text']!r}")
        tap_key("client", ACTIVATE_KEY)
        wait_for(f"[host] {npc:08X} says the picked line", lambda: line_playing("", npc), 10)
        wait_for(f"[client] copy {copy:08X} voices it", lambda: line_playing("client", copy), 5)
        def followed():
            host, client = dialogue(""), dialogue("client")
            if not host.get("speaker"):
                return client_closed or not client.get("menu")
            topics = [o["topic"] for o in host.get("options", [])]
            return topics and topics != before and [o["topic"] for o in client.get("options", [])] == topics
        client_closed = False
        wait_for("[both] the conversation follows the pick (ended for both, or the same next list)", followed, 30)
        hold()
    finally:
        close_dialogues()
        remove_host_npc(npc)


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
    _, session_id, copy = spawn_host_npc(LYDIA, restrained=True)
    host_copy = other_player(actors_on("client"))
    log_file = plugin_log("client")
    start = log_file.stat().st_size
    console_on("client", f"player.moveto {copy:08X}")
    wait_for("[client] the teleport arrives (cell arrival)",
             lambda: "Arrived in cell" in log_file.read_bytes()[start:].decode(errors="replace"), 10)
    end = time.monotonic() + 1.5
    while time.monotonic() < end:
        actors = actors_on("client")
        if other_player(actors) != host_copy:
            raise TestFailure(f"the host's character was respawned: copy {host_copy:08X} is now {other_player(actors)}")
        now = next((int(a) for a, s in actors.items() if s.get("session_id") == session_id), None)
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
    """The party goes to `cell`: the host travels with coc (or `command`, e.g. "cow Tamriel 7 -8") and the
    client follows it (the party tether). Each game's arrival is waited for first: until then both still
    show the other player in the old cell, and that check alone passed at once."""
    before = {name: player_cell(name) for name in ("", "client")}
    console_on("", command or f"coc {cell}")
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
        show(observer, other_player)
        select_instance(observer)
        copy = other_player((read_status() or {}).get("actors", {}))
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


def scenario_copy_leaves_water_with_owner():
    """The host swims in the river and then stands on its bank (put there, as a script's MoveTo would): its copy
    on the client swims while it does and stops swimming with it, though a copy's own controller never gets the
    support that ends a swim (Character::sub_37946, docs/progress.md)."""
    both_go("river", RIVER_CELL)
    console_on("", "tcai")
    try:
        for name, spot in (("", RIVER_SPOT), ("client", RIVER_BANK)):
            for axis, value in zip("xyz", spot):
                console_on(name, f"player.setpos {axis} {value}")
        wait_for("[host] swims", lambda: water("").get("swimming"), 10)
        show("client", other_player)
        select_instance("client")
        copy = other_player((read_status() or {}).get("actors", {}))
        wait_for("[client] the host's character swims", lambda: water("client", copy).get("swimming"), 10)
        for axis, value in zip("xyz", (RIVER_BANK[0] + 150, RIVER_BANK[1], RIVER_BANK[2])):
            console_on("", f"player.setpos {axis} {value}")
        wait_for("[host] stands on the bank, not swimming", lambda: not water("").get("swimming"), 10)
        wait_for("[client] the host's character stops swimming with it", lambda: not water("client", copy).get("swimming"), 10)
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


def scenario_party_stacks_on_arrival():
    """The party arrives on one spot (a coc puts the host on the cell's marker, the tether the client where the
    host arrived): both stand on it, passing through each other on both games, and both games
    show the same view, though each player looked its own way before. The client walks off it (nothing
    else happens) and then into the host, which stops it: its collision is back. And again after travelling home."""
    home = False
    try:
        for cell in (INN, HOME_CELL):
            look_apart()
            both_go(cell)
            home = cell == HOME_CELL
            stacked(f"in {cell}, the players stand on one spot")
            same_view(f"In {cell}, both games show the same view")
            # Ahead, the way the arrival faces (into the room).
            at, heading = my_position("client"), local_player("client")["rotation"][2]
            walk_off("client", (at[0] + 400 * math.sin(heading), at[1] + 400 * math.cos(heading)), "walks off the stack")
            bumps_into_other("client")
            hold()
    finally:
        if not home:
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
        return len(cells) == 1 and all(other_player(actors_on(n)) for n in ("", "client"))
    wait_for(f"{where}: both in one cell, each game shows the other player", check, 30)


def follows(instance, cell_before):
    """The party tether takes this player along: its game arrives somewhere new without its own travel."""
    wait_for(f"[{instance or 'host'}] follows the party", lambda: player_cell(instance) not in (None, cell_before), 60)
    wait_for(f"[{instance or 'host'}] the arrival's fade is over", lambda: takes_input(instance), 15)


def scenario_party_uses_door():
    """The host walks out of the inn through its door and the client follows (the party tether); then the
    client leads back in and the host follows. Each time both games end up showing the other player and the
    same view (the follower faces the way the leader arrived), and outside the client shows the host's NPCs."""
    try:
        both_go(INN)
        client_before = player_cell("client")
        look_apart()
        travel("", lambda: activate_on("", INN_DOOR_INSIDE), player_cell(""))
        follows("client", client_before)
        players_together("outside the inn")
        wait_for("[client] the arrival's fade is over", lambda: takes_input("client"), 15)
        same_view("Outside the inn, both games show the same view")
        wait_on("client", "shows the host's NPCs outside", lambda actors: remote_npcs(actors), 30)
        host_before = player_cell("")
        look_apart()
        travel("client", lambda: activate_on("client", INN_DOOR_OUTSIDE), player_cell("client"))
        follows("", host_before)
        players_together("back in the inn")
        wait_for("[host] the arrival's fade is over", lambda: takes_input(""), 15)
        same_view("Back in the inn, both games show the same view")
        hold()
    finally:
        both_go(HOME_CELL)


def scenario_party_quick_travel():
    """The client walks out of the inn and straight back in while the host's game stalls (a hitch, a slow
    load), so the host gets both travels at once: it ends up where the party went last (the inn), and the
    party settles there instead of bouncing between the two (a follower's stop on the way must not lead).
    Loads here take ~0.2 s, too short for the race without the stall."""
    try:
        both_go(INN)
        inn = player_cell("")
        with game_stalled(""):
            activate_on("client", INN_DOOR_INSIDE)
            wait_for("[client] out of the inn", lambda: player_cell("client") not in (None, inn), 30)
            wait_for("[client] the arrival's fade is over", lambda: takes_input("client"), 15)
            activate_on("client", INN_DOOR_OUTSIDE)
            wait_for("[client] back in the inn", lambda: player_cell("client") == inn, 30)
        players_together("back in the inn")
        wait_for("[host] the arrival's fade is over", lambda: takes_input(""), 15)
        # Settled: for 5 s nobody travels again.
        end = time.monotonic() + 5
        while time.monotonic() < end:
            cells = {name or "host": player_cell(name) for name in ("", "client")}
            if set(cells.values()) != {inn}:
                raise TestFailure(f"the party moved on after settling in the inn ({inn}): {cells}")
            time.sleep(0.2)
        hold()
    finally:
        both_go(HOME_CELL)


def scenario_party_fast_travels():
    """The party fast travels from the river to Riverwood within one worldspace, twice: the host leads and
    the client follows (the party tether), then the client leads and the host follows. The host's trip
    passes time and the client keeps the host's time; each time both games end up showing the other
    player in Riverwood. Fast travel here is the console's FastTravel: it queues the same move, with the
    marker, as the map's."""
    def hour(instance):
        return game_state(instance).get("time", {}).get("hour")
    try:
        both_go("river", RIVER_CELL)
        before, client_before = hour(""), player_cell("client")
        travel("", lambda: console_on("", f"fasttravel {INN_DOOR_OUTSIDE:X}"), player_cell(""))
        passed = (hour("") - before) % 24
        if passed < 0.5:
            raise TestFailure(f"the host's trip passed {passed:.2f} h: fast travel passes time")
        follows("client", client_before)
        players_together("in Riverwood, the host leading")
        wait_for("[client] keeps the host's time", lambda: close(hour("client"), hour(""), 0.1), 10)
        try:
            wait_for("[client] keeps the host's days passed and date", same_calendar, 10)
        except TestFailure:
            raise TestFailure(calendars("[client] off the host's calendar after its trip"))

        both_go("river", RIVER_CELL)
        host_before = player_cell("")
        travel("client", lambda: console_on("client", f"fasttravel {INN_DOOR_OUTSIDE:X}"), player_cell("client"))
        follows("", host_before)
        players_together("in Riverwood, the client leading")
        wait_for("[client] keeps the host's time after its own trip", lambda: close(hour("client"), hour(""), 0.1), 10)
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
    equip_roundtrip("", "player", "client", other_player, "host's character")


def scenario_equip_client_player():
    """Client equips/unequips a sword; the host's copy of the client must follow."""
    equip_roundtrip("client", "player", "", other_player, "client's character")


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


HORSE = "00109E3D"   # WhiterunPlayerHorse (Skyrim.esm), a plain horse to put down
ACTIVATE_KEY = 0x12  # E: activates what the player looks at; riding, gets off


def actor_state(instance, form_id):
    select_instance(instance)
    return ((read_status() or {}).get("world") or {}).get(str(form_id)) or {}


def player_copy(instance):
    """That game's copy of the other player (its form id), or None."""
    select_instance(instance)
    world = (read_status() or {}).get("world") or {}
    return next((int(k) for k, v in world.items() if v.get("player") and k != "20"), None)


def scenario_guest_rides_host_horse():
    """Outdoors (no riding indoors), the client's player gets on the host's horse: the host gives the horse to
    the client's game to move, and the host's copy of the rider sits on it. Getting off gives it back to the host,
    the copy getting off with it."""
    both_go(OUTDOOR_CELL)
    horse = None
    try:
        rider = player_copy("")
        before = set(local_player("").get("local_actors", {}))
        console_on("", f"player.placeatme {HORSE} 1")
        horse = int(next(iter(wait_for("[host] the horse loaded",
                                       lambda: set(local_player("").get("local_actors", {})) - before, 15))))
        sid = wait_for("[host] the horse has a session id",
                       lambda: (s := host_npc_state(horse).get("session_id")) is not None and s >= 0 and (s,), 10)[0]
        copy = copy_of("client", sid)
        # A placed actor fades in, and nothing activates it until it has (TESNPC::Activate).
        wait_for("[client] the horse has faded in", lambda: actor_state("client", copy).get("mod", {}).get("fade_state") == 0, 10)
        activate_on("client", copy)
        wait_for("[client] its player rides the horse, now its game's to move",
                 lambda: actor_state("client", 0x14).get("placement", {}).get("riding")
                 and actor_state("client", copy).get("role") == "own", 15)
        # A player gets off once seated: until the mount animation ends the engine refuses (Actor::Dismount).
        wait_for("[client] its player is seated", lambda: not actor_state("client", 0x14).get("placement", {}).get("getting_on_off"), 10)
        wait_for("[host] the client's player sits on the horse, a copy here",
                 lambda: actor_state("", rider).get("placement", {}).get("riding")
                 and actor_state("", horse).get("role") == "copy"
                 and actor_state("", horse).get("placement", {}).get("ridden"), 10)
        # While riding, the horse and the rider agree with their copies (the rest of Riverwood is not this scenario's).
        riding = [p for p in check_desync() if f", sid {sid}]" in p or "[client 00000014," in p]
        if riding:
            raise TestFailure("while riding: " + " | ".join(riding))
        tap_key("client", ACTIVATE_KEY)
        wait_for("[client] its player got off, the horse a copy again",
                 lambda: not actor_state("client", 0x14).get("placement", {}).get("riding")
                 and actor_state("client", copy).get("role") == "copy", 15)
        wait_for("[host] the horse is its own again, the client's player off it",
                 lambda: actor_state("", horse).get("role") == "own"
                 and not actor_state("", rider).get("placement", {}).get("riding")
                 and not actor_state("", horse).get("placement", {}).get("ridden"), 10)
    finally:
        if actor_state("client", 0x14).get("placement", {}).get("riding"):
            tap_key("client", ACTIVATE_KEY)
            wait_for("[client] its player got off for the cleanup",
                     lambda: not actor_state("client", 0x14).get("placement", {}).get("riding"), 15)
        if horse is not None:
            console_on("", f"{horse:08X}.disable")
            console_on("", f"{horse:08X}.markfordelete")
        both_go(HOME_CELL)


# --- Hands: draw, cast, sheathe -------------------------------------------------------------

def copy_drawn(expected):
    return lambda actors: other_player(actors) is not None and actors[str(other_player(actors))].get("weapon_drawn") == expected


def draw_roundtrip(actor_instance, observer, label):
    """Player readies then sheathes a sword; the other game's copy must follow."""
    give_sword(actor_instance)
    show(observer, other_player)
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
    target = other_player(status.get("actors", {}))
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
    show(observer, other_player)
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
    show(observer, other_player)
    copy = other_player(actors_on(observer))
    console_on(actor_instance, f"player.moveto {COOKING_SPIT:08X}")
    wait_for(f"[{actor_instance or 'host'}] player is at the cooking spit", lambda: sit_state(actor_instance, 0x14) == IN_FURNITURE, 5)
    wait_for(f"[{observer or 'host'}] {label} is at the cooking spit", lambda: sit_state(observer, copy) == IN_FURNITURE, 5)
    hold()
    activate_on(actor_instance, COOKING_SPIT)
    wait_for(f"[{actor_instance or 'host'}] player leaves it", lambda: sit_state(actor_instance, 0x14) == 0, 5)
    wait_for(f"[{observer or 'host'}] {label} leaves it", lambda: sit_state(observer, copy) == 0, 5)
    if other_player(actors_on(observer)) != copy:
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
        target = other_player(actors)
        return target is not None and (actors[str(target)]["casting"][RIGHT_HAND] == spell) == expected
    return check


def cast_roundtrip(actor_instance, observer, label):
    """Player casts Flames (a held stream) by holding the attack button, and lets go; the other
    game's copy must cast, then stop."""
    give_spell(actor_instance, FLAMES)
    show(observer, other_player)
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
    health_roundtrip("", "player", lambda: local_player("").get("health"), "client", other_player, "host's character")


def scenario_health_client_player():
    """Client player loses/regains health; the host's copy of the client must match."""
    health_roundtrip("client", "player", lambda: local_player("client").get("health"), "", other_player,
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
    client_on_host = other_player(actors_on(""))
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
    client_on_host = other_player(actors_on(""))
    dead = lambda: host_world(npc).get("state", {}).get("dead")
    # In front of its head, as a player would face it (its heading: 0 north, clockwise).
    front = math.degrees(actors_on("client")[str(copy)]["rotation"][2])
    with GodMode():
        if weapon == "spell":
            give_spell("client", FIREBOLT)
            place_before("client", copy, SPELL_GAP, front)
        else:
            give_sword("client")
            place_before("client", copy, MELEE_GAP, front)
        draw("client")
        # A killing blow can start a kill move instead (KillMoveRandom): the kill move then kills it.
        in_kill_move = lambda: player_command("", {"cmd": "kill_move_check", "attacker": npc, "victim": npc})["in_kill_move"]
        for hit in range(1, 16):
            before = host_npc_health(npc)
            if weapon == "spell":
                cast_spell("client", copy, FIREBOLT)
                hit_lands(npc, before, f"hit {hit}", 3)
            else:
                attack("client")
                wait_for(f"[host] hit {hit} lands or starts a kill move",
                         lambda: (h := host_npc_health(npc)) is not None and h < before - 0.5 or in_kill_move(), 3)
            if dead() or in_kill_move():
                break
        wait_for("[host] the wolf dies", dead, 8)
    killer = wait_for("[host] killer recorded", lambda: host_world(npc).get("ai", {}).get("killer"), 5)
    if killer != client_on_host:
        raise TestFailure(f"host records killer {killer:08X}, expected the client's character {client_on_host:08X}")
    log(f"host's wolf killed by the client's character {killer:08X} after {hit} hits")
    wait_on("client", "the wolf's copy dies too", lambda actors: actors.get(str(copy), {}).get("dead"), 5)
    # The death plays out: the copy's body comes to rest where its owner's lies.
    rest = wait_for("[host] the wolf's body comes to rest",
                    comes_to_rest(lambda: host_world(npc)["placement"]["position"]), 10)
    wait_on("client", "the copy's body lies with it",
            lambda actors: distance((actors.get(str(copy)) or {}).get("actual") or [1e9] * 3, rest) <= POSITION_TOLERANCE, 3)
    hold()


KILL_MOVE_RANDOM = 50  # the engine's default chance of a kill move on a fatal hit (global KillMoveRandom)


KILL_MOVE_LAG = 0.6   # seconds: the client's half of a kill move may start this long after the host's
PAIR_TOLERANCE = 15   # units / degrees: the pair's layout in a copy's game against its owner's
BLOW_TOLERANCE = 0.35  # seconds: the blow into the kill move, on the client against the host


def scenario_host_kill_move():
    """The host's player kills a wolf with a kill move: on the client the host's character and the wolf's
    copy play it as one pair (the engine pairs them, laid out as on the host), a moment later and at the
    same pace, and the wolf dies with its owner. Leaves an animation trace (timeline, side-by-side frames)."""
    npc, _, copy = spawn_host_npc(WOLF, restrained=True)
    host_on_client = other_player(actors_on("client"))
    trace = AnimTrace("host-kill-move", {"": {npc: "wolf", 0x14: "player"},
                                         "client": {copy: "wolf", host_on_client: "player", 0x14: "watcher"}})
    with GodMode():
        console_on("", f"set KillMoveRandom to 100")
        try:
            give_sword("")
            draw("")
            console_on("", f"{npc:08X}.setrestrained 0")
            console_on("", f"{npc:08X}.startcombat player")
            wait_for("[host] the wolf fights the host's player", lambda: host_world(npc).get("ai", {}).get("combat_target") == 0x14, 8)
            # One hit from death, the host in front of its head; the client a few steps to the side, watching.
            console_on("", f"{npc:08X}.damageav health {host_world(npc)['values']['health'] - 3:.0f}")
            front = math.degrees(host_world(npc)["placement"]["rotation"][2])
            place_before("", npc, MELEE_GAP, front)
            place_before("client", copy, 300, front + 90)
            with trace:
                trace.mark("before the swing")
                press("", "right")
                try:
                    started = trace.wait_state("", "player", "kill_move", True, 3)
                finally:
                    release("", "right")
                if not (killer := trace.action_before(started)):
                    raise TestFailure("[host] the player is in a kill move with no action that started it")
                # The client: the host's character's copy takes the paired idle and the engine pairs the wolf's copy
                # with it (each in a kill move, together).
                pair = {who: trace.wait_state("client", who, "kill_move", True, 3) for who in ("player", "wolf")}
                halves = {"player": trace.action_before(pair["player"])}
                if not halves["player"] or halves["player"]["idle"] != killer["idle"] or abs(pair["player"]["f"] - pair["wolf"]["f"]) > 1:
                    raise TestFailure(f"the client's halves are not one pair: {halves['player']}, {pair}")
                lag = halves["player"]["time"] - killer["time"]
                trace.mark("the kill move starts", started["time"], halves["player"]["time"])
                owner_kill = trace.wait("", "wolf", "KillActor", 5)
                copy_kill = trace.wait("client", "wolf", "KillActor", 5)
                # Mid-animation, settled: the pair laid out on the client as on the host.
                host_layout = trace.layout("", "player", "wolf", owner_kill["time"] - 0.3)
                client_layout = trace.layout("client", "player", "wolf", copy_kill["time"] - 0.3)
                trace.mark("the blow lands", owner_kill["time"], copy_kill["time"])
                died = trace.wait_state("", "wolf", "life", 1, 3)
                copy_died = trace.wait_state("client", "wolf", "life", 1, 3)
                trace.mark("the wolf dies", died["time"], copy_died["time"])
                over = trace.wait_state("", "player", "kill_move", False, 5, after=started["time"])
                copy_over = trace.wait_state("client", "player", "kill_move", False, 5, after=pair["player"]["time"])
                trace.mark("the kill move is over", over["time"], copy_over["time"])
                rest = wait_for("[host] the wolf's body comes to rest",
                                comes_to_rest(lambda: host_world(npc)["placement"]["position"]), 10)
                wait_on("client", "the copy's body lies with it",
                        lambda actors: distance((actors.get(str(copy)) or {}).get("actual") or [1e9] * 3, rest) <= POSITION_TOLERANCE, 3)
                trace.mark("after")
        finally:
            console_on("", f"set KillMoveRandom to {KILL_MOVE_RANDOM}")
    log("\n" + trace.timeline())
    host_blow, client_blow = owner_kill["time"] - killer["time"], copy_kill["time"] - halves["player"]["time"]
    stood = copy_died["time"] - copy_kill["time"]
    log(f"the blow {host_blow * 1000:.0f}ms into the host's kill move, {client_blow * 1000:.0f}ms into the client's; "
        f"the wolf's copy dies {stood * 1000:.0f}ms after its blow")
    log(f"client's pair {lag * 1000:.0f}ms after the host's; layout (distance, bearing, heading) "
        f"host {tuple(round(v) for v in host_layout)}, client {tuple(round(v) for v in client_layout)}")
    # A kill cam that slows the host's clock slows the client's too (one clock for everyone).
    slowest = {game: min((e["state"]["time"] for e in trace.entries if e["kind"] == "game" and e["game"] == game), default=1.0)
               for game in ("", "client")}
    log(f"slowest clock: host {slowest['']:.2f}, client {slowest['client']:.2f}")
    if abs(slowest[""] - slowest["client"]) > 0.05:
        raise TestFailure(f"the host's clock slowed to {slowest['']:.2f}, the client's to {slowest['client']:.2f}")
    if abs(host_blow - client_blow) > BLOW_TOLERANCE or stood > BLOW_TOLERANCE:
        raise TestFailure(f"the client's kill move ran ahead of the host's: the blow {client_blow * 1000:.0f}ms in against "
                          f"{host_blow * 1000:.0f}ms, its wolf standing {stood * 1000:.0f}ms until its owner died")
    if lag > KILL_MOVE_LAG:
        raise TestFailure(f"the client's kill move started {lag * 1000:.0f}ms after the host's")
    if any(abs((a - b + 180) % 360 - 180) > PAIR_TOLERANCE for a, b in zip(host_layout, client_layout)):
        raise TestFailure(f"the client's pair is laid out {client_layout}, the host's {host_layout}")
    if pulled := [e for e in trace.entries if e["who"] == "watcher" and e["kind"] == "state"
                  and (e["state"].get("kill_move") or e["state"].get("bIsSynced"))]:
        raise TestFailure(f"the client's own player was pulled into the pair: {pulled[0]}")
    hold()


def scenario_client_kill_move():
    """The client's player kills the host's wolf with a kill move: on the host the client's character and the real
    wolf play it as one pair, a moment later and at the same pace, and the host records the client's character as
    the killer. The wolf's copy on the client dies with its owner. Leaves an animation trace."""
    npc, _, copy = spawn_host_npc(WOLF, restrained=True)
    client_on_host = other_player(actors_on(""))
    trace = AnimTrace("client-kill-move", {"": {npc: "wolf", client_on_host: "player", 0x14: "watcher"},
                                           "client": {copy: "wolf", 0x14: "player"}})
    with GodMode():
        console_on("client", "set KillMoveRandom to 100")
        try:
            give_sword("client")
            draw("client")
            # One hit from death, the client in front of its head; the host a few steps to the side, watching.
            console_on("", f"{npc:08X}.damageav health {host_world(npc)['values']['health'] - 3:.0f}")
            wait_on("client", "the wolf's copy shows it one hit from death",
                    lambda actors: (actors.get(str(copy), {}).get("health") or 100) < 5, 3)
            front = math.degrees(actors_on("client")[str(copy)]["rotation"][2])
            place_before("client", copy, MELEE_GAP, front)
            place_before("", npc, 300, front + 90)
            wait_for("[client] a blow now is a kill move", lambda: (check := player_command("client", {"cmd": "kill_move_check", "victim": copy}))
                     ["should_attack_kill"] and check["attack_target"] == copy, 3)
            with trace:
                trace.mark("before the swing")
                press("client", "right")
                try:
                    started = trace.wait_state("client", "player", "kill_move", True, 3)
                finally:
                    release("client", "right")
                if not (killer := trace.action_before(started)):
                    raise TestFailure("[client] the player is in a kill move with no action that started it")
                # The host: the client's character's copy takes the paired idle, the real wolf its half (each in a
                # kill move, together). The copy's action is recorded the frame after it plays.
                pair = {who: trace.wait_state("", who, "kill_move", True, 3) for who in ("player", "wolf")}
                halves = {"player": trace.wait("", "player", killer["event"], 3, kind="action", after=started["time"])}
                if halves["player"]["idle"] != killer["idle"] or abs(pair["player"]["f"] - pair["wolf"]["f"]) > 1:
                    raise TestFailure(f"the host's halves are not one pair: {halves['player']}, {pair}")
                lag = halves["player"]["time"] - killer["time"]
                trace.mark("the kill move starts", halves["player"]["time"], started["time"])
                copy_kill = trace.wait("client", "wolf", "KillActor", 5)
                owner_kill = trace.wait("", "wolf", "KillActor", 5)
                host_layout = trace.layout("", "player", "wolf", owner_kill["time"] - 0.3)
                client_layout = trace.layout("client", "player", "wolf", copy_kill["time"] - 0.3)
                trace.mark("the blow lands", owner_kill["time"], copy_kill["time"])
                died = trace.wait_state("", "wolf", "life", 1, 3)
                copy_died = trace.wait_state("client", "wolf", "life", 1, 3)
                trace.mark("the wolf dies", died["time"], copy_died["time"])
                rest = wait_for("[host] the wolf's body comes to rest",
                                comes_to_rest(lambda: host_world(npc)["placement"]["position"]), 10)
                wait_on("client", "the copy's body lies with it",
                        lambda actors: distance((actors.get(str(copy)) or {}).get("actual") or [1e9] * 3, rest) <= POSITION_TOLERANCE, 3)
                trace.mark("after")
        finally:
            console_on("client", f"set KillMoveRandom to {KILL_MOVE_RANDOM}")
    log("\n" + trace.timeline())
    recorded = host_world(npc).get("ai", {}).get("killer")
    client_blow, host_blow = copy_kill["time"] - killer["time"], owner_kill["time"] - halves["player"]["time"]
    stood = copy_died["time"] - copy_kill["time"]
    log(f"the blow {client_blow * 1000:.0f}ms into the client's kill move, {host_blow * 1000:.0f}ms into the host's; "
        f"the wolf's copy dies {stood * 1000:.0f}ms after its blow (its owner's death a network delay later)")
    log(f"host's pair {lag * 1000:.0f}ms after the client's; layout (distance, bearing, heading) "
        f"client {tuple(round(v) for v in client_layout)}, host {tuple(round(v) for v in host_layout)}")
    if recorded != client_on_host:
        raise TestFailure(f"host records killer {recorded or 0:08X}, expected the client's character {client_on_host:08X}")
    if abs(host_blow - client_blow) > BLOW_TOLERANCE:
        raise TestFailure(f"the host's kill move ran at another pace: the blow {host_blow * 1000:.0f}ms in against "
                          f"the client's {client_blow * 1000:.0f}ms")
    if lag > KILL_MOVE_LAG:
        raise TestFailure(f"the host's kill move started {lag * 1000:.0f}ms after the client's")
    if stood > KILL_MOVE_LAG + BLOW_TOLERANCE:
        raise TestFailure(f"the wolf's copy stood {stood * 1000:.0f}ms after its blow")
    if any(abs((a - b + 180) % 360 - 180) > PAIR_TOLERANCE for a, b in zip(host_layout, client_layout)):
        raise TestFailure(f"the host's pair is laid out {host_layout}, the client's {client_layout}")
    if pulled := [e for e in trace.entries if e["who"] == "watcher" and e["kind"] == "state"
                  and (e["state"].get("kill_move") or e["state"].get("bIsSynced"))]:
        raise TestFailure(f"the host's own player was pulled into the pair: {pulled[0]}")
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


OAKFLESH = 0x5AD5C  # 60 s armor spell with hit visuals (AlterationArmorFXS and its art)


def active_spells(instance, actor):
    """The spells of the actor's active effects in that game."""
    select_instance(instance)
    effects = (((read_status() or {}).get("world") or {}).get(str(actor)) or {}).get("effects") or []
    return {e.get("spell") for e in effects}


def scenario_copy_made_later_shows_effects():
    """The host's staged Vilkas casts Oakflesh on himself while the client is away. The client joins again: the
    copy it makes then shows the visuals from its spawn, nothing having changed on the host since (the owner sends
    its effects only when the engine starts or ends one). A dispel ends them on both."""
    try:
        stage_host_npc(VILKAS)
        session_id = host_npc_state(VILKAS)["session_id"]
        disconnect_client()
        wait_on("", "the client's character leaves the host's game", lambda actors: other_player(actors) is None)
        console_on("", f"{VILKAS:X}.cast {OAKFLESH:08X} {VILKAS:X} left")
        shown = wait_for("[host] Vilkas shows Oakflesh", lambda: visuals("", VILKAS), 3)
        rejoin_client()
        copy = copy_of("client", session_id)
        if OAKFLESH not in active_spells("", VILKAS):
            raise TestFailure(f"[host] Vilkas's Oakflesh ended before the client came back: {active_spells('', VILKAS)}")
        try:
            wait_for("[client] the copy made on joining shows it", lambda: shown <= visuals("client", copy), 3)
        except TestFailure:
            raise TestFailure(f"[client] its copy of Vilkas shows {visuals('client', copy)}, not the host's {shown}")
        console_on("", f"{VILKAS:X}.dispelallspells")
        wait_for("[host] Oakflesh is dispelled", lambda: not (shown & visuals("", VILKAS)), 3)
        wait_for("[client] the copy's visuals end too", lambda: not (shown & visuals("client", copy)), 3)
    finally:
        console_on("", f"{VILKAS:X}.dispelallspells")
        release_host_npc(VILKAS)


# Gift of Charity's AlchFortifyPersuasion (0xD6947) has no FX Persist: its hit shader plays one pass of its hour.
CHARITY_SHADER = 0x4E220  # HealMysticFXS


def shown_visuals(instance, form_id):
    """The shaders and art in that game's list on the actor, the finished ones still fading out too: a one-shot
    hit shader is finished from its start ("visuals" leaves those out)."""
    select_instance(instance)
    return set(((((read_status() or {}).get("world") or {}).get(str(form_id)) or {}).get("mod") or {}).get("visuals_shown") or [])


def scenario_copy_flashes_one_shot_effects():
    """An effect whose visuals play once (no FX Persist) flashes on the copy as on its owner: the host's staged
    Vilkas takes Gift of Charity (a beggar's blessing, an hour long). Its shader shows on the host's Vilkas and on
    the client's copy, then ends on both by itself while the blessing still runs (the owner sends the flash when its
    engine starts it)."""
    try:
        stage_host_npc(VILKAS)
        copy = copy_of("client", host_npc_state(VILKAS)["session_id"])
        console_on("", f"{VILKAS:X}.cast {GIFT_OF_CHARITY:08X} {VILKAS:X} left")
        wait_for("[host] Vilkas flashes the blessing", lambda: CHARITY_SHADER in shown_visuals("", VILKAS), 3)
        wait_for("[client] the copy flashes it too", lambda: CHARITY_SHADER in shown_visuals("client", copy), 3)
        wait_for("[host] the flash ends", lambda: CHARITY_SHADER not in shown_visuals("", VILKAS), 15)
        wait_for("[client] the copy's flash ends by itself", lambda: CHARITY_SHADER not in shown_visuals("client", copy), 15)
        if GIFT_OF_CHARITY not in active_spells("", VILKAS):
            raise TestFailure(f"[host] the blessing ended with its flash: {active_spells('', VILKAS)}")
    finally:
        console_on("", f"{VILKAS:X}.dispelallspells")
        release_host_npc(VILKAS)


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


DOOR_OPEN, DOOR_OPENING, DOOR_CLOSED, DOOR_CLOSING = 1, 2, 3, 4  # TESObjectREFR::OpenState


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


# Kolbjorn Barrow's pressure plates (DLC2dunKolbjornPressureTileScript): `akActivator == playerRef`, the
# player stored in a script variable at OnInit; the plate's state is Waiting, inTrigger while the player stands on it.
KOLBJORN_CELL, KOLBJORN_PLATE, PLATE_SCRIPT = "DLC2KolbjornBarrow01", 0x0401DC12, "DLC2dunKolbjornPressureTileScript"


def plate_state(instance):
    select_instance(instance)
    return ((rpc({"cmd": "script_state", "object": KOLBJORN_PLATE, "script": PLATE_SCRIPT}) or {}).get("state") or "").lower()


def scenario_plate_counts_client():
    """The client walks across a pressure plate whose unpatched vanilla script only reacts to the player:
    on the host its character counts as the player (docs/design/party-model.md, "A client's actions on
    the host", rule 1), so the host's plate goes down under it and comes up again behind it."""
    try:
        both_go(KOLBJORN_CELL)
        for name in ("", "client"):
            if plate_state(name) != "waiting":
                raise TestFailure(f"[{name or 'host'}] plate {KOLBJORN_PLATE:X} is '{plate_state(name)}', not Waiting, to begin with")
        place_before("client", KOLBJORN_PLATE, 150)
        press("client", "forward")
        try:
            wait_for("[host] the plate is down under the client's character", lambda: plate_state("") == "intrigger", 10)
            wait_for("[host] the plate is up again behind it", lambda: plate_state("") == "waiting", 10)
        finally:
            release("client", "forward")
    finally:
        both_go(HOME_CELL)


def ref_enabled(instance, ref):
    select_instance(instance)
    return (rpc({"cmd": "ref_enabled", "object": ref}) or {}).get("enabled")


def item_count(instance, holder, item):
    select_instance(instance)
    return (rpc({"cmd": "item_count", "object": holder, "item": item}) or {}).get("count")


def client_on_host():
    return wait_on("", "shows the client's character", other_player)


# Rebel's Cairn's sword in the stone: the box (dunRebelsCairnActivator: `obj == Game.GetPlayer()`) activates
# the manager (dunRebelsCairnSwordInStoneManager), which asks Game.GetPlayer() for Red Eagle's sword, takes it
# and opens the secret door; the sword in the stone gives it back and closes the door. The door's Open and
# Close sequences take seconds and only play while a camera sees the door: the players watch it.
REBELS_CAIRN_CELL, SWORD_BOX, SWORD_IN_STONE, SECRET_DOOR, RED_EAGLE_SWORD = "RebelsCairn01", 0xA05C0, 0xA05AC, 0xC149D, 0x9FD50
HALLDIR_STAFF = 0xAB704  # worth more than the baseline's axe: the engine's pick when it chooses a weapon for an NPC


def copy_equips(instance, copy, since):
    """The items that game equipped on the copy itself since that trail number (EquipManager's "Copy X equip Y: ours")."""
    lines, _ = log_trail(instance, since)
    prefix = f"Copy {copy:X} equip "
    return {int(line["text"][len(prefix):].split(":")[0], 16) for line in lines
            if line["text"].startswith(prefix) and line["text"].endswith(": ours")}


def scenario_sword_box_asks_client():
    """The client puts Red Eagle's sword in the stone and takes it back. On the host the box's check
    counts its character as the player (rule 1), and the script the box starts asks that character for
    the sword and hands it back to it (rule 2, docs/design/party-model.md): the host's player, who has
    none, opens the door all the same and never gets the sword. The client's own box runs nothing
    (world-state sync step 5): its sword goes once and comes back once, its door opens from the host's, and
    it sees the host's script swap the box and the sword in the stone (step 6, enabled state). The client carries
    a staff worth more than its axe: when the script takes the sword from the host's copy, the engine picks a
    weapon for the copy (an NPC losing an item), which a copy refuses: it keeps its owner's hands."""
    console_on("client", f"player.additem {HALLDIR_STAFF:X} 1")
    try:
        both_go(REBELS_CAIRN_CELL)
        copy = client_on_host()
        _, since = log_trail("")
        for name in ("", "client"):
            if door_state(name, SECRET_DOOR) != DOOR_CLOSED:
                raise TestFailure(f"[{name or 'host'}] secret door {SECRET_DOOR:X} is not closed to begin with")
        if item_count("", 0x14, RED_EAGLE_SWORD):
            raise TestFailure("[host] the host's player holds Red Eagle's sword to begin with")
        for name in ("", "client"):
            place_before(name, SECRET_DOOR, 400)
        console_on("client", f"player.additem {RED_EAGLE_SWORD:08X} 1")
        wait_for("[host] the client's character holds the sword", lambda: item_count("", copy, RED_EAGLE_SWORD) == 1, 5)
        activate_on("client", SWORD_BOX)
        for name in ("", "client"):
            wait_for(f"[{name or 'host'}] secret door {SECRET_DOOR:X} opens", lambda name=name: door_state(name, SECRET_DOOR) == DOOR_OPEN, 15)
        wait_for("[client] the player gave up the sword", lambda: item_count("client", 0x14, RED_EAGLE_SWORD) == 0, 5)
        if item_count("", 0x14, RED_EAGLE_SWORD):
            raise TestFailure("[host] the host's player holds a sword it never had")
        wait_for("[host] the copy gave up the sword", lambda: item_count("", copy, RED_EAGLE_SWORD) == 0, 5)
        if HALLDIR_STAFF in copy_equips("", copy, since):
            raise TestFailure(f"[host] the copy equipped the staff its owner never held when the sword was taken from it")
        # The host's script swaps the box for the sword in the stone; the client sees it from the host (step 6).
        for name in ("", "client"):
            wait_for(f"[{name or 'host'}] the sword stands in the stone, the box is gone",
                     lambda name=name: ref_enabled(name, SWORD_IN_STONE) and ref_enabled(name, SWORD_BOX) is False, 5)
        activate_on("client", SWORD_IN_STONE)
        for name in ("", "client"):
            wait_for(f"[{name or 'host'}] secret door {SECRET_DOOR:X} closes", lambda name=name: door_state(name, SECRET_DOOR) == DOOR_CLOSED, 15)
        wait_for("[client] the player has the sword back", lambda: item_count("client", 0x14, RED_EAGLE_SWORD) == 1, 5)
        for name in ("", "client"):
            wait_for(f"[{name or 'host'}] the box is back, the stone empty",
                     lambda name=name: ref_enabled(name, SWORD_BOX) and ref_enabled(name, SWORD_IN_STONE) is False, 5)
        if item_count("", 0x14, RED_EAGLE_SWORD):
            raise TestFailure("[host] the sword went to the host's player, not the client's character")
        if (swords := item_count("client", 0x14, RED_EAGLE_SWORD)) != 1:
            raise TestFailure(f"[client] the player holds {swords} swords: the box's script ran more than once")
    finally:
        if swords := item_count("client", 0x14, RED_EAGLE_SWORD):
            console_on("client", f"player.removeitem {RED_EAGLE_SWORD:08X} {swords}")
        console_on("client", f"player.removeitem {HALLDIR_STAFF:X} 1")
        both_go(HOME_CELL)


# Soljund's Sinkhole: the boss trigger (dunSoljundsBossSCRIPT: `obj == game.getPlayer()`, once per cell attach)
# goes to `polling`, where it stays, and registers for an update; every seventh update the fire statue casts
# Firebolt at game.getPlayer().
SOLJUND_CELL, SOLJUND_TRIGGER, SOLJUND_SCRIPT = "SoljundsSinkhole01", 0x6AEBE, "dunSoljundsBossSCRIPT"
SOLJUND_FIRE, SOLJUND_FIRE_DAMAGE = 0x12FD0, 0x12F03  # the statue's fireSpell (Firebolt) and its fire damage effect


def boss_trigger_state(instance):
    select_instance(instance)
    return ((rpc({"cmd": "script_state", "object": SOLJUND_TRIGGER, "script": SOLJUND_SCRIPT}) or {}).get("state") or "").lower()


def fire_hits(since):
    """Whom the host's game saw the statue's fire hit (harness engine_events), from event `since` on."""
    events, _ = engine_events("", since)
    return {e["target"] for e in events if e.get("kind") == "hit" and e.get("source") == SOLJUND_FIRE}


def scenario_statue_fires_at_client():
    """The client walks into Soljund's boss trigger: on the host its character counts as the player
    (rule 1), and the updates the trigger registers for run for it (rule 2), so the host's fire statue
    shoots at the client's character, not at the host's player, who stands out of the way. Checked by the
    host's own hits; the client's own trigger runs nothing (world-state sync step 5), and the host's hit
    burns the client's player through its game. No god mode: it keeps the hit from landing at all."""
    try:
        both_go(SOLJUND_CELL)
        copy = client_on_host()
        _, since = engine_events("")
        _, client_since = engine_events("client")
        place_before("client", SOLJUND_TRIGGER, 200)
        press("client", "forward")
        try:
            wait_for("[host] the trigger is polling", lambda: boss_trigger_state("") == "polling", 10)
        finally:
            release("client", "forward")
        wait_for("[host] the statue's fire hits the client's character", lambda: copy in fire_hits(since), 15)
        if 0x14 in fire_hits(since):
            raise TestFailure("[host] the statue's fire hit the host's player too")
        if boss_trigger_state("client") == "polling":
            raise TestFailure("[client] its own trigger ran (world-state sync step 5): its statue would fire too")
        # Its owner's game applies the host's hit (MagicTarget: the host's world on a guest's character).
        wait_for("[client] the host's fire burns the player", lambda: any(
            e.get("kind") == "magic_effect_apply" and e.get("target") == 0x14 and e.get("effect") == SOLJUND_FIRE_DAMAGE
            for e in engine_events("client", client_since)[0]), 5)
    finally:
        for name in ("", "client"):
            console_on(name, "player.restoreav health 1000")
        both_go(HOME_CELL)


# The claw door of the E3 demo's Bleak Falls Barrow (no quest, no traps): three rings
# (HallofStoriesDiskScript: `actorRef == game.GetPlayer()`, state positionN, three turns go round) and the
# keyhole (HallofStoriesKeyholeScript: `actorRef == game.GetPlayer()`, then
# `game.GetPlayer().GetItemCount(claw)`; busy for three seconds, back to mainState if a ring is wrong).
# Rings only finish turning while a camera sees them: the players stand before the door (east of it).
CLAW_DOOR_CELL, CLAW_KEYHOLE, CLAW = "E3demoBleakFallsBarrow", 0x847D6, 0x999E7
CLAW_RINGS = {0x847D3: "position01", 0x847D4: "position03", 0x847D5: "position02"}  # ring: its solved state


def claw_door_state(instance, ref):
    select_instance(instance)
    script = "HallofStoriesKeyholeScript" if ref == CLAW_KEYHOLE else "HallofStoriesDiskScript"
    return ((rpc({"cmd": "script_state", "object": ref, "script": script}) or {}).get("state") or "").lower()


def before_claw_door():
    both_go(CLAW_DOOR_CELL)
    place_before("", CLAW_KEYHOLE, 450, bearing=90)
    place_before("client", CLAW_KEYHOLE, 300, bearing=90)


def ring_looks_alike(ring):
    """The ring's pose and record on the client are the host's (world_state's pose: its nodes' rotations)."""
    return not reference_differences(world_state("", [ring])[ring], world_state("client", [ring])[ring])


def scenario_ring_turns_for_client():
    """The client turns a ring of a claw door three times round. On the host the ring's check counts
    its character as the player (rule 1, docs/design/party-model.md), so the host's ring turns with it.
    The client runs no world script (it never ran the ring's: its script has no state); its ring turns as
    the host's does, from the host's animations."""
    ring = next(iter(CLAW_RINGS))
    try:
        before_claw_door()
        start = claw_door_state("", ring)
        if not start.startswith("position"):
            raise TestFailure(f"[host] ring {ring:X} is '{start}' to begin with")
        wait_for(f"[client] ring {ring:X} looks as the host's to begin with", lambda: ring_looks_alike(ring), 5)
        position = int(start[-1])
        for _ in range(3):
            position = position % 3 + 1
            activate_on("client", ring)
            wait_for(f"[host] ring {ring:X} turns to position {position}",
                     lambda: claw_door_state("", ring) == f"position0{position}", 10)
            wait_for(f"[client] ring {ring:X} turned as the host's", lambda: ring_looks_alike(ring), 10)
            if state := claw_door_state("client", ring):
                raise TestFailure(f"[client] ring {ring:X}'s own script ran: it is '{state}'")
    finally:
        both_go(HOME_CELL)


def scenario_late_join_gets_ring_pose():
    """The host turns a claw door's ring while the client is away; the client rejoins at the door. Its ring was
    never animated there, so only the cell's state (the ring's graph, kGraph) can show the host's turn."""
    ring = next(iter(CLAW_RINGS))
    try:
        before_claw_door()
        wait_for(f"[client] ring {ring:X} looks as the host's to begin with", lambda: ring_looks_alike(ring), 5)
        start = claw_door_state("", ring)
        disconnect_client()
        console_on("", f"{ring:X}.activate player")
        turned = f"position0{int(start[-1]) % 3 + 1}"
        wait_for(f"[host] ring {ring:X} turns to {turned}", lambda: claw_door_state("", ring) == turned, 10)
        rejoin_client()
        wait_for(f"[client] ring {ring:X} looks as the host's turned one", lambda: ring_looks_alike(ring), 15)
    finally:
        # Back to where it was: two more turns.
        for _ in range(2):
            if claw_door_state("", ring) != start:
                before = claw_door_state("", ring)
                console_on("", f"{ring:X}.activate player")
                wait_for(f"[host] ring {ring:X} turns on", lambda: claw_door_state("", ring) != before, 10)
        both_go(HOME_CELL)


def scenario_keyhole_asks_client():
    """The client, holding the claw, puts it in a claw door whose rings are wrong. On the host the
    keyhole counts its character as the player (rule 1) and asks it for the claw (rule 2): the host's
    player has none, yet the keyhole takes the try (busy, then back) and the door stays shut."""
    try:
        before_claw_door()
        for name in ("", "client"):
            states = {ring: claw_door_state(name, ring) for ring in CLAW_RINGS}
            if all(states[ring] == solved for ring, solved in CLAW_RINGS.items()):
                raise TestFailure(f"[{name or 'host'}] every ring is solved to begin with: the door would open for good")
            if claw_door_state(name, CLAW_KEYHOLE) != "mainstate":
                raise TestFailure(f"[{name or 'host'}] keyhole {CLAW_KEYHOLE:X} is '{claw_door_state(name, CLAW_KEYHOLE)}' to begin with")
        if item_count("", 0x14, CLAW):
            raise TestFailure("[host] the host's player holds the claw to begin with")
        copy = client_on_host()
        console_on("client", f"player.additem {CLAW:08X} 1")
        wait_for("[host] the client's character holds the claw", lambda: item_count("", copy, CLAW) == 1, 5)
        activate_on("client", CLAW_KEYHOLE)
        wait_for("[host] the keyhole takes the claw", lambda: claw_door_state("", CLAW_KEYHOLE) == "busy", 5)
        wait_for("[host] the keyhole gives up: a ring is wrong", lambda: claw_door_state("", CLAW_KEYHOLE) == "mainstate", 10)
    finally:
        if claws := item_count("client", 0x14, CLAW):
            console_on("client", f"player.removeitem {CLAW:08X} {claws}")
        both_go(HOME_CELL)


# Volunruud's relief door (dunVolunruudDoorSCRIPT, a vanilla script with a stored player): `player = game.getPlayer()`
# at onLoad, then on activation (dunVolunruudQST at stage 10 or later) `player.getItemCount(relic)` and
# `player.RemoveItem(relic)` for each of its two relics; one in, its static shows it in the relief and the door says
# the other is missing (a message box).
VOLUNRUUD_CELL, VOLUNRUUD_DOOR, VOLUNRUUD_SCRIPT = "Volunruud01", 0xCDDA4, "dunVolunruudDoorSCRIPT"
VOLUNRUUD_QUEST, VOLUNRUUD_QUEST_ID = "dunVolunruudQST", 0x8ADFA
VOLUNRUUD_AXE, VOLUNRUUD_AXE_SHOWN = 0x8ADFB, 0x8FFDB  # relic01base and relic01static
MESSAGE_BOX_OK = (636, 389)  # a one-line message box's one button, in window pixels (as LEAVE_IT_BUTTON)


def close_message_box(instance):
    """The player points at the box's one button and clicks it (vanilla message boxes take only the mouse, and a
    button takes a click once the cursor came onto it)."""
    move_cursor(instance, (MESSAGE_BOX_OK[0], MESSAGE_BOX_OK[1] + 80))
    move_cursor(instance, MESSAGE_BOX_OK)
    click(instance)
    wait_for(f"[{instance or 'host'}] the message box is closed", lambda: "MessageBoxMenu" not in menus(instance), 5)


# Dragonborn's QA shout button in QASmoke (DLC2QALearnShouts, a vanilla script: on activation `akActivator ==
# Game.getplayer()`, then game.teachword and game.unlockword for each word of its list, DLC2QAShoutList's twelve).
QA_SHOUT_BUTTON, QA_SHOUT_SCRIPT = 0x0402A891, "DLC2QALearnShouts"
QA_SHOUT_WORDS = [0x040179D9, 0x040179DA, 0x040179DB, 0x040200C2, 0x040200C3, 0x040200C4,
                  0x0401DF93, 0x0401DF94, 0x0401DF95, 0x040200E4, 0x040200E5, 0x040200E6]


def knows_word(instance, word):
    return player_command(instance, {"cmd": "form", "form": word}).get("known")


def scenario_words_reach_client():
    """Forms as a native's arguments, on the guest's game: the client presses the QA shout button. The host's game
    runs its script for the client (party-model.md, rule 2), and Game.TeachWord and Game.UnlockWord act on this
    game's player (NativeReach), so the client's game runs them, each word carried by its GameId: the client learns
    the twelve words, the host's player none."""
    def reset():
        for name in ("", "client"):
            for word in QA_SHOUT_WORDS:
                player_command(name, {"cmd": "form_known", "form": word, "known": False})
        # The button as a first press finds it (a press closes it for good).
        player_command("", {"cmd": "script_var", "object": QA_SHOUT_BUTTON, "script": QA_SHOUT_SCRIPT, "var": "currentIndex", "value": 0})
        player_command("", {"cmd": "script_state", "object": QA_SHOUT_BUTTON, "script": QA_SHOUT_SCRIPT, "state": "open"})
        for name in ("", "client"):
            console_on(name, f"{QA_SHOUT_BUTTON:X}.sae Open")
    reset()
    try:
        place_before("client", QA_SHOUT_BUTTON, 110)
        activate_on("client", QA_SHOUT_BUTTON)
        wait_for("[client] learns the twelve words", lambda: all(knows_word("client", w) for w in QA_SHOUT_WORDS), 15)
        frame = game_frame("")
        def host_stays():
            if learned := [f"{w:08X}" for w in QA_SHOUT_WORDS if knows_word("", w)]:
                raise TestFailure(f"[host] its player learned the client's words {learned}")
            return game_frame("") > frame + 60
        wait_for("[host] learns none", host_stays, 10)
    finally:
        reset()


# --- Cutscenes: the whole party sees them (docs/design/cutscenes.md) -------------------------------------------

ALL_CONTROLS = 0xFFFFFFFF  # ControlMap's enabled controls with nothing taken
FADE_TO_BLACK_IMOD = 0xF756D  # FadeToBlackImod (IMAD), Harkon's bite's fade
# Game.DisablePlayerControls(movement, fighting, cam switch, looking, sneaking, menu, activate, journal tabs, POV type)
TAKE_CONTROLS = [True, True, False, False, False, True, True, False, 0]
GIVE_CONTROLS = [True] * 8 + [0]


def screen(instance):
    """What a cutscene does to that game's screen: its enabled controls, image-space modifiers and camera shake."""
    return player_command(instance, {"cmd": "screen"})


def script_call(instance, klass, function, args=(), on=None):
    """That game queues the call as a quest's script would (no player's action behind it)."""
    request = {"cmd": "papyrus_call", "class": klass, "function": function, "args": list(args)}
    if on is not None:
        request["self"] = on
    player_command(instance, request)


def player_script_call(instance, klass, function, args=(), on=None):
    """That game queues the call as a script its own player's action started (a lever, fishing, a picked line)."""
    request = {"cmd": "papyrus_call", "class": klass, "function": function, "args": list(args), "player_action": True}
    if on is not None:
        request["self"] = on
    player_command(instance, request)


def screen_marker(instance, other):
    """A party call after the calls before it (a shake, from a script with no player behind it): once `other`
    shakes, any of the earlier calls sent to it has arrived and run too (one session, in order)."""
    wait_for(f"[{other or 'host'}] no shake before the marker", lambda: screen(other)["shake"]["seconds"] <= 0, 5)
    script_call(instance, "Game", "ShakeCamera", [None, 0.5, 2.0])
    wait_for(f"[{other or 'host'}] the marker's shake arrives", lambda: screen(other)["shake"]["seconds"] > 0, 5)


def party_controls(state, what):
    """Both games' controls are `state` (a ControlMap mask; None: anything but all on)."""
    for name in ("", "client"):
        wait_for(f"[{name or 'host'}] controls {what}",
                 lambda name=name: (screen(name)["controls"] != ALL_CONTROLS) if state is None else screen(name)["controls"] == state, 5)


def scenario_cutscene_fade_reaches_party():
    """In the host's cutscene (controls taken) its script fades to black (ImageSpaceModifier.Apply on FadeToBlackImod)
    and shakes the camera: the client's screen fades and shakes too, and clears with the host's; the controls come
    back with the host's."""
    try:
        script_call("", "Game", "DisablePlayerControls", TAKE_CONTROLS)
        party_controls(None, "taken by the host's cutscene")
        script_call("", "ImageSpaceModifier", "Apply", [1.0], on=FADE_TO_BLACK_IMOD)
        script_call("", "Game", "ShakeCamera", [None, 0.5, 2.0])
        for name in ("", "client"):
            wait_for(f"[{name or 'host'}] fades to black", lambda name=name: FADE_TO_BLACK_IMOD in screen(name)["imods"], 5)
            wait_for(f"[{name or 'host'}] shakes", lambda name=name: screen(name)["shake"]["seconds"] > 0, 5)
        script_call("", "ImageSpaceModifier", "Remove", [], on=FADE_TO_BLACK_IMOD)
        for name in ("", "client"):
            wait_for(f"[{name or 'host'}] the fade clears", lambda name=name: FADE_TO_BLACK_IMOD not in screen(name)["imods"], 5)
        script_call("", "Game", "EnablePlayerControls", GIVE_CONTROLS)
        party_controls(ALL_CONTROLS, "back")
    finally:
        script_call("", "ImageSpaceModifier", "Remove", [], on=FADE_TO_BLACK_IMOD)
        script_call("", "Game", "EnablePlayerControls", GIVE_CONTROLS)


def scenario_guest_cutscene_reaches_host():
    """A script with no player's action behind it on the client's game takes its player's controls and fades to
    black: the host's controls and screen follow, and come back with the client's. (Its player's own script would be
    its moment alone: player-moment-stays-theirs.)"""
    try:
        script_call("client", "Game", "DisablePlayerControls", TAKE_CONTROLS)
        party_controls(None, "taken by the client's cutscene")
        script_call("client", "ImageSpaceModifier", "Apply", [1.0], on=FADE_TO_BLACK_IMOD)
        wait_for("[host] fades to black", lambda: FADE_TO_BLACK_IMOD in screen("")["imods"], 5)
        script_call("client", "ImageSpaceModifier", "Remove", [], on=FADE_TO_BLACK_IMOD)
        wait_for("[host] the fade clears", lambda: FADE_TO_BLACK_IMOD not in screen("")["imods"], 5)
        script_call("client", "Game", "EnablePlayerControls", GIVE_CONTROLS)
        party_controls(ALL_CONTROLS, "back with the client's")
    finally:
        script_call("client", "ImageSpaceModifier", "Remove", [], on=FADE_TO_BLACK_IMOD)
        script_call("client", "Game", "EnablePlayerControls", GIVE_CONTROLS)


MONK_SCENE_QUEST = 0x76F39  # MG04MonkSceneQuest: stage 0 applies its intro, then its loop; stage 200 pops to its outro
MONK_INTRO_IMOD, MONK_LOOP_IMOD, MONK_OUTRO_IMOD = 0xEF960, 0xEF95F, 0xEF961


def scenario_player_moment_stays_theirs():
    """A script one player's action started (a lever, fishing, the lute) takes that player's controls and fades their
    screen: that game's alone, the other's controls and screen stay as they are (PartyNatives; docs/design/cutscenes.md,
    "Whose moment"). The host's player first, then the client's."""
    for player, other in (("", "client"), ("client", "")):
        try:
            player_script_call(player, "Game", "DisablePlayerControls", TAKE_CONTROLS)
            player_script_call(player, "ImageSpaceModifier", "Apply", [1.0], on=FADE_TO_BLACK_IMOD)
            wait_for(f"[{player or 'host'}] its own moment takes its controls and fades",
                     lambda: screen(player)["controls"] != ALL_CONTROLS and FADE_TO_BLACK_IMOD in screen(player)["imods"], 5)
            screen_marker(player, other)
            if screen(other)["controls"] != ALL_CONTROLS:
                raise TestFailure(f"[{other or 'host'}] its controls are {screen(other)['controls']:X}: another player's moment took them")
            if FADE_TO_BLACK_IMOD in screen(other)["imods"]:
                raise TestFailure(f"[{other or 'host'}] fades with another player's moment")
            player_script_call(player, "ImageSpaceModifier", "Remove", [], on=FADE_TO_BLACK_IMOD)
            player_script_call(player, "Game", "EnablePlayerControls", GIVE_CONTROLS)
            wait_for(f"[{player or 'host'}] its controls come back", lambda: screen(player)["controls"] == ALL_CONTROLS, 5)
        finally:
            # Each game's own, never sent: a party give-back could land after the next direction's moment.
            for name in ("", "client"):
                player_script_call(name, "ImageSpaceModifier", "Remove", [], on=FADE_TO_BLACK_IMOD)
                player_script_call(name, "Game", "EnablePlayerControls", GIVE_CONTROLS)


def scenario_player_stage_cutscene_reaches_party():
    """A player's own script shaking the camera shakes that game only; a quest stage that player's script sets is the
    story, and its fragment's screen calls reach the client too (MG04MonkSceneQuest stage 200: its loop pops to its
    outro, ImageSpaceModifier.PopTo)."""
    for name in ("", "client"):
        wait_for(f"[{name or 'host'}] no shake yet", lambda name=name: screen(name)["shake"]["seconds"] <= 0, 5)
    try:
        player_script_call("", "Game", "ShakeCamera", [None, 0.5, 2.0])
        wait_for("[host] its own script shakes it", lambda: screen("")["shake"]["seconds"] > 0, 5)
        script_call("", "ImageSpaceModifier", "Apply", [1.0], on=FADE_TO_BLACK_IMOD)  # the marker: a party call after it
        wait_for("[client] the marker's fade arrives", lambda: FADE_TO_BLACK_IMOD in screen("client")["imods"], 5)
        if screen("client")["shake"]["seconds"] > 0:
            raise TestFailure("[client] shakes with the host player's own script")
        script_call("", "ImageSpaceModifier", "Remove", [], on=FADE_TO_BLACK_IMOD)

        player_command("", {"cmd": "quest_allow_rerun", "quest": MONK_SCENE_QUEST})  # it runs once per save
        script_call("", "Quest", "Start", [], on=MONK_SCENE_QUEST)  # the world's: its intro and loop are everyone's
        for name in ("", "client"):
            wait_for(f"[{name or 'host'}] the scene's loop", lambda name=name: MONK_LOOP_IMOD in screen(name)["imods"], 10)
        player_script_call("", "Quest", "SetCurrentStageID", [200], on=MONK_SCENE_QUEST)
        wait_for("[host] its stage 200 pops to the outro", lambda: MONK_OUTRO_IMOD in screen("")["imods"], 5)
        wait_for("[client] the host player's stage pops it to the outro too", lambda: MONK_OUTRO_IMOD in screen("client")["imods"], 5)
    finally:
        for imod in (FADE_TO_BLACK_IMOD, MONK_INTRO_IMOD, MONK_LOOP_IMOD, MONK_OUTRO_IMOD):
            script_call("", "ImageSpaceModifier", "Remove", [], on=imod)
        for name in ("", "client"):
            wait_for(f"[{name or 'host'}] its screen clears", lambda name=name: not screen(name)["imods"], 5)
        console_on("", "resetquest MG04MonkSceneQuest")
        client_quest_matches(MONK_SCENE_QUEST, "the monks' scene is reset")


def scenario_screen_reaches_party():
    """Outside a cutscene too, a script's shake and fade are the party's (a trap, a boss's music, a spell's look): the
    host's script shakes the camera and fades, and the client's screen does too, and clears with the host's."""
    try:
        script_call("", "Game", "ShakeCamera", [None, 0.5, 2.0])
        script_call("", "ImageSpaceModifier", "Apply", [1.0], on=FADE_TO_BLACK_IMOD)
        for name in ("", "client"):
            wait_for(f"[{name or 'host'}] shakes and fades",
                     lambda name=name: screen(name)["shake"]["seconds"] > 0 and FADE_TO_BLACK_IMOD in screen(name)["imods"], 5)
        script_call("", "ImageSpaceModifier", "Remove", [], on=FADE_TO_BLACK_IMOD)
        wait_for("[client] the fade clears", lambda: FADE_TO_BLACK_IMOD not in screen("client")["imods"], 5)
        if screen("client")["controls"] != ALL_CONTROLS:
            raise TestFailure(f"[client] its controls are {screen('client')['controls']:X} though no script took them")
    finally:
        script_call("", "ImageSpaceModifier", "Remove", [], on=FADE_TO_BLACK_IMOD)


def scenario_relief_takes_client_relics():
    """The client, carrying Volunruud's ceremonial axe, puts it in the relief. On the host the door's script holds the
    host's player (stored when its cell loaded) and asks that variable for the relics: in a stack run for the client,
    the native sees the client's character in its place (party-model.md, rule 2: a stored player), so the axe is
    taken from the client and the relief shows it on both games, though the host's player holds none; the host's box
    says the sword is still missing (the host's until messages go to everyone). The quest is set to stage 10 first
    (a first activation sets it, with a box) and stays there: stages only go forward. The relief is emptied after."""
    try:
        both_go(VOLUNRUUD_CELL)
        copy = client_on_host()
        if item_count("", 0x14, VOLUNRUUD_AXE):
            raise TestFailure(f"[host] the host's player holds the axe {VOLUNRUUD_AXE:X} to begin with")
        for name in ("", "client"):
            if ref_enabled(name, VOLUNRUUD_AXE_SHOWN):
                raise TestFailure(f"[{name or 'host'}] the axe {VOLUNRUUD_AXE_SHOWN:X} stands in the relief to begin with")
        wait_for("[host] the door's script holds the host's player",
                 lambda: script_var("", VOLUNRUUD_DOOR, VOLUNRUUD_SCRIPT, "player") == 0x14, 10)
        if (quest_stage("", VOLUNRUUD_QUEST_ID) or 0) < 10:
            console_on("", f"setstage {VOLUNRUUD_QUEST} 10")
        wait_for(f"[host] {VOLUNRUUD_QUEST} is at stage 10", lambda: quest_stage("", VOLUNRUUD_QUEST_ID) == 10, 5)
        console_on("client", f"player.additem {VOLUNRUUD_AXE:08X} 1")
        wait_for("[host] the client's character holds the axe", lambda: item_count("", copy, VOLUNRUUD_AXE) == 1, 5)
        for name in ("", "client"):
            place_before(name, VOLUNRUUD_DOOR, 200)
        activate_on("client", VOLUNRUUD_DOOR)
        wait_for("[client] the player gave up the axe", lambda: item_count("client", 0x14, VOLUNRUUD_AXE) == 0, 10)
        for name in ("", "client"):
            wait_for(f"[{name or 'host'}] the axe stands in the relief", lambda name=name: ref_enabled(name, VOLUNRUUD_AXE_SHOWN), 5)
        wait_for("[host] the relief says the sword is still missing", lambda: "MessageBoxMenu" in menus(""), 5)
        close_message_box("")
        if item_count("", 0x14, VOLUNRUUD_AXE):
            raise TestFailure("[host] the host's player holds an axe it never had")
    finally:
        if "MessageBoxMenu" in menus(""):
            close_message_box("")
        if count := item_count("client", 0x14, VOLUNRUUD_AXE):
            console_on("client", f"player.removeitem {VOLUNRUUD_AXE:08X} {count}")
        if ref_enabled("", VOLUNRUUD_AXE_SHOWN):
            console_on("", f"{VOLUNRUUD_AXE_SHOWN:08X}.disable")
        script_var("", VOLUNRUUD_DOOR, VOLUNRUUD_SCRIPT, "relic01in", 0)
        both_go(HOME_CELL)


# Solitude's temple shrines (TempleBlessingScript): each casts its god's blessing on whoever activates it, and
# shows the blessing messages when that is the player.
SHRINE_CELL = "SolitudeTempleoftheDivines"
SHRINES = {"": (0xD9880, 0xFB995), "client": (0xD9882, 0xFB988)}  # who prays: (shrine, its blessing)


def blessings(instance):
    """The shrine blessings on that game's player."""
    select_instance(instance)
    effects = (((read_status() or {}).get("world") or {}).get("20") or {}).get("effects") or []
    return {e.get("spell") for e in effects} & {blessing for _, blessing in SHRINES.values()}


def scenario_shrine_blesses_client():
    """The host and the client each pray at a different temple shrine: each player gets only their own
    shrine's blessing, though each game sees the other's character pray too (the host's script casts at
    the client's character, which its owner's game applies; upstream's patch skipped other players)."""
    try:
        both_go(SHRINE_CELL)
        for name in ("", "client"):
            if blessings(name):
                raise TestFailure(f"[{name or 'host'}] the player is blessed to begin with")
        for name, (shrine, blessing) in SHRINES.items():
            place_before(name, shrine, 150)
            activate_on(name, shrine)
            wait_for(f"[{name or 'host'}] the player is blessed by shrine {shrine:X}", lambda name=name, blessing=blessing: blessing in blessings(name), 5)
        for name, (_, blessing) in SHRINES.items():
            if blessings(name) != {blessing}:
                raise TestFailure(f"[{name or 'host'}] the player holds blessings {sorted(hex(b) for b in blessings(name))}, not only its own {blessing:X}")
    finally:
        for name in ("", "client"):
            if blessings(name):
                console_on(name, "player.dispelallspells")
        both_go(HOME_CELL)


# The Companions' first trial (C00VilkasTrainingQuest): Vilkas's alias (C00TrainerScript) counts the hits whose
# `akAggressor` is `Game.GetPlayer()`; three pass the trial (stage 150), a hit by anyone else ends it (110).
# Stage 10 starts the fight while C00 is at 20. His sword is given at 200, from his dialogue after the trial.
VILKAS, C00_QUEST, VILKAS_TRAINING, VILKAS_HATE = 0x1A695, 0x4B2D9, 0xA3EBC, 0x10F5A0


def host_quest(quest):
    select_instance("")
    response = rpc({"cmd": "quest", "quest": quest}) or {}
    return response.get("current_stage"), {stage for stage, done in response.get("stages", []) if done}


def scenario_vilkas_counts_client_hits():
    """The client spars with Vilkas for the Companions' trial. On the host his trial script counts the
    client's character as the player (rule 1): three hits pass the trial instead of ending it ("someone
    else hit him"). Rule 1 only: nothing the hits start asks for the player."""
    try:
        console_on("", "resetquest C00VilkasTrainingQuest")
        console_on("", "setstage C00 20")
        wait_for("[host] C00 is at stage 20", lambda: host_quest(C00_QUEST)[0] == 20, 5)
        vilkas = stage_host_npc(VILKAS)
        console_on("", "setstage C00VilkasTrainingQuest 10")
        wait_for("[host] the trial fight starts", lambda: host_quest(VILKAS_TRAINING)[0] == 10, 5)
        full = host_npc_health(VILKAS)
        give_sword("client")
        draw("client")
        for hit in range(1, 4):
            place_before("client", vilkas, MELEE_GAP)  # each hit knocks him back out of reach
            # He heals between hits: a hit is only seen against his full health.
            wait_for("[host] Vilkas's health is full", lambda: host_npc_health(VILKAS) >= full, 5)
            attack("client")
            hit_lands(VILKAS, full, f"the client's hit {hit}")
            if 110 in host_quest(VILKAS_TRAINING)[1]:
                raise TestFailure("[host] the trial ended: the client's hit counted as someone else's")
        wait_for("[host] the trial is passed", lambda: 150 in host_quest(VILKAS_TRAINING)[1], 5)
    finally:
        release_host_npc(VILKAS)
        for command in (f"{VILKAS:X}.setfactionrank {VILKAS_HATE:X} -1", f"{VILKAS:X}.stopcombat",
                        f"{VILKAS:X}.restoreav health 1000", "resetquest C00VilkasTrainingQuest"):
            console_on("", command)




# Skyrim.esm packages: DefaultSandboxCurrentLocation256 and DefaultSandboxCurrentLocation1024.
SANDBOX_256, SANDBOX_1024 = 0x956B8, 0xBFB6B


def is_current_package(instance, actor, package):
    """The condition GetIsCurrentPackage on the actor in that game, as a dialogue line reads it."""
    select_instance(instance)
    return (rpc({"cmd": "is_current_package", "actor": actor, "package": package}) or {}).get("result")


def ai_state(instance, actor):
    select_instance(instance)
    return (((read_status() or {}).get("world") or {}).get(str(actor)) or {}).get("ai_state") or {}


def running_package(instance, actor):
    select_instance(instance)
    return ((((read_status() or {}).get("world") or {}).get(str(actor)) or {}).get("ai") or {}).get("running_package", 0)


def scenario_copy_runs_owners_package():
    """The host's Vilkas runs one package, then another (staged: frozen, the package put in place as his evaluation
    would). The client's copy runs the same one each time: its package field and the condition GetIsCurrentPackage,
    which dialogue lines read on the speaker, answer as on the host, and its running slot, which plugins (Open
    Animation Replacer) read inline, holds it (copy-contract.md, "AI state")."""
    try:
        stage_host_npc(VILKAS)
        for package in (SANDBOX_256, SANDBOX_1024):
            player_command("", {"cmd": "put_package", "actor": VILKAS, "package": package})
            if not is_current_package("", VILKAS, package):
                raise TestFailure(f"[host] Vilkas does not run {package:X} after it was put in place")
            try:
                wait_for(f"[client] its Vilkas runs {package:X}", lambda: ai_state("client", VILKAS).get("package") == package
                         and is_current_package("client", VILKAS, package) and running_package("client", VILKAS) == package, 3)
            except TestFailure:
                raise TestFailure(f"[client] its Vilkas does not run the host's {package:X}: package "
                                  f"{ai_state('client', VILKAS).get('package', 0):X}, GetIsCurrentPackage "
                                  f"{is_current_package('client', VILKAS, package)}, running slot "
                                  f"{running_package('client', VILKAS):X}")
    finally:
        release_host_npc(VILKAS)


def scenario_copy_in_owners_combat():
    """The host's Vilkas (staged, his AI on: combat needs it) starts combat with the host's player, then stops. On the client his copy, which has no combat controller, answers IsInCombat and GetCombatState
    as the host's Vilkas does (copy-contract.md, "AI state"). Its combat target is not checked: the engine clears the
    field on a copy, which its own callers see out of combat (progress.md, overnight slice 3)."""
    try:
        stage_host_npc(VILKAS)
        freeze("", VILKAS, False)  # combat needs his AI (restrained, he does not start it either)
        console_on("", f"{VILKAS:X}.startcombat player")
        wait_for("[host] Vilkas is in combat", lambda: ai_state("", VILKAS).get("in_combat"), 5)
        host_player = other_player(actors_on("client"))
        try:
            wait_for("[client] its Vilkas is in combat with the host's character",
                     lambda: (s := ai_state("client", VILKAS)).get("in_combat") and s.get("combat") == ai_state("", VILKAS).get("combat"), 3)
        except TestFailure:
            raise TestFailure(f"[client] its Vilkas is not in the host's combat: {ai_state('client', VILKAS)} "
                              f"(host {ai_state('', VILKAS)}, the host's character here {host_player:X})")
        console_on("", f"{VILKAS:X}.stopcombat")
        wait_for("[host] Vilkas is out of combat", lambda: not ai_state("", VILKAS).get("in_combat"), 5)
        wait_for("[client] its Vilkas is out of combat", lambda: not ai_state("client", VILKAS).get("in_combat"), 3)
    finally:
        freeze("", VILKAS)  # first: thawed, he takes up the fight with the host's player again after a stopcombat
        for command in (f"{VILKAS:X}.stopcombat", "player.stopcombat"):
            console_on("", command)
        release_host_npc(VILKAS)


def scenario_guest_player_in_hosts_combat():
    """The host's Vilkas (staged, his AI on) starts combat with the client's character, then stops. The client's
    player counts as in combat while he fights it, as a player does from the combat groups targeting it (41238):
    the host's NPCs are copies on the client, with no combat groups, so their AI state counts instead."""
    try:
        stage_host_npc(VILKAS)
        freeze("", VILKAS, False)
        client_character = client_on_host()
        console_on("", f"{VILKAS:X}.startcombat {client_character:X}")
        wait_for("[host] Vilkas fights the client's character",
                 lambda: ai_state("", VILKAS).get("combat_target") == client_character, 5)
        try:
            wait_for("[client] the player is in combat", lambda: ai_state("client", 0x14).get("in_combat"), 3)
        except TestFailure:
            raise TestFailure(f"[client] the player is not in combat: its Vilkas {ai_state('client', VILKAS)}")
        console_on("", f"{VILKAS:X}.stopcombat")
        wait_for("[client] the player is out of combat", lambda: not ai_state("client", 0x14).get("in_combat"), 5)
    finally:
        freeze("", VILKAS)  # first: thawed, he takes up the fight with the host's player again after a stopcombat
        for command in (f"{VILKAS:X}.stopcombat", "player.stopcombat"):
            console_on("", command)
        console_on("client", "player.stopcombat")
        release_host_npc(VILKAS)


def scenario_guest_player_in_switched_combat():
    """The host's Vilkas (staged, his AI on) fights the host's player, then turns on the client's character without
    leaving combat: both are his targets, and the host's player steps 1500 units away (the closer one wins). A switch
    inside a fight sends no combat event (TESCombatEvent is a state change): the host's combat controller setting
    its target (33235) says so. The client's player counts as in combat from then on."""
    try:
        stage_host_npc(VILKAS)
        freeze("", VILKAS, False)
        client_character = client_on_host()
        console_on("", f"{VILKAS:X}.startcombat player")
        wait_for("[host] Vilkas fights the host's player", lambda: ai_state("", VILKAS).get("combat_target") == 0x14, 5)
        wait_for("[client] its Vilkas is in combat", lambda: ai_state("client", VILKAS).get("in_combat"), 3)
        if ai_state("client", 0x14).get("in_combat"):
            raise TestFailure("[client] the player is in combat while Vilkas fights the host's player")
        console_on("", f"{VILKAS:X}.startcombat {client_character:X}")  # his second target; he keeps the first
        console_on("", f"player.setpos x {actor_place('', 0x14)['position'][0] + 1500:.0f}")
        wait_for("[host] Vilkas turns on the client's character",
                 lambda: ai_state("", VILKAS).get("combat_target") == client_character, 5)
        try:
            wait_for("[client] the player is in combat", lambda: ai_state("client", 0x14).get("in_combat"), 3)
        except TestFailure:
            raise TestFailure(f"[client] the player is not in combat after the switch: its Vilkas {ai_state('client', VILKAS)} "
                              f"(host {ai_state('', VILKAS)})")
        console_on("", f"{VILKAS:X}.stopcombat")
        wait_for("[client] the player is out of combat", lambda: not ai_state("client", 0x14).get("in_combat"), 5)
    finally:
        freeze("", VILKAS)  # first: thawed, he takes up the fight with the host's player again after a stopcombat
        for command in (f"{VILKAS:X}.stopcombat", "player.stopcombat"):
            console_on("", command)
        console_on("client", "player.stopcombat")
        release_host_npc(VILKAS)


POTENTIAL_MARRIAGE = 0x19809  # Skyrim.esm PotentialMarriageFaction: read by dialogue conditions, no AI of its own


def faction_rank(instance, actor, faction):
    """The condition GetFactionRank on the actor in that game (-1: not a member), as a dialogue line reads it."""
    select_instance(instance)
    response = rpc({"cmd": "faction_rank", "actor": actor, "faction": faction}) or {}
    if not response.get("ok"):
        raise TestFailure(f"[{instance or 'host'}] faction_rank {actor:X} {faction:X}: {response.get('error')}")
    return response["result"]


def scenario_copy_holds_owners_factions():
    """The host's Vilkas is given a rank in a faction, another, and leaves it (the console's AddToFaction, SetFactionRank,
    RemoveFromFaction). The client's copy answers GetFactionRank as he does each time: the owner sends its factions
    when the engine writes them, with no periodic check."""
    try:
        copy = stage_host_npc(VILKAS)
        # He is in it from his base at rank 0; out of it, the condition answers below 0 (-2 for a base faction removed).
        for command, member in ((f"addtofaction {POTENTIAL_MARRIAGE:X} 1", 1), (f"setfactionrank {POTENTIAL_MARRIAGE:X} 3", 3),
                                (f"removefromfaction {POTENTIAL_MARRIAGE:X}", None)):
            console_on("", f"{VILKAS:X}.{command}")
            rank = faction_rank("", VILKAS, POTENTIAL_MARRIAGE)
            if (rank != member) if member is not None else rank >= 0:
                raise TestFailure(f"[host] Vilkas's rank is {rank} after {command}")
            try:
                wait_for(f"[client] its Vilkas has rank {rank}", lambda: faction_rank("client", copy, POTENTIAL_MARRIAGE) == rank, 3)
            except TestFailure:
                raise TestFailure(f"[client] its Vilkas has rank {faction_rank('client', copy, POTENTIAL_MARRIAGE)} after the "
                                  f"host's {command}, not {rank}")
    finally:
        console_on("", f"{VILKAS:X}.setfactionrank {POTENTIAL_MARRIAGE:X} 0")  # as his base has him
        release_host_npc(VILKAS)


# A QASmoke chest of 17 stacks; the container menu's Take All (R, the game's controlmap.txt) takes them all and closes it.
SMALL_CHEST, SWEETROLL, TAKE_ALL_KEY = 0xC2CE8, 0x64B3D, 0x13
# The menu's lists (InventoryLists.as): 1 one panel, 2 two panels; the others are transitions, which drop input.
ITEM_LISTS = "_root.Menu_mc.InventoryLists_mc"
LISTS_SHOWN = (1, 2)


def container_items(instance, chest):
    """What the chest holds in that game: {item: count}, none of 0."""
    return {i["item"]: i["count"] for i in player_command(instance, {"cmd": "container_items", "object": chest})["items"] if i["count"]}


def menu_var(instance, menu, path):
    select_instance(instance)
    return (rpc({"cmd": "menu_var", "menu": menu, "path": path}) or {}).get("value")


def item_differences(a, b, a_name, b_name):
    """Where two {item: count} differ, as 'item: 2 (host) vs 0 (client)'."""
    return "; ".join(f"{item:X}: {a.get(item, 0)} ({a_name}) vs {b.get(item, 0)} ({b_name})"
                     for item in sorted(set(a) | set(b)) if a.get(item, 0) != b.get(item, 0))


def take_all(instance, chest):
    """The player opens the chest (activates it) and takes everything (R), which closes it."""
    who = f"[{instance or 'host'}]"
    place_before(instance, chest, 100)
    activate_on(instance, chest)
    wait_for(f"{who} the chest's menu shows its lists",
             lambda: menu_var(instance, "ContainerMenu", ITEM_LISTS + ".currentState") in LISTS_SHOWN, 5)
    tap_key(instance, TAKE_ALL_KEY)
    wait_for(f"{who} the menu closed", lambda: takes_input(instance), 5)


def scenario_chest_take_all():
    """Both players use Take All: items reach the acting player and its copy.
    The other game keeps its menu closed and its own player's inventory unchanged."""
    chest = SMALL_CHEST
    before = container_items("", chest)
    held = {name: {item: item_count(name, 0x14, item) or 0 for item in [*before, SWEETROLL]} for name in ("", "client")}
    try:
        if (theirs := container_items("client", chest)) != before:
            raise TestFailure(f"[guest] chest {chest:X} holds other items than the host's to begin with: "
                              + item_differences(before, theirs, "host", "guest"))
        if SWEETROLL in before:
            raise TestFailure(f"[host] chest {chest:X} holds sweetrolls to begin with")
        console_on("", f"{chest:X}.additem {SWEETROLL:X} 2")
        for name in ("", "client"):
            wait_for(f"[{'guest' if name else 'host'}] chest {chest:X} holds 2 sweetrolls",
                     lambda name=name: item_count(name, chest, SWEETROLL) == 2, 5)
        take_all("client", chest)
        if "ContainerMenu" in menus(""):
            raise TestFailure("[host] the guest's opening of the chest opened the host's menu")
        left = container_items("client", chest)
        if SWEETROLL in left:
            raise TestFailure(f"[guest] Take All left {left[SWEETROLL]} sweetrolls in the chest")
        try:
            wait_for(f"[host] chest {chest:X} holds what the guest's does ({len(left)} kinds)",
                     lambda: container_items("", chest) == left, 5)
        except TestFailure as error:
            raise TestFailure(f"{error}: {item_differences(container_items('', chest), left, 'host', 'guest')}") from None
        copy = client_on_host()
        wait_for("[host] the guest player's copy holds the sweetrolls", lambda: item_count("", copy, SWEETROLL) == 2, 5)
        if item_count("", 0x14, SWEETROLL) != held[""][SWEETROLL]:
            raise TestFailure("[host] the guest's Take All changed the host player's inventory")
        if (rolls := item_count("client", 0x14, SWEETROLL)) != held["client"][SWEETROLL] + 2:
            raise TestFailure(f"[guest] the player holds {rolls} sweetrolls, not {held['client'][SWEETROLL] + 2}")
        # The host's own take must also empty the guest's copy of the chest.
        console_on("", f"{chest:X}.additem {SWEETROLL:X} 1")
        wait_for("[guest] the sweetroll is in its chest", lambda: item_count("client", chest, SWEETROLL) == 1, 5)
        take_all("", chest)
        if "ContainerMenu" in menus("client"):
            raise TestFailure("[guest] the host's Take All opened the guest's container menu")
        wait_for("[host] Take All puts the sweetroll in the host player's inventory",
                 lambda: item_count("", 0x14, SWEETROLL) == held[""][SWEETROLL] + 1, 5)
        copy = other_player(actors_on("client"))
        wait_for("[guest] the host player's copy holds the sweetroll",
                 lambda: item_count("client", copy, SWEETROLL) == held[""][SWEETROLL] + 1, 5)
        wait_for(f"[guest] chest {chest:X} holds no sweetroll", lambda: item_count("client", chest, SWEETROLL) == 0, 5)
        if container_items("client", chest) != container_items("", chest):
            raise TestFailure(f"[guest] chest {chest:X} holds other items than the host's")
        if (rolls := item_count("client", 0x14, SWEETROLL)) != held["client"][SWEETROLL] + 2:
            raise TestFailure(f"[guest] the player holds {rolls} sweetrolls: the host's take reached it")
    finally:
        passed = sys.exc_info()[0] is None  # a failed step's name is the failure, not the clean-up's
        for name in ("", "client"):
            if "ContainerMenu" in menus(name):
                player_command(name, {"cmd": "menu", "name": "ContainerMenu", "show": False})
                wait_for(f"[{'guest' if name else 'host'}] the container menu is closed after cleanup",
                         lambda name=name: "ContainerMenu" not in menus(name), 5)
            for item, count in held[name].items():
                if (taken := (item_count(name, 0x14, item) or 0) - count) > 0:
                    console_on(name, f"player.removeitem {item:X} {taken}")
        for item, count in before.items():
            if (missing := count - (item_count("", chest, item) or 0)) > 0:
                console_on("", f"{chest:X}.additem {item:X} {missing}")
        if extra := item_count("", chest, SWEETROLL):
            console_on("", f"{chest:X}.removeitem {SWEETROLL:X} {extra}")
        if passed:
            wait_for("[guest] the chest is as it was", lambda: container_items("client", chest) == before, 5)


def guest_locks_taken(since):
    """The guests' lock proposals the host's game took since that trail number (its log trail)."""
    lines, _ = log_trail("", since)
    return [line["text"] for line in lines if line["text"].startswith("HostEngine: took a guest's lock")]


def scenario_guest_loads_propose_no_locks():
    """A guest's own world changes no lock on the host's: its cell loads and its world scripts record lock changes
    (AddLockChange from scripts' OnLoad and the engine's cell jobs), which only a player's lockpick or key may
    propose. The party goes to Riverwood and back: the host takes none of the guest's."""
    _, since = log_trail("")
    try:
        both_go(OUTDOOR_CELL)
        both_go(HOME_CELL)
    finally:
        if taken := guest_locks_taken(since):
            raise TestFailure(f"[host] took {len(taken)} lock changes from the guest's loads: " + "; ".join(t[len("HostEngine: took a guest's "):] for t in taken[:5]))


REFR_LOCK = 1 << 12  # REFR change flag LockExtra


def form_save(instance, form, **fields):
    """The form's save data in that game (FormState::Save), as the harness's form_save gives it."""
    return player_command(instance, {"cmd": "form_save", "form": form, **fields})


def scenario_form_state_round_trips():
    """World-state sync step 3: a form's save data through our own buffers. The client's character base
    and the host's copy of it save the same bytes (the appearance went through FormState on spawn, form ids
    as network ids); a door's lock saved on the host, unlocked, and loaded back is locked again."""
    mine = form_save("client", 0x14, base=True)
    select_instance("")
    copy = next(iter(other_players()), None)
    if copy is None:
        raise TestFailure("[host] no copy of the client's character")
    theirs = form_save("", copy, base=True, flags=mine["flags"])
    if theirs["data"] != mine["data"]:
        raise TestFailure(f"[host] the copy's base {theirs['form']:X} saves other bytes than the client's "
                          f"own ({len(theirs['data']) // 2} / {len(mine['data']) // 2})")
    log(f"[host] the copy's base saves the client's {len(mine['data']) // 2} bytes")
    door = inner_door()
    try:
        lock_on("", door, True, 50)
        wait_for(f"[host] door {door:X} is locked at 50", lambda: door_lock("", door) == (True, 50), 10)
        saved = form_save("", door, flags=REFR_LOCK)
        lock_on("", door, False)
        wait_for(f"[host] door {door:X} is unlocked", lambda: not door_lock("", door)[0], 10)
        player_command("", {"cmd": "form_load", "form": door, "flags": REFR_LOCK, "data": saved["data"]})
        wait_for(f"[host] door {door:X} is locked at 50 again", lambda: door_lock("", door) == (True, 50), 10)
        if form_save("", door, flags=REFR_LOCK)["data"] != saved["data"]:
            raise TestFailure(f"[host] door {door:X} saves other bytes after the load")
    finally:
        for name in ("", "client"):
            if door_lock(name, door)[0]:
                lock_on(name, door, False)


BEAST_FORM, WEREWOLF_CHANGE_EFFECT, WEREWOLF_BEAST_RACE, PLAYER_WEREWOLF_QUEST = 0x92C48, 0x92C45, 0xCDD84, 0x2BA16
# --- The world as each game shows it: world-state sync's check (docs/design/world-state-sync.md step 7) ---

# What `world_state` reads of a reference, compared with these tolerances (rotation in radians). "changes" is its
# change record's flags (what a game keeps when the cell unloads); the last four are the side effects a change has:
# where its 3D's nodes are (any animation), its physics bodies, its behavior graph's variables and its effects.
WORLD_FIELDS = ("base", "cell", "flags", "position", "rotation", "scale", "has_3d", "hidden", "position_3d",
                "lock", "open_state", "owner", "items", "changes", "pose", "collision", "graph", "effects")
WORLD_TOLERANCES = {"position": 1.0, "position_3d": 1.0, "rotation": 0.01, "scale": 0.001}
POSE_TOLERANCE, ROTATE_TOLERANCE, BODY_TOLERANCE = 1.0, 0.01, 2.0
CREATED_ID = 0xFF000000  # references a game made itself, numbered per game
FORM_DISABLED = 0x800
FORM_DELETED = 0x20
FORM_PERSISTENT = 0x400  # also dynamic: promoted while a script holds the reference (BGSDynamicPersistenceManager)
FORM_ALTERED = 0x2  # TESForm kAltered: the engine's "has changes" bookkeeping, not the world's state
FORM_HEADING_TARGET = 0x40000  # has an ExtraHeadingTarget (19584, from SetHeadingTargetRefHandle): look-at bookkeeping
# Each game's own bookkeeping, not world state: a guest runs no world script to hold a reference, and a twin its cell
# freed is made again from the cell's state.
FORM_NOT_SYNCED = FORM_ALTERED | FORM_HEADING_TARGET | FORM_PERSISTENT
# Change record bits each game keeps for itself: ANIMATION (a graph's state, e.g. furniture someone used),
# PROMOTED (a reference its player touched made persistent) and GAME_ONLY_EXTRA (runtime bookkeeping, never sent).
CHANGES_FORM_FLAGS = 0x1  # the record of the form flags (enabled, deleted, and the bookkeeping ones)
CHANGES_OWN = 0x10000000 | 0x2000000 | 0x80000000
CHANGES_MOVED, CHANGES_HAVOK_MOVED = 0x2, 0x4


def world_state(instance, ids=None):
    """The references in that game's loaded cells (or only `ids`), actors aside (copies: `desync`): {id: fields}."""
    select_instance(instance)
    response = rpc({"cmd": "world_state", "details": True, **({"ids": ids} if ids else {})}, timeout=20) or {}
    if not response.get("ok"):
        raise TestFailure(f"[{instance or 'host'}] world_state refused: {response.get('error', 'no answer')}")
    return {r["id"]: r for r in response["references"] if not r.get("actor")}


def near(a, b, tolerance, angle=False):
    a, b = (v if isinstance(v, list) else [v] for v in (a, b))
    def gap(x, y):
        d = abs(x - y) % (2 * math.pi) if angle else abs(x - y)
        return min(d, 2 * math.pi - d) if angle else d
    return len(a) == len(b) and all(gap(x, y) <= tolerance for x, y in zip(a, b))


def show_value(field, value):
    if value is None:
        return "-"
    if field in ("base", "cell", "owner"):
        return f"{value:X}"
    if field in ("flags", "changes"):
        return f"{value:#x}"
    if isinstance(value, list):
        return "(" + ", ".join(f"{v:.2f}" if field == "rotation" else f"{v:.0f}" for v in value) + ")"
    if isinstance(value, dict):
        return "{" + ", ".join(f"{k}: {v}" for k, v in value.items()) + "}"
    return str(value)


def pose_moved(a, b, name):
    return not near(a[name]["position"], b[name]["position"], POSE_TOLERANCE) or not near(a[name]["rotate"], b[name]["rotate"], ROTATE_TOLERANCE)


def pose_difference(a, b, a_again=(), b_again=()):
    """The nodes of two poses ({name: {position, rotate}}) that are not where the other game's are. With each pose
    sampled again, a node that moved between one game's samples animates on its own (an idle loop, which the engine
    also stops off-camera) and is skipped."""
    a, b = a or {}, b or {}
    moved = []
    for name in sorted(set(a) | set(b)):
        if any(name in a and name in again and pose_moved(a, again, name) for again in a_again) or \
                any(name in b and name in again and pose_moved(b, again, name) for again in b_again):
            continue
        if name not in a or name not in b:
            moved.append(f"{name} {'host only' if name in a else 'client only'}")
        elif not near(a[name]["position"], b[name]["position"], POSE_TOLERANCE):
            moved.append(f"{name} at {show_value('position', a[name]['position'])} vs {show_value('position', b[name]['position'])}")
        elif not near(a[name]["rotate"], b[name]["rotate"], ROTATE_TOLERANCE):
            moved.append(f"{name} turned differently")
    return "; ".join(moved[:6]) + (f" (+{len(moved) - 6} more)" if len(moved) > 6 else "")


def collision_difference(a, b):
    """Two lists of physics bodies ({node, in_world, position}) that differ, in order."""
    a, b = a or [], b or []
    if len(a) != len(b):
        return f"{len(a)} bodies vs {len(b)}"
    different = [f"{x['node']}: {'in' if x['in_world'] else 'out of'} world at {show_value('position', x['position'])} vs "
                 f"{'in' if y['in_world'] else 'out of'} at {show_value('position', y['position'])}"
                 for x, y in zip(a, b) if x["in_world"] != y["in_world"] or not near(x["position"], y["position"], BODY_TOLERANCE)]
    return "; ".join(different[:4])


def reference_differences(host, guest, host_again=(), guest_again=()):
    """Where the guest's reference differs from the host's: {field: 'host value vs guest value'}. host_again,
    guest_again: the reference sampled again in each game, to skip what animates on its own."""
    differences = {}
    for field in WORLD_FIELDS:
        a, b = host.get(field), guest.get(field)
        if field == "pose":
            # Untouched in both and without a graph: only its model's own loops, on each game's clock (frozen
            # off-camera at different phases).
            if not any(r.get(k) for r in (host, guest) for k in ("changes", "graph")):
                continue
            if text := pose_difference(a, b, [again.get("pose") or {} for again in host_again or ()],
                                       [again.get("pose") or {} for again in guest_again or ()]):
                differences[field] = text
            continue
        if field == "collision":
            if text := collision_difference(a, b):
                differences[field] = text
            continue
        if field == "lock":  # no lock reads as unlocked at level 0, as the lock row does
            a, b = (v or {"level": 0, "locked": False} for v in (a, b))
        if field == "flags" and a is not None and b is not None:
            a, b = a & ~FORM_NOT_SYNCED, b & ~FORM_NOT_SYNCED
        if field == "changes":
            a, b = (a or 0) & ~CHANGES_OWN or None, (b or 0) & ~CHANGES_OWN or None
            # A form-flags record for a flag each game keeps itself (a look-at target): equal flags otherwise.
            if ((host.get("flags") or 0) ^ (guest.get("flags") or 0)) & ~FORM_NOT_SYNCED == 0:
                a, b = (a or 0) & ~CHANGES_FORM_FLAGS or None, (b or 0) & ~CHANGES_FORM_FLAGS or None
            # Moved by a call or by physics: either way its position is what a save keeps.
            a, b = ((v | CHANGES_MOVED) & ~CHANGES_HAVOK_MOVED if v and v & CHANGES_HAVOK_MOVED else v for v in (a, b))
        if field in WORLD_TOLERANCES and a is not None and b is not None:
            same = near(a, b, WORLD_TOLERANCES[field], angle=field == "rotation")
        else:
            same = a == b
        if same:
            continue
        if field in ("items", "graph"):
            a, b = a or {}, b or {}
            differences[field] = ", ".join(f"{key}: {a.get(key, 0)} vs {b.get(key, 0)}"
                                           for key in sorted(set(a) | set(b)) if a.get(key, 0) != b.get(key, 0))
        else:
            differences[field] = f"{show_value(field, a)} vs {show_value(field, b)}"
            if field in ("flags", "changes"):
                differences[field] += f" (bits {(a or 0) ^ (b or 0):#x})"
    return differences


def reference_label(ref, game=None):
    kind = FORM_TYPE_NAMES.get(ref["type"], ref["type"])
    if ref["id"] < CREATED_ID:
        return f"{ref['id']:X} ({kind} {ref['base']:X})"
    return f"created {kind} {ref['base']:X} at {show_value('position', ref['position'])} ({ref['id']:X} on {game})"


def world_differences(host, guest, host_again=(), guest_again=()):
    """Where the guest's world differs from the host's: {reference: {field: 'host vs guest'}}. A reference one game
    lacks differs in "present"; created references pair up by base and position (their ids differ per game).
    host_again, guest_again: each game sampled again, so what animates on its own is skipped."""
    differences = {}
    for ref_id, ref in host.items():
        if ref_id < CREATED_ID:
            if ref_id not in guest:
                differences[reference_label(ref)] = {"present": "host only"}
            elif fields := reference_differences(ref, guest[ref_id], [again[ref_id] for again in host_again if ref_id in again],
                                                 [again[ref_id] for again in guest_again if ref_id in again]):
                differences[reference_label(ref)] = fields
    for ref_id, ref in guest.items():
        if ref_id < CREATED_ID and ref_id not in host:
            differences[reference_label(ref)] = {"present": "client only"}
    # A created reference deleted is gone (the engine frees it): only a living one is compared. A cast's projectiles
    # and hazards are each game's own, as the cast is (HostEngine's ReadCreate sends plain references only).
    def alive_created(world):
        return [r for r in world.values() if r["id"] >= CREATED_ID and not (r.get("flags") or 0) & FORM_DELETED
                and FORM_TYPE_NAMES.get(r["type"]) not in ("PROJ", "HAZD")]
    unpaired = alive_created(guest)
    for ref in alive_created(host):
        twin = next((r for r in unpaired if r["base"] == ref["base"] and near(r["position"], ref["position"], 2.0)), None)
        if twin is None:
            differences[reference_label(ref, "host")] = {"present": "host only"}
            continue
        unpaired.remove(twin)
        if fields := reference_differences(ref, twin):
            differences[reference_label(ref, "host")] = fields
    for ref in unpaired:
        differences[reference_label(ref, "client")] = {"present": "client only"}
    return differences


IDLE_SAMPLES, IDLE_INTERVAL = 3, 0.3  # host samples a second apart: what moved between them animates on its own


def sample_worlds(idle_samples=IDLE_SAMPLES):
    """Both games' worlds, and each again over a second: (host, guest, [host again], [guest again])."""
    host, guest = world_state(""), world_state("client")
    host_again, guest_again = [], []
    for _ in range(idle_samples):
        time.sleep(IDLE_INTERVAL)
        host_again.append(world_state(""))
        guest_again.append(world_state("client"))
    return host, guest, host_again, guest_again


WORLD_WINDOW = 3.0  # seconds a world difference must last to count: the guest applies a change a frame or more later


def shared_world_differences(idle_samples=IDLE_SAMPLES):
    """world_differences over the cells both games have loaded (the exterior grids around two players differ)."""
    worlds = sample_worlds(idle_samples)
    cells = {r["cell"] for r in worlds[0].values()} & {r["cell"] for r in worlds[1].values()}
    def cut(world):
        return {i: r for i, r in world.items() if r["cell"] in cells}
    return world_differences(cut(worlds[0]), cut(worlds[1]), [cut(w) for w in worlds[2]], [cut(w) for w in worlds[3]])


def check_world(known=frozenset(), window=WORLD_WINDOW):
    """The world differences that last the whole window, without the (reference, field) pairs in `known` (already
    different before): {reference: {field: 'host vs guest'}}."""
    def new(differences):
        return any((label, f) not in known for label, fields in differences.items() for f in fields)
    # One quick look first: the idle samples only take differences away, so none here is none.
    if not new(shared_world_differences(idle_samples=0)):
        return {}
    deadline = time.monotonic() + window
    persistent = None
    while True:
        current = {label: {f: t for f, t in fields.items() if (label, f) not in known}
                   for label, fields in shared_world_differences().items()}
        if persistent is not None:
            current = {label: {f: t for f, t in fields.items() if f in persistent.get(label, {})}
                       for label, fields in current.items()}
        persistent = {label: fields for label, fields in current.items() if fields}
        if not persistent or time.monotonic() > deadline:
            return persistent


def world_pairs(differences):
    return {(label, field) for label, fields in differences.items() for field in fields}


def describe_world_differences(differences, limit=None):
    """One line per reference: 'label: field host vs guest; ...'."""
    return [f"{label}: " + "; ".join(f"{field} {text}" for field, text in fields.items())
            for label, fields in list(differences.items())[:limit]]


def command_world_diff(args):
    differences = world_differences(*sample_worlds())
    if not differences:
        print("in sync: every reference in the loaded cells matches the host's")
        return
    fields = {}
    for changed in differences.values():
        for field in changed:
            fields[field] = fields.get(field, 0) + 1
    print(f"{len(differences)} references differ (" + ", ".join(f"{f} {n}" for f, n in fields.items()) + ")")
    for line in describe_world_differences(differences, args.details or None):
        print("  " + line)
    raise SystemExit(1)


def pick_reference(refs, type_name, nth=0, where=lambda r: True):
    """The nth reference of that base type the host shows (enabled, with 3D), lowest ids first."""
    type_id = next(i for i, name in FORM_TYPE_NAMES.items() if name == type_name)
    found = sorted(i for i, r in refs.items() if r["type"] == type_id and i < CREATED_ID and r["has_3d"]
                   and not r["flags"] & FORM_DISABLED and where(r))
    if len(found) <= nth:
        raise TestFailure(f"no {type_name} #{nth} in the host's cell to change")
    return found[nth]


GOLD = 0xF
# One change of each kind world-state sync could carry, made on the host with the console: which reference
# (type, nth, filter), the lines that stage it in both games first, the change, and what puts it back.
# {r} is the reference; {z}, {angle}, {scale} its values before.
WORLD_CHANGES = [
    ("disable", ("STAT", 0), [], "{r}.disable", ["{r}.enable"]),
    ("enable", ("STAT", 1), ["{r}.disable"], "{r}.enable", []),
    # Statics cannot be moved (the console's SetPos and SetAngle leave them).
    ("move", ("ACTI", 0), [], "{r}.setpos z {z_up}", ["{r}.setpos z {z}"]),
    # The console's SetAngle leaves the 3D's world transform until something updates it: a SetPos in place does.
    ("rotate", ("ACTI", 1), [], ["{r}.setangle z {turned}", "{r}.setpos z {z}"], ["{r}.setangle z {angle}", "{r}.setpos z {z}"]),
    ("scale", ("STAT", 2), [], "{r}.setscale 2", ["{r}.setscale {scale}"]),
    # At its own level: the console's Lock reads level 0 as none given, so no level could be put back.
    ("lock", ("DOOR", 0, lambda r: not r.get("lock", {}).get("locked")), [], "{r}.lock {level}", ["{r}.unlock"]),
    ("open", ("DOOR", 0), [], "{r}.setopenstate 1", ["{r}.setopenstate 0"]),
    ("add items", ("CONT", 0), [], f"{{r}}.additem {GOLD:X} 7", [f"{{r}}.removeitem {GOLD:X} 7"]),
    # No console line clears an owner: left owned (the client follows), each run takes the next unowned container.
    ("own", ("CONT", 1, lambda r: not r.get("owner")), [], "{r}.setownership", []),
]


def change_values(ref):
    z, angle = ref["position"][2], math.degrees(ref["rotation"][2]) % 360
    return {"z": f"{z:.2f}", "z_up": f"{z + 64:.2f}", "angle": f"{angle:.2f}", "turned": f"{(angle + 45) % 360:.2f}",
            "scale": f"{ref['scale']:.3f}", "level": (ref.get("lock") or {}).get("level", 0)}


def at_rest(instance, ref, timeout=5):
    """Waits until the reference stops changing in that game (a door done swinging), as a player would before
    acting on it again."""
    last = {}
    time.sleep(0.1)  # a console line's engine call can run a frame later
    def still():
        now = world_state(instance, [ref]).get(ref) or {}
        settled = last and not reference_differences(last, now) and now.get("open_state") not in (DOOR_OPENING, DOOR_CLOSING)
        last.clear()
        last.update(now)
        return settled
    wait_for(f"[{instance or 'host'}] {ref:X} at rest", still, timeout)


def guest_matches(ref, timeout=3):
    """Waits for the client's reference to match the host's; the fields that still differ, or none."""
    differences = {}
    def matches():
        host, guest = world_state("", [ref]).get(ref), world_state("client", [ref]).get(ref)
        differences.clear()
        differences.update(reference_differences(host, guest) if guest else {"present": "host only"})
        return not differences
    with contextlib.suppress(TestFailure):
        wait_for(f"[client] {ref:X} as the host's", matches, timeout)
    return differences


# What the engine raised on a reference (harness EngineEvents): Papyrus's OnActivate, OnLockStateChanged, OnOpen and
# OnClose, OnCellAttach and OnCellDetach, OnLoad and OnUnload, OnItemAdded and OnItemRemoved come from them.
OBJECT_EVENTS = {"activate", "lock_changed", "open", "close", "cell_attach", "cell_detach", "loaded", "unloaded"}


def object_events(instance, since, ref):
    """The object events that game raised on the reference since that number, by kind in order."""
    events, _ = engine_events(instance, since)
    return [e["kind"] for e in events if (e["kind"] in OBJECT_EVENTS and e.get("object") == ref)
            or (e["kind"] == "container_changed" and ref in (e.get("from"), e.get("to")))]


def put_back_left(before, ref):
    """What still differs from `before` in the host's reference, but its change record: a state put back can leave
    its flag (the engine's bookkeeping; the client must still match it)."""
    return {f: t for f, t in reference_differences(before, world_state("", [ref])[ref]).items() if f != "changes"}


def world_change_reaches_guest(name, pick, stage, change, undo, refs):
    """Makes one change on the host and puts it back in both games: what the client got wrong, or none. The
    events each game raised on the reference must match too: they are the change's side effects on scripts."""
    ref = pick_reference(refs, *pick)
    values = {"r": f"{ref:X}", **change_values(refs[ref])}
    for line in stage:
        for game in ("", "client"):
            console_on(game, line.format(**values))
    before = world_state("", [ref])[ref]
    since = {game: engine_events(game)[1] for game in ("", "client")}
    differences = {}
    try:
        changes = [change] if isinstance(change, str) else change
        for line in changes:
            console_on("", line.format(**values))
        try:
            wait_for(f"[host] {name}: {ref:X} changed", lambda: reference_differences(before, world_state("", [ref])[ref]), 3)
        except TestFailure:
            return {"test": f"the host's `{'; '.join(changes).format(**values)}` changed nothing it reads"}
        differences = guest_matches(ref)
        host_events, guest_events = (object_events(game, since[game], ref) for game in ("", "client"))
        if host_events != guest_events:
            differences["events"] = f"{host_events} vs {guest_events}"
        return differences
    finally:
        # Put back on the host: the client must follow that too (a state back to its default is a change as well).
        # Repaired in the client only where it did not (its own change would be proposed).
        # A door ignores a SetOpenState while it swings: the host is put back at rest, and checked against how it was.
        for attempt in range(2 if undo else 0):
            at_rest("", ref)
            for line in undo:
                console_on("", line.format(**values))
            with contextlib.suppress(TestFailure):
                wait_for(f"[host] {ref:X} as before", lambda: not put_back_left(before, ref), 5)
                break
        else:
            if undo:
                differences["test"] = "the host's put-back left " + "; ".join(f"{f} {t}" for f, t in put_back_left(before, ref).items())
        if undo and (left := guest_matches(ref)):
            differences["put back"] = "; ".join(f"{f} {t}" for f, t in left.items())
            for line in undo:
                console_on("client", line.format(**values))
        # The put-back's events, raised a frame apart in each game, stay in this kind's window, not the next's.
        with contextlib.suppress(TestFailure):
            wait_for(f"[both] {ref:X}'s events alike", lambda: object_events("", since[""], ref) == object_events("client", since["client"], ref), 3)


def settled(ref, still_for=1.0, timeout=30):
    """Waits until the host's physics object stays within half a unit and 0.005 rad of one spot for still_for
    seconds (HostEngine's rest rule)."""
    anchor = {}
    def still():
        now = world_state("", [ref]).get(ref)
        if now is None:
            return False
        if not anchor or not near(anchor["position"], now["position"], 0.5) or not near(anchor["rotation"], now["rotation"], 0.005, angle=True):
            anchor.update(position=now["position"], rotation=now["rotation"], since=time.monotonic())
            return False
        return time.monotonic() - anchor["since"] >= still_for
    wait_for(f"[host] {ref:X} comes to rest", still, timeout)


def created_reaches_guest(refs, base):
    """The host places a new object: the client shows one like it. Deleted again in both games."""
    before = {game: set(world_state(game)) for game in ("", "client")}
    console_on("", f"player.placeatme {base:X} 1")
    try:
        made = wait_for("[host] the new object", lambda: set(world_state("")) - before[""], 3)
        # It drops and rolls (a ladle rocks for 20s): compared where the host's comes to rest (each game simulates
        # the fall, the host's rest goes out).
        for ref in made:
            settled(ref)
        missing = {}
        def twin_shown():
            host, guest = world_state(""), world_state("client")
            missing.clear()
            missing.update({label: fields for label, fields in world_differences(host, guest).items() if label.startswith("created")})
            return not missing
        with contextlib.suppress(TestFailure):
            wait_for("[client] a twin of the host's new object", twin_shown, 3)
        return {label: "; ".join(f"{f} {t}" for f, t in fields.items()) for label, fields in missing.items()}
    finally:
        for game in ("", "client"):
            for ref in set(world_state(game)) - before[game]:
                console_on(game, f"{ref:X}.disable")
                console_on(game, f"{ref:X}.markfordelete")


def scenario_world_changes_reach_guest():
    """World-state sync's check: every kind of change to a reference the host can make, one at a time, each put
    back after. Reports every kind the client's world does not follow, then what still differs in the whole
    loaded world, rather than stopping at the first."""
    refs = world_state("")
    problems = []
    if before := world_differences(*sample_worlds()):
        problems += ["before any change:"] + ["  " + line for line in describe_world_differences(before, 20)]
    for name, pick, stage, change, undo in WORLD_CHANGES:
        if differences := world_change_reaches_guest(name, pick, stage, change, undo, refs):
            problems.append(f"{name}: " + "; ".join(f"{f} {t}" for f, t in differences.items()))
            log(f"NOT SYNCED {problems[-1]}")
        else:
            log(f"synced: {name}")
    misc = pick_reference(refs, "MISC")
    if differences := created_reaches_guest(refs, refs[misc]["base"]):
        problems.append("create: " + "; ".join(f"{label} {text}" for label, text in differences.items()))
        log(f"NOT SYNCED {problems[-1]}")
    else:
        log("synced: create")
    if after := world_differences(*sample_worlds()):
        problems += ["after putting everything back:"] + ["  " + line for line in describe_world_differences(after, 20)]
    if problems:
        raise TestFailure("the client's world does not follow the host's:\n  " + "\n  ".join(problems))



def scenario_late_join_gets_cell_state():
    """World sync's state at a cell load, for a game that missed changes: the host locks a door and disables a
    static while the client is away. The client rejoins standing in the same cell (no load to ask with: it asks
    for the cells it has loaded on joining), and its cell is the host's."""
    refs = world_state("")
    door = pick_reference(refs, "DOOR", 0, lambda r: not (r.get("lock") or {}).get("locked"))
    static = pick_reference(refs, "STAT", 3)
    level = (refs[door].get("lock") or {}).get("level", 0)
    def host_changed():
        return (world_state("", [door])[door].get("lock") or {}).get("locked") and world_state("", [static])[static]["flags"] & FORM_DISABLED
    disconnect_client()
    try:
        console_on("", f"{door:X}.lock 50")
        console_on("", f"{static:X}.disable")
        wait_for("[host] the door locked and the static gone", host_changed, 5)
        if not guest_matches(door, 1) or not guest_matches(static, 1):
            raise TestFailure("[client] has the host's changes while away: the test shows nothing")
        rejoin_client()
        differences = {f"{ref:X}": text for ref in (door, static) if (text := guest_matches(ref, 10))}
        if differences:
            raise TestFailure("[client] rejoined with its own cell, not the host's: " +
                              "; ".join(f"{ref} {fields}" for ref, fields in differences.items()))
    finally:
        if game_state("client").get("online") is False:
            rejoin_client()
        lock_on("", door, False, level)
        console_on("", f"{static:X}.enable")
    for ref in (door, static):
        if left := guest_matches(ref, 5):
            raise TestFailure(f"[client] {ref:X} not put back as the host's: {left}")


def scenario_guest_opens_hosts_roll():
    """A container's leveled entries are the host's roll (world sync step 2b): in the inn, the client opens the
    container with the most leveled entries as its player would (an open is what rolls them), and it holds what the
    host's does, which the host's game rolled when the client's inn loaded."""
    try:
        both_go(INN)
        refs = world_state("")
        found = [r for r in refs.values() if r.get("leveled") and r["has_3d"] and not r["flags"] & FORM_DISABLED and r.get("items")]
        if not found:
            raise TestFailure("[host] no container in the inn holds a leveled roll: the test shows nothing")
        chest = max(found, key=lambda r: (r["leveled"], -r["id"]))["id"]
        host = container_items("", chest)
        place_before("client", chest, 100)
        activate_on("client", chest)
        wait_for(f"[client] {chest:X}'s menu shows its lists",
                 lambda: menu_var("client", "ContainerMenu", ITEM_LISTS + ".currentState") in LISTS_SHOWN, 5)
        if (theirs := container_items("client", chest)) != host:
            raise TestFailure(f"[client] opened {chest:X} holding its own roll: " + item_differences(host, theirs, "host", "client"))
        rpc({"cmd": "menu", "name": "ContainerMenu", "show": False})
        wait_for("[client] the menu closed", lambda: takes_input("client"), 5)
    finally:
        both_go(HOME_CELL)


SINKHOLE = "SoljundsSinkhole01"
BASE_PICKED = 0x80  # change record: the base object, e.g. a placed leveled item's pick


def scenario_placed_leveled_items_are_hosts():
    """A reference placed with a leveled base picks it when its cell first loads (Soljund's Sinkhole's clutter book:
    `DummyBook` with XLIB `LItemBookClutter`, 124 books): the client's is the host's pick, its 3D the new base's.
    Where it rests is step 5's (physics), not checked here."""
    try:
        both_go(SINKHOLE)
        refs = world_state("")
        picked = sorted(i for i, r in refs.items() if (r.get("changes") or 0) & BASE_PICKED and i < CREATED_ID)
        if not picked:
            raise TestFailure(f"[host] nothing in {SINKHOLE} picked its base: the test shows nothing")
        def differences():
            host, guest = world_state("", picked), world_state("client", picked)
            found = {}
            for ref in picked:
                a, b = host[ref], guest.get(ref)
                if b is None:
                    found[ref] = "client lacks it"
                elif a["base"] != b["base"]:
                    found[ref] = f"base {a['base']:X} vs {b['base']:X}"
                elif (a["has_3d"], a.get("hidden")) != (b["has_3d"], b.get("hidden")):
                    found[ref] = f"3D {a['has_3d']}/{a.get('hidden')} vs {b['has_3d']}/{b.get('hidden')}"
                elif sorted(a.get("pose") or {}) != sorted(b.get("pose") or {}):
                    found[ref] = f"model nodes {sorted(a.get('pose') or {})} vs {sorted(b.get('pose') or {})}"
            return found
        left = {}
        def matches():
            left.clear()
            left.update(differences())
            return not left
        with contextlib.suppress(TestFailure):
            wait_for(f"[client] {len(picked)} picked bases as the host's", matches, 5)
        if left:
            raise TestFailure("[client] picked its own: " + "; ".join(f"{ref:X} {text}" for ref, text in left.items()))
    finally:
        both_go(HOME_CELL)

COMPANIONS_HOUSEKEEPING = 0x4B2D9  # C00
WEREWOLF_TRANSFORM_VISUAL = 0xF8209  # the change effect's VFXSpell effect (WerewolfTransformVisual): sets stage 1


def script_running(instance, script):
    """A stack in that game's VM runs a function of that script (harness script_stacks), waiting ones too."""
    prefix = script.lower() + "."
    return any(frame.lower().startswith(prefix) for stack in player_command(instance, {"cmd": "script_stacks"})["stacks"]
               for frame in stack["frames"])


def werewolf_running(instance):
    select_instance(instance)
    return bool((rpc({"cmd": "quest", "quest": PLAYER_WEREWOLF_QUEST}) or {}).get("flags", 0) & 1)


def script_var(instance, form, script, name, value=None):
    """A script variable or auto property on that form (harness script_var), set first when a value is given."""
    request = {"cmd": "script_var", "object": form, "script": script, "var": name}
    if value is not None:
        request["value"] = value
    response = player_command(instance, request)
    return response["value"]


def log_trail(instance, since=0):
    """That game's plugin log lines (info and up) numbered `since` and later, and the next number (harness log_trail)."""
    response = player_command(instance, {"cmd": "log_trail", "since": since})
    return response["lines"], response["next"]


def trail_marks():
    """Where each game's log trail is now, to read from on a failure."""
    marks = {}
    for name in ("", "client"):
        try:
            marks[name] = log_trail(name, 1 << 62)[1]
        except (TestFailure, OSError, KeyError):
            marks[name] = None
    return marks


SYMBOLIZE = {}


def symbolized(text):
    """A log line's backtrace frames ("backtrace #NN module+0xOFF") with names (scripts/re/symbolize_crash.py); the
    lock-order checker's own frames left out."""
    if "backtrace #" not in text:
        return text
    if not SYMBOLIZE:
        sys.path.insert(0, str(ROOT / "scripts/re"))
        import symbolize_crash
        SYMBOLIZE.update(module=symbolize_crash, engine=symbolize_crash.engine_resolver())
    module = SYMBOLIZE["module"]
    out = []
    for line in text.split("\n"):
        match = module.FRAME.search(line)
        if not match:
            out.append(line)
            continue
        number, where, offset = match.group(1), match.group(2), int(match.group(3), 16)
        if where.lower() == "skyrimse.exe":
            name = SYMBOLIZE["engine"](offset)
        elif where.lower().startswith(DLL.stem.lower()):
            name = module.symbolize_ours(DLL, offset)
            if "LockOrder.cpp" in name:
                continue
        else:
            name = ""
        out.append(f"      #{number} {where}+{offset:#x}  {name}")
    return "\n".join(out)


def failure_digest(marks, started, failed_at, keep=60, context=10):
    """What each game saw during a failed scenario, up to the failure: its open menus, its log lines from the failing
    step on (marked '>', the last `keep`), the `context` before them and every warning and error of the scenario.
    Printed under the failure, so it explains itself."""
    step = CURRENT_STEP
    out = [f"  failing step: {step['description']}" if step["description"] else "  failing step: none waited on"]
    for name in ("", "client"):
        who = name or "host"
        try:
            lines, _ = log_trail(name, marks.get(name) or 0) if marks.get(name) is not None else ([], 0)
            open_menus = sorted(menus(name) - {"HUD Menu"})
        except (TestFailure, OSError, KeyError) as error:
            out.append(f"  [{who}] no trail: {error}")
            continue
        # A line logged within a moment of the failure is still its own; later ones are the clean-up's.
        before = [line for line in lines if line["time_ms"] / 1000 <= failed_at + 0.25]
        in_step = [line for line in before if line["time_ms"] / 1000 >= step["time"]][-keep:]
        earlier = [line for line in before if line["time_ms"] / 1000 < step["time"]]
        shown = {line["seq"] for line in earlier[-context:]}
        earlier = [line for line in earlier if line["seq"] in shown or line["level"] != "info"]
        left_out = len(lines) - len(in_step) - len(earlier)
        out.append(f"  [{who}] menus open: {', '.join(open_menus) or 'none'}; log lines: {len(in_step)} in the failing step, "
                   f"{len(earlier)} before it" + (f" ({left_out} more left out)" if left_out else "") + ":")
        for line in earlier + in_step:
            at = line["time_ms"] / 1000
            mark = ">" if at >= step["time"] else " "
            level = "" if line["level"] == "info" else f"{line['level']}: "
            out.append(f"  {mark} {at - started:6.2f}s {level}{symbolized(line['text'])}")
    return "\n".join(out)


def engine_events(instance, since=0):
    """That game's engine script events from number `since` on (harness EngineEvents), and the next number."""
    response = player_command(instance, {"cmd": "engine_events", "since": since})
    return response["events"], response["next"]


def race_of(instance, actor):
    return ((((read_status() if select_instance(instance) is None else None) or {}).get("world") or {})
            .get(str(actor), {}).get("identity", {}).get("race"))


def copy_race_on_host():
    """The race the host shows for the client's character (its copy is respawned on a change of form)."""
    select_instance("")
    copy = next(iter(other_players()), None)
    return race_of("", copy) if copy else None


def scenario_client_beast_form_stays_client():
    """The client turns into a werewolf with Beast Form (the Shout key): its own character changes, the host
    shows it changed, and the host's own player does not (upstream's bug: the change effect's
    `Target == Game.GetPlayer()` passed for the client and the transformation went to the host's player).
    The change effect's script runs on the client's game only (the target's owner applies effects), so the
    host's PlayerWerewolfQuest stays stopped. Then the client changes back."""
    host_race = race_of("", 0x14)
    if werewolf_running("client"):
        raise TestFailure("[client] PlayerWerewolfQuest already runs: an earlier change never ended")
    # The Companions' setup marks the first change as the rampage's: the power would only clear the mark.
    if script_var("client", COMPANIONS_HOUSEKEEPING, "CompanionsHousekeepingScript", "PlayerIsWerewolfVirgin", 0):
        raise TestFailure("[client] C00's PlayerIsWerewolfVirgin stays set")
    console_on("client", f"player.addspell {BEAST_FORM:X}")
    console_on("client", f"player.equipspell {BEAST_FORM:X} voice")
    since = 0

    def seen(kind, **fields):
        return lambda: any(e["kind"] == kind and all(e.get(k) == v for k, v in fields.items())
                           for e in engine_events("client", since)[0])

    def werewolf_stage():
        select_instance("client")
        return (rpc({"cmd": "quest", "quest": PLAYER_WEREWOLF_QUEST}) or {}).get("current_stage")

    try:
        wait_for("[client] Beast Form is the selected power", lambda: local_player("client").get("selected_power") == BEAST_FORM, 5)
        _, since = engine_events("client")
        tap("client", "shout")  # a power goes out on the release
        # The chain: the change effect starts PlayerWerewolfQuest (stage 0 preps the shift) and casts the
        # transform visual, which plays the change and sets stage 1 (SetRace) on its SetRace event or after 10s.
        wait_for("[client] the change effect starts on the client's character",
                 seen("magic_effect_apply", effect=WEREWOLF_CHANGE_EFFECT, target=0x14), 5)
        wait_for("[client] PlayerWerewolfQuest starts", seen("quest_start", quest=PLAYER_WEREWOLF_QUEST), 5)
        wait_for("[client] the transform visual starts", seen("magic_effect_apply", effect=WEREWOLF_TRANSFORM_VISUAL, target=0x14), 5)
        wait_for("[client] PlayerWerewolfQuest reaches the shift (stage 1)", lambda: (werewolf_stage() or 0) >= 1, 15)
        wait_for("[client] the client is a werewolf", lambda: race_of("client", 0x14) == WEREWOLF_BEAST_RACE, 10)
        wait_for("[host] shows the client as a werewolf", lambda: copy_race_on_host() == WEREWOLF_BEAST_RACE, 20)
        if race_of("", 0x14) != host_race:
            raise TestFailure(f"[host] the host's player changed race ({race_of('', 0x14):X}): the client's change went to it")
        if werewolf_running(""):
            raise TestFailure("[host] PlayerWerewolfQuest runs on the host: the client's change reached the host's scripts")
    except TestFailure:
        if since:
            log("[client] engine events since the press: " + "; ".join(
                " ".join(f"{k}={v:X}" if isinstance(v, int) and k != "seq" else f"{k}={v}" for k, v in e.items())
                for e in engine_events("client", since)[0]))
        raise
    finally:
        if werewolf_running("client"):
            # The transform visual's OnEffectStart ends after its Utility.Wait(10) with SetStage(1) when the
            # player is not a werewolf: changed back before that, the client would change again.
            wait_for("[client] the transform visual's script is done",
                     lambda: not script_running("client", "WerewolfTransformVisual"), 20)
            console_on("client", f"setstage {PLAYER_WEREWOLF_QUEST:X} 100")  # the quest's change back
            wait_for("[client] human again", lambda: race_of("client", 0x14) != WEREWOLF_BEAST_RACE, 30)
            wait_for("[client] PlayerWerewolfQuest shuts down", lambda: not werewolf_running("client"), 30)
            wait_for("[host] shows the client human again", lambda: copy_race_on_host() != WEREWOLF_BEAST_RACE, 30)
        console_on("client", f"player.removespell {BEAST_FORM:X}")


def scenario_load_door_not_replayed():
    """In the inn, the client walks out through its door: the host's game gets the activation and
    does not replay it (the door would take the client's copy, or anyone, out of the host's cell);
    only the client's own game takes it through, and the host follows by the party tether."""
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
        wait_for("[host] follows the client by the party tether",
                 lambda: "Party travel: following" in log_file.read_bytes()[start:].decode(errors="replace"), 30)
        players_together("outside the inn")
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
              "!": (0x02, True), "?": (0x35, True), "/": (0x35, False)}


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


WAIT_KEY, MENU_RIGHT_KEY, MENU_ACCEPT_KEY = 0x14, 0x20, 0x12   # T, D, E (the game's controlmap.txt)


def calendar(instance):
    return game_state(instance).get("time", {})


def wait_hours(instance, hours):
    """The player opens the Wait menu (T), moves its slider to `hours` (D) and waits (E); returns once
    the wait is over and the menu closed."""
    who = f"[{instance or 'host'}]"
    wait_for(f"{who} the game takes input", lambda: takes_input(instance), 10)
    tap_key(instance, WAIT_KEY)
    wait_for(f"{who} the Wait menu shows its hours", lambda: game_state(instance).get("wait_hours") is not None, 5)
    while (shown := game_state(instance).get("wait_hours")) < hours:
        tap_key(instance, MENU_RIGHT_KEY)
        wait_for(f"{who} the slider moves past {shown:g} h",
                 lambda: (game_state(instance).get("wait_hours") or 0) > shown, 3)
    if shown != hours:
        raise TestFailure(f"{who} the slider shows {shown:g} h, not {hours}")
    tap_key(instance, MENU_ACCEPT_KEY)
    wait_for(f"{who} has waited and the menu closed", lambda: takes_input(instance), 10 + 2 * hours)  # about an hour a second


def same_calendar(tolerance=1 / 1440):
    """Both games show the same date, whole days passed and GameDaysPassed (within a game minute)."""
    host, guest = calendar(""), calendar("client")
    return all(host.get(k) == guest.get(k) for k in ("day", "month", "year", "raw_days")) \
        and abs(host.get("days_passed", 0) - guest.get("days_passed", -1)) < tolerance


def calendars(why):
    host, guest = calendar(""), calendar("client")
    fields = ("days_passed", "raw_days", "day", "month", "year", "hour")
    return f"{why}: host " + ", ".join(f"{k} {host.get(k)}" for k in fields) + \
        "; client " + ", ".join(f"{k} {guest.get(k)}" for k in fields)


def scenario_host_wait_reaches_guest():
    """The host waits 3 hours across midnight in the Wait menu, as a player does: the client's calendar takes
    the jump, days passed (GameDaysPassed and the whole days the engine counts on from) and the date."""
    console_on("", "set gamehour to 22.5")
    wait_for("[client] at the host's 22:30", lambda: close(calendar("client").get("hour"), 22.5, 0.2), 10)
    before = calendar("")
    wait_hours("", 3)
    after = calendar("")
    if after.get("raw_days") != before.get("raw_days") + 1:
        raise TestFailure(f"[host] 3 hours from 22:30 passed {after.get('raw_days') - before.get('raw_days')} midnights")
    try:
        wait_for("[client] days passed and the date follow the host's", same_calendar, 10)
    except TestFailure:
        raise TestFailure(calendars("[client] days passed and the date are not the host's"))


def scenario_guest_sets_time():
    """At 22:30 the client types `/settime 6 0` in the chat: the host's clock runs on to 06:00 the next day, as
    a wait does, and both games show it, days passed and date included. The command is no chat line."""
    console_on("", "set gamehour to 22.5")
    wait_for("[client] at the host's 22:30", lambda: close(calendar("client").get("hour"), 22.5, 0.2), 10)
    before = calendar("")
    say("client", "/settime 6 0")
    wait_for("[host] its clock is at 06:00", lambda: close(calendar("").get("hour"), 6.0, 0.1), 5)
    if calendar("").get("raw_days") != before.get("raw_days") + 1:
        raise TestFailure(f"[host] 22:30 to 06:00 passed {calendar('').get('raw_days') - before.get('raw_days')} midnights")
    try:
        wait_for("[client] at the host's 06:00, days passed and date", lambda: same_calendar()
                 and close(calendar("client").get("hour"), calendar("").get("hour"), 0.05), 10)
    except TestFailure:
        raise TestFailure(calendars("[client] not on the host's calendar after the set time"))
    for name in ("", "client"):
        if any("settime" in l["text"] for l in chat(name).get("lines", [])):
            raise TestFailure(f"[{name or 'host'}] shows the command as a chat line")


def scenario_late_join_gets_calendar():
    """The host waits a day: leaving, the client's own clock counts on from the host's days passed, not those of
    the save it loaded. The host waits again while the client is away: it joins with the host's days passed and date."""
    wait_hours("", 24)
    try:
        wait_for("[client] days passed and the date follow the host's", same_calendar, 10)
    except TestFailure:
        raise TestFailure(calendars("[client] not on the host's calendar after its wait"))
    joined = calendar("client").get("days_passed")
    disconnect_client()
    wait_hours("", 12)
    left = calendar("client").get("days_passed")  # its own clock ran on for the host's wait
    if abs(left - joined) > 0.05:
        raise TestFailure(f"[client] days passed went from {joined} to {left} after leaving")
    if same_calendar():
        raise TestFailure("[client] has the host's days passed while away: the test shows nothing")
    rejoin_client()
    try:
        wait_for("[client] joins with the host's days passed and date", same_calendar, 10)
    except TestFailure:
        raise TestFailure(calendars("[client] joined off the host's calendar"))


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


def clock_speed(instance):
    """How fast the game's clock runs (1 normal; a kill cam or Slow Time slows it)."""
    select_instance(instance)
    return (read_status() or {}).get("clock_speed")


def clocks_run_at(speed, why):
    for name in ("", "client"):
        wait_for(f"[{name or 'host'}] its clock runs at {speed} ({why})",
                 lambda name=name: abs((clock_speed(name) or 0) - speed) < 0.01, 3)


def scenario_clock_slows_for_everyone():
    """A game slowing its clock slows every game's (a kill cam, Slow Time, a bow zoom; here the console's
    `sgtm`, the same engine call), and back to normal, every game is."""
    try:
        for slower in ("client", ""):
            console_on(slower, "sgtm 0.5")
            clocks_run_at(0.5, f"the {slower or 'host'}'s slowed")
            console_on(slower, "sgtm 1")
            clocks_run_at(1.0, f"the {slower or 'host'}'s back")
        # Two at once: the slowest holds both until it ends.
        console_on("", "sgtm 0.5")
        console_on("client", "sgtm 0.25")
        clocks_run_at(0.25, "the client's slower than the host's")
        console_on("client", "sgtm 1")
        clocks_run_at(0.5, "the host's still slowed")
        console_on("", "sgtm 1")
        clocks_run_at(1.0, "both back")
    finally:
        for name in ("", "client"):
            console_on(name, "sgtm 1")


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



# Isolation resets the world/player fixture and removes owned test NPCs between scenarios.
# Cleanup that cannot reach the baseline fails the scenario that left it.
HOME_FILE = "home.json"     # in each game's control dir, written by `duo --keep-running`
TEST_ITEMS = {IRON_SWORD}   # given out by scenarios, never part of the baseline
HOME_TOLERANCE = 40.0       # units from the recorded spot
PLAYER_VALUES = ("health", "magicka", "stamina")


def world_sync_problems():
    host, guest = game_state(""), game_state("client")
    problems = []
    if host.get("weather") != guest.get("weather"):
        problems.append(f"weather: host {host.get('weather')}, guest {guest.get('weather')}")
    if not same_calendar() or not close(host.get("time", {}).get("hour"), guest.get("time", {}).get("hour"), 0.05):
        problems.append(calendars("calendar differs"))
    return problems


def reset_world(home):
    world = home["world"]
    console_on("", f"fw {world['weather']:X}")
    for field, global_name in (("year", "gameyear"), ("month", "gamemonth"), ("day", "gameday"),
                               ("hour", "gamehour"), ("scale", "timescale")):
        console_on("", f"set {global_name} to {world['time'][field]:.8f}")


def record_world_home():
    # Clear noon is the fixture; only the host is forced, and the guest follows normal sync.
    time_ = calendar("")
    world = {"weather": CLEAR_WEATHER, "time": {key: time_[key] for key in ("year", "month", "day", "hour", "scale")},
             "raw_days_at_record": time_.get("raw_days")}
    world["time"]["hour"] = 12.0
    reset_world({"world": world})
    wait_for("baseline: the guest follows the host's weather and calendar", lambda: not world_sync_problems(), 10)
    return world


def save_home(cell_name=HOME_CELL):
    """Records the world fixture and each player's placement, equipment, level and maximum values."""
    world = record_world_home()
    for name in ("", "client"):
        me = player_snapshot(name)
        placement = me.get("placement") or {}
        if not placement.get("position"):
            raise TestFailure(f"{name or 'host'}: no player snapshot to record the baseline from")
        home = {"position": placement["position"], "heading": placement["rotation"][2],
                "equipment": sorted(i for i in me.get("equipment", []) if i not in TEST_ITEMS),
                "level": game_state(name).get("level"), "cell": placement.get("cell"), "cell_name": cell_name,
                "values": {value: me.get("values", {}).get(f"max_{value}") for value in PLAYER_VALUES},
                "world": world}
        atomic_write(CONTROL / HOME_FILE, json.dumps(home) + "\n")
    log("Recorded both players' baseline (home.json)")


def load_home(instance):
    select_instance(instance)
    try:
        return json.loads((CONTROL / HOME_FILE).read_text())
    except (OSError, ValueError):
        return None


def upgrade_home():
    homes = {name: load_home(name) for name in ("", "client")}
    if all(home and home.get("world") and home.get("values") for home in homes.values()):
        return
    if not all(homes.values()):
        raise TestFailure("no player baseline: start duo --keep-running first")
    world = record_world_home()
    for name, home in homes.items():
        me = player_snapshot(name)
        home.update(world=world, cell=me.get("placement", {}).get("cell"), cell_name=HOME_CELL,
                    values={value: me.get("values", {}).get(f"max_{value}") for value in PLAYER_VALUES})
        atomic_write(CONTROL / HOME_FILE, json.dumps(home) + "\n")
    log("Upgraded home.json with the world fixture and player values")


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
        target = home.get("values", {}).get(value)
        if target is not None and not close(values.get(f"max_{value}"), target, 0.5):
            run_console(f"player.setav {value} {target:g}")
        if values.get(value, 0) < (target if target is not None else values.get(f"max_{value}", 0)) - 0.5:
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
    home = load_home("")
    for name in ("", "client"):
        for npc in owned_npcs(name):
            console_on(name, f"{npc:08X}.disable")
            console_on(name, f"{npc:08X}.markfordelete")
    for name in ("", "client"):
        place = load_home(name)
        if place.get("cell") and player_cell(name) != place["cell"]:
            if not place.get("cell_name"):
                raise TestFailure("baseline cell differs and has no coc name; boot duo with --cell")
            console_on(name, f"coc {place['cell_name']}")
            wait_for(f"[{name or 'guest'}] back in the baseline cell",
                     lambda name=name, place=place: player_cell(name) == place["cell"], 60)
    for name in ("", "client"):
        reset_player(name, load_home(name))
    reset_world(home)


def check_baseline():
    """What differs from the baseline in either game (empty: a scenario can start)."""
    problems = []
    world = load_home("")["world"]
    for name in ("", "client"):
        game = name or "host"
        home = load_home(name)
        me = player_snapshot(name)
        state = me.get("state") or {}
        values = me.get("values") or {}
        current = game_state(name)
        if current.get("weather") != world["weather"]:
            problems.append(f"{game}: weather {current.get('weather')}, baseline {world['weather']}")
        for field in ("year", "month", "day", "hour", "scale"):
            if not close(current.get("time", {}).get(field), world["time"][field], 0.05):
                problems.append(f"{game}: calendar {field} {current.get('time', {}).get(field)}, baseline {world['time'][field]}")
        if home.get("cell") and player_cell(name) != home["cell"]:
            problems.append(f"{game}: not in the baseline cell {home['cell']:X}")
        if any(state.get("casting") or []):
            problems.append(f"{game}: player still casting {state['casting']}")
        if foreign_in_hands(state, home):
            problems.append(f"{game}: player holds {state['hands']} in the hands")
        if state.get("weapon_drawn"):
            problems.append(f"{game}: player's hands are raised")
        if sorted(me.get("equipment", [])) != home["equipment"]:
            problems.append(f"{game}: player wears {sorted(me.get('equipment', []))}, baseline {home['equipment']}")
        for value in PLAYER_VALUES:
            if (target := home.get("values", {}).get(value)) is not None and not close(values.get(f"max_{value}"), target, 0.5):
                problems.append(f"{game}: maximum {value} {values.get(f'max_{value}')}, baseline {target}")
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
        if other_player(actors) is None:
            problems.append(f"{game}: does not show the other player")
        if copies := remote_npcs(actors):
            problems.append(f"{game}: still shows NPC copies {[f'{n:08X}' for n in copies]}")
    if not problems:
        problems = check_desync(samples=1)
    return problems


def settle_baseline(timeout=8.0):
    """Apply the baseline once, then wait for the commanded state; report anything left different."""
    try:
        reset_baseline()
        deadline = time.monotonic() + timeout
        while True:
            problems = check_baseline()
            if not problems or time.monotonic() >= deadline:
                return problems
            check_for_crash()
            time.sleep(0.1)
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
    "join-refused-shows-why": scenario_join_refused_shows_why,
    "join-refused-plugins-differ": scenario_join_refused_plugins_differ,
    "difficulty-follows-host": scenario_difficulty_follows_host,
    "global-reaches-guest": scenario_global_reaches_guest,
    "late-join-gets-globals": scenario_late_join_gets_globals,
    "quest-stage-reaches-guest": scenario_quest_stage_reaches_guest,
    "guest-story-starts-no-quest": scenario_guest_story_starts_no_quest,
    "guest-spawns-no-critters": scenario_guest_spawns_no_critters,
    "wildlife-takes-hosts-pick": scenario_wildlife_takes_hosts_pick,
    "guest-joins-by-invite": scenario_guest_joins_by_invite,
    "loaded-save-hosts-then-joins-friend": scenario_loaded_save_hosts_then_joins_friend,
    "death-stage-runs-once": scenario_death_stage_runs_once,
    "quest-completes-for-guest": scenario_quest_completes_for_guest,
    "quest-roles-match-host": scenario_quest_roles_match_host,
    "quest-fill-loads-on-guest": scenario_quest_fill_loads_on_guest,
    "late-join-gets-quests": scenario_late_join_gets_quests,
    # characters
    "steady-state": scenario_steady_state,
    "move-host-npc": scenario_move_host_npc,
    "copy-jumps-with-owner": scenario_copy_jumps_with_owner,
    "remove-host-npc": scenario_remove_host_npc,
    "copy-ends-voice-line": scenario_copy_ends_voice_line,
    "dialogue-peer-starts": scenario_dialogue_peer_starts,
    "dialogue-last-one-out-ends-it": scenario_dialogue_last_one_out_ends_it,
    "dialogue-nobody-moves": scenario_dialogue_nobody_moves,
    "dialogue-join-by-talking": scenario_dialogue_join_by_talking,
    "dialogue-host-in-background": scenario_dialogue_host_in_background,
    "dialogue-pick-reaches-host": scenario_dialogue_pick_reaches_host,
    "dialogue-options-are-the-hosts": scenario_dialogue_options_are_the_hosts,
    "dialogue-quest-option-from-host": scenario_dialogue_quest_option_from_host,
    "dialogue-pick-says-the-hosts-line": scenario_dialogue_pick_says_the_hosts_line,
    "natives-reach-matches-ida": scenario_natives_reach_matches_ida,
    "dialogue-picker-gets-speech-experience": scenario_dialogue_picker_gets_speech_experience,
    "dialogue-picker-trains": scenario_dialogue_picker_trains,
    "dialogue-refused-start": scenario_dialogue_refused_start,
    "dialogue-said-line-waits": scenario_dialogue_said_line_waits,
    "dialogue-ends-on-travel": scenario_dialogue_ends_on_travel,
    "dialogue-said-once-line": scenario_dialogue_said_once_line,
    "dialogue-unanswered-question-ends-it": scenario_dialogue_unanswered_question_ends_it,
    "dialogue-npc-removed": scenario_dialogue_npc_removed,
    "dialogue-npc-starts": scenario_dialogue_npc_starts,
    "dialogue-several-questions-deep": scenario_dialogue_several_questions_deep,
    "teleport-keeps-copies": scenario_teleport_keeps_copies,
    "party-changes-cells": scenario_party_changes_cells,
    "party-stacks-on-arrival": scenario_party_stacks_on_arrival,
    "party-uses-door": scenario_party_uses_door,
    "party-quick-travel": scenario_party_quick_travel,
    "party-fast-travels": scenario_party_fast_travels,
    # inventory
    "equip-host-player": scenario_equip_host_player,
    "equip-client-player": scenario_equip_client_player,
    "equip-host-npc": scenario_equip_host_npc,
    "repair-equipment-drift": scenario_repair_equipment_drift,
    "guest-rides-host-horse": scenario_guest_rides_host_horse,
    # hands
    "draw-host-player": scenario_draw_host_player,
    "draw-client-player": scenario_draw_client_player,
    "sneak-host-player": scenario_sneak_host_player,
    "furniture-host-player": scenario_furniture_host_player,
    "furniture-client-player": scenario_furniture_client_player,
    "sneak-client-player": scenario_sneak_client_player,
    "dive-host-player": scenario_dive_host_player,
    "copy-leaves-water-with-owner": scenario_copy_leaves_water_with_owner,
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
    "host-kill-move": scenario_host_kill_move,
    "client-kill-move": scenario_client_kill_move,
    "clock-slows-for-everyone": scenario_clock_slows_for_everyone,
    # magic
    "burning-shows-on-copy": scenario_burning_shows_on_copy,
    "copy-made-later-shows-effects": scenario_copy_made_later_shows_effects,
    "copy-flashes-one-shot-effects": scenario_copy_flashes_one_shot_effects,
    # objects
    "door-opens-for-both": scenario_door_opens_for_both,
    "chest-take-all": scenario_chest_take_all,
    "guest-loads-propose-no-locks": scenario_guest_loads_propose_no_locks,
    "load-door-not-replayed": scenario_load_door_not_replayed,
    "plate-counts-client": scenario_plate_counts_client,
    "sword-box-asks-client": scenario_sword_box_asks_client,
    "statue-fires-at-client": scenario_statue_fires_at_client,
    "ring-turns-for-client": scenario_ring_turns_for_client,
    "late-join-gets-ring-pose": scenario_late_join_gets_ring_pose,
    "keyhole-asks-client": scenario_keyhole_asks_client,
    "relief-takes-client-relics": scenario_relief_takes_client_relics,
    "words-reach-client": scenario_words_reach_client,
    "cutscene-fade-reaches-party": scenario_cutscene_fade_reaches_party,
    "guest-cutscene-reaches-host": scenario_guest_cutscene_reaches_host,
    "screen-reaches-party": scenario_screen_reaches_party,
    "player-moment-stays-theirs": scenario_player_moment_stays_theirs,
    "player-stage-cutscene-reaches-party": scenario_player_stage_cutscene_reaches_party,
    "guest-script-stage-reaches-host": scenario_guest_script_stage_reaches_host,
    "shrine-blesses-client": scenario_shrine_blesses_client,
    "vilkas-counts-client-hits": scenario_vilkas_counts_client_hits,
    "copy-runs-owners-package": scenario_copy_runs_owners_package,
    "copy-in-owners-combat": scenario_copy_in_owners_combat,
    "guest-player-in-hosts-combat": scenario_guest_player_in_hosts_combat,
    "guest-player-in-switched-combat": scenario_guest_player_in_switched_combat,
    "copy-holds-owners-factions": scenario_copy_holds_owners_factions,
    "client-beast-form-stays-client": scenario_client_beast_form_stays_client,
    # chat
    "chat-reaches-everyone": scenario_chat_reaches_everyone,
    "map-marker-reaches-everyone": scenario_map_marker_reaches_everyone,
    # world
    "host-time-reaches-client": scenario_host_time_reaches_client,
    "host-wait-reaches-guest": scenario_host_wait_reaches_guest,
    "late-join-gets-calendar": scenario_late_join_gets_calendar,
    "guest-sets-time": scenario_guest_sets_time,
    "weather-follows-host": scenario_weather_follows_host,
    "form-state-round-trips": scenario_form_state_round_trips,
    "world-changes-reach-guest": scenario_world_changes_reach_guest,
    "late-join-gets-cell-state": scenario_late_join_gets_cell_state,
    "guest-opens-hosts-roll": scenario_guest_opens_hosts_roll,
    "placed-leveled-items-are-hosts": scenario_placed_leveled_items_are_hosts,
    # input (real keyboard and mouse; only when named or tagged)
    "input-keyboard-draw": scenario_input_keyboard_draw,
    "input-mouse-cast": scenario_input_mouse_cast,
}

# What each scenario exercises, for running only the ones a change can affect:
# `scenario --tag movement,spawn`. Full suites belong to release soaks. Scenarios tagged
# `input` drive the real keyboard and mouse and run only when named or tagged. Scenarios tagged
# `restart` restart a game (minutes): in the full suite, but in a tag run only with `--tag restart`.
TAGS = {
    "session-roster": {'session'},
    "party-panel": {'session', 'ui', 'reload'},
    "party-shows-levels": {'session', 'ui'},
    "late-join-client": {'session', 'spawn', 'inventory', 'hands', 'values'},
    "host-rejoins": {'session', 'spawn'},
    "host-kicks-client": {'session', 'spawn'},
    "join-refused-shows-why": {'session'},
    "join-refused-plugins-differ": {'session', 'restart'},
    "difficulty-follows-host": {'session'},
    "global-reaches-guest": {'world'},
    "late-join-gets-globals": {'world', 'session'},
    "quest-stage-reaches-guest": {'world', 'quest'},
    "guest-story-starts-no-quest": {'world', 'quest', 'cells'},
    "guest-spawns-no-critters": {'world', 'scripts', 'objects'},
    "wildlife-takes-hosts-pick": {'spawn', 'cells'},
    "guest-joins-by-invite": {'session', 'steam', 'spawn', 'ui'},
    "loaded-save-hosts-then-joins-friend": {'session', 'steam', 'ui'},
    "death-stage-runs-once": {'world', 'scripts', 'quest', 'spawn'},
    "quest-completes-for-guest": {'world', 'quest'},
    "quest-roles-match-host": {'world', 'quest'},
    "quest-fill-loads-on-guest": {'quest', 'world'},
    "late-join-gets-quests": {'world', 'quest', 'session'},
    "steady-state": {'session', 'movement', 'spawn'},
    "move-host-npc": {'movement'},
    "copy-jumps-with-owner": {'movement', 'spawn'},
    "remove-host-npc": {'spawn'},
    "copy-ends-voice-line": {'dialogue', 'ai'},
    "dialogue-peer-starts": {'dialogue', 'spawn'},
    "dialogue-last-one-out-ends-it": {'dialogue', 'spawn'},
    "dialogue-nobody-moves": {'dialogue', 'spawn'},
    "dialogue-join-by-talking": {'dialogue', 'spawn'},
    "dialogue-host-in-background": {'dialogue', 'spawn'},
    "dialogue-pick-reaches-host": {'dialogue', 'spawn'},
    "dialogue-quest-option-from-host": {'dialogue', 'spawn', 'quests'},
    "dialogue-options-are-the-hosts": {'dialogue', 'spawn'},
    "dialogue-pick-says-the-hosts-line": {'dialogue', 'spawn'},
    "natives-reach-matches-ida": {'scripts'},
    "dialogue-picker-gets-speech-experience": {'dialogue', 'spawn'},
    "dialogue-picker-trains": {'dialogue', 'spawn'},
    "dialogue-refused-start": {'dialogue', 'spawn'},
    "dialogue-said-line-waits": {'dialogue', 'spawn'},
    "dialogue-ends-on-travel": {'dialogue', 'spawn', 'travel'},
    "dialogue-said-once-line": {'dialogue', 'spawn'},
    "dialogue-unanswered-question-ends-it": {'dialogue', 'spawn'},
    "dialogue-npc-removed": {'dialogue', 'spawn'},
    "dialogue-npc-starts": {'dialogue', 'spawn'},
    "dialogue-several-questions-deep": {'dialogue', 'spawn'},
    "teleport-keeps-copies": {'spawn', 'movement', 'cells'},
    "party-changes-cells": {'spawn', 'cells'},
    "party-stacks-on-arrival": {'cells'},
    "party-uses-door": {'spawn', 'cells', 'objects'},
    "party-quick-travel": {'spawn', 'cells', 'objects'},
    "party-fast-travels": {'spawn', 'cells', 'world'},
    "equip-host-player": {'inventory'},
    "equip-client-player": {'inventory'},
    "equip-host-npc": {'inventory'},
    "repair-equipment-drift": {'inventory'},
    "guest-rides-host-horse": {'spawn', 'movement'},
    "draw-host-player": {'hands'},
    "draw-client-player": {'hands'},
    "sneak-host-player": {'movement', 'ai'},
    "sneak-client-player": {'movement', 'ai'},
    "dive-host-player": {'movement', 'ai', 'dialogue', 'cells'},
    "copy-leaves-water-with-owner": {'movement', 'cells'},
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
    "host-kill-move": {'combat', 'values', 'animation'},
    "client-kill-move": {'combat', 'values', 'animation'},
    "clock-slows-for-everyone": {'world', 'combat'},
    "burning-shows-on-copy": {'combat', 'magic'},
    "copy-made-later-shows-effects": {'magic', 'spawn'},
    "copy-flashes-one-shot-effects": {'magic', 'spawn'},
    "door-opens-for-both": {'objects'},
    "chest-take-all": {'objects', 'inventory'},
    "guest-loads-propose-no-locks": {'objects', 'cells'},
    "load-door-not-replayed": {'objects', 'cells'},
    "plate-counts-client": {'objects', 'scripts', 'cells'},
    "sword-box-asks-client": {'objects', 'scripts', 'cells', 'inventory'},
    "statue-fires-at-client": {'scripts', 'cells', 'magic'},
    "ring-turns-for-client": {'objects', 'scripts', 'cells'},
    "late-join-gets-ring-pose": {'world', 'cells', 'session', 'scripts'},
    "keyhole-asks-client": {'objects', 'scripts', 'cells', 'inventory'},
    "relief-takes-client-relics": {'objects', 'scripts', 'cells', 'inventory'},
    "words-reach-client": {'objects', 'scripts'},
    "cutscene-fade-reaches-party": {'scripts'},
    "guest-cutscene-reaches-host": {'scripts'},
    "screen-reaches-party": {'scripts'},
    "player-moment-stays-theirs": {'scripts'},
    "player-stage-cutscene-reaches-party": {'scripts', 'quests'},
    "guest-script-stage-reaches-host": {'scripts', 'quests', 'world'},
    "shrine-blesses-client": {'objects', 'scripts', 'cells', 'magic'},
    "vilkas-counts-client-hits": {'combat', 'scripts', 'quests', 'inventory'},
    "copy-runs-owners-package": {'spawn', 'ai'},
    "copy-in-owners-combat": {'combat', 'ai'},
    "guest-player-in-hosts-combat": {'combat', 'ai'},
    "guest-player-in-switched-combat": {'combat', 'ai'},
    "copy-holds-owners-factions": {'ai', 'spawn'},
    "client-beast-form-stays-client": {'scripts', 'magic', 'spawn', 'quests'},
    "chat-reaches-everyone": {'ui', 'session'},
    "map-marker-reaches-everyone": {'ui', 'world'},
    "host-time-reaches-client": {'world'},
    "host-wait-reaches-guest": {'world'},
    "late-join-gets-calendar": {'world', 'session'},
    "guest-sets-time": {'world', 'ui'},
    "weather-follows-host": {'world', 'cells'},
    "form-state-round-trips": {'world', 'objects', 'spawn'},
    "world-changes-reach-guest": {'world', 'objects', 'inventory'},
    "late-join-gets-cell-state": {'world', 'objects', 'session'},
    "guest-opens-hosts-roll": {'world', 'objects', 'inventory', 'cells'},
    "placed-leveled-items-are-hosts": {'world', 'objects', 'cells'},
    "input-keyboard-draw": {'input', 'hands'},
    "input-mouse-cast": {'input', 'hands', 'magic'},
}


# All matching globs contribute; core asks for a full suite without selecting it.
SOURCE_TAGS = [
    # Shared specs change the Skyrim contract as well as the fake's checks.
    ("Code/tests/specs/*", {'spec'}),
    # Every game's lifecycle, dispatch, hooks and shared engine access.
    ("Code/client/World.*", {'core'}),
    ("Code/client/*Pch.h", {'core'}),
    ("Code/client/Components.h", {'core'}),
    ("Code/client/main.cpp", {'core'}),
    ("Code/client/skse_*", {'core'}),
    ("Code/client/Plugin*", {'core'}),
    ("Code/client/RunningGame.*", {'core'}),
    ("Code/client/Utils.*", {'core'}),
    ("Code/client/SessionIds.*", {'core'}),
    ("Code/client/VersionDb.*", {'core'}),
    ("Code/client/SkseRipAllocator.h", {'core'}),
    ("Code/client/TaskQueue.*", {'core'}),
    ("Code/client/HookSwitch.*", {'core'}),
    ("Code/client/Breadcrumbs.*", {'core'}),
    ("Code/client/CrashHandler.*", {'core'}),
    ("Code/client/HeapHistory.*", {'core'}),
    ("Code/client/NvidiaUtil.*", {'core'}),
    ("Code/client/Hooks/*", {'core'}),
    ("Code/client/Services/*TransportService.*", {'core'}),
    ("Code/client/Services/*RunnerService.*", {'core'}),
    ("Code/client/Services/Host/HostAuthority.*", {'core'}),
    ("Code/client/Services/Host/HostNetwork.*", {'core'}),
    ("Code/client/FormIds.h", {'core'}),

    # Each game's systems, including headers beside the Generic implementations.
    ("Code/client/Services/*InventoryService.*", {'inventory'}),
    ("Code/client/Services/*CharacterService.*", {'spawn', 'movement', 'animation', 'ai', 'dialogue', 'combat', 'cells'}),
    ("Code/client/Services/*ActorValueService.*", {'values'}),
    ("Code/client/Services/*CombatService.*", {'combat'}),
    ("Code/client/Services/*MagicService.*", {'magic'}),
    ("Code/client/Services/*EffectVisualsService.*", {'magic'}),
    ("Code/client/Services/*AiStateService.*", {'ai'}),
    ("Code/client/Services/*DialogueService.*", {'dialogue'}),
    ("Code/client/Services/*ObjectService.*", {'objects'}),
    ("Code/client/Services/*CutsceneService.*", {'scripts'}),
    ("Code/client/Services/*PapyrusService.*", {'scripts', 'reload'}),
    ("Code/client/Services/*PlayerService.*", {'session', 'values', 'spawn'}),
    ("Code/client/Services/*SessionService.*", {'session'}),
    ("Code/client/Services/*SteamService.*", {'steam'}),
    ("Code/client/Services/*MapService.*", {'ui', 'world'}),
    ("Code/client/Services/*TravelService.*", {'cells', 'travel'}),
    ("Code/client/Services/*TimeScaleService.*", {'world', 'combat'}),
    ("Code/client/Services/*CommandService.*", {'world', 'ui'}),
    ("Code/client/Services/*ImguiService.*", {'ui'}),
    ("Code/client/Services/Debug*", {'ui'}),
    ("Code/client/Services/JoinStatus.h", {'session'}),
    ("Code/client/Services/WorldStateLog.h", {'world', 'quest', 'objects'}),

    # Hosting/joining constructs the role's systems; scripts also cross these entry points.
    ("Code/client/Services/HostService.*", {'session', 'scripts', 'world', 'objects'}),
    ("Code/client/Services/GuestService.*", {'session', 'scripts', 'world', 'quest'}),
    ("Code/client/Services/Host/HostSession.*", {'session'}),
    ("Code/client/Services/Host/HostPlayers.*", {'session'}),
    ("Code/client/Services/Host/JoinRefusal.*", {'session', 'restart'}),
    ("Code/client/Services/Host/HostCharacters.*", {'spawn', 'movement', 'cells'}),
    ("Code/client/Services/Host/HostGame.h", {'spawn', 'world', 'quest', 'objects', 'session'}),
    ("Code/client/Services/Host/HostConversation.*", {'dialogue', 'scripts'}),
    ("Code/client/Services/Host/HostDialogue.*", {'dialogue'}),
    ("Code/client/Services/Guest/GuestDialogue.*", {'dialogue'}),
    ("Code/client/Services/Host/HostPlayerNatives.*", {'scripts', 'quest'}),
    ("Code/client/Services/Host/HostClock.*", {'world'}),
    ("Code/client/Services/Guest/GuestClock.*", {'world'}),
    ("Code/client/Services/Host/HostWeather.*", {'world', 'cells'}),
    ("Code/client/Services/Guest/GuestWeather.*", {'world', 'cells'}),
    ("Code/client/Services/Host/HostTimeScale.*", {'world', 'combat'}),
    ("Code/client/Services/Host/HostWorldState.*", {'world', 'quest', 'objects', 'cells', 'inventory'}),
    ("Code/client/Services/Guest/GuestWorldState.*", {'world', 'quest', 'objects', 'cells', 'inventory'}),

    # Adapters span systems; the common bridge and actor ownership underpin every scenario.
    ("Code/client/Bridges/SkyrimBridge.*", {'core'}),
    ("Code/client/Bridges/I*Bridge.h", {'core'}),
    ("Code/client/Bridges/HostEngine.*", {'world', 'quest', 'objects', 'cells', 'inventory'}),
    ("Code/client/Bridges/GuestEngine.*", {'world', 'quest', 'objects', 'cells', 'inventory', 'scripts'}),
    ("Code/client/Bridges/DialogueBridge.*", {'dialogue'}),
    ("Code/client/Bridges/SteamBridge.*", {'steam'}),
    ("Code/client/Games/Skyrim/LoadGameEvents.*", {'steam'}),
    ("Code/client/Events/GameLoadedEvent.h", {'steam'}),
    ("Code/client/Events/SessionNoticeEvent.h", {'steam', 'ui'}),
    ("Code/client/Bridges/ContainerItems.*", {'objects', 'inventory'}),
    ("Code/client/Bridges/PhysicsRests.*", {'world', 'objects'}),
    ("Code/client/Components/*AnimationComponent.h", {'animation', 'movement', 'hands'}),
    ("Code/client/Components/*WeaponDrawComponent.h", {'hands'}),
    ("Code/client/Components/*Component.h", {'spawn'}),
    ("Code/client/Components/ActorValuesComponent.h", {'values'}),
    ("Code/client/Components/CombatComponent.h", {'combat'}),
    ("Code/client/Components/InterpolationComponent.h", {'movement'}),
    ("Code/client/Components/OwnerPlaceComponent.h", {'movement', 'cells'}),
    ("Code/client/Components/WaitingFor*", {'spawn'}),
    ("Code/client/Events/*ConnectedEvent.h", {'core'}),
    ("Code/client/Events/DisconnectedEvent.h", {'core'}),
    ("Code/client/Events/UpdateEvent.h", {'core'}),
    ("Code/client/Events/Listen.h", {'core'}),
    ("Code/client/Events/Actor*Event.h", {'spawn'}),
    ("Code/client/Events/Reference*Event.h", {'spawn', 'movement', 'cells'}),
    ("Code/client/Events/PlayerArrivedEvent.h", {'spawn', 'cells'}),
    ("Code/client/Events/PlayerFormChangeEvent.h", {'session', 'spawn'}),
    ("Code/client/Events/*NativeEvent.h", {'scripts'}),
    ("Code/client/Events/WorldCallEvent.h", {'scripts', 'quest'}),
    ("Code/client/Events/Papyrus*Event.h", {'scripts'}),
    ("Code/client/Events/ScriptAnimationEvent.h", {'scripts', 'objects'}),
    ("Code/client/Events/*Inventory*Event.h", {'inventory'}),
    ("Code/client/Events/EquipmentChangeEvent.h", {'inventory'}),
    ("Code/client/Events/ContainerChangeEvent.h", {'inventory', 'objects'}),
    ("Code/client/Events/ActivateEvent.h", {'objects'}),
    ("Code/client/Events/LockChangeEvent.h", {'objects'}),
    ("Code/client/Events/AiStateChangeEvent.h", {'ai'}),
    ("Code/client/Events/FactionsChangeEvent.h", {'ai'}),
    ("Code/client/Events/DialogueEvent.h", {'dialogue'}),
    ("Code/client/Events/SubtitleEvent.h", {'dialogue'}),
    ("Code/client/Events/HealthChangeEvent.h", {'values'}),
    ("Code/client/Events/HitEvent.h", {'combat'}),
    ("Code/client/Events/AddExperienceEvent.h", {'combat'}),
    ("Code/client/Events/ProjectileLaunchedEvent.h", {'combat', 'magic'}),
    ("Code/client/Events/*Spell*Event.h", {'magic'}),
    ("Code/client/Events/*CastEvent.h", {'magic'}),
    ("Code/client/Events/AddTargetEvent.h", {'magic'}),
    ("Code/client/Events/EffectVisualsChangeEvent.h", {'magic'}),
    ("Code/client/Events/BeastFormChangeEvent.h", {'magic', 'spawn', 'scripts'}),
    ("Code/client/Events/MountEvent.h", {'movement'}),
    ("Code/client/Events/DismountEvent.h", {'movement'}),
    ("Code/client/Events/DifficultyChangeEvent.h", {'session'}),
    ("Code/client/Events/MoveActorEvent.h", {'movement'}),
    ("Code/client/Events/*WaypointEvent.h", {'world', 'ui'}),
    ("Code/client/Events/SetTimeCommandEvent.h", {'world', 'ui'}),
    ("Code/client/Events/TimeScaleEvent.h", {'world', 'combat'}),
    ("Code/client/Systems/AnimationSystem.*", {'animation', 'movement', 'hands'}),
    ("Code/client/Systems/InterpolationSystem.*", {'movement'}),
    ("Code/client/Systems/FaceGenSystem.*", {'spawn'}),
    ("Code/client/Systems/CacheSystem.*", {'spawn'}),
    ("Code/client/Systems/RenderSystem*", {'ui'}),
    ("Code/client/ModCompat/*", {'animation', 'movement', 'hands'}),
    ("Code/client/Ui/*", {'ui', 'session'}),

    # Papyrus cause/routing rules, natives and the VM; Party* stays scripts only.
    ("Code/client/Games/Skyrim/Papyrus/*", {'scripts'}),
    ("Code/client/Games/Skyrim/Papyrus/*Events*", {'quest', 'quests'}),
    ("Code/client/Games/Skyrim/Papyrus/GuestScripts.*", {'quest', 'quests'}),
    ("Code/client/Games/Skyrim/Misc/BSScript.*", {'scripts', 'quest'}),
    ("Code/client/Games/Skyrim/Misc/GameVM.*", {'scripts', 'quest'}),
    ("Code/client/Games/Skyrim/Misc/NativeFunction.h", {'scripts'}),
    ("Code/client/Games/Skyrim/Misc/BGSStoryManager.*", {'quest', 'scripts'}),
    ("Code/client/SkyrimVM64.cpp", {'scripts'}),
    ("Code/client/ScriptValues.*", {'scripts'}),

    # Engine ownership, storage, names and frame/render plumbing are shared.
    ("Code/client/Games/ActorExtension.*", {'core'}),
    ("Code/client/Games/*Memory.*", {'core'}),
    ("Code/client/Games/*Primitives.*", {'core'}),
    ("Code/client/Games/*Rtti.*", {'core'}),
    ("Code/client/Games/Skyrim/RTTI.*", {'core'}),
    ("Code/client/Games/EngineNames.h", {'core'}),
    ("Code/client/Games/Events.h", {'core'}),
    ("Code/client/Games/*Renderer.*", {'core'}),
    ("Code/client/Games/System.cpp", {'core'}),
    ("Code/client/Games/*TES.*", {'core'}),
    ("Code/client/Games/ModManager.*", {'core'}),
    ("Code/client/Games/Overrides.h", {'core'}),
    ("Code/client/Games/Crc64.*", {'core'}),
    ("Code/client/Games/INISettingCollection.*", {'core'}),
    ("Code/client/Games/Skyrim/BSCore/*", {'core'}),
    ("Code/client/Games/Skyrim/BSSystem/*", {'core'}),
    ("Code/client/Games/Skyrim/BSGraphics/*", {'core'}),
    ("Code/client/Games/Skyrim/BSRandom/*", {'core'}),
    ("Code/client/Games/Skyrim/Events/*", {'core'}),
    ("Code/client/Games/Skyrim/Default*", {'core'}),
    ("Code/client/Games/Skyrim/NetImmerse/*", {'core'}),

    # Actors, their copies, animation, combat, input and magic glue.
    ("Code/client/Games/Skyrim/RefrInteraction.h", {'movement'}),
    ("Code/client/Games/Skyrim/Actor.*", {'spawn', 'movement', 'values', 'combat', 'ai', 'hands'}),
    ("Code/client/Games/Skyrim/PlayerCharacter.*", {'session', 'spawn', 'cells', 'combat', 'values'}),
    ("Code/client/Games/*Animation*", {'animation', 'movement', 'hands'}),
    ("Code/client/Games/Skyrim/Havok/*", {'animation', 'movement', 'combat'}),
    ("Code/client/Games/*CopyAiState.*", {'ai', 'combat', 'movement'}),
    ("Code/client/Games/ActorUpkeep.h", {'combat'}),
    ("Code/client/Games/Skyrim/AiStateEvents.*", {'ai'}),
    ("Code/client/Games/Skyrim/QuestEvents.*", {'quest'}),
    ("Code/client/Games/Skyrim/GlobalEvents.*", {'world', 'quest'}),
    ("Code/client/Games/Skyrim/ActorValueEvents.*", {'values', 'combat'}),
    ("Code/client/Games/QuestChanges.h", {'quest'}),
    ("Code/client/Games/ScriptAssignments.h", {'quest'}),
    ("Code/client/Games/DeathActions.h", {'combat', 'values'}),
    ("Code/client/Events/ActorDeathChangeEvent.h", {'combat', 'values'}),
    ("Code/client/Events/ActorValueChangeEvent.h", {'values'}),
    ("Code/client/Games/Skyrim/FactionEvents.*", {'ai'}),
    ("Code/client/Games/Skyrim/AI/*", {'ai', 'movement', 'scripts'}),
    ("Code/client/Games/Skyrim/BSInput/*", {'input', 'hands', 'magic', 'scripts'}),
    ("Code/client/Games/Skyrim/Camera/*", {'scripts', 'combat'}),
    ("Code/client/Games/Skyrim/Combat/*", {'combat', 'ai'}),
    ("Code/client/Games/*Magic/*", {'magic', 'combat'}),
    ("Code/client/Games/Skyrim/Effects/*", {'magic', 'values'}),
    ("Code/client/Games/Skyrim/EffectEvents.*", {'magic'}),
    ("Code/client/Games/Skyrim/Projectiles/*", {'combat', 'magic'}),
    ("Code/client/Games/Skyrim/EquipManager.*", {'inventory', 'hands', 'magic'}),
    ("Code/client/Games/Skyrim/ContainerEvents.*", {'inventory', 'objects'}),
    ("Code/client/Games/Skyrim/PlayerEvents.*", {'session', 'spawn', 'magic', 'scripts'}),
    ("Code/client/Games/Skyrim/PlayerCollision.*", {'cells', 'movement'}),
    ("Code/client/Games/Skyrim/Misc/ActorState.*", {'hands', 'movement'}),
    ("Code/client/Games/Skyrim/Misc/ActorValueOwner.h", {'values'}),
    ("Code/client/Games/Skyrim/Misc/*Movement*.h", {'movement'}),
    ("Code/client/Games/Skyrim/Misc/IPostAnimation*", {'animation'}),
    ("Code/client/Games/Skyrim/Misc/MiddleProcess.h", {'ai', 'combat', 'magic'}),
    ("Code/client/Games/Skyrim/Misc/InventoryEntry.h", {'inventory'}),
    ("Code/client/Games/Skyrim/Misc/TintMask.h", {'spawn'}),
    ("Code/client/Games/Skyrim/Misc/BS*String.*", {'core'}),
    ("Code/client/Games/Misc/*", {'dialogue', 'ai', 'objects'}),

    # Reference persistence, travel, the calendar and menus.
    ("Code/client/Games/Form*", {'world', 'objects', 'spawn'}),
    ("Code/client/Games/IFormFactory.h", {'world', 'spawn'}),
    ("Code/client/Games/*References.*", {'spawn', 'movement', 'ai', 'objects'}),
    ("Code/client/Games/*ReferenceStates.*", {'world', 'objects'}),
    ("Code/client/Games/BGSCreatedObjectManager.*", {'world', 'spawn', 'inventory'}),
    ("Code/client/Games/*SaveLoad.*", {'world', 'objects', 'spawn', 'reload'}),
    ("Code/client/Games/*TimeManager.*", {'world'}),
    ("Code/client/Games/Skyrim/TESObjectREFR.*", {'core'}),
    ("Code/client/Games/Skyrim/ReferenceEvents.*", {'world', 'objects', 'cells'}),
    ("Code/client/Games/Skyrim/LoadingScreen.cpp", {'cells', 'travel'}),
    ("Code/client/Games/Skyrim/Fixes/*", {'cells'}),
    ("Code/client/Games/Skyrim/BSTimer.*", {'world', 'combat'}),
    ("Code/client/Games/Skyrim/Sky/*", {'world', 'cells'}),
    ("Code/client/Games/Skyrim/Interface/*", {'ui', 'dialogue', 'input', 'scripts'}),
    ("Code/client/Games/Skyrim/Components/*", {'spawn', 'inventory', 'magic', 'ai', 'quest'}),
    ("Code/client/Games/Skyrim/ExtraData/*", {'inventory', 'objects', 'ai'}),

    # Engine forms: quest state and dialogue are distinct from equipment and shared base forms.
    ("Code/client/Games/Skyrim/Forms/*Quest*", {'quest', 'quests', 'scripts'}),
    ("Code/client/Games/Skyrim/Forms/*Alias*", {'quest'}),
    ("Code/client/Games/Skyrim/Forms/BGSStoryManager*", {'quest', 'scripts'}),
    ("Code/client/Games/Skyrim/Forms/*Topic*", {'dialogue'}),
    ("Code/client/Games/Skyrim/Forms/ActorValueInfo.*", {'values'}),
    ("Code/client/Games/Skyrim/Forms/*Item*", {'inventory', 'magic'}),
    ("Code/client/Games/Skyrim/Forms/EffectSetting.h", {'magic'}),
    ("Code/client/Games/Skyrim/Forms/TESAmmo.h", {'inventory', 'combat'}),
    ("Code/client/Games/Skyrim/Forms/TESObjectARM*.h", {'inventory'}),
    ("Code/client/Games/Skyrim/Forms/TESObjectWEAP.h", {'inventory', 'combat'}),
    ("Code/client/Games/Skyrim/Forms/BGSOutfit.h", {'inventory', 'spawn'}),
    ("Code/client/Games/Skyrim/Forms/TESGlobal.h", {'world'}),
    ("Code/client/Games/Skyrim/Forms/FormChanges.*", {'world', 'objects'}),
    ("Code/client/Games/Skyrim/Forms/TESObjectCELL.*", {'world', 'cells', 'objects'}),
    ("Code/client/Games/Skyrim/Forms/TESWorldSpace.*", {'world', 'cells'}),
    ("Code/client/Games/Skyrim/Forms/TESWeather.h", {'world', 'cells'}),
    ("Code/client/Games/Skyrim/Forms/TESLev*", {'world', 'objects'}),
    ("Code/client/Games/Skyrim/Forms/TESIdleForm.h", {'animation'}),
    ("Code/client/Games/Skyrim/Forms/BGSAction.h", {'animation'}),
    ("Code/client/Games/Skyrim/Forms/TESFaction.h", {'ai'}),
    ("Code/client/Games/Skyrim/Forms/TESPackage.h", {'ai'}),
    ("Code/client/Games/Skyrim/Forms/TESNPC.*", {'spawn'}),
    ("Code/client/Games/Skyrim/Forms/TESActorBase.h", {'spawn'}),
    ("Code/client/Games/Skyrim/Forms/TESRace.h", {'spawn', 'magic'}),
    ("Code/client/Games/Skyrim/Forms/BGSHeadPart.h", {'spawn'}),
    ("Code/client/Games/Skyrim/Forms/Script.h", {'scripts'}),
    ("Code/client/Games/Skyrim/Forms/BGSLocation.h", {'world', 'quest'}),
    ("Code/client/Games/Skyrim/Forms/BGSEncounterZone.h", {'world', 'spawn'}),
    ("Code/client/Games/Skyrim/Forms/BGSKeyword.h", {'core'}),
    ("Code/client/Games/Skyrim/Forms/BGSListForm.h", {'core'}),
    ("Code/client/Games/Skyrim/Forms/TESForm.h", {'core'}),
    ("Code/client/Games/Skyrim/Forms/TESObject.h", {'core'}),
    ("Code/client/Games/Skyrim/Forms/TESBound*.h", {'core'}),

    # Harness commands select their systems; staging and shared readouts gate every run.
    ("Code/client/TestHarness/CommandsInput.cpp", {'input', 'hands', 'magic', 'movement'}),
    ("Code/client/TestHarness/CommandsSession.cpp", {'session', 'reload'}),
    ("Code/client/TestHarness/CommandsWorld.cpp", {'core', 'world', 'quest', 'scripts', 'dialogue', 'inventory', 'objects', 'ai', 'animation'}),
    ("Code/client/TestHarness/CommandsStaging.cpp", {'core', 'inventory', 'movement', 'ai', 'ui'}),
    ("Code/client/TestHarness/Commands.*", {'core'}),
    ("Code/client/TestHarness/AnimationTrace.*", {'animation', 'combat'}),
    ("Code/client/TestHarness/PlayerInput.*", {'input', 'hands', 'magic', 'movement', 'combat', 'objects', 'dialogue'}),
    ("Code/client/TestHarness/InputTrace.*", {'input'}),
    ("Code/client/TestHarness/PlayerEvents.cpp", {'scripts'}),
    ("Code/client/TestHarness/QuestFragments.cpp", {'quest', 'scripts'}),
    ("Code/client/TestHarness/ChangeLog.*", {'world', 'objects'}),
    ("Code/client/TestHarness/Session.*", {'session'}),
    ("Code/client/TestHarness/CommandServer.*", {'core'}),
    ("Code/client/TestHarness/ConsoleOutput.*", {'core'}),
    ("Code/client/TestHarness/ControlFiles.*", {'core'}),
    ("Code/client/TestHarness/EngineCalls.*", {'core'}),
    ("Code/client/TestHarness/EngineEvents.*", {'core'}),
    ("Code/client/TestHarness/Harness.h", {'core'}),
    ("Code/client/TestHarness/LogTrail.*", {'core'}),
    ("Code/client/TestHarness/Status.*", {'core'}),
    ("Code/client/TestHarness/TestHarness.*", {'core'}),
    ("Code/client/HotReload/*", {'core', 'reload'}),
    ("Code/hotreload/*", {'core', 'reload'}),
    ("Code/vanilla_harness/*", {'core'}),

    # Wire, factories and shared payloads carry every system; leaf messages name their system.
    ("Code/encoding/Wire/*", {'core'}),
    ("Code/encoding/*Pch.h", {'core'}),
    ("Code/encoding/Opcodes.h", {'core'}),
    ("Code/encoding/MetaMessage.h", {'core'}),
    ("Code/encoding/ChatMessageTypes.h", {'ui', 'session'}),
    ("Code/encoding/Messages/Message.*", {'core'}),
    ("Code/encoding/Messages/*MessageFactory.*", {'core'}),
    ("Code/encoding/Messages/Payloads.*", {'core'}),
    ("Code/encoding/Messages/Relayed*.h", {'core'}),
    ("Code/encoding/Messages/*Inventory*", {'inventory'}),
    ("Code/encoding/Messages/*Equipment*", {'inventory'}),
    ("Code/encoding/Messages/*Container*", {'inventory', 'objects'}),
    ("Code/encoding/Messages/*DrawWeapon*", {'hands'}),
    ("Code/encoding/Messages/*Actor*Value*", {'values'}),
    ("Code/encoding/Messages/*Health*", {'values'}),
    ("Code/encoding/Messages/*Death*", {'values', 'spawn'}),
    ("Code/encoding/Messages/*Respawn*", {'spawn', 'values'}),
    ("Code/encoding/Messages/*Spell*", {'magic'}),
    ("Code/encoding/Messages/*Cast*", {'magic', 'hands'}),
    ("Code/encoding/Messages/*Effect*", {'magic'}),
    ("Code/encoding/Messages/*AddTarget*", {'magic', 'combat'}),
    ("Code/encoding/Messages/*Projectile*", {'combat', 'magic'}),
    ("Code/encoding/Messages/*Experience*", {'combat'}),
    ("Code/encoding/Messages/*AiState*", {'ai'}),
    ("Code/encoding/Messages/*Factions*", {'ai'}),
    ("Code/encoding/Messages/*Dialogue*", {'dialogue'}),
    ("Code/encoding/Messages/*Subtitle*", {'dialogue'}),
    ("Code/encoding/Messages/*Character*", {'spawn'}),
    ("Code/encoding/Messages/*Assignment*", {'spawn'}),
    ("Code/encoding/Messages/*ReferencesMove*", {'movement'}),
    ("Code/encoding/Messages/*Mount*", {'movement'}),
    ("Code/encoding/Messages/*EnterCell*", {'cells'}),
    ("Code/encoding/Messages/*PartyTravel*", {'cells', 'travel'}),
    ("Code/encoding/Messages/*Authentication*", {'session', 'restart'}),
    ("Code/encoding/Messages/*PlayerJoined*", {'session'}),
    ("Code/encoding/Messages/*PlayerLeft*", {'session'}),
    ("Code/encoding/Messages/*PlayerLevel*", {'session', 'ui'}),
    ("Code/encoding/Messages/*SettingsChange*", {'session'}),
    ("Code/encoding/Messages/*ChatMessage*", {'session', 'ui'}),
    ("Code/encoding/Messages/*Waypoint*", {'world', 'ui'}),
    ("Code/encoding/Messages/*Time*", {'world', 'combat'}),
    ("Code/encoding/Messages/*Weather*", {'world', 'cells'}),
    ("Code/encoding/Messages/*WorldState*", {'world', 'quest', 'objects'}),
    ("Code/encoding/Messages/*CellState*", {'world', 'objects', 'cells'}),
    ("Code/encoding/Messages/*Activate*", {'objects'}),
    ("Code/encoding/Messages/*LockChange*", {'objects'}),
    ("Code/encoding/Messages/*Native*", {'scripts'}),
    ("Code/encoding/Messages/*WorldCall*", {'scripts', 'quest'}),

    # Shared structs and transport; ownership ids and load order are common infrastructure.
    ("Code/encoding/Structs/*Animation*", {'animation', 'movement', 'hands'}),
    ("Code/encoding/Structs/ActionEvent.*", {'animation', 'hands', 'movement'}),
    ("Code/encoding/Structs/ActorAiState.*", {'ai'}),
    ("Code/encoding/Structs/ActorData.*", {'spawn'}),
    ("Code/encoding/Structs/ActorValues.*", {'values'}),
    ("Code/encoding/Structs/ActorWhereabouts.*", {'spawn', 'cells'}),
    ("Code/encoding/Structs/DialogueOption.*", {'dialogue'}),
    ("Code/encoding/Structs/Faction*", {'ai'}),
    ("Code/encoding/Structs/Game*", {'core'}),
    ("Code/encoding/Structs/LoadOrder.*", {'core'}),
    ("Code/encoding/Structs/Mods.*", {'core'}),
    ("Code/encoding/Structs/GridCellCoords.*", {'cells'}),
    ("Code/encoding/Structs/Inventory.*", {'inventory'}),
    ("Code/encoding/Structs/LockData.*", {'objects'}),
    ("Code/encoding/Structs/MagicEquipment.*", {'inventory', 'magic'}),
    ("Code/encoding/Structs/Movement.*", {'movement'}),
    ("Code/encoding/Structs/ReferenceUpdate.*", {'movement', 'animation'}),
    ("Code/encoding/Structs/*NetQuantize.*", {'movement'}),
    ("Code/encoding/Structs/ScriptValue.*", {'scripts'}),
    ("Code/encoding/Structs/TimeModel.*", {'world'}),
    ("Code/encoding/Structs/Tints.*", {'spawn'}),
    ("Code/encoding/Structs/WeaponState.h", {'hands', 'magic'}),
    ("Code/network/*", {'core'}),

    # Shared libraries, rendering dependencies, build recipes and test tooling.
    ("Code/base/*", {'core'}),
    ("Code/common/*", {'core'}),
    ("Code/external/*", {'core'}),
    ("Code/components/*", {'core'}),
    ("Code/*xmake.lua", {'core'}),
    ("Code/.clang-format", {'core'}),
    ("scripts/*", {'core'}),
    ("xmake.lua", {'core'}),
]


def validate_source_tags():
    known = set().union(*TAGS.values()) | {'core'}
    for pattern, tags in SOURCE_TAGS:
        if unknown := tags - known:
            raise TestFailure(f"SOURCE_TAGS {pattern}: unknown tag(s) {sorted(unknown)}")


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
    host_on_client = (client.get("actors") or {}).get(str(other_player(client.get("actors") or {})))
    client_on_host = (host.get("actors") or {}).get(str(other_player(host.get("actors") or {})))
    rows.append(("host's character", describe(host_player), describe(host_on_client)))
    rows.append(("client's character", describe(client_on_host), describe(client_player)))
    # Pair each host NPC with the client copy linked to the same server actor; anything left
    # unpaired on either side is a desync (a copy the host no longer has, or a missing copy).
    copies = {v.get("session_id"): v for v in (client.get("actors") or {}).values() if not v.get("other_player")}
    for npc, owned in sorted((host_player.get("local_actors") or {}).items()):
        copy = copies.pop(owned.get("session_id"), None)
        gap = f"  {distance(owned['position'], copy['actual']):.0f} apart" if copy and owned.get("position") else ""
        mine, theirs = owned.get("identity") or {}, (copy or {}).get("identity") or {}
        # Same name and race, or the copy is a different-looking actor (built from the wrong record).
        wrong = "" if not copy or (mine.get("name"), mine.get("race")) == (theirs.get("name"), theirs.get("race")) \
            else f" WRONG-ACTOR({theirs.get('name') or '?'})"
        label = f"NPC {mine.get('name') or ''} {int(npc):08X}".replace("  ", " ")[:20]
        rows.append((label, describe(owned), (describe(copy) + wrong if copy else "MISSING") + gap))
    for session_id, copy in copies.items():
        rows.append((f"NPC server {session_id}", "GONE on host", describe(copy)))
    lines = [f"{'':20s} {'HOST game':24s} {'CLIENT game':24s}"]
    lines += [f"{who:20s} {h:24s} {c:24s}" for who, h, c in rows]
    return "\n".join(lines)


# --- Desync checker -------------------------------------------------------------------------
# Each game's status.json carries "world": a snapshot of every loaded actor (TestHarness/Status.cpp
# Snapshot()). Every actor has exactly one owner game; the other game holds a copy linked by the
# same session id. The checker pairs them and compares every snapshot field, so a field added to
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
    if field == "ai_state.combat_target":
        return None  # each game's own form id for the target (a player is 14 on its own game): copy-in-owners-combat
    if field == "ai_state.package" and (owner or 0) >= 0xFF000000:
        return None  # a package the engine made has no id the other game shares: the copy holds do-nothing
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
    return f"{name} [{game} {int(form_id):08X}, sid {snapshot['mod']['session_id']}]"


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
    copies = {game: {s["mod"]["session_id"]: (fid, s) for fid, s in world.items() if s["role"] == "copy"}
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
            sid = owned["mod"]["session_id"]
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


def save_file(instance, name):
    select_instance(instance)
    return MY_GAMES / "Saves" / f"{name}.ess"


def save_game(instance, name):
    """`save <name>` in that game; returns where its .ess lands (wait for it with saves_written)."""
    path = save_file(instance, name)
    for stale in (path, path.with_suffix(".skse")):
        stale.unlink(missing_ok=True)
    console_on(instance, f"save {name}")
    return path


def saves_written(*paths):
    """A wait_for predicate: every save exists, with its co-save, and is the same size twice 0.5s apart."""
    seen = {}
    def written():
        sizes = tuple(p.stat().st_size if p.exists() and p.with_suffix(".skse").exists() else -1 for p in paths)
        if -1 in sizes or seen.get("sizes") != sizes:
            seen.update(sizes=sizes, since=time.monotonic())
            return False
        return time.monotonic() - seen["since"] >= 0.5
    return written


def command_save_diff(args):
    """Saves both games at once and compares the two worlds (scripts/re/save_diff.py)."""
    sys.path.insert(0, str(ROOT / "scripts/re"))
    import save_diff
    name = f"coopdiff-{args.label}"
    with contextlib.redirect_stdout(sys.stderr if args.json else sys.stdout):  # --json: stdout is the report
        host, client, noise = save_both(name, args.noise, save_diff)
    host = save_diff.Save(host)
    report = save_diff.compare(host, save_diff.Save(client), noise, save_diff.editor_ids(host, GAME / "Data"))
    if args.json:
        print(json.dumps(report, indent=1, default=lambda o: sorted(o) if isinstance(o, set) else str(o)))
    else:
        save_diff.print_report(report, args.details)


def save_both(name, noise_seconds, save_diff):
    """Both games saved as `name`, and with `noise_seconds` the host's noise (save_diff.noise_from)."""
    host, client = save_game("", name), save_game("client", name)
    wait_for("both saves written", saves_written(host, client), 60)
    noise = None
    if noise_seconds:
        # The same game again: what changes on its own in that time is noise, not drift.
        started = time.monotonic()
        wait_for(f"{noise_seconds:.0f}s of play", lambda: time.monotonic() - started >= noise_seconds, noise_seconds + 5)
        again = save_game("", name + "-again")
        wait_for("the second host save written", saves_written(again), 60)
        noise = save_diff.noise_from(host, again)
    return host, client, noise


def quest_snapshot(instance, quest):
    """One quest as that game holds it now (harness `quest`): flags, stage, done stages, objectives, fills."""
    select_instance(instance)
    response = rpc({"cmd": "quest", "quest": quest}) or {}
    if not response.get("ok"):
        raise TestFailure(f"[{instance or 'host'}] quest {quest:08X}: {response.get('error', 'no answer')}")
    return {"flags": response["flags"], "stage": response["current_stage"],
            "done": sorted(stage for stage, done in response["stages"] if done),
            "objectives": {index: state for index, state in response["objectives"] if state},
            "aliases": {a["id"]: (a["ref"], a["name"]) for a in response["aliases"]},
            "bases": {a["id"]: a.get("base", 0) for a in response["aliases"]},
            "locations": {a["id"]: a["location"] for a in response["aliases"] if a.get("location")}}


def snapshot_differences(host, client, names):
    """What differs between the host's and the client's snapshot of one quest, readable."""
    def ref(form_id):
        return names.get(form_id, f"{form_id:08X}") if form_id else "unfilled"

    out = []
    if host["flags"] & SYNCED_QUEST_FLAGS != client["flags"] & SYNCED_QUEST_FLAGS:
        out.append(f"flags {host['flags'] & SYNCED_QUEST_FLAGS:#06x} / {client['flags'] & SYNCED_QUEST_FLAGS:#06x}")
    if host["stage"] != client["stage"]:
        out.append(f"stage {host['stage']} / {client['stage']}")
    for label, a, b in (("done only on host", host["done"], client["done"]),
                        ("done only on client", client["done"], host["done"])):
        if missing := sorted(set(a) - set(b)):
            out.append(f"{label}: {missing}")
    for index in sorted(set(host["objectives"]) | set(client["objectives"])):
        a, b = host["objectives"].get(index, 0), client["objectives"].get(index, 0)
        if a != b:
            out.append(f"objective {index}: {a} / {b}")
    for alias in sorted(set(host["aliases"]) | set(client["aliases"])):
        (a, name), (b, _) = host["aliases"].get(alias, (0, "?")), client["aliases"].get(alias, (0, "?"))
        # A reference a game created has an id of its own: not synced yet (docs/progress.md, loose ends).
        if a != b and not (a >= 0xFF000000 or b >= 0xFF000000):
            out.append(f"alias {name}: {ref(a)} / {ref(b)}")
    return out


def travel_on_host(command):
    """A console command that moves the host far (coc): the host arrives elsewhere, the client next to it."""
    def where(instance):
        return local_player(instance).get("position")
    before = where("")
    console_on("", command)
    wait_for(f"[host] arrives after '{command}'",
             lambda: (here := where("")) and math.dist(here, before) > 2000, 90)
    wait_for("[client] arrives with the host (the party tether)",
             lambda: (a := where("")) and (b := where("client")) and math.dist(a, b) < 1000, 90)


def command_quest_walk(args):
    """Walks the host through a quest's stages on a running duo (boot it with `duo --save`), the client
    leaving and rejoining on the way, and reports at each step how the client's quest compares."""
    sys.path.insert(0, str(ROOT / "scripts/re"))
    import save_diff
    found = save_diff.plugin_quest(GAME / "Data" / args.plugin, args.quest)
    if not found:
        raise TestFailure(f"no quest {args.quest} in {args.plugin}")
    local_id, plugin_stages = found
    select_instance("")
    order = [p.lower() for p in save_diff.Save(save_file("", args.names_from)).plugins]
    quest = order.index(args.plugin.lower()) << 24 | local_id & 0xFFFFFF
    names = save_diff.editor_ids(save_diff.Save(save_file("", args.names_from)), GAME / "Data")
    done_now = set(quest_snapshot("", quest)["done"])
    if args.stages:
        route = [int(s) for s in args.stages.split(",")]
    elif args.route_from:
        # The stages a later save of the same playthrough has done: the route its player took.
        later = save_diff.Save(save_file("", args.route_from))
        parsed = save_diff.parse_quest(later, later.forms[quest])
        route = sorted(stage for stage, done in parsed.get("stages", {}).items() if done and stage not in done_now)
    else:
        start = quest_snapshot("", quest)["stage"]
        route = [stage for stage in plugin_stages if stage > start and stage not in done_now]
    log(f"{args.quest} ({quest:08X}): route {route}")

    steps = []

    def compare(label, guest_here=True):
        host = quest_snapshot("", quest)
        if not guest_here:
            steps.append({"step": label, "host_stage": host["stage"], "guest": "away"})
            return
        # The client gets the time a sync would need; a report, so a miss is recorded, not raised.
        deadline, differences = time.monotonic() + args.settle, None
        while True:
            differences = snapshot_differences(host, quest_snapshot("client", quest), names)
            if not differences or time.monotonic() >= deadline:
                break
            time.sleep(0.1)
            host = quest_snapshot("", quest)
        steps.append({"step": label, "host_stage": host["stage"], "differences": differences})
        log(f"{label}: " + ("matches" if not differences else "; ".join(differences)))

    compare("start (both loaded the save)")
    if not quest_snapshot("", quest)["flags"] & 1:  # not running: `setstage` needs it started (aliases filled)
        # --start-by: the trip that makes the game start it (a story manager event); else `startquest`.
        if args.start_by:
            for command in (c.strip() for c in args.start_by.split(";")):
                if command.startswith("wait "):  # a start condition the host's game reaches itself
                    needed, needed_stage = command[5:].split(":")
                    needed_id = next(form for form, name in names.items() if name == needed)
                    wait_for(f"[host] {needed} reaches stage {needed_stage}",
                             lambda: int(needed_stage) in quest_snapshot("", needed_id)["done"], 180)
                else:
                    travel_on_host(command)
        else:
            console_on("", f"startquest {args.quest}")
        wait_for(f"[host] {args.quest} runs", lambda: quest_snapshot("", quest)["flags"] & 1, 60)
        compare("host's game starts the quest")
    away = False
    for index, stage in enumerate(route):
        if args.leave_after is not None and index == args.leave_after:
            disconnect_client()
            away = True
            steps.append({"step": "guest leaves", "host_stage": quest_snapshot("", quest)["stage"], "guest": "away"})
        if away and index == args.leave_after + args.away:
            rejoin_client()
            away = False
            compare("guest rejoins")
        console_on("", f"setstage {args.quest} {stage}")
        wait_for(f"[host] {args.quest} stage {stage} done", lambda: stage in quest_snapshot("", quest)["done"], 15)
        compare(f"host sets stage {stage}", guest_here=not away)
        if not quest_snapshot("", quest)["flags"] & 1:  # the stage's script ended the quest (a branch)
            log(f"{args.quest} ended at stage {quest_snapshot('', quest)['stage']}: the walk stops")
            break
    if away:
        rejoin_client()
        compare("guest rejoins")

    # The whole world at the end: every quest and global that differs, from both games' saves.
    host_path, client_path, _ = save_both(f"questwalk-{args.quest}", 0, save_diff)
    host_save, client_save = save_diff.Save(host_path), save_diff.Save(client_path)
    report = save_diff.compare(host_save, client_save, None, names)
    quests = [f for f in report["forms"] if f["type"] == "QUST"]
    globals_ = report["global_variables"]

    out_dir = TEST_HOME / "quest-walks"
    out_dir.mkdir(parents=True, exist_ok=True)
    out = out_dir / f"{time.strftime('%Y%m%d-%H%M%S')}-{args.quest}.json"
    out.write_text(json.dumps({"quest": args.quest, "route": route, "steps": steps,
                               "quests": quests, "global_variables": globals_}, indent=1, default=str))

    print(f"\n{args.quest}: {sum(1 for s in steps if s.get('differences') == [])} of "
          f"{sum(1 for s in steps if 'differences' in s)} checked steps match")
    for s in steps:
        state = "guest away" if s.get("guest") == "away" else "matches" if not s["differences"] else \
            "; ".join(s["differences"])
        print(f"  {s['step']:<32} host at {s['host_stage']:<4} {state}")
    print(f"\nAt the end, {len(quests)} quests and {len(globals_)} globals differ between the games:")
    for f in quests[:args.details]:
        print(f"  {f['label']}: " + ("; ".join(f.get("fields", [])) or f.get("kind", "")))
    for g in globals_[:args.details]:
        print(f"  global {g['label']}: {g['host']} / {g['client']}")
    print(f"\nReport: {out}")


# The engine's FormType (CommonLibSSE-NG FormTypes.h) -> record name, for change-rate.
FORM_TYPE_NAMES = dict(enumerate(
    "NONE TES4 GRUP GMST KYWD LCRT AACT TXST MICN GLOB CLAS FACT HDPT EYES RACE SOUN ASPC SKIL MGEF SCPT LTEX ENCH "
    "SPEL SCRL ACTI TACT ARMO BOOK CONT DOOR INGR LIGH MISC APPA STAT SCOL MSTT GRAS TREE FLOR FURN WEAP AMMO NPC_ "
    "LVLN KEYM ALCH IDLM NOTE COBJ PROJ HAZD SLGM LVLI WTHR CLMT SPGD RFCT REGN NAVI CELL REFR ACHR PMIS PARW PGRE "
    "PBEA PFLA PCON PBAR PHZD WRLD LAND NAVM TLOD DIAL INFO QUST IDLE PACK CSTY LSCR LVSP ANIO WATR EFSH TOFT EXPL "
    "DEBR IMGS IMAD FLST PERK BPTD ADDN AVIF CAMS CPTH VTYP MATT IPCT IPDS ARMA ECZN LCTN MESG RGDL DOBJ LGTM MUSC "
    "FSTP FSTS SMBN SMQN SMEN DLBR MUST DLVW WOOP SHOU EQUP RELA SCEN ASTP OTFT ARTO MATO MOVT SNDR DUAL SNCT SOPM "
    "COLL CLFM REVB LENS LSPR VOLI".split()))


def change_log(instance):
    select_instance(instance)
    response = rpc({"cmd": "change_log"}, timeout=10) or {}
    if not response.get("ok"):
        raise TestFailure(f"[{instance or 'host'}] change_log: {response.get('error', 'no answer (a test build?)')}")
    return response


def print_change_rates(game, log_):
    """One game's change records per form type and flag, per second of the window."""
    sys.path.insert(0, str(ROOT / "scripts/re"))
    import save_diff
    seconds = max(log_["seconds"], 0.001)
    by_type = {}
    for bucket in log_["buckets"]:
        by_type.setdefault(FORM_TYPE_NAMES.get(bucket["type"], str(bucket["type"])), []).append(bucket)
    total = sum(b["calls"] for b in log_["buckets"])
    print(f"\n{game}: {total} change calls in {seconds:.1f}s ({total / seconds:.1f}/s)"
          + ("  [tables full: counts are partial]" if log_.get("full") else ""))
    for name, buckets in sorted(by_type.items(), key=lambda kv: -sum(b["calls"] for b in kv[1])):
        calls = sum(b["calls"] for b in buckets)
        distinct = log_["forms"].get(str(buckets[0]["type"]), 0)
        print(f"  {name}  {calls} calls ({calls / seconds:.2f}/s), {distinct} distinct forms recorded")
        for b in sorted(buckets, key=lambda b: -b["calls"])[:10]:
            flags = "+".join(save_diff.flag_names(name, b["flags"])) or "0"
            samples = " ".join(f"{s:08X}" for s in b["samples"])
            print(f"      {b['calls']:7d} {b['calls'] / seconds:8.2f}/s  recorded {b['recorded']:7d}  "
                  f"{'forced ' if b['forced'] else ''}{flags}  e.g. {samples}")


def command_change_rate(args):
    """Counts the engine's change records (Code/client/TestHarness/ChangeLog.cpp) over a window: idle for
    --seconds, or while the named scenarios run."""
    games = ("", "client") if args.instance in (None, "both") else ("client" if args.instance == "client" else "",)
    for name in games:
        select_instance(name)
        if not (rpc({"cmd": "change_log_reset"}) or {}).get("ok"):
            raise TestFailure(f"[{name or 'host'}] change_log_reset refused (a test build with ChangeLog?)")
    if args.scenarios:
        with contextlib.redirect_stdout(sys.stderr if args.json else sys.stdout):
            command_scenario(argparse.Namespace(names=args.scenarios, tag=None, hold=0, repeat=1, shuffle=False,
                                                seed=None, fail_fast=False))
    else:
        started = time.monotonic()
        wait_for(f"{args.seconds:.0f}s of play", lambda: time.monotonic() - started >= args.seconds, args.seconds + 5)
    logs = {name or "host": change_log(name) for name in games}
    if args.json:
        print(json.dumps(logs, indent=1))
        return
    for game, log_ in logs.items():
        print_change_rates(game, log_)


PLUGIN_LOADED = re.compile(r"\] Loaded \d+ resources$")  # the plugin's start (and each hot reload's)
UNSYNCED_LINE = re.compile(r"^\[([^\]]+)\].*Reference sync: change flags ([0-9A-F]+) of ([0-9A-F]+) have no row")


def command_unsynced(args):
    """The reference change flags the host's game had with no row to send them (ReferenceStates, logged the first
    time each since the plugin loaded): what vanilla still needs a row for."""
    sys.path.insert(0, str(ROOT / "scripts/re"))
    import save_diff
    lines = plugin_log("").read_text(errors="replace").splitlines()
    loads = [i for i, line in enumerate(lines) if PLUGIN_LOADED.search(line)]
    found = {}
    for line in lines[loads[-1] if loads else 0:]:
        if match := UNSYNCED_LINE.match(line):
            stamp, flags, ref = match.group(1), int(match.group(2), 16), int(match.group(3), 16)
            for bit in range(32):
                if flags >> bit & 1:
                    found.setdefault(bit, (stamp, ref))
    if not found:
        print("every reference change the host had went out with a row")
        return
    for bit, (stamp, ref) in sorted(found.items()):
        print(f"  {1 << bit:#010x}  {save_diff.flag_names('REFR', 1 << bit)[0]:24s} first on {ref:X}  ({stamp})")


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


def scenario_names(requested, tag):
    names = requested or [name for name in SCENARIOS if "input" not in TAGS.get(name, set()) or tag]
    if tag:
        wanted = set(tag.split(","))
        # Restarting a game takes minutes: only when named, or asked for by its tag.
        if "restart" not in wanted:
            names = [name for name in names if "restart" not in TAGS.get(name, set()) or name in (requested or [])]
        known = set().union(*TAGS.values())
        if wanted - known:
            raise TestFailure(f"unknown tag(s) {sorted(wanted - known)}; known: {', '.join(sorted(known))}")
        names = [name for name in names if TAGS.get(name, set()) & wanted]
    unknown = [name for name in names if name not in SCENARIOS]
    if unknown:
        raise TestFailure(f"unknown scenario(s) {unknown}; known: {', '.join(SCENARIOS)}")
    return names


def source_matches(path):
    return [(pattern, tags) for pattern, tags in SOURCE_TAGS if fnmatch.fnmatchcase(path, pattern)]


def affected_source(path):
    if path.startswith("Code/tests/specs/"):
        return True
    if path.startswith(("docs/", "Code/tests/")):
        return False
    return path.startswith(("Code/", "scripts/")) or Path(path).suffix in {
        '.c', '.cc', '.cpp', '.cxx', '.h', '.hpp', '.inl', '.py', '.sh', '.ps1', '.lua',
    }


def changed_sources(base):
    paths = set()
    for command in (["git", "diff", "--name-only", "-z", base, "--"],
                    ["git", "ls-files", "--others", "--exclude-standard", "-z"]):
        result = subprocess.run(command, cwd=ROOT, capture_output=True, text=True)
        if result.returncode:
            raise TestFailure(f"{' '.join(command)}: {result.stderr.strip()}")
        paths.update(filter(None, result.stdout.split("\0")))
    return paths


def command_affected(args):
    if args.check:
        sources = sorted(p.relative_to(ROOT).as_posix() for p in (ROOT / "Code").rglob("*")
                         if p.is_file() and affected_source(p.relative_to(ROOT).as_posix()))
        uncovered = [path for path in sources if not source_matches(path)]
        for path in uncovered:
            print(f"no tag for {path}")
        if uncovered:
            raise TestFailure(f"{len(uncovered)} source file(s) have no SOURCE_TAGS pattern")
        print(f"SOURCE_TAGS covers all {len(sources)} files under Code/ (excluding other Code/tests/).")
        if not args.run:
            return

    paths = args.files if args.files is not None else changed_sources(args.base)
    reasons = collections.defaultdict(list)
    uncovered = []
    for path in sorted({Path(path).as_posix() for path in paths}):
        if not affected_source(path):
            continue
        matches = source_matches(path)
        if not matches:
            print(f"no tag for {path}")
            uncovered.append(path)
        for pattern, tags in matches:
            for tag in tags:
                reasons[tag].append((path, pattern))

    print("tags: " + (", ".join(sorted(reasons)) or "none"))
    for tag in sorted(reasons):
        print(f"  {tag}:")
        for path, pattern in reasons[tag]:
            print(f"    {path} <- {pattern}")
    if 'core' in reasons:
        print("core changed: the full suite covers this, run it before a release")
        if args.run:
            print("The full suite was not run; --run selects only the other tags' scenarios.")
    tags = ",".join(sorted(set(reasons) - {'core'}))
    names = scenario_names([], tags) if tags else []
    print(f"scenarios ({len(names)}):")
    for name in names:
        print(f"  {name}")
    if uncovered:
        raise TestFailure(f"{len(uncovered)} changed source file(s) have no SOURCE_TAGS pattern; no scenarios run")
    if args.run and names:
        if len(names) == len(SCENARIOS):
            raise TestFailure("affected selected the full suite; run it explicitly with scenario")
        command_scenario(argparse.Namespace(names=[], tag=tags, hold=0, repeat=1, shuffle=False,
                                            seed=None, fail_fast=False, dry_run=args.dry_run))
    elif args.run:
        print("No targeted scenarios to run.")


def command_scenario(args):
    global HOLD
    HOLD = args.hold
    names = scenario_names(args.names, args.tag)
    if getattr(args, "dry_run", False):
        print("Would run scenario" + (f" --tag {args.tag}" if args.tag else ":"))
        for name in names:
            print(f"  {name}")
        return
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
    upgrade_home()
    order = [name for _ in range(args.repeat) for name in names]
    seed = args.seed
    if args.shuffle:
        seed = args.seed if args.seed is not None else random.randrange(1 << 30)
        random.Random(seed).shuffle(order)
        log(f"Shuffled order (--seed {seed} repeats it): {' '.join(order)}")
    observe = getattr(args, "observe", lambda event, **data: None)
    observe("order", order=order, seed=seed)

    suite_start = time.monotonic()
    log("=== baseline: resetting both games")
    if problems := settle_baseline():
        raise TestFailure("cannot reach the baseline before the first scenario: " + " | ".join(problems))
    # (reference, field) pairs the two worlds already differ in: a scenario is checked for new ones only.
    if baseline_world := check_world():
        log("warning: the worlds differ at the baseline: " + " | ".join(describe_world_differences(baseline_world, 20)))
    known_world = world_pairs(baseline_world)
    runs = []  # (name, seconds, result)
    for index, name in enumerate(order):
        log(f"=== {name}: {SCENARIOS[name].__doc__}")
        observe("start", name=name, index=index)
        scenario_start = time.monotonic()
        marks, wall_start = trail_marks(), time.time()
        CURRENT_STEP.update(description=None, time=wall_start)
        try:
            known_locks = lock_order_reports()
            SCENARIOS[name]()
            wait_for("the guest follows the host's weather and calendar", lambda: not world_sync_problems(), 5)
            # Whatever the scenario did, both games must agree afterwards.
            problems = check_desync()
            if problems:
                raise TestFailure("desync afterwards: " + " | ".join(problems))
            if world := check_world(known_world):
                raise TestFailure(f"world differs afterwards ({len(world)} references): " +
                                  " | ".join(describe_world_differences(world, 20)))
            new_locks = [v for k, v in lock_order_reports().items() if k not in known_locks]
            for game, report, ours in new_locks:
                if not ours:
                    log(f"warning: engine-only lock-order pattern on {game}: {report}")
            if ours_locks := [f"{game}: {report}" for game, report, ours in new_locks if ours]:
                raise TestFailure("lock order (our code on the stack): " + " | ".join(ours_locks))
            result = "PASS"
        except TestFailure as error:
            result = f"FAIL ({error})"
            log(f"=== {name}: what each game saw\n" + failure_digest(marks, wall_start, error.time))
        # Back to the baseline for the next one; what the reset cannot undo is this scenario's.
        leftovers = settle_baseline()
        if leftovers:
            left = "left behind: " + " | ".join(leftovers)
            result = f"FAIL ({left})" if result == "PASS" else f"{result} [{left}]"
        elif world := check_world(known_world):
            # Left different at home: this scenario's, not the next one's.
            left = f"world left different ({len(world)} references): " + " | ".join(describe_world_differences(world, 20))
            result = f"FAIL ({left})" if result == "PASS" else f"{result} [{left}]"
            known_world |= world_pairs(world)
        runs.append((name, time.monotonic() - scenario_start, result))
        observe("result", name=name, seconds=runs[-1][1], result=result)
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


def soak_snapshot():
    games = {}
    for name, role in (("", "host"), ("client", "guest")):
        select_instance(name)
        status = read_status()
        stale = status is None
        if stale:
            try:
                status = json.loads((CONTROL / "status.json").read_text())
            except (OSError, ValueError):
                status = {}
        sample = {"frames": status.get("frames"), "locks": status.get("lock_order", {}).get("reports", []),
                  "heartbeat_ms": status.get("heartbeat_ms"), "memory": None, "stale_status": stale,
                  "build": status.get("game", {}).get("build"), "generation": status.get("reload", {}).get("generation")}
        try:
            pid = game_process(name)
            fields = dict(line.split(":", 1) for line in Path(f"/proc/{pid}/status").read_text().splitlines() if ":" in line)
            sample["memory"] = {"pid": pid, **{key: int(fields.get(field, "0 kB").split()[0]) * 1024
                                for key, field in (("rss_bytes", "VmRSS"), ("swap_bytes", "VmSwap"), ("virtual_bytes", "VmSize"))}}
        except (TestFailure, OSError, ValueError) as error:
            sample["unavailable"] = str(error)
        games[role] = sample
    return games


def soak_frame_deltas(before, after):
    result = {}
    for role in ("host", "guest"):
        first, last = before.get(role, {}), after.get(role, {})
        old, new = first.get("frames") or {}, last.get("frames") or {}
        pid = (first.get("memory") or {}).get("pid")
        same_process = pid is not None and pid == (last.get("memory") or {}).get("pid") \
            and first.get("generation") == last.get("generation")
        result[role] = {key: new[key] - old[key] for key in ("frames", "stalls")
                        if same_process and key in new and key in old and new[key] >= old[key]}
    return result


def soak_facts(rounds):
    crashes, locks, failures = set(), set(), set()
    for round_ in rounds:
        crashes.update(crash["signature"] for crash in round_.get("crashes", []))
        for sample in [round_.get("before", {}), round_.get("after", {})] + [
                entry.get(phase, {}) for entry in round_.get("scenarios", []) for phase in ("before", "after")]:
            for game in sample.values():
                for report in game.get("locks", []):
                    match = LOCK_PATTERN.match(report)
                    sites = match.group(3) if match else report
                    locks.add(" vs ".join(sorted(sites.split(" vs "))))
        failures.update(entry["name"] for entry in round_.get("scenarios", []) if entry.get("result", "").startswith("FAIL"))
    return crashes, locks, failures


def soak_summary(rounds, previous, memory_rounds=None):
    facts, old = soak_facts(rounds), soak_facts(previous)
    summary = {key: sorted(current - known) for key, current, known in zip(
        ("new_crash_signatures", "new_lock_patterns", "first_failures"), facts, old)}
    growth = []
    for role in ("host", "guest"):
        values = []
        for round_ in memory_rounds if memory_rounds is not None else rounds:
            # Fresh processes are compared at the same boundary: after the shuffled suite.
            memory = round_.get("after", {}).get(role, {}).get("memory")
            if memory and round_.get("complete"):
                values.append({"round": round_["index"], "bytes": memory["rss_bytes"] + memory["swap_bytes"]})
        if len(values) >= 2 and all(b["bytes"] > a["bytes"] for a, b in zip(values, values[1:])) \
                and values[-1]["bytes"] - values[0]["bytes"] >= 16 * 1024 * 1024:
            growth.append({"game": role, "samples": values, "growth_bytes": values[-1]["bytes"] - values[0]["bytes"]})
    summary["memory_growth"] = growth
    summary["round_errors"] = [{"round": r["index"], "message": r["error"]} for r in rounds
                               if r.get("error") and not r.get("scenarios")]
    return summary


def soak_summary_line(summary):
    findings = [f"{len(summary[key])} {label[:-1] if len(summary[key]) == 1 else label}" for key, label in (
        ("new_crash_signatures", "new crash signatures"), ("new_lock_patterns", "new lock patterns"),
        ("first_failures", "first failures"), ("memory_growth", "memory growth trends"),
        ("round_errors", "round errors")) if summary[key]]
    return ", ".join(findings) or "nothing new"


def soak_crashes(before, directory):
    found = []
    for name, role in (("", "host"), ("client", "guest")):
        for path in crash_logs(name):
            if path in before[name]:
                continue
            try:
                report = crash_report(name, path)
            except (OSError, ValueError, subprocess.SubprocessError, ImportError, TestFailure) as error:
                report = f"Could not symbolize crash: {error}"
            destination = directory / f"{role}-{path.name}"
            shutil.copy2(path, destination)
            report_file = destination.with_suffix(".report.txt")
            report_file.write_text(report)
            text = path.read_text(errors="replace")
            frames = re.findall(r"^\s*\[\d+\]\[P\].*?(\S+\.(?:exe|dll)\+[0-9A-Fa-f]+)", text, re.MULTILINE)[:5]
            exception = re.search(r'Unhandled exception "([^"]+)"', text)
            signature = (exception.group(1) if exception else "unknown exception") + ": " + (" > ".join(frames) or "no probable frames")
            found.append({"game": role, "log": str(destination), "report": str(report_file),
                          "signature": signature, "top_frames": frames})
            KNOWN_CRASHES[name].add(path)
    return found


def stop_duo():
    for name in ("", "client"):
        select_instance(name)
        if not stop_game():
            raise TestFailure(f"{name or 'guest'}: could not stop the test game")
    for _, connection in list(_rpc_connections.items()):
        connection[2].close()
        connection[1].close()
    _rpc_connections.clear()


def command_soak(args):
    if args.tag is not None and not args.tag.strip():
        raise TestFailure("--tag must name a tag; omit it only for a full suite")
    if args.rounds is not None and args.rounds <= 0:
        raise TestFailure("--rounds must be positive")
    hours = args.hours if args.hours is not None else (6 if args.rounds is None else None)
    if hours is not None and (not math.isfinite(hours) or hours <= 0):
        raise TestFailure("--hours must be finite and positive")
    names = scenario_names([], args.tag) if args.tag else list(SCENARIOS)
    if not names:
        raise TestFailure("no scenarios selected")
    seed = args.seed if args.seed is not None else random.randrange(1 << 30)
    if args.dry_run:
        print(f"Would boot a fresh duo each round; {str(args.rounds) + ' rounds' if args.rounds else str(hours) + ' hours'}; "
              f"seed {seed}; {len(names)} scenarios")
        return

    base = TEST_HOME / "soak"
    previous_files = sorted(base.glob("*/report.json"), key=lambda p: p.stat().st_mtime)
    previous, previous_file = {}, None
    for path in reversed(previous_files):
        try:
            previous = json.loads(path.read_text())
            previous_file = str(path)
            break
        except (OSError, ValueError):
            continue
    directory = base / (time.strftime("%Y%m%d-%H%M%S") + "-" + uuid.uuid4().hex[:6])
    directory.mkdir(parents=True)
    report_file = directory / "report.json"
    report = {"schema": 1, "started": time.time(), "seed": seed, "tag": args.tag, "hours": hours,
              "requested_rounds": args.rounds, "previous_report": previous_file, "rounds": []}
    started = time.monotonic()
    deadline = started + hours * 3600 if hours is not None else math.inf

    def save_report():
        report["seconds"] = time.monotonic() - started
        report["summary"] = soak_summary(report["rounds"], previous.get("rounds", []))
        atomic_write(report_file, json.dumps(report, indent=2) + "\n")

    save_report()
    log(f"Soak {'smoke test' if args.tag else 'full suite'}; report: {report_file}")
    try:
        while time.monotonic() < deadline and (args.rounds is None or len(report["rounds"]) < args.rounds):
            index = len(report["rounds"]) + 1
            round_dir = directory / f"round-{index:04d}"
            round_dir.mkdir()
            round_ = {"index": index, "seed": seed + index - 1, "scenarios": [], "attempted_order": [], "crashes": [],
                      "runner_log": str(round_dir / "runner.log"), "phase": "setup"}
            round_["order"] = list(names)
            random.Random(round_["seed"]).shuffle(round_["order"])
            report["rounds"].append(round_)
            prior = previous.get("rounds", []) + report["rounds"][:-1]
            crash_before = {name: set(crash_logs(name)) for name in ("", "client")}
            round_started = time.monotonic()
            log(f"=== soak round {index}, seed {round_['seed']}: {len(names)} scenarios")
            save_report()

            def observe(event, **data):
                if event == "order":
                    round_["order"] = data["order"]
                elif event == "start":
                    round_["attempted_order"].append(data["name"])
                    round_["scenarios"].append({**data, "seed": round_["seed"], "result": "RUNNING", "before": soak_snapshot()})
                elif event == "result":
                    entry = round_["scenarios"][-1]
                    entry.update(data, after=soak_snapshot())
                    entry["frame_delta"] = soak_frame_deltas(entry["before"], entry["after"])
                    if data["result"] != "PASS":
                        entry["failed_step"] = dict(CURRENT_STEP)
                save_report()

            try:
                with (round_dir / "runner.log").open("w", buffering=1) as output, \
                        contextlib.redirect_stdout(output), contextlib.redirect_stderr(output):
                    stop_duo()
                    boot_start = time.monotonic()
                    round_["phase"] = "boot"
                    run_dir = new_run_dir(f"soak-round-{index:04d}")
                    round_["run_dir"] = str(run_dir)
                    save_report()
                    duo = argparse.Namespace(cell=HOME_CELL, save=None, boot_timeout=240, world_timeout=180,
                                             seconds=60, keep_running=True, level=None, npc="", run_dir=run_dir)
                    command_duo(duo)
                    round_["boot_seconds"] = time.monotonic() - boot_start
                    round_["phase"] = "scenarios"
                    round_["before"] = soak_snapshot()
                    save_report()
                    command_scenario(argparse.Namespace(names=names if not args.tag else [], tag=args.tag, hold=0,
                                                       repeat=1, shuffle=True, seed=round_["seed"], fail_fast=False,
                                                       dry_run=False, observe=observe))
            except (TestFailure, SystemExit, OSError) as error:
                round_["error"] = f"{round_['phase']}: {error}"
            finally:
                round_["after"] = soak_snapshot()
                for entry in round_["scenarios"]:
                    if entry["result"] == "RUNNING":
                        entry["result"] = "FAIL (round aborted: " + round_.get("error", "interrupted") + ")"
                round_["not_run"] = round_["order"][len(round_["attempted_order"]):]
                round_["complete"] = not round_["not_run"] and len(round_["scenarios"]) == len(names)
                round_["crashes"] = soak_crashes(crash_before, round_dir)
                for name in ("", "client"):
                    select_instance(name)
                    collect_logs(round_dir)
                round_["seconds"] = time.monotonic() - round_started
                round_["summary"] = soak_summary([round_], prior, memory_rounds=report["rounds"])
                round_["comparison"] = f"round {index - 1}" if index > 1 else previous_file
                save_report()
            passed = sum(entry["result"] == "PASS" for entry in round_["scenarios"])
            log(f"round {index}: {passed}/{len(names)} passed, {round_['seconds']:.1f}s; "
                + soak_summary_line(round_["summary"]) + (f" (compared with {round_['comparison']})" if round_["comparison"] else ""))
    except KeyboardInterrupt:
        report["interrupted"] = True
        raise
    finally:
        report["finished"] = time.time()
        save_report()
        stop_duo()
    print(f"{len(report['rounds'])} rounds, {soak_summary_line(report['summary'])}; report: {report_file}")
    if any(round_.get("error") or round_["crashes"] or any(entry["result"] != "PASS" for entry in round_["scenarios"])
           for round_ in report["rounds"]):
        raise SystemExit(1)


def main():
    from coop_specs import register
    register(SCENARIOS, TAGS)
    for name in ("spec:cutscene-controls-reach-party", "spec:leaver-cutscene-gives-back"):
        if name in TAGS:
            TAGS[name].add("session")
    try:
        validate_source_tags()
    except TestFailure as error:
        raise SystemExit(f"FAIL: {error}")
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    commands = parser.add_subparsers(dest="command", required=True)
    for name in ("launch", "duo", "repro"):
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
        if name == "duo":
            sub.add_argument("--seconds", type=float, default=30, help="how long to wait for the games to show each other")
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
            sub.add_argument("--level", type=int,
                             help="raise both players to this level after loading (late saves are level 1)")
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
    scenario.add_argument("--dry-run", action="store_true", help="print the selection without contacting Skyrim")
    affected = commands.add_parser("affected", help="select scenarios from changed source files")
    affected.add_argument("--base", default="HEAD", metavar="REF", help="compare against REF (default HEAD), including untracked files")
    affected.add_argument("--run", action="store_true", help="run the selected scenarios; never expand core into the full suite")
    affected.add_argument("--check", action="store_true", help="check Code/ coverage, including shared specs but excluding other Code/tests/")
    affected.add_argument("--dry-run", action="store_true", help="with --run: print what scenario would run without contacting Skyrim")
    affected.add_argument("--files", nargs="*", help=argparse.SUPPRESS)
    soak = commands.add_parser("soak", help="fresh duo and shuffled scenarios each round; --tag limits a smoke test")
    budget = soak.add_mutually_exclusive_group()
    budget.add_argument("--hours", type=float, help="start rounds for H hours (default 6); finish the last round")
    budget.add_argument("--rounds", type=int, help="run N rounds instead of a time budget")
    soak.add_argument("--seed", type=int, help="first round's shuffle seed; each round adds one")
    soak.add_argument("--tag", help="smoke test only these comma-separated scenario tags")
    soak.add_argument("--dry-run", action="store_true", help="print the scope without booting or stopping games")
    stop = commands.add_parser("stop", help="stop the test game(s) (both instances unless --instance)")
    console = commands.add_parser("console", help="run a console command in the running test game")
    console.add_argument("text", nargs="+")
    status = commands.add_parser("status", help="both games side by side: health, death, weapon, casting")
    status.add_argument("--follow", "-f", action="store_true", help="keep refreshing")
    commands.add_parser("locks", help="both games' lock-order checker reports (deadlock patterns)")
    rate = commands.add_parser("change-rate", help="the engine's change records per form type and flag, idle or during scenarios")
    rate.add_argument("scenarios", nargs="*", help="run these scenarios as the window (default: --seconds idle)")
    rate.add_argument("--seconds", type=float, default=30, help="idle window without scenarios (default 30)")
    rate.add_argument("--instance", choices=("host", "client", "both"), help="which game (default: both)")
    rate.add_argument("--json", action="store_true", help="both games' raw counts as JSON")
    save_diff = commands.add_parser("save-diff", help="save both games at once and compare the two worlds block by block")
    save_diff.add_argument("--label", default="now", help="names the saves coopdiff-<label> (default now)")
    save_diff.add_argument("--noise", type=float, default=0, metavar="SECONDS",
                           help="save the host again after this long: what differs between those two is marked noise")
    save_diff.add_argument("--details", type=int, default=25, help="forms listed per class (0: summary only)")
    save_diff.add_argument("--json", action="store_true", help="the whole report as JSON")
    walk = commands.add_parser("quest-walk", help="walk the host through a quest on a running duo; compare the client's at each step")
    walk.add_argument("quest", help="the quest's editor id (MQ202)")
    walk.add_argument("--plugin", default="Skyrim.esm", help="the plugin defining it (default Skyrim.esm)")
    walk.add_argument("--names-from", required=True, metavar="SAVE",
                      help="the save the duo booted from: its plugin order and names")
    walk.add_argument("--route-from", metavar="SAVE", help="a later save of the playthrough: walk the stages it has done")
    walk.add_argument("--stages", help="walk these stages (comma-separated) instead")
    walk.add_argument("--start-by", metavar="COMMANDS",
                      help="the trip that makes the game start it, ;-separated: travel commands (coc X) and "
                           "`wait QUEST:STAGE` for a condition the host's game reaches itself (default startquest)")
    walk.add_argument("--leave-after", type=int, metavar="N", help="the client leaves after N stages")
    walk.add_argument("--away", type=int, default=2, help="stages the host sets while the client is away (default 2)")
    walk.add_argument("--settle", type=float, default=3, help="seconds the client gets to match each step (default 3)")
    walk.add_argument("--details", type=int, default=30, help="end-of-walk differences listed (default 30)")
    crash = commands.add_parser("crash", help="the newest crash of each game: engine state (CrashLoggerSSE) and our last engine changes")
    crash.add_argument("--instance", choices=["client"], help="only this game (default: both)")
    commands.add_parser("unsynced", help="the reference change flags the host had with no row to send them")
    world_diff = commands.add_parser("world-diff", help="compare every reference in the loaded cells between the two games")
    world_diff.add_argument("--details", type=int, default=40, help="references listed (0: all)")
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
        if args.command == "launch":
            command_launch(args)
        elif args.command == "duo":
            command_duo(args)
        elif args.command == "scenario":
            command_scenario(args)
        elif args.command == "affected":
            command_affected(args)
        elif args.command == "soak":
            command_soak(args)
        elif args.command == "reload":
            command_reload(args)
        elif args.command == "console":
            run_console(" ".join(args.text))
            log("Done")
        elif args.command == "locks":
            command_locks(args)
        elif args.command == "save-diff":
            command_save_diff(args)
        elif args.command == "change-rate":
            command_change_rate(args)
        elif args.command == "quest-walk":
            command_quest_walk(args)
        elif args.command == "crash":
            command_crash(args)
        elif args.command == "repro":
            command_repro(args)
        elif args.command == "unsynced":
            command_unsynced(args)
        elif args.command == "world-diff":
            command_world_diff(args)
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
    sys.modules["coop_test"] = sys.modules[__name__]
    main()

