"""coop_test.py profile: where a running duo's time goes (docs/design/testing.md, Profiling).

Per game over one window: fps and frame-time percentiles, CPU and memory, network bytes by message, each hook's calls
per second (the plugin's `perf` command), our zones' total and self time (a Tracy capture, opened in Tracy's viewer
to dig in) and, with --stacks, whole-process call stacks, engine and ours (the plugin's `sample` command).
"""
import argparse
import bisect
import collections
import contextlib
import csv
import hashlib
import io
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import time
import zipfile
from .common import (
    DLL,
    PLUGINS,
    ROOT,
    TEST_HOME,
    WINDOWS,
)
from .errors import TestFailure
from .utilities import log

TRACY_VERSION = "0.11.1"  # the plugin's (xmake.lua): a capture needs the same protocol
TRACY_WINDOWS_SHA256 = "e01e73903dd8babb634cb9ae48f3d5b52f3b519cf895548dd06538190554dc8a"
TRACY_TOOLS = ROOT / f"build/third_party/tracy-{TRACY_VERSION}"
PROFILES = TEST_HOME / "profiles"
LLVM_SYMBOLIZER = ROOT / "build/toolchains/LLVM/bin" / ("llvm-symbolizer.exe" if WINDOWS else "llvm-symbolizer")
# Leaves where a sampled thread blocks rather than runs (by export name: ntdll's other system calls, VirtualQuery's
# say, are work).
WAITING_CALLS = re.compile(r"!(Nt|Zw)(WaitFor|Wait|DelayExecution|RemoveIoCompletion|ReplyWaitReceive|SignalAndWait|"
                           r"AlertThreadByThreadId|UserMsgWaitForMultipleObjectsEx|UserGetMessage)|!NtGdiDdDDIWaitFor")


