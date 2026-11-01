"""The harness's runtime on Windows: both games run natively as this user, on its Steam, in windows side by side.

What Linux gets from a Proton prefix per game, Windows shares: Documents/My Games (INIs, saves, SKSE and crash logs)
and Plugins.txt are the user's own. Each game's process is the PID recorded at launch (prefix/game.json).
"""
import _winapi
import contextlib
import json
import os
import shutil
import subprocess
import time
from .common import (
    ADDRESS_LIBRARY,
    BUNDLED_START,
    DLL,
    GAME,
    PLUGINS,
    SKSE_DIR,
    STEAM_GAME,
    TEST_HOME,
    UI_SOURCE,
    WINDOW_SIZE,
)
from .runtime import Runtime
from .utilities import (
    log,
    set_ini,
)

if (seven_zip := os.path.expandvars(r"%ProgramFiles%\7-Zip")) and os.path.exists(seven_zip):  # deploy_crash_logger's 7z
    os.environ["PATH"] += os.pathsep + seven_zip
GAME_EXE = "SkyrimSE.exe"
CRASH_OWNERS = TEST_HOME / "crash-owners.json"
STARTED = time.time()


def psutil():
    try:
        import psutil
    except ImportError:
        raise SystemExit("The Windows harness needs psutil: python -m pip install psutil")
    return psutil


def link(source, target):
    """target names source: a symlink (Developer Mode), else a hard link (files) or junction (folders)."""
    try:
        target.symlink_to(source, target_is_directory=source.is_dir())
        return
    except OSError:
        pass
    try:
        if source.is_dir():
            _winapi.CreateJunction(str(source), str(target))
        else:
            os.link(source, target)
    except OSError as error:
        raise SystemExit(f"Cannot link {target} to {source} ({error}). Turn on Developer Mode (symlinks), or keep the "
                         f"game and build/ on one drive (hard links).")


