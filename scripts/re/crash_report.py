#!/usr/bin/env python3
"""One readable report per crash: what the engine was doing, and what our code did to it just before.

  scripts/re/crash_report.py CRASH_LOG [--plugin-log tp_client.log]

CRASH_LOG is CrashLoggerSSE's crash-*.log (My Games/Skyrim Special Edition/SKSE in the game's
prefix; `coop_test.py crash` finds it). The report keeps its essentials: the exception, the
access-violation analysis, the objects the registers and stack point to, and the probable call
stack with engine function names added from build/re/annotations.json. From our plugin's log it
adds the breadcrumbs our crash handler writes (the last engine changes our code made and the
hooks of ours that ran, see Code/client/Breadcrumbs.h), the heap history when it was on (who
allocated and freed the memory the crash touched, Code/client/HeapHistory.h) and the last lines
logged before the crash.
"""
import argparse
import json
from pathlib import Path
import re
import subprocess

import build_re_data as re_data
import symbolize_crash

SECTIONS = ("ACCESS VIOLATION ANALYSIS:", "POSSIBLE RELEVANT OBJECTS:", "CALL STACK")
FRAME_ID = re.compile(r"-> (\d+)\+0x([0-9A-F]+)")
BREADCRUMBS = "Last engine changes by our code"
TOTALS = "Hooks of ours and engine changes by our code: calls in total"
TOTAL = re.compile(r"^\s+total (\S+) (\d+) last (\d+)$")
HEAP = "Heap history of the memory"
HEAP_WATCHED = "Heap history of the watched addresses"
HEAP_FRAME = re.compile(r"backtrace #\d+ (\S+)\+0x([0-9a-f]+)")
CRASH_MARK = "crash occurred!"
# Plugin log lines that repeat every frame and drown out the rest.
NOISE = re.compile(r"Removing inventory item|refused by the engine, not sent|Spawn Actor: ")


def engine_names():
    try:
        return {a["id"]: a["name"] for a in json.loads(re_data.OUT_JSON.read_text())["annotations"]}
    except (OSError, ValueError):
        return {}


def section(lines, header, limit=40):
    """The lines of a CrashLoggerSSE section (indented, up to the next blank line)."""
    for index, line in enumerate(lines):
        if line.startswith(header):
            body = []
            for following in lines[index + 1:]:
                if not following.strip():
                    break
                body.append(following.rstrip())
            return body[:limit]
    return []


def name_frames(frames, names):
    named = []
    for frame in frames:
        match = FRAME_ID.search(frame)
        name = names.get(int(match.group(1))) if match else None
        named.append(f"{frame}   [{name}]" if name else frame)
    return named


CRUMB = re.compile(r"^\s+(\d+)(?: \.\.\s+(\d+))?\s+\[(\d+)\] (\S+) (.*?)(?: x(\d+))?$")


def summarize_crumbs(crumbs, totals_lines, rare=3, newest=25):
    """Breadcrumbs as: calls per hook, the rare events in order (what ran at most `rare` times in
    the window, and every engine change of ours), anything on a temporary form (FF..., placed or
    spawned at runtime), and the newest few."""
    parsed = []
    for line in crumbs:
        match = CRUMB.match(line)
        if match:
            parsed.append((match.group(4), match.group(5), int(match.group(6) or 1), line))
    totals, last = {}, {}
    for line in totals_lines:
        match = TOTAL.match(line)
        if match:
            totals[match.group(1)], last[match.group(1)] = int(match.group(2)), int(match.group(3))
    for what, _, calls, _ in parsed:
        totals.setdefault(what, calls)
    names = engine_names()
    def label(what):
        """A probe's engine function name (probe:13408 -> probe:13408 TES::sub_13408)."""
        if what.startswith("probe:") and what[6:].isdigit() and int(what[6:]) in names:
            return f"{what} {names[int(what[6:])]}"
        return what
    out = [f"  {calls:>8}  {label(what)} (last {last.get(what, '?')} ms before)"
           for what, calls in sorted(totals.items(), key=lambda item: -item[1])]
    rare_lines = [line for what, _, _, line in parsed
                  if totals[what] <= rare or not what.startswith(("Hook", "&Hook", "probe:"))]
    probes = [line for what, _, _, line in parsed if what.startswith("probe:")]
    temporary = [line for _, subject, _, line in parsed if re.search(r"\bFF[0-9A-F]{6}\b", subject)]
    sections = [("Calls in total per hook or change (those that ran in the last 10s):", out),
                ("Probed engine functions (--probe), oldest first:", probes[-80:]),
                (f"Rare events (at most {rare} calls) and our engine changes, oldest first:", rare_lines[-60:]),
                ("On temporary forms (FF..., placed or spawned at runtime):", temporary[-40:]),
                (f"Newest {newest}:", [line for *_, line in parsed][-newest:])]
    result = []
    for title, lines in sections:
        if lines:
            result += ["", title] + lines
    return result


