#!/usr/bin/env python3
"""Turn the crash handler's "backtrace #NN module+0xOFF" lines into names.

  scripts/re/symbolize_crash.py [tp_client.log] [--dll path/to/SkyrimTogetherClient.dll]

SkyrimSE.exe frames -> nearest Address Library ID + best known name (build/re/annotations.json,
falling back to the raw ID list); our DLL frames -> function and source line (addr2line).
Defaults to the newest crash in the deployed plugin's log.
"""
import argparse
import bisect
import json
from pathlib import Path
import re
import subprocess

import build_re_data as re_data

ROOT = re_data.ROOT
DEFAULT_LOG = re_data.GAME / "Data/SKSE/Plugins/logs/tp_client.log"
DEFAULT_DLL = ROOT / "build/windows/x64/dev/SkyrimTogetherClient.dll"
FRAME = re.compile(r"backtrace #(\d+) (\S+)\+0x([0-9a-f]+)")


def engine_resolver():
    ids = re_data.load_address_library(re_data.VERSIONLIB)
    names = {}
    if re_data.OUT_JSON.exists():
        names = {a["id"]: a["name"] for a in json.loads(re_data.OUT_JSON.read_text())["annotations"]}
    table = sorted((rva, ident) for ident, rva in ids.items())
    starts = [rva for rva, _ in table]

    def resolve(rva):
        index = bisect.bisect_right(starts, rva) - 1
        start, ident = table[index]
        return f"ID {ident} {names.get(ident, '(unnamed)')} +{rva - start:#x}"
    return resolve


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("log", nargs="?", type=Path, default=DEFAULT_LOG)
    parser.add_argument("--dll", type=Path, default=DEFAULT_DLL)
    args = parser.parse_args()

    lines = args.log.read_text(errors="replace").splitlines()
    starts = [i for i, line in enumerate(lines) if "crash occurred" in line]
    if not starts:
        raise SystemExit(f"No crash in {args.log}")
    crash = lines[starts[-1]:]
    print("\n".join(crash[:2]))
    resolve_engine = engine_resolver()
    image_base = 0x180000000
    for line in crash:
        match = FRAME.search(line)
        if not match:
            continue
        number, module, offset = match.group(1), match.group(2), int(match.group(3), 16)
        if module.lower() == "skyrimse.exe":
            where = resolve_engine(offset)
        elif module.lower() == args.dll.name.lower():
            result = subprocess.run(["x86_64-w64-mingw32-addr2line", "-f", "-C", "-e", str(args.dll),
                                     hex(image_base + offset)], capture_output=True, text=True).stdout.split("\n")
            source = result[1].split("/Code/")[-1] if len(result) > 1 else ""
            where = f"{result[0]}  (Code/{source})" if result and result[0] != "??" else "?"
        else:
            where = ""
        print(f"#{number} {module}+{offset:#x}  {where}")


if __name__ == "__main__":
    main()
