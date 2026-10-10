#!/usr/bin/env python3
"""Build and publish the movement DLL without restarting Skyrim.

Only Code/hotreload/MovementCore.cpp and MovementMath.h are reloadable today.
The resident SKSE DLL must be rebuilt/installed once before using this tool.
"""
import argparse
import hashlib
import json
import math
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import time
import uuid

ROOT = Path(__file__).resolve().parents[1]
CORE = ROOT / "Code/hotreload"
SOURCES = [CORE / name for name in ("MovementCore.cpp", "MovementMath.h", "MovementApi.h")]


def fingerprint():
    return hashlib.sha256(b"".join(path.read_bytes() for path in SOURCES)).hexdigest()[:20]


def build(compiler=None):
    compiler = compiler or os.environ.get("COOP_CXX", "x86_64-w64-mingw32-g++-posix")
    output_dir = ROOT / "build/hotreload"
    output_dir.mkdir(parents=True, exist_ok=True)
    build_id = fingerprint()
    # Build in a unique temporary location; a failed compile cannot corrupt the
    # last successful artifact or publish a partial DLL to the running game.
    with tempfile.TemporaryDirectory(prefix="compile-", dir=output_dir) as temporary:
        candidate = Path(temporary) / "SkyrimCoopMovementCore.dll"
        command = [compiler, "-std=c++17", "-O1", "-g", "-shared", "-static-libgcc",
                   "-static-libstdc++", "-fno-exceptions", "-fno-rtti",
                   f'-DCOOP_BUILD_ID="{build_id}"', str(CORE / "MovementCore.cpp"),
                   "-o", str(candidate)]
        subprocess.run(command, check=True, cwd=ROOT)
        if fingerprint() != build_id:
            raise RuntimeError("Sources changed during compilation; retry after saving.")
        result = output_dir / "SkyrimCoopMovementCore.dll"
        os.replace(candidate, result)
    print(f"Built movement code {build_id}: {result}", flush=True)
    return result, build_id


def atomic_write(path, data):
    temporary = path.with_name(path.name + "." + uuid.uuid4().hex + ".tmp")
    try:
        with temporary.open("w", encoding="utf-8") as output:
            output.write(data)
            output.flush()
            os.fsync(output.fileno())
        os.replace(temporary, path)
    finally:
        temporary.unlink(missing_ok=True)


def directory(plugins):
    if not plugins:
        raise RuntimeError("Set SKYRIM_SKSE_PLUGINS or pass --plugins /path/to/Data/SKSE/Plugins")
    plugins = Path(plugins).expanduser().resolve()
    if not plugins.is_dir():
        raise RuntimeError(f"SKSE plugins directory does not exist: {plugins}")
    return plugins / "SkyrimCoopHotReload"


def read_status(folder):
    try:
        status = json.loads((folder / "status.json").read_text())
    except (OSError, ValueError):
        return None
    if not isinstance(status, dict):
        return None
    heartbeat = status.get("heartbeat_ms", 0)
    if not isinstance(heartbeat, (int, float)) or not math.isfinite(heartbeat):
        return None
    # Windows/Wine uses the same wall clock. A stale file from a previous game
    # process is never an acknowledgement from the current one.
    if not -1000 <= time.time() * 1000 - heartbeat <= 3000:
        return None
    return status


def wait_for_request(folder, request, build_id, timeout, previous_pid=None):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        status = read_status(folder)
        if status and status.get("request") == request:
            if previous_pid is not None and status.get("pid") != previous_pid:
                raise RuntimeError("Skyrim PID changed during reload; this is not a live reload.")
            if status.get("state") == "rejected":
                raise RuntimeError(f"Plugin rejected module: {status.get('error')}")
            expected_state = "builtin" if request == "builtin" else "loaded"
            if status.get("state") == expected_state and status.get("build_id") == build_id:
                print(f"Acknowledged by Skyrim PID {status['pid']}: {build_id}, "
                      f"generation {status['generation']}. This confirms loading, not actor movement.")
                return status
        time.sleep(0.1)
    raise RuntimeError("No fresh acknowledgement from Skyrim. Check that the updated resident "
                       "plugin is installed and the game is loaded and updating. Request remains pending.")


def publish(folder, module, build_id, timeout):
    folder.mkdir(parents=True, exist_ok=True)
    before = read_status(folder)
    request = "movement-" + uuid.uuid4().hex + ".dll"
    # The mapped DLL is never overwritten. Publish bytes first, then an atomic
    # request file. Keep previous versions for rollback and debugger symbols.
    temporary = folder / (request + ".tmp")
    shutil.copyfile(module, temporary)
    os.replace(temporary, folder / request)
    atomic_write(folder / "request.txt", request + "\n")
    return wait_for_request(folder, request, build_id, timeout, before.get("pid") if before else None)