class Profiling:
    def tracy_tool(ctx, name):
        """Tracy's `capture` or `csvexport`: Windows from the release zip, Linux built from the tag's source."""
        if WINDOWS:
            tool = TRACY_TOOLS / f"tracy-{name}.exe"
            if not tool.exists():
                TRACY_TOOLS.mkdir(parents=True, exist_ok=True)
                archive = TRACY_TOOLS / "windows.zip"
                url = f"https://github.com/wolfpld/tracy/releases/download/v{TRACY_VERSION}/windows-{TRACY_VERSION}.zip"
                log(f"Downloading Tracy {TRACY_VERSION}'s tools")
                subprocess.run(["curl", "-sSfL", "-o", str(archive), url], check=True)
                if (digest := hashlib.sha256(archive.read_bytes()).hexdigest()) != TRACY_WINDOWS_SHA256:
                    archive.unlink()
                    raise ctx.failure(f"Tracy tools checksum mismatch: {digest}")
                with zipfile.ZipFile(archive) as zipped:
                    zipped.extractall(TRACY_TOOLS)
            return tool
        source = TRACY_TOOLS / "source"
        tool = source / name / "build" / f"tracy-{name}"
        if not tool.exists():
            if not source.exists():
                log(f"Fetching Tracy {TRACY_VERSION}'s source")
                subprocess.run(["git", "clone", "--depth", "1", "--branch", f"v{TRACY_VERSION}",
                                "https://github.com/wolfpld/tracy", str(source)], check=True)
            log(f"Building tracy-{name} (once)")
            subprocess.run(["cmake", "-S", str(source / name), "-B", str(source / name / "build"),
                            "-DCMAKE_BUILD_TYPE=Release"], check=True, stdout=subprocess.DEVNULL)
            subprocess.run(["cmake", "--build", str(source / name / "build"), "-j8"], check=True, stdout=subprocess.DEVNULL)
        return tool

    def process_cpu_seconds(ctx, pid):
        """A process's CPU time so far (all threads, kernel and user)."""
        fields = Path(f"/proc/{pid}/stat").read_text().rsplit(")", 1)[1].split()
        return (int(fields[11]) + int(fields[12])) / os.sysconf("SC_CLK_TCK")

    def perf_sample(ctx, game, window_ms=60000):
        """What the profile compares before and after: the plugin's perf readout, frames, the process's CPU time."""
        response = game.rpc({"cmd": "perf", "window_ms": int(window_ms)}, timeout=10) or {}
        if not response.get("ok"):
            raise ctx.failure(f"[{game.name or 'host'}] no perf readout ({response.get('error', 'no answer')}); "
                              f"is the deployed build current? (`duo --keep-running` deploys it)")
        status = game.read_status() or {}
        pid = ctx.game_process(game.name)
        return {"time": time.monotonic(), "perf": response["perf"], "frames": (status.get("frames") or {}).get("frames", 0),
                "stalls": (status.get("frames") or {}).get("stalls", 0), "cpu": ctx.process_cpu_seconds(pid),
                "memory": ctx.process_memory(pid), "line": status.get("line") or {}}

    def add_fight_load(ctx, wolves):
        """A fight that loads both games: god mode for both players, wolves at the host's."""
        for game in ctx.games:
            ctx.run_console("tgm", _game=game)
        ctx.run_console(f"player.placeatme 00023ABE {wolves}", _game=ctx.host)
        log(f"{wolves} wolves at the host; waiting for the fight to start")
        time.sleep(8)

    def command_profile(ctx, args):
        games = [ctx.host, ctx.guest] if args.instance in (None, "both") else [ctx.game(args.instance)]
        for game in games:
            if not game.read_status():
                raise ctx.failure(f"{game.name or 'host'} game is not running: start `{sys.argv[0]} duo --keep-running` first")
        run_dir = PROFILES / time.strftime("%Y%m%d-%H%M%S")
        run_dir.mkdir(parents=True, exist_ok=True)
        if args.load:
            kind, _, count = args.load.partition("=")
            if kind != "wolves" or not count.isdigit():
                raise ctx.failure(f"--load {args.load}: only wolves=N")
            ctx.add_fight_load(int(count))

        captures = {}
        if not args.no_tracy:
            for game in games:
                output = run_dir / f"{game.name or 'host'}.tracy"
                command = [str(ctx.tracy_tool("capture")), "-o", str(output), "-a", "127.0.0.1", "-p", str(game.tracy_port),
                           "-f", "-s", str(int(args.seconds))]
                # In the game's network namespace when it has one (Linux duo): Tracy listens on its localhost.
                command = ctx.as_instance_user(command, dict(os.environ), _game=game)
                captures[game.name] = (output, subprocess.Popen(command, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE))
        samples = {}
        if args.stacks:
            for game in games:
                response = game.rpc({"cmd": "sample", "seconds": args.seconds, "interval_ms": 1}) or {}
                if not response.get("ok"):
                    raise ctx.failure(f"[{game.name or 'host'}] sampling refused: {response.get('error', 'no answer')}")
                samples[game.name] = response["run"]
        before = {game.name: ctx.perf_sample(game) for game in games}
        log(f"Profiling {', '.join(game.name or 'host' for game in games)} for {args.seconds:.0f}s")

        started = time.monotonic()
        if args.scenario:
            scenarios = argparse.Namespace(hold=0, names=args.scenario, shuffle=False, seed=None, repeat=1, until_fail=0,
                                           tag=None, fail_fast=False, dry_run=False, no_restart=True)
            with contextlib.suppress(TestFailure, SystemExit):
                ctx.command_scenario(scenarios)
        time.sleep(max(0.0, args.seconds - (time.monotonic() - started)))
        window_ms = (time.monotonic() - started) * 1000
        after = {game.name: ctx.perf_sample(game, window_ms) for game in games}

        zones = {}
        for name, (output, process) in captures.items():
            try:
                _, error = process.communicate(timeout=60)
            except subprocess.TimeoutExpired:
                process.kill()
                error = b"timed out"
            if output.exists():
                zones[name] = ctx.tracy_zones(output)
            else:
                log(f"[{name or 'host'}] no Tracy capture ({error.decode(errors='replace').strip()[-200:]})")
        stacks = {}
        for name, run in samples.items():
            game = ctx.game(name)
            result = ctx.wait_for(f"[{name or 'host'}] stack samples written",
                                  lambda: (lambda r: r if r.get("finished", 0) >= run else None)(game.rpc({"cmd": "sample_result"}) or {}),
                                  60, _game=game)
            stacks[name] = ctx.stack_report(Path(result["file"]), run_dir / f"{name or 'host'}-stacks")

        summary = ctx.profile_summary(games, before, after, zones, stacks, args)
        (run_dir / "summary.md").write_text(summary, encoding="utf-8")
        (run_dir / "raw.json").write_text(json.dumps({"before": before, "after": after}, indent=1, default=str))
        print(summary)
        log(f"Profile: {run_dir}" + (f"  (open a .tracy in Tracy's viewer: {TRACY_TOOLS / 'tracy-profiler.exe'})"
                                     if WINDOWS and captures else ""))

    def tracy_zones(ctx, capture):
        """{zone: {calls, total_ms, self_ms, mean_us, max_us}} from a capture (tracy-csvexport, total and self)."""
        tool = str(ctx.tracy_tool("csvexport"))
        zones = {}
        for flags, key in (([], "total_ms"), (["-e"], "self_ms")):
            result = subprocess.run([tool, *flags, str(capture)], capture_output=True, text=True)
            for row in csv.DictReader(io.StringIO(result.stdout)):
                zone = zones.setdefault(row["name"], {})
                zone[key] = int(row["total_ns"]) / 1e6
                zone.update(calls=int(row["counts"]), mean_us=int(row["mean_ns"]) / 1e3, max_us=int(row["max_ns"]) / 1e3)
        return zones

    def stack_report(ctx, samples, out_prefix):
        """The plugin's samples symbolized: each thread's top inclusive and self functions, and a speedscope file."""
        modules, threads, stacks, header = [], {}, [], {}
        for line in Path(samples).read_text(encoding="utf-8", errors="replace").splitlines():
            kind, _, rest = line.partition(" ")
            if kind == "module":
                base, size, path = rest.split(" ", 2)
                modules.append((int(base, 16), int(base, 16) + int(size, 16), path))
            elif kind == "thread":
                fields = rest.split(" ", 6)
                threads[int(fields[0])] = {"samples": int(fields[2]), "cpu_ms": int(fields[4]),
                                           "name": fields[6] if len(fields) > 6 else ""}
            elif kind == "stack":
                fields = rest.split()
                stacks.append((int(fields[0]), int(fields[1]), [int(frame, 16) for frame in fields[2:]]))
            elif kind == "duration_ms":
                values = rest.split()
                header = dict(zip(["duration_ms"] + values[1::2], [values[0]] + values[2::2]))
        modules.sort()
        names = ctx.frame_namer(modules, {frame for _, _, frames in stacks for frame in frames})
        report = {}
        main_thread = int(header.get("main_thread", 0))
        for thread, info in threads.items():
            own = [(count, frames) for tid, count, frames in stacks if tid == thread]
            total = sum(count for count, _ in own)
            if not total:
                continue
            running = [(count, frames) for count, frames in own if not WAITING_CALLS.search(names[frames[0]])]
            busy = sum(count for count, _ in running)
            inclusive, own_time = collections.Counter(), collections.Counter()
            for count, frames in running:
                for name in {names[frame] for frame in frames}:
                    inclusive[name] += count
                own_time[names[frames[0]]] += count
            # Shares of all the thread's samples; the frames under nearly every running sample (the thread's roots) left out.
            report[thread] = {"main": thread == main_thread, "name": info["name"], "cpu_ms": info["cpu_ms"], "samples": total,
                              "waiting": 1 - busy / total,
                              "inclusive": [(name, count / total) for name, count in inclusive.most_common(40)
                                            if count < 0.8 * busy][:20],
                              "self": [(name, count / total) for name, count in own_time.most_common(15)]}
        ctx.write_speedscope(out_prefix.with_suffix(".speedscope.json"), threads, stacks, names)
        return {"threads": report, "duration_ms": int(header.get("duration_ms", 0)),
                "speedscope": str(out_prefix.with_suffix(".speedscope.json"))}

    def frame_namer(ctx, modules, frames):
        """{address: name}: our DLL's functions (llvm-symbolizer, its PDB), the engine's nearest Address Library ID
        with the name ENGINE_REFERENCE.md gives it, else module+offset."""
        def file_name(path):
            return Path(path.replace("\\", "/")).name

        ours = re.compile(re.escape(DLL.stem) + r"(-[\w-]+)?(\.\d+\.\d+)?\.dll", re.IGNORECASE)
        engine = ctx.engine_names()
        names, our_frames = {}, {}
        for frame in frames:
            module = next((m for m in modules if m[0] <= frame < m[1]), None)
            if not module:
                names[frame] = f"(no module: a hook stub or JIT) {frame:#x}"
                continue
            base, _, path = module
            filename = file_name(path)
            if ours.fullmatch(filename):
                our_frames[frame] = frame - base
            elif filename.lower() == "skyrimse.exe" and engine:
                names[frame] = engine(frame - base)
            else:
                names[frame] = ctx.export_name(path, filename, frame - base)
        if our_frames:
            deployed = next((path for path in (PLUGINS / "SkyrimCoop" / DLL.name, PLUGINS / DLL.name) if path.exists()), None)
            if deployed and deployed.read_bytes() != DLL.read_bytes():
                log(f"WARNING: {DLL.name} was rebuilt after the games loaded it: our function names may be wrong "
                    f"(`reload` or `duo --keep-running` deploys the new build)")
            tool = str(LLVM_SYMBOLIZER) if LLVM_SYMBOLIZER.exists() else "llvm-symbolizer"
            items = sorted(our_frames.items())
            result = subprocess.run([tool, f"--obj={DLL}", "--relative-address", "--functions=linkage", "--no-inlines",
                                     "--demangle", "--output-style=GNU"],
                                    input="\n".join(f"{offset:#x}" for _, offset in items), capture_output=True, text=True)
            lines = result.stdout.split("\n")
            for index, (frame, offset) in enumerate(items):
                name = lines[2 * index] if 2 * index < len(lines) and lines[2 * index] not in ("", "??") else f"{offset:#x}"
                names[frame] = "ours " + name
        return names

    def export_name(ctx, path, filename, rva):
        """A system DLL's frame by its nearest preceding export (ntdll!NtWaitForSingleObject), else module+offset."""
        cache = ctx.__dict__.setdefault("_exports", {})
        if filename not in cache:
            cache[filename] = pe_exports(windows_path(path))
        exports = cache[filename]
        index = bisect.bisect_right([start for start, _ in exports], rva) - 1
        if index < 0:
            return f"{filename}+{rva:#x}"
        start, name = exports[index]
        return f"{filename}!{name}+{rva - start:#x}"

    def engine_names(ctx):
        """An RVA's nearest preceding Address Library ID in SkyrimSE.exe, named where docs/re knows it; None without
        the Address Library."""
        sys.path.insert(0, str(ROOT / "scripts/re"))
        import build_re_data as re_data
        library = PLUGINS / "versionlib-1-6-1170-0.bin"
        if not library.exists():
            return None
        ids = re_data.load_address_library(library)
        named = {}
        if re_data.OUT_JSON.exists():
            named = {a["id"]: a["name"] for a in json.loads(re_data.OUT_JSON.read_text())["annotations"]}
        else:  # the committed reference: the IDs we use, with community names
            row = re.compile(r"^\| (\d+) \| `0x[0-9a-f]+` \| `([^`]+)`")
            for line in (ROOT / "docs/re/ENGINE_REFERENCE.md").read_text(encoding="utf-8").splitlines():
                if (match := row.match(line)) and match.group(2) != "?":
                    named[int(match.group(1))] = match.group(2)
        table = sorted((rva, ident) for ident, rva in ids.items())
        starts = [rva for rva, _ in table]

        def name(rva):
            index = bisect.bisect_right(starts, rva) - 1
            if index < 0:
                return f"SkyrimSE.exe+{rva:#x}"
            start, ident = table[index]
            return f"engine {named.get(ident, f'ID {ident}')}+{rva - start:#x}"
        return name

    def write_speedscope(ctx, path, threads, stacks, names):
        """The samples as a speedscope file (one sampled profile per thread): speedscope.app opens it as a flame graph."""
        frames, index = [], {}
        profiles = []
        for thread, info in threads.items():
            samples, weights = [], []
            for tid, count, stack in stacks:
                if tid != thread:
                    continue
                ids = []
                for frame in reversed(stack):  # root first
                    name = names[frame]
                    if name not in index:
                        index[name] = len(frames)
                        frames.append({"name": name})
                    ids.append(index[name])
                samples.append(ids)
                weights.append(count)
            if samples:
                profiles.append({"type": "sampled", "name": f"thread {thread} {info['name']}".strip(), "unit": "none",
                                 "startValue": 0, "endValue": sum(weights), "samples": samples, "weights": weights})
        path.write_text(json.dumps({"$schema": "https://www.speedscope.app/file-format-schema.json",
                                    "shared": {"frames": frames}, "profiles": profiles, "name": path.stem}))

    def profile_summary(ctx, games, before, after, zones, stacks, args):
        lines = [f"# Profile {time.strftime('%Y-%m-%d %H:%M:%S')}", ""]
        if args.load:
            lines.append(f"Load: {args.load}")
        if args.scenario:
            lines.append(f"Scenarios: {', '.join(args.scenario)}")
        for game in games:
            name = game.name or "host"
            first, last = before[game.name], after[game.name]
            seconds = last["time"] - first["time"]
            perf = last["perf"]
            frame, update = perf.get("frame_ms", {}), perf.get("update_ms", {})
            memory = last["memory"]
            lines += ["", f"## {name}", "",
                      f"- **{(last['frames'] - first['frames']) / seconds:.1f} fps**, frame ms p50 {frame.get('p50', 0):.1f} / "
                      f"p95 {frame.get('p95', 0):.1f} / p99 {frame.get('p99', 0):.1f} / max {frame.get('max', 0):.1f}, "
                      f"stalls +{last['stalls'] - first['stalls']}",
                      f"- our update ms p50 {update.get('p50', 0):.2f} / p95 {update.get('p95', 0):.2f} / max {update.get('max', 0):.1f}",
                      f"- **CPU {(last['cpu'] - first['cpu']) / seconds * 100:.0f}%** of a core, memory {memory['rss_bytes'] >> 20} MB "
                      f"resident ({(perf.get('memory_mb') or {}).get('private', '?')} MB private)",
                      f"- messages {perf.get('messages', {}).get('bytes', 0) / seconds / 1024:.1f} KB/s"]
            top = (perf.get("messages") or {}).get("top") or []
            if top:
                lines += ["", "| message | way | /s | KB/s |", "|---|---|---|---|"]
                lines += [f"| {row['name']} | {row['way']} | {row['count'] / seconds:.1f} | {row['bytes'] / seconds / 1024:.2f} |"
                          for row in top[:10]]
            old, new = first["perf"].get("calls", {}), perf.get("calls", {})
            rates = sorted(((new[hook] - old.get(hook, 0)) / seconds, hook) for hook in new)[::-1]
            if rates:
                lines += ["", f"Hooks and bridge calls: {sum(rate for rate, _ in rates):,.0f}/s in all", "",
                          "| call | /s |", "|---|---|"]
                lines += [f"| {hook} | {rate:,.0f} |" for rate, hook in rates[:12] if rate > 0]
            if game.name in zones:
                ranked = sorted(zones[game.name].items(), key=lambda item: -item[1].get("total_ms", 0))
                lines += ["", "Our zones (Tracy)", "", "| zone | calls | total ms | self ms | mean us | max us |",
                          "|---|---|---|---|---|---|"]
                lines += [f"| {zone} | {z.get('calls', 0)} | {z.get('total_ms', 0):.1f} | {z.get('self_ms', 0):.1f} | "
                          f"{z.get('mean_us', 0):.0f} | {z.get('max_us', 0):.0f} |" for zone, z in ranked[:15]]
            if game.name in stacks:
                report = stacks[game.name]
                lines += ["", f"Call stacks (speedscope: {report['speedscope']})"]
                for thread, info in sorted(report["threads"].items(), key=lambda item: (not item[1]["main"], -item[1]["cpu_ms"]))[:4]:
                    lines += ["", f"Thread {thread}{' (main)' if info['main'] else ''} {info['name']}: {info['samples']} samples, "
                                  f"{info['waiting'] * 100:.0f}% waiting", "", "| inclusive | self | function |", "|---|---|---|"]
                    own = dict(info["self"])
                    lines += [f"| {share * 100:.1f}% | {own.get(name, 0) * 100:.1f}% | {name[:140]} |"
                              for name, share in info["inclusive"][:12]]
        return "\n".join(lines) + "\n"