def name_heap_frames(heap, dll=symbolize_crash.DEFAULT_DLL):
    """The heap history with each frame named: engine frames by Address Library ID, ours by function
    and source line. Our allocator hooks' own frames are left out."""
    resolve_engine = symbolize_crash.engine_resolver()
    ours = sorted({int(m.group(2), 16) for line in heap if (m := HEAP_FRAME.search(line))
                   and m.group(1).lower().startswith(dll.stem.lower())})
    tool = str(symbolize_crash.LLVM_SYMBOLIZER) if symbolize_crash.LLVM_SYMBOLIZER.exists() else "llvm-symbolizer"
    names = {}
    if ours:
        output = subprocess.run([tool, f"--obj={dll}", "--relative-address"], input="\n".join(map(hex, ours)),
                                capture_output=True, text=True).stdout.split("\n\n")
        for offset, block in zip(ours, output):
            parts = block.strip().split("\n")
            names[offset] = f"{parts[0]}  (Code/{parts[1].split('/Code/')[-1]})" if len(parts) > 1 else parts[0]
    out = []
    for line in heap:
        match = HEAP_FRAME.search(line)
        if not match:
            out.append(line)
            continue
        module, offset = match.group(1), int(match.group(2), 16)
        if module.lower() == "skyrimse.exe":
            out.append(f"        {resolve_engine(offset)}")
        else:
            name = names.get(offset, "")
            if re.match(r"(HookForm(Allocate|Free)|HeapHistory|Memory::)", name):
                continue
            out.append(f"        {module}+{offset:#x}  {name}")
    return out


def plugin_context(plugin_log, breadcrumbs=4096, before=15):
    """Our breadcrumbs and the last meaningful lines before the newest crash in the plugin log."""
    try:
        lines = plugin_log.read_text(errors="replace").splitlines()
    except OSError:
        return [], [], ([], [], []), []
    crash = max((i for i, line in enumerate(lines) if CRASH_MARK in line), default=None)
    if crash is None:
        return [], [], ([], [], []), []
    last = [line for line in lines[max(0, crash - 400):crash] if not NOISE.search(line)][-before:]
    def block(header):
        for i in range(crash, len(lines)):
            if header in lines[i]:
                body = []
                for line in lines[i + 1:]:
                    if "] [error]   " not in line:
                        break
                    body.append(line.split("] [error]", 1)[1])
                return body
        return []
    poisoned = [line.split("] [error] ", 1)[1] for line in lines[crash:crash + 2000] if "Heap quarantine: " in line or "Heap guard: " in line]
    return block(BREADCRUMBS)[-breadcrumbs:], block(TOTALS), (poisoned, block(HEAP_WATCHED), block(HEAP)), last


def report(crash_log, plugin_log=None):
    lines = crash_log.read_text(errors="replace").splitlines()
    names = engine_names()
    out = [f"Crash: {crash_log}"]
    out += [line for line in lines[:12] if line.startswith(("CRASH TIME", "Unhandled exception", "Access Violation"))]
    for header in SECTIONS:
        body = section(lines, header)
        if header == "CALL STACK":
            body = name_frames([frame for frame in body if "[P]" in frame or "SkyrimTogether" in frame], names)
        if body:
            out += ["", header.rstrip(":")] + body
    if plugin_log:
        crumbs, totals, heap, last = plugin_context(plugin_log)
        poisoned, watched, heap = heap
        if poisoned:
            out += [""] + poisoned
        if watched:
            out += ["", "Heap history of the watched addresses since boot (SKYRIMCOOP_HEAP_WATCH; oldest first):"]
            out += name_heap_frames(watched)
        if heap:
            out += ["", "Heap history: who allocated and who FREED the memory the crash's registers and stack "
                    "point into (oldest first):"] + name_heap_frames(heap)
        if crumbs:
            out += ["", f"{BREADCRUMBS} and hooks that ran, last 10s (ms before the crash: first .. last, "
                    "[last thread], what, form or object, calls; full list in the plugin log):"]
            out += summarize_crumbs(crumbs, totals)
        if last:
            out += ["", f"Last lines of {plugin_log.name} before the crash:"] + last
    return "\n".join(out)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("crash_log", type=Path)
    parser.add_argument("--plugin-log", type=Path)
    args = parser.parse_args()
    print(report(args.crash_log, args.plugin_log))


if __name__ == "__main__":
    main()