def observe(folder, actor_id, seconds, tolerance):
    """Require fresh samples from a loaded engine actor, with actual displacement."""
    initial = read_status(folder)
    if not initial or initial.get("state") != "loaded":
        raise RuntimeError("No live game with a loaded movement module.")
    start_ms = time.time() * 1000
    deadline = time.monotonic() + seconds
    positions = []
    last_sample = None
    while time.monotonic() < deadline:
        status = read_status(folder)
        if not status:
            raise RuntimeError("Game heartbeat stopped during observation.")
        if (status.get("pid"), status.get("generation"), status.get("state")) != (
                initial.get("pid"), initial.get("generation"), "loaded"):
            raise RuntimeError("Game or module changed during observation; rerun this check.")
        if status.get("invalid_outputs", 0):
            raise RuntimeError("Module returned invalid movement output.")
        actor = status.get("actors", {}).get(str(actor_id))
        if actor and actor.get("observed_ms", 0) >= start_ms and actor.get("observed_ms") != last_sample:
            if actor.get("generation") == status.get("generation") and actor.get("has_3d"):
                requested, actual = actor["requested"], actor["actual"]
                if len(requested) != 3 or len(actual) != 3 or not all(
                        math.isfinite(value) for value in requested + actual):
                    raise RuntimeError("Invalid engine position observation.")
                if math.dist(requested, actual) > tolerance:
                    raise RuntimeError(f"Actor {actor_id:08X}: engine position {actual} differs "
                                       f"from requested {requested}")
                positions.append(actual)
                last_sample = actor["observed_ms"]
        time.sleep(0.2)
    if len(positions) < 2 or not any(math.dist(positions[0], p) > tolerance for p in positions[1:]):
        raise RuntimeError("No verified displacement of a loaded engine actor. "
                           "The bot must be registered and moving; packet traffic alone is insufficient.")
    print(f"Observed actor {actor_id:08X} move in Skyrim PID {initial['pid']} "
          f"using generation {initial['generation']} ({len(positions)} engine samples). "
          "This verifies position readback, not rendered animation or physics.")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("command", choices=["build", "reload", "watch", "status", "builtin", "observe", "host", "simulate"])
    parser.add_argument("--plugins", default=os.environ.get("SKYRIM_SKSE_PLUGINS"))
    parser.add_argument("--compiler")
    parser.add_argument("--bot", type=Path, default=ROOT / "build/linux/x86_64/release/fake_player")
    parser.add_argument("--timeout", type=float, default=15)
    parser.add_argument("--actor", type=lambda value: int(value, 16), help="Skyrim actor FormID in hex")
    parser.add_argument("--seconds", type=float, default=5)
    parser.add_argument("--tolerance", type=float, default=2, help="Skyrim world units")
    args = parser.parse_args()
    if not all(math.isfinite(v) and v > 0 for v in (args.timeout, args.seconds, args.tolerance)):
        parser.error("timeout, seconds and tolerance must be positive finite numbers")
    if args.command == "build":
        build(args.compiler)
        return
    folder = directory(args.plugins)
    if args.command == "host":
        folder.mkdir(parents=True, exist_ok=True)
        if not (folder / "request.txt").exists():
            atomic_write(folder / "request.txt", "builtin\n")
        request = uuid.uuid4().hex
        atomic_write(folder / "session.txt", request + "\n")
        deadline = time.monotonic() + args.timeout
        while time.monotonic() < deadline:
            status = read_status(folder)
            session = status.get("session", {}) if status else {}
            if session.get("request") == request:
                if session.get("state") == "failed":
                    raise RuntimeError(session.get("error"))
                if session.get("state") == "ready":
                    print(f"Host ready in Skyrim PID {status['pid']} on port {session['port']}. "
                          "Player snapshot captured; run simulate in another terminal.")
                    return
            time.sleep(0.2)
        raise RuntimeError("Host preparation timed out. Load a save and check status / tp_client.log.")
    elif args.command == "simulate":
        status = read_status(folder)
        session = status.get("session", {}) if status else {}
        if session.get("state") != "ready" or not session.get("world_ready"):
            raise RuntimeError("Run host with a save loaded before starting the network bot.")
        if not args.bot.is_file():
            raise RuntimeError(f"Bot not built: {args.bot}. Build the native fake_player target first.")
        subprocess.run([str(args.bot.resolve()), "--fixture-dir", str(folder / session["snapshot"]),
                        "--port", str(session["port"]), "--duration", str(max(1, int(args.seconds)))],
                       cwd=ROOT, check=True)
    elif args.command == "status":
        status = read_status(folder)
        if not status:
            raise RuntimeError("No fresh Skyrim heartbeat. Old status files do not count as a live game.")
        print(json.dumps(status, indent=2))
    elif args.command == "observe":
        if args.actor is None:
            parser.error("observe requires --actor FORM_ID")
        observe(folder, args.actor, args.seconds, args.tolerance)
    elif args.command == "builtin":
        folder.mkdir(parents=True, exist_ok=True)
        before = read_status(folder)
        atomic_write(folder / "request.txt", "builtin\n")
        wait_for_request(folder, "builtin", "builtin", args.timeout, before.get("pid") if before else None)
    elif args.command == "reload":
        module, build_id = build(args.compiler)
        publish(folder, module, build_id, args.timeout)
    else:
        previous = None
        print("Watching movement sources. Ctrl-C stops watching; Skyrim keeps running.", flush=True)
        while True:
            current = fingerprint()
            if current != previous:
                # Debounce editors that save multiple files in succession.
                time.sleep(0.3)
                if fingerprint() != current:
                    continue
                previous = current
                try:
                    module, build_id = build(args.compiler)
                    publish(folder, module, build_id, args.timeout)
                except (RuntimeError, OSError, subprocess.CalledProcessError) as error:
                    print(f"Reload failed: {error}", file=sys.stderr, flush=True)
            time.sleep(0.3)


if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        pass
    except (RuntimeError, OSError, subprocess.CalledProcessError) as error:
        print(f"ERROR: {error}", file=sys.stderr)
        sys.exit(1)
