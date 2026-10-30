"""Runtime for the co-op harness."""
from pathlib import Path
import contextlib
import json
import math
import os
import pwd
import shutil
import signal
import socket
import subprocess
import sys
import time
import uuid
from .common import (
    BUNDLED_START,
    CLIENT_ADDRESS,
    CLIENT_NETNS,
    CLIENT_USER,
    CONSOLE_ERRORS,
    CONSOLE_ON_REFERENCE,
    CRASH_LOGGER_FILES,
    CRASH_LOGGER_INI,
    CRASH_LOGGER_SHA256,
    CRASH_LOGGER_VERSION,
    DLL,
    DUMMY_XORG_CONFIG,
    GAME,
    GAME_MEMORY_LIMIT_MB,
    HOST_SIDE_ADDRESS,
    LOADER,
    PLUGINS,
    PROTON,
    ROOT,
    RUNTIME,
    STEAM,
    TEST_HOME,
    UI_SOURCE,
    VANILLA_HARNESS,
)
from .utilities import (
    atomic_write,
    log,
    set_ini,
)


class Runtime:
    def sudo(ctx, *command):
        return subprocess.run(["sudo", "-n", *command], capture_output=True, text=True)

    def ensure_client_netns(ctx):
        """The namespace, its link to this one (10.77.0.1 here, 10.77.0.2 there), NAT out and DNS; kept until reboot."""
        if CLIENT_NETNS not in ctx.sudo("ip", "netns", "list").stdout.split():
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
                if (result := ctx.sudo(*command)).returncode != 0:
                    raise ctx.failure(f"netns setup failed: {' '.join(command)}: {result.stderr.strip()}")
        ctx.sudo("sysctl", "-q", "-w", "net.ipv4.ip_forward=1")
        # Forwarding is dropped by default here (Docker's policy): let the namespace's traffic through, then NAT it.
        for table, chain, rule in (("filter", "FORWARD", ["-i", "coop2-host", "-j", "ACCEPT"]),
                                   ("filter", "FORWARD", ["-o", "coop2-host", "-j", "ACCEPT"]),
                                   ("nat", "POSTROUTING", ["-s", "10.77.0.0/24", "!", "-o", "coop2-host", "-j", "MASQUERADE"])):
            if ctx.sudo("iptables", "-t", table, "-C", chain, *rule).returncode != 0:
                ctx.sudo("iptables", "-t", table, "-I", chain, "1", *rule)
        resolv = Path(f"/etc/netns/{CLIENT_NETNS}/resolv.conf")
        if not resolv.exists():
            ctx.sudo("mkdir", "-p", str(resolv.parent))
            subprocess.run(["sudo", "-n", "tee", str(resolv)], input="nameserver 1.1.1.1\nnameserver 8.8.8.8\n",
                           text=True, capture_output=True)

    def in_client_netns(ctx, pid):
        """That process runs in the client's namespace."""
        mine = ctx.sudo("readlink", f"/proc/{pid}/ns/net").stdout.strip()
        theirs = ctx.sudo("ip", "netns", "exec", CLIENT_NETNS, "readlink", "/proc/self/ns/net").stdout.strip()
        return bool(mine) and mine == theirs

    def instance_runtime(ctx, *, _game=None):
        """The Steam runtime this instance's game runs in: the client user's own copy (its container hard-links the
        runtime's files, which it may only do for files it owns), refreshed when Steam updates the runtime."""
        _game = ctx.game(_game)
        if not ctx.instance_user(_game=_game):
            return RUNTIME
        copy = TEST_HOME / f"runtime-{ctx.instance_user(_game=_game)}"
        if not (copy / "VERSIONS.txt").exists() or (copy / "VERSIONS.txt").read_bytes() != (RUNTIME / "VERSIONS.txt").read_bytes():
            log(f"Copying the Steam runtime for {ctx.instance_user(_game=_game)} (once per runtime update, ~800 MB)")
            ctx.sudo("rm", "-rf", str(copy))
            ctx.sudo("install", "-d", "-o", ctx.instance_user(_game=_game), "-g", ctx.instance_user(_game=_game), str(copy))
            result = ctx.sudo("-u", ctx.instance_user(_game=_game), "rsync", "-a", "--exclude", "/var/", f"{RUNTIME}/", f"{copy}/")
            if result.returncode != 0:
                raise ctx.failure(f"copying the Steam runtime failed: {result.stderr.strip()}")
        return copy

    def instance_user(ctx, *, _game=None):
        """The OS user this instance's game runs as, when not this one (CLIENT_USER for the client)."""
        _game = ctx.game(_game)
        return CLIENT_USER if _game.name == "client" and CLIENT_USER else None

    def as_instance_user(ctx, command, env, *, _game=None):
        """The command run as the instance's user (sudo, the environment passed explicitly, files group-writable), so
        its game talks to that user's Steam; unchanged for this user."""
        _game = ctx.game(_game)
        user = ctx.instance_user(_game=_game)
        if not user:
            return command
        personal = {"HOME", "USER", "LOGNAME", "XDG_RUNTIME_DIR", "DBUS_SESSION_BUS_ADDRESS", "XAUTHORITY", "MAIL"}
        # HOST_HOME and its kin name this user's home, where Steam's container would find this user's Steam.
        assignments = [f"{key}={value}" for key, value in env.items()
                       if key not in personal and not key.startswith("HOST_")]
        # Its own session bus (user_bus): without one, a game's D-Bus client starts one that outlives the game.
        assignments.append(f"DBUS_SESSION_BUS_ADDRESS=unix:path={ctx.user_bus(user)}")
        ctx.ensure_client_netns()
        return ["sudo", "-n", "ip", "netns", "exec", CLIENT_NETNS, "sudo", "-u", user, "-H", "--", "env", *assignments,
                "bash", "-c", 'umask 002; exec "$@"', "_", *command]

    def user_bus(ctx, user):
        """A session bus for that user, started once and left running (the client user has no desktop session)."""
        path = Path(f"/tmp/skyrimcoop-{user}-bus")
        if not path.is_socket():
            subprocess.run(["sudo", "-n", "-u", user, "-H", "dbus-daemon", "--session", f"--address=unix:path={path}",
                            "--fork", "--nopidfile"], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        return path

    def game_env(ctx, *, _game=None):
        _game = ctx.game(_game)
        env = dict(os.environ)
        steam = Path(f"/home/{ctx.instance_user(_game=_game)}/.steam/steam") if ctx.instance_user(_game=_game) else STEAM
        env.update(SteamAppId="489830", SteamGameId="489830", STEAM_COMPAT_DATA_PATH=str(_game.prefix),
                   STEAM_COMPAT_CLIENT_INSTALL_PATH=str(steam), WINEDEBUG=env.get("WINEDEBUG", "-all"),
                   SKYRIMCOOP_LOG_LEVEL=env.get("SKYRIMCOOP_LOG_LEVEL", "debug"),
                   # `stop` kills the games: no crash report and zip from that at the next boot.
                   SKYRIMCOOP_NO_CRASH_REPORTS="1",
                   # The UI straight from the repo (Wine's Z: is /): edits show up in game within a second.
                   SKYRIMCOOP_UI_DIR=env.get("SKYRIMCOOP_UI_DIR", "Z:" + str(UI_SOURCE).replace("/", "\\")))
        if _game.name:
            env["SKYRIMCOOP_INSTANCE"] = _game.name
        if ctx.instance_user(_game=_game):  # in its namespace: reachable on its link, and the host is across it
            env.update(SKYRIMCOOP_COMMAND_ADDRESS=CLIENT_ADDRESS, SKYRIMCOOP_JOIN_HOST=HOST_SIDE_ADDRESS)
        if not ctx.VISIBLE:
            env["DISPLAY"] = _game.display
        return env

    def uses_prefix(ctx, environ, keys=("STEAM_COMPAT_DATA_PATH", "WINEPREFIX"), *, _game=None):
        """That process environment names our test prefix, in any path spelling (another worktree's build/
        link, or Steam's rewrite of STEAM_COMPAT_DATA_PATH while WINEPREFIX keeps ours)."""
        _game = ctx.game(_game)
        prefix = _game.prefix.resolve()
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

    def process_environs(ctx):
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

    def signal_process(ctx, pid, sig):
        """os.kill, through sudo for the client user's processes."""
        try:
            os.kill(pid, sig)
        except PermissionError:
            subprocess.run(["sudo", "-n", "kill", f"-{sig.name.removeprefix('SIG')}", str(pid)], capture_output=True)

    def prefix_processes(ctx, *, _game=None):
        """PIDs started for our test prefix (matched by environment, so nothing else is touched)."""
        _game = ctx.game(_game)
        return [pid for pid, environ in ctx.process_environs().items() if ctx.uses_prefix(environ, _game=_game)]

    def game_process(ctx, instance):
        """The instance's SkyrimSE.exe (by its wine prefix; not the launcher stubs that share the name)."""
        _game = ctx.game(instance)
        for pid, environ in ctx.process_environs().items():
            entry = Path("/proc") / str(pid)
            try:
                if "SkyrimSE.exe" in (entry / "cmdline").read_bytes().decode(errors="replace") and \
                        ctx.uses_prefix(environ, ("WINEPREFIX",), _game=_game) and \
                        int((entry / "statm").read_text().split()[1]) * 4096 > 100 << 20:
                    return pid
            except (OSError, ValueError, IndexError):
                pass
        raise ctx.failure(f"[{instance or 'host'}] no running SkyrimSE.exe")

    def game_stalled(ctx, instance):
        """The game's process stops (as a hitch or a slow load would hold it) and runs again afterwards: what
        reaches it meanwhile waits on its socket."""
        pid = ctx.game_process(instance)
        ctx.signal_process(pid, signal.SIGSTOP)
        log(f"[{instance or 'host'}] game stalled")
        try:
            yield
        finally:
            ctx.signal_process(pid, signal.SIGCONT)
            log(f"[{instance or 'host'}] game runs again")

    def stop_game(ctx, *, _game=None):
        _game = ctx.game(_game)
        pids = ctx.prefix_processes(_game=_game)
        for sig in (signal.SIGTERM, signal.SIGKILL):
            for pid in pids:
                try:
                    ctx.signal_process(pid, sig)
                except ProcessLookupError:
                    pass
            deadline = time.monotonic() + 5
            while time.monotonic() < deadline and (pids := ctx.prefix_processes(_game=_game)):
                time.sleep(0.2)
            if not pids:
                return True
        return not ctx.prefix_processes(_game=_game)

    def check_prerequisites(ctx):
        required = [GAME / "SkyrimSE.exe", GAME / "skse64_loader.exe", PROTON / "proton", RUNTIME / "run", DLL]
        missing = [str(path) for path in required if not path.exists()]
        if missing:
            hint = ("\nBuild the DLL: scripts/xmake-msvc.sh f -m dev && scripts/xmake-msvc.sh build -j16 SkyrimTogetherClientDLL && scripts/xmake-msvc.sh build SkyrimCoopLoader")
            raise ctx.failure("Missing:\n  " + "\n  ".join(missing) + hint)
        if subprocess.run(["pgrep", "-x", "steam"], capture_output=True).returncode != 0:
            raise ctx.failure("Steam must be running (Skyrim's DRM needs it).")
        if (PLUGINS / "skyrim_coop_hooks.dll").exists():
            raise ctx.failure(f"Remove the old relay plugin first: {PLUGINS / 'skyrim_coop_hooks.dll'}")

    def copy_if_changed(ctx, source, target):
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

    def deploy_ui(ctx):
        """The in-game UI's documents, styles and fonts: Plugins/SkyrimCoop/ui (UiService)."""
        for source in sorted(UI_SOURCE.rglob("*")):
            if source.is_file():
                ctx.copy_if_changed(source, PLUGINS / "SkyrimCoop/ui" / source.relative_to(UI_SOURCE))

    def require_test_harness(ctx):
        """These scripts drive the plugin through its test harness (Code/client/TestHarness), which only
        builds configured with --test_harness=y have; a player build would never answer."""
        if b"SkyrimCoopHotReload" not in DLL.read_bytes():
            raise ctx.failure(f"{DLL} has no test harness. Configure with scripts/xmake-msvc.sh f -m dev "
                              "(it turns on --test_harness=y) and build again.")

    def deploy_dll(ctx):
        """With the loader built: Plugins/SkyrimCoopLoader.dll loads Plugins/SkyrimCoop/<client>.
        Otherwise the client is the SKSE plugin. Never both: the client would load twice."""
        ctx.require_test_harness()
        ctx.deploy_ui()
        direct = PLUGINS / "SkyrimTogetherClient.dll"
        if LOADER.exists():
            direct.unlink(missing_ok=True)
            ctx.copy_if_changed(LOADER, PLUGINS / LOADER.name)
            ctx.copy_if_changed(DLL, PLUGINS / "SkyrimCoop" / DLL.name)
        else:
            (PLUGINS / LOADER.name).unlink(missing_ok=True)
            ctx.copy_if_changed(DLL, direct)

    def deploy_crash_logger(ctx):
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
                raise ctx.failure(f"CrashLoggerSSE archive checksum mismatch: {digest}")
            subprocess.run(["7z", "e", "-y", f"-o{cache}", str(archive), "SKSE/Plugins/*"], check=True,
                           stdout=subprocess.DEVNULL)
        for name in CRASH_LOGGER_FILES:
            ctx.copy_if_changed(cache / name, PLUGINS / name)
        # The plugin fills in every other key (and its comment) on start.
        (PLUGINS / "CrashLogger.ini").write_text(CRASH_LOGGER_INI)

    def crash_logs(ctx, name):
        """CrashLoggerSSE's logs for a game instance, oldest first."""
        directory = TEST_HOME / f"prefix-{name or 'host'}/pfx/drive_c/users/steamuser/Documents/My Games/Skyrim Special Edition/SKSE"
        return sorted(path for path in directory.glob("crash-*.log") if path.suffixes == [".log"])

    def plugin_log(ctx, name):
        return PLUGINS / f"logs/tp_client{'-' + name if name else ''}.log"

    def settle_crash_log(ctx, crash_log, timeout=30, *, _game=None):
        """Waits for CrashLoggerSSE to finish writing (the file stops growing for 2s, or the game is
        gone): reporting or stopping the game earlier cuts the log short."""
        _game = ctx.game(_game)
        deadline = time.monotonic() + timeout
        size, stable_since = -1, time.monotonic()
        while time.monotonic() < deadline:
            current = crash_log.stat().st_size if crash_log.exists() else 0
            if current != size:
                size, stable_since = current, time.monotonic()
            elif time.monotonic() - stable_since >= 2 or not ctx.prefix_processes(_game=_game):
                return
            time.sleep(0.25)

    def kept_logs(ctx, crash_log):
        """Both games' plugin logs kept beside a crash log (crash-<time>.host.log, .client.log): the next boot rewrites
        them, and the other game's log shows what it sent before the crash."""
        return {name: crash_log.with_suffix(f".{name or 'host'}.log") for name in ("", "client")}

    def crash_report(ctx, name, crash_log, ours=True):
        """The crash's report; with `ours`, our plugin's log adds the breadcrumbs and heap history."""
        _game = ctx.game(name)
        ctx.settle_crash_log(crash_log, _game=_game)
        kept = ctx.kept_logs(crash_log)
        for game, path in kept.items():
            if not path.exists() and ctx.plugin_log(game).exists():
                shutil.copyfile(ctx.plugin_log(game), path)
        sys.path.insert(0, str(ROOT / "scripts/re"))
        import crash_report as report
        return report.report(crash_log, kept[name] if ours and kept[name].exists() else ctx.plugin_log(name) if ours else None)

    def check_for_crash(ctx):
        if time.monotonic() < ctx._next_crash_check:
            return
        ctx._next_crash_check = time.monotonic() + 1.0
        for name in ("", "client"):
            new = [path for path in ctx.crash_logs(name) if path not in ctx.KNOWN_CRASHES[name]]
            if new:
                ctx.KNOWN_CRASHES[name].update(new)
                raise ctx.failure(f"{name or 'host'} crashed:\n" + ctx.crash_report(name, new[-1]))

    def remove_plugin(ctx):
        (PLUGINS / "SkyrimTogetherClient.dll").unlink(missing_ok=True)
        (PLUGINS / LOADER.name).unlink(missing_ok=True)
        (PLUGINS / VANILLA_HARNESS.name).unlink(missing_ok=True)

    def ensure_virtual_display(ctx, *, _game=None):
        """Starts an off-screen X server for the game unless one is already running. The GPU still
        renders (Vulkan presents to it); only the window is invisible. Prefers Xorg's dummy driver,
        which reports a real 60 Hz mode: on Xvfb (0 Hz) DXVK divides by zero in Skyrim's renderer init."""
        _game = ctx.game(_game)
        socket = Path("/tmp/.X11-unix") / f"X{_game.display.lstrip(':')}"
        if socket.exists():
            ctx.allow_instance_user(_game=_game)
            return
        log_file = ROOT / "build/coop-test/xorg-display.log"
        log_file.parent.mkdir(parents=True, exist_ok=True)
        if (Path("/etc/X11") / DUMMY_XORG_CONFIG).exists() and Path("/usr/lib/xorg/Xorg").exists():
            # The real binary: the Xorg wrapper only lets console users start a server.
            command = ["/usr/lib/xorg/Xorg", _game.display, "-config", DUMMY_XORG_CONFIG, "-noreset",
                       "-nolisten", "tcp", "-logfile", str(log_file)]
        else:
            log("Using Xvfb (0 Hz, Skyrim will crash); run `coop_test.py setup-display` once for the dummy Xorg")
            command = ["Xvfb", _game.display, "-screen", "0", "1920x1080x24", "-nolisten", "tcp"]
        subprocess.Popen(command, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, start_new_session=True)
        deadline = time.monotonic() + 10
        while not socket.exists():
            if time.monotonic() > deadline:
                raise ctx.failure(f"Off-screen X server did not start on {_game.display} (see {log_file})")
            time.sleep(0.1)
        ctx.allow_instance_user(_game=_game)

    def allow_instance_user(ctx, *, _game=None):
        """Lets the instance's user draw on its display (xhost)."""
        _game = ctx.game(_game)
        if user := ctx.instance_user(_game=_game):
            subprocess.run(["xhost", f"+SI:localuser:{user}"], env={**os.environ, "DISPLAY": ctx.game_env(_game=_game)["DISPLAY"]},
                           stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

    def ensure_user_steam(ctx, *, _game=None):
        """The instance user's Steam client is running (its game's Steam API needs it), started silently on the game's
        display if not. Logged in once by hand: `sudo -u coop2 -H env DISPLAY=:1 steam`."""
        _game = ctx.game(_game)
        user = ctx.instance_user(_game=_game)
        if not user:
            return
        ctx.ensure_client_netns()

        def steam_pids():
            return [int(p) for p in subprocess.run(["pgrep", "-u", user, "-x", "steam"], capture_output=True,
                                                   text=True).stdout.split()]
        if (pids := steam_pids()) and all(ctx.in_client_netns(pid) for pid in pids):
            return
        if pids:  # outside the namespace it gets no game: restart it inside (its login is kept)
            log(f"[{_game.name}] restarting {user}'s Steam inside {CLIENT_NETNS}")
            ctx.sudo("pkill", "-u", user, "-x", "steam")
            ctx.wait_for(f"[{_game.name}] {user}'s old Steam exited", lambda: not steam_pids(), 60, _game=_game)
        ctx.allow_instance_user(_game=_game)
        subprocess.Popen(["sudo", "-n", "ip", "netns", "exec", CLIENT_NETNS, "sudo", "-u", user, "-H", "env",
                          f"DISPLAY={ctx.game_env(_game=_game)['DISPLAY']}", f"DBUS_SESSION_BUS_ADDRESS=unix:path={ctx.user_bus(user)}",
                          "/usr/games/steam", "-silent"],
                         stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, start_new_session=True)
        ctx.wait_for(f"[{_game.name}] {user}'s Steam is running in {CLIENT_NETNS}",
                 lambda: subprocess.run(["pgrep", "-u", user, "-f", "steamwebhelper"], capture_output=True).returncode == 0,
                 180, _game=_game)

    def setup_display(ctx):
        """One-time: the dummy video driver and its config (needs sudo)."""
        if not Path("/usr/lib/xorg/modules/drivers/dummy_drv.so").exists():
            subprocess.run(["sudo", "apt-get", "install", "-y", "xserver-xorg-video-dummy"], check=True)
        subprocess.run(["sudo", "cp", str(ROOT / "scripts/coop" / DUMMY_XORG_CONFIG), "/etc/X11/"], check=True)
        log(f"Installed /etc/X11/{DUMMY_XORG_CONFIG}")

    def enable_bundled_start(ctx, *, _game=None):
        _game = ctx.game(_game)
        source = ROOT / "GameFiles/Skyrim" / BUNDLED_START
        target = _game.install / "Data" / BUNDLED_START
        if not target.exists() or target.read_bytes() != source.read_bytes():
            shutil.copyfile(source, target)
        order = ctx.load_order_file(_game=_game)
        lines = order.read_text().splitlines() if order.exists() else []
        lines = [line for line in lines if line.lstrip("*") not in (BUNDLED_START, "Quick Start - SE.esp")]
        order.parent.mkdir(parents=True, exist_ok=True)
        order.write_text("\n".join(lines + [f"*{BUNDLED_START}"]) + "\n")

    def prepare_prefix(ctx, bundled_start=True, *, _game=None):
        _game = ctx.game(_game)
        if not (_game.prefix / "pfx/drive_c/users/steamuser").exists():
            log("Creating the test Proton prefix (first run only, ~30s)...")
            _game.prefix.mkdir(parents=True, exist_ok=True)
            subprocess.run(ctx.as_instance_user([str(ctx.instance_runtime(_game=_game) / "run"), "--", str(PROTON / "proton"), "run", "wineboot", "-u"],
                                            ctx.game_env(_game=_game), _game=_game), env=ctx.game_env(_game=_game), cwd=_game.install, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                           timeout=300)
        _game.my_games.mkdir(parents=True, exist_ok=True)
        skyrim_ini, prefs_ini = _game.my_games / "Skyrim.ini", _game.my_games / "SkyrimPrefs.ini"
        if not skyrim_ini.exists():
            shutil.copy(_game.install / "Skyrim_Default.ini", skyrim_ini)
        if not prefs_ini.exists():
            shutil.copy(_game.install / "Low.ini", prefs_ini)
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
        if bundled_start:
            ctx.enable_bundled_start(_game=_game)

    def wait_for(ctx, description, predicate, timeout, launcher=None, *, _game=None):
        _game = ctx.game(_game)
        log(description + "...")
        ctx.CURRENT_STEP.update(description=description, time=time.time())
        ctx.observe("checkpoint", phase=ctx.phase, description=description)
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            result = predicate()
            if result:
                return result
            if launcher is not None and launcher.poll() is not None and not ctx.prefix_processes(_game=_game):
                raise ctx.failure(f"Skyrim exited while waiting: {description}")
            ctx.check_for_crash()
            time.sleep(0.05)
        raise ctx.failure(f"Timed out after {timeout:.0f}s: {description}")

    def launch(ctx, args, run_dir, *, _game=None):
        _game = ctx.game(_game)
        if ctx.prefix_processes(_game=_game):
            raise ctx.failure("The test Skyrim is already running. Use `coop_test.py stop` first.")
        if not ctx.VISIBLE:
            ctx.ensure_virtual_display(_game=_game)
        vanilla = getattr(args, "vanilla", False)
        if vanilla:  # A/B baseline: same prefix and settings, SKSE without our plugin
            ctx.remove_plugin()
            if VANILLA_HARNESS.exists():  # runs the harness's commands, nothing of ours
                ctx.copy_if_changed(VANILLA_HARNESS, PLUGINS / VANILLA_HARNESS.name)
        else:
            (PLUGINS / VANILLA_HARNESS.name).unlink(missing_ok=True)
            ctx.deploy_dll()
        ctx.deploy_crash_logger()
        main_menu = getattr(args, "main_menu", False)
        command = None if main_menu else f"load {args.save}" if args.save else f"coc {args.cell}"
        ctx.prepare_prefix(_game=_game)
        _game.control.mkdir(parents=True, exist_ok=True)
        for stale in ("status.json", "session.txt", "console.txt", "rpc.json", "request.txt", "boot.txt"):
            (_game.control / stale).unlink(missing_ok=True)
        if command:
            atomic_write(_game.control / "boot.txt", command + "\n")  # run by the plugin at the main menu
        log(f"Launching Skyrim ({'to the main menu' if main_menu else f'start command: {command!r}'})")
        output = open(run_dir / f"proton{'-' + _game.name if _game.name else ''}.log", "w")
        ctx.ensure_user_steam(_game=_game)
        launcher = subprocess.Popen(
            ctx.as_instance_user([str(ctx.instance_runtime(_game=_game) / "run"), "--", str(PROTON / "proton"), "waitforexitandrun",
                              str(_game.install / "skse64_loader.exe")], ctx.game_env(_game=_game), _game=_game),
            env=ctx.game_env(_game=_game), cwd=_game.install, stdout=output, stderr=subprocess.STDOUT, start_new_session=True)
        if vanilla and not VANILLA_HARNESS.exists():
            log(f"Vanilla run (no plugin): at the main menu, open the console (~) and type `{command}`")
            return launcher
        status = ctx.wait_for("Waiting for the plugin heartbeat (SKSE loaded our DLL)", _game.read_status,
                          args.boot_timeout, launcher, _game=_game)
        log(f"Plugin alive in Skyrim (Windows PID {status['pid']})")
        if main_menu:
            ctx.wait_for("Waiting for the main menu", lambda: ((_game.read_status() or {}).get("game") or {}).get("main_menu"), args.world_timeout, launcher, _game=_game)
            return launcher
        ctx.wait_for(f"Waiting for the player to be in the world ({command})",
                 lambda: (_game.read_status() or {}).get("session", {}).get("world_ready"), args.world_timeout, launcher, _game=_game)
        log("Player is in the world")
        # The Survival Mode prompt opens on the first exterior and pauses the game until answered.
        ctx.run_console("set Survival_PlayerHasBeenPrompted to 1", _game=_game)
        return launcher

    def collect_logs(ctx, run_dir, *, _game=None):
        _game = ctx.game(_game)
        sources = {
            "status.json": _game.control / "status.json",
            "tp_client.log": _game.install / f"Data/SKSE/Plugins/logs/tp_client{'-' + _game.name if _game.name else ''}.log",
            "skse64.log": _game.my_games / "SKSE/skse64.log",
            "skse64_loader.log": _game.my_games / "SKSE/skse64_loader.log",
        }
        for name, path in sources.items():
            if path.exists():
                shutil.copy(path, run_dir / (f"{_game.name}-{name}" if _game.name else name))

    def new_run_dir(ctx, kind):
        run_dir = TEST_HOME / "runs" / (time.strftime("%Y%m%d-%H%M%S") + "-" + kind)
        run_dir.mkdir(parents=True, exist_ok=True)
        return run_dir

    def run_console(ctx, command, timeout=10, *, _game=None):
        """Runs a console command in the test game (after the boot command) and waits until the console ran it; returns
        what it printed. A line the console could not run fails the scenario with the console's own error."""
        _game = ctx.game(_game)
        request = {"cmd": "console", "line": command}
        if on_reference := CONSOLE_ON_REFERENCE.match(command):
            request.update(line=on_reference.group(2), target=int(on_reference.group(1), 16))
        response = _game.rpc(request, timeout=timeout)
        if response and response.get("ok"):
            ticket, deadline = response.get("ticket"), time.monotonic() + timeout
            while ticket is not None:
                result = _game.rpc({"cmd": "console_result", "ticket": ticket}) or {}
                if result.get("ran"):
                    output = result.get("output") or []
                    log(f"Console: {command}" + (f"  -> {' | '.join(output)}" if output else ""))
                    if errors := [line for line in output if CONSOLE_ERRORS.search(line)]:
                        raise ctx.failure(f"console could not run `{command}`: {' | '.join(errors)}")
                    return output
                if time.monotonic() > deadline:
                    raise ctx.failure(f"console line `{command}` never ran (still queued after {timeout}s)")
                time.sleep(0.01)
            log(f"Console: {command}")
            return []
        request_id = uuid.uuid4().hex[:12]
        atomic_write(_game.control / "console.txt", f"{request_id} {command}\n")
        ctx.wait_for(f"Console: {command}", lambda: (_game.read_status() or {}).get("console_id") == request_id, timeout, _game=_game)

    def face_actor(ctx, actor_id, *, _game=None):
        """Turns the player toward an actor so it is on screen (for screenshots / `watch`)."""
        _game = ctx.game(_game)
        status = _game.read_status() or {}
        player = (status.get("player") or {}).get("position")
        actor = (status.get("actors", {}).get(str(actor_id), {}).get("actual")
                 or ((status.get("world") or {}).get(str(actor_id)) or {}).get("placement", {}).get("position"))
        if not player:
            return
        if not actor:
            # Aiming at nothing casts wherever the player happens to face: say so instead.
            raise ctx.failure(f"cannot face {actor_id:X}: not in this game's status (gone, or respawned under a new id?)")
        # Skyrim heading: degrees clockwise from north (+Y).
        heading = math.degrees(math.atan2(actor[0] - player[0], actor[1] - player[1])) % 360
        ctx.run_console(f"player.setangle z {heading:.0f}", _game=_game)
        # Level, not looking down after walking: aimed spells fly where the player looks.
        ctx.run_console("player.setangle x 0", _game=_game)

    def start_session(ctx, command, timeout=60, *, _game=None):
        """Writes a session request (`<id>` hosts, `<id> join <port>` joins) and waits until ready."""
        _game = ctx.game(_game)
        request = uuid.uuid4().hex
        atomic_write(_game.control / "session.txt", f"{request} {command}".strip() + "\n")

        def ready():
            session = (_game.read_status() or {}).get("session", {})
            if session.get("request") != f"{request} {command}".strip():
                return None
            if session.get("state") == "failed":
                raise ctx.failure(f"Session failed: {session.get('error')}")
            return session if session.get("state") == "ready" else None
        return ctx.wait_for(f"{_game.name or 'host'}: {'joining' if command else 'hosting'}", ready, timeout, _game=_game)

    def other_players(ctx, *, _game=None):
        """Remote actors this instance shows in the engine with 3D: {form id: position}."""
        _game = ctx.game(_game)
        status = _game.read_status() or {}
        return {int(actor): sample["actual"] for actor, sample in status.get("actors", {}).items() if sample.get("has_3d")}

    def both_see_each_other(ctx):
        """{"host"/"client": remote players} once each game shows the other's player, else None."""
        _game = ctx.game()
        seen = {}
        for name in ("", "client"):
            _game = ctx.game(name)
            players = ctx.other_players(_game=_game)
            if not players:
                return None
            seen[name or "host"] = players
        return seen

    def report_game_memory(ctx):
        """Prints each running game's memory (resident + swapped) and warns when a restart is due."""
        _game = ctx.game()
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

    def reload_games(ctx, seconds=60):
        """Hot reloads the client in both games (the deployed build, rebuilt or not) and reconnects them."""
        _game = ctx.game()
        if not LOADER.exists():
            raise ctx.failure(f"Build the loader first: scripts/xmake-msvc.sh build SkyrimCoopLoader ({LOADER})")
        ctx.deploy_dll()
        started = time.monotonic()
        names = ("", "client")
        for name in names:
            # The new client would act on the old session request (and join a stale port).
            _game = ctx.game(name)
            (_game.control / "session.txt").unlink(missing_ok=True)
        before = {}
        for name in names:
            _game = ctx.game(name)
            response = _game.rpc({"cmd": "reload"})
            if not response or not response.get("ok"):
                raise ctx.failure(f"{name or 'host'}: the reload request failed ({response}); was the game "
                                  f"launched with the loader? Relaunch with `duo --keep-running`.")
            before[name] = response["reload"]["generation"]
        for name in names:
            _game = ctx.game(name)

            def reloaded():
                reload = (_game.read_status() or {}).get("reload") or {}
                if reload.get("generation", -1) > before[name]:
                    if reload.get("error"):
                        raise ctx.failure(f"{name or 'host'}: reloaded but {reload['error']}")
                    return reload
                if reload.get("error"):
                    raise ctx.failure(f"{name or 'host'}: reload refused: {reload['error']}")
                return None
            ctx.wait_for(f"{name or 'host'}: reloading", reloaded, 30, _game=_game)
        log(f"Both games run the new build ({time.monotonic() - started:.1f}s)")

        _game = ctx.game('')
        port = ctx.start_session("", _game=_game)["port"]
        _game = ctx.game('client')
        ctx.start_session(f"join {port}", _game=_game)
        seen = ctx.wait_for("Each game shows the other player", ctx.both_see_each_other, seconds, _game=_game)
        for name in names:
            _game = ctx.game(name)
            ctx.face_actor(next(iter(seen[name or "host"])), _game=_game)