def windows_path(path):
    """A path the game reported, readable here: Wine's Z: is /, its C: is the prefix's drive_c (not resolved: None)."""
    if WINDOWS:
        return Path(path)
    if path[:2].upper() == "Z:":
        return Path(path[2:].replace("\\", "/"))
    return None


def pe_exports(path):
    """[(rva, name)] sorted: a PE file's named exports, from its export directory; [] when unreadable."""
    import struct
    try:
        data = Path(path).read_bytes() if path else b""
        header = struct.unpack_from("<I", data, 0x3C)[0]
        sections = struct.unpack_from("<H", data, header + 6)[0]
        optional = struct.unpack_from("<H", data, header + 20)[0]
        export_rva = struct.unpack_from("<I", data, header + 24 + 112)[0]  # PE32+: data directory 0
        table = header + 24 + optional

        def offset(rva):
            for i in range(sections):
                size, virtual, _, raw = struct.unpack_from("<IIII", data, table + i * 40 + 8)
                if virtual <= rva < virtual + max(size, 1):
                    return rva - virtual + raw
            raise ValueError(rva)
        directory = offset(export_rva)
        count_names, functions, names, ordinals = struct.unpack_from("<IIII", data, directory + 24)
        result = []
        for i in range(count_names):
            name_at = offset(struct.unpack_from("<I", data, offset(names) + 4 * i)[0])
            ordinal = struct.unpack_from("<H", data, offset(ordinals) + 2 * i)[0]
            function = struct.unpack_from("<I", data, offset(functions) + 4 * ordinal)[0]
            result.append((function, data[name_at:data.index(b"\0", name_at)].decode(errors="replace")))
        return sorted(result)
    except (OSError, ValueError, struct.error):
        return []
