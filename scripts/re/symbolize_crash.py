#!/usr/bin/env python3
"""Turn the crash handler's "backtrace #NN module+0xOFF" lines into names.

  scripts/re/symbolize_crash.py [tp_client.log] [--dll path/to/SkyrimTogetherClient.dll]

SkyrimSE.exe frames -> nearest Address Library ID + best known name (build/re/annotations.json,
falling back to the raw ID list); our DLL frames -> function and source line (addr2line).
Defaults to the newest crash in the deployed plugin's log. A log without a crash (say a
LockOrder report or a HangWatchdog dump cut out of one) is symbolized whole, headers included.
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


LLVM_SYMBOLIZER = ROOT / "build/toolchains/LLVM/bin/llvm-symbolizer"


def symbolize_ours(dll, offset):
    """Function and source line of an RVA in our DLL (clang-cl writes a PDB, so llvm-symbolizer)."""
    tool = str(LLVM_SYMBOLIZER) if LLVM_SYMBOLIZER.exists() else "llvm-symbolizer"
    result = subprocess.run([tool, f"--obj={dll}", "--relative-address", hex(offset)],
                            capture_output=True, text=True).stdout.split("\n")
    if not result or result[0] in ("", "??"):
        return "?"
    source = result[1].split("/Code/")[-1] if len(result) > 1 else ""
    return f"{result[0]}  (Code/{source})"


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("log", nargs="?", type=Path, default=DEFAULT_LOG)
    parser.add_argument("--dll", type=Path, default=DEFAULT_DLL)
    args = parser.parse_args()

    lines = args.log.read_text(errors="replace").splitlines()
    starts = [i for i, line in enumerate(lines) if "crash occurred" in line]
    whole = not starts
    crash = lines if whole else lines[starts[-1]:]
    if not whole:
        print("\n".join(crash[:2]))
    resolve_engine = engine_resolver()
    for line in crash:
        match = FRAME.search(line)
        if not match:
            if whole and line.strip():
                print(line)
            continue
        number, module, offset = match.group(1), match.group(2), int(match.group(3), 16)
        if module.lower() == "skyrimse.exe":
            where = resolve_engine(offset)
        # The hot reload loader runs copies: SkyrimTogetherClient[-instance].<pid>.<n>.dll.
        elif module.lower() == args.dll.name.lower() or re.fullmatch(
                re.escape(args.dll.stem) + r"(-[\w-]+)?\.\d+\.\d+\.dll", module, re.IGNORECASE):
            where = symbolize_ours(args.dll, offset)
        else:
            where = ""
        print(f"#{number} {module}+{offset:#x}  {where}")


if __name__ == "__main__":
    main()