class WindowsRuntime(Runtime):
    def ensure_client_netns(ctx):
        pass

    def in_client_netns(ctx, pid):
        return False

    def instance_user(ctx, *, _game=None):
        return None

    def as_instance_user(ctx, command, env, *, _game=None):
        return command

    def ensure_user_steam(ctx, *, _game=None):
        pass

    def own_prefix(ctx, *, _game=None):
        pass

    def ensure_virtual_display(ctx, *, _game=None):
        pass

    def setup_display(ctx):
        raise ctx.failure("Windows shows the games in windows: no off-screen display to set up")

    def game_env(ctx, *, _game=None):
        _game = ctx.game(_game)
        env = dict(os.environ)
        env.update(SteamAppId="489830", SteamGameId="489830",
                   SKYRIMCOOP_LOG_LEVEL=env.get("SKYRIMCOOP_LOG_LEVEL", "debug"),
                   SKYRIMCOOP_NO_CRASH_REPORTS="1",
                   SKYRIMCOOP_UI_DIR=env.get("SKYRIMCOOP_UI_DIR", str(UI_SOURCE)))
        if _game.name:
            env["SKYRIMCOOP_INSTANCE"] = _game.name
        env["TRACY_PORT"] = str(_game.tracy_port)
        return env

    def recorded_process(ctx, _game):
        """The game's live process recorded at launch, or None (exited, or the PID reused)."""
        try:
            record = json.loads((_game.prefix / "game.json").read_text())
            process = psutil().Process(record["pid"])
            if process.create_time() == record["created"]:
                return process
        except (OSError, ValueError, KeyError, psutil().Error):
            pass
        return None

    def prefix_processes(ctx, *, _game=None):
        _game = ctx.game(_game)
        process = ctx.recorded_process(_game)
        return [process.pid] if process else []

    def game_process(ctx, instance):
        if process := ctx.recorded_process(ctx.game(instance)):
            return process.pid
        raise ctx.failure(f"[{instance or 'host'}] no running {GAME_EXE}")

    @contextlib.contextmanager
    def game_stalled(ctx, instance):
        process = psutil().Process(ctx.game_process(instance))
        process.suspend()
        log(f"[{instance or 'host'}] game stalled")
        try:
            yield
        finally:
            process.resume()
            log(f"[{instance or 'host'}] game runs again")

    def stop_game(ctx, *, _game=None):
        _game = ctx.game(_game)
        if process := ctx.recorded_process(_game):
            with contextlib.suppress(psutil().Error):
                process.kill()
                process.wait(10)
        if ctx.prefix_processes(_game=_game):
            return False
        (_game.prefix / "game.json").unlink(missing_ok=True)  # stopped, not crashed
        return True

    def process_memory(ctx, pid):
        memory = psutil().Process(pid).memory_info()
        return {"rss_bytes": memory.rss, "swap_bytes": max(memory.pagefile - memory.rss, 0), "virtual_bytes": memory.vms}

    def process_cpu_seconds(ctx, pid):
        times = psutil().Process(pid).cpu_times()
        return times.user + times.system

    def report_game_memory(ctx):
        sizes = []
        for game in ctx.games:
            if process := ctx.recorded_process(game):
                with contextlib.suppress(psutil().Error):
                    sizes.append(process.memory_info().rss // (1 << 20))
        if sizes:
            print("Game memory: " + ", ".join(f"{mb}MB" for mb in sorted(sizes)))

    def check_prerequisites(ctx):
        ctx.ensure_duo_game()
        required = [GAME / GAME_EXE, GAME / "skse64_loader.exe", PLUGINS / "versionlib-1-6-1170-0.bin", DLL]
        missing = [str(path) for path in required if not path.exists()]
        if missing:
            raise ctx.failure("Missing:\n  " + "\n  ".join(missing) + "\nBuild the DLL: scripts/xmake-msvc.sh f -m dev && "
                              "scripts/xmake-msvc.sh build -j8 SkyrimTogetherClientDLL && scripts/xmake-msvc.sh build SkyrimCoopLoader")
        if not any(process.info["name"] and process.info["name"].lower() == "steam.exe"
                   for process in psutil().process_iter(["name"])):
            raise ctx.failure("Steam must be running (Skyrim's DRM needs it).")

    def ensure_duo_game(ctx):
        """The duo's game folder: the 1.6.1170 game's files and SKSE's linked, its own Data/SKSE/Plugins."""
        plugins = GAME / "Data/SKSE/Plugins"
        plugins.mkdir(parents=True, exist_ok=True)
        own = {"Data", "SkyrimSELauncher.exe"}
        own_data = {"SKSE", BUNDLED_START}
        sources = [(source, GAME / source.name) for source in STEAM_GAME.iterdir() if source.name not in own]
        sources += [(source, GAME / "Data" / source.name) for source in (STEAM_GAME / "Data").iterdir()
                    if source.name not in own_data]
        sources += [(source, GAME / source.name) for source in SKSE_DIR.glob("skse64_*") if source.suffix in (".exe", ".dll")]
        sources += [(ADDRESS_LIBRARY / "versionlib-1-6-1170-0.bin", plugins / "versionlib-1-6-1170-0.bin")]
        for source, target in sources:
            if not target.is_symlink() and not target.exists() and source.exists():
                link(source, target)

    def crash_logs(ctx, name):
        """CrashLoggerSSE's logs of one game, oldest first. Both games write them into the shared My Games: a new log
        belongs to the launched games gone by the time it is settled (until then, to neither); one from before this
        harness started counts as seen by both."""
        directory = ctx.game(name).my_games / "SKSE"
        logs = sorted(path for path in directory.glob("crash-*.log") if path.suffixes == [".log"])
        try:
            owners = json.loads(CRASH_OWNERS.read_text())
        except (OSError, ValueError):
            owners = {}
        if unknown := [path for path in logs if path.name not in owners]:
            gone = [game.name for game in ctx.games
                    if (game.prefix / "game.json").exists() and not ctx.recorded_process(game)]
            for path in unknown:
                written = path.stat().st_mtime
                if written < STARTED:
                    owners[path.name] = None
                elif gone and time.time() - written > 2:
                    owners[path.name] = gone
            CRASH_OWNERS.parent.mkdir(parents=True, exist_ok=True)
            CRASH_OWNERS.write_text(json.dumps(owners, indent=1))
        return [path for path in logs if path.name in owners and (owners[path.name] is None or name in owners[path.name])]

    def prepare_prefix(ctx, bundled_start=True, *, _game=None):
        _game = ctx.game(_game)
        _game.prefix.mkdir(parents=True, exist_ok=True)
        _game.my_games.mkdir(parents=True, exist_ok=True)
        skyrim_ini, prefs_ini = _game.my_games / "Skyrim.ini", _game.my_games / "SkyrimPrefs.ini"
        if not skyrim_ini.exists():
            shutil.copy(_game.install / "Skyrim_Default.ini", skyrim_ini)
        if not prefs_ini.exists():
            shutil.copy(_game.install / "Low.ini", prefs_ini)
        # bBackgroundMouse: DirectInput's mouse non-exclusive, so a game window does not capture the desktop's cursor
        # (the harness drives input through the engine's dispatch, not the mouse).
        set_ini(skyrim_ini, {"General": {"sIntroSequence": "", "sStartingConsoleCommand": "", "bAlwaysActive": "1"},
                             "Controls": {"bBackgroundMouse": "1"},
                             "Camera": {"fAutoVanityModeDelay": "1000000"},
                             "Interface": {"bShowTutorials": "0"}})
        # Read at the game's start: the games launch one after the other, each at its own place.
        width, height = WINDOW_SIZE
        set_ini(prefs_ini, {"Display": {"bFull Screen": "0", "bBorderless": "0",
                                        "iSize W": str(width), "iSize H": str(height),
                                        "iLocation X": str(width if _game.name else 0), "iLocation Y": "0"},
                            "AudioMenu": {"fAudioMasterVolume": "0.0000"},
                            "Interface": {"bDialogueSubtitles": "1", "bGeneralSubtitles": "1"}})
        if bundled_start:
            ctx.enable_bundled_start(_game=_game)

    def start_game(ctx, run_dir, *, _game=None):
        """SKSE's loader starts the game and exits: the game's process is the SkyrimSE.exe that appears."""
        _game = ctx.game(_game)
        before = {process.pid for process in psutil().process_iter(["name"]) if process.info["name"] == GAME_EXE}
        output = open(run_dir / f"launcher{'-' + _game.name if _game.name else ''}.log", "w")
        launcher = subprocess.Popen([str(_game.install / "skse64_loader.exe")], env=ctx.game_env(_game=_game),
                                    cwd=_game.install, stdout=output, stderr=subprocess.STDOUT,
                                    creationflags=subprocess.CREATE_NEW_PROCESS_GROUP)

        def started():
            return next((process for process in psutil().process_iter(["name"])
                         if process.info["name"] == GAME_EXE and process.pid not in before), None)
        process = ctx.wait_for(f"{_game.name or 'host'}: {GAME_EXE} starts", started, 60, _game=_game)
        (_game.prefix / "game.json").write_text(json.dumps({"pid": process.pid, "created": process.create_time()}))
        return launcher
