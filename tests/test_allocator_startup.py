#!/usr/bin/env python3
"""Test the MinGW allocator before main() and on foreign Windows threads."""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--rpmalloc", type=Path, required=True,
                        help="MinGW rpmalloc package directory containing include/ and lib/")
    parser.add_argument("--wine", default="/opt/wine-stable/bin/wine")
    parser.add_argument("--hopscotch", type=Path, help="hopscotch-map include directory")
    args = parser.parse_args()
    if args.hopscotch is None:
        headers = sorted((Path.home() / ".xmake/packages/h/hopscotch-map").glob("*/*/include/tsl/hopscotch_map.h"))
        if not headers:
            parser.error("Pass --hopscotch with the hopscotch-map include directory")
        args.hopscotch = headers[0].parents[1]
    with tempfile.TemporaryDirectory(prefix="skyrimcoop-allocator-test-") as temp:
        directory = Path(temp)
        binary = directory / "allocator_startup.exe"
        subprocess.run([
            "x86_64-w64-mingw32-g++-posix", "-std=c++17", "-O1", "-static",
            "-I", str(ROOT / "Code/TiltedCore"),
            "-I", str(ROOT / "Code/client"),
            "-I", str(ROOT / "Code/libraries/reverse"),
            "-I", str(ROOT / "Code/external/mem"),
            "-I", str(args.hopscotch),
            "-I", str(args.rpmalloc / "include"),
            str(ROOT / "Code/tests/hot_reload/allocator_startup.cpp"),
            str(ROOT / "Code/TiltedCore/RpmallocAllocator.cpp"),
            str(args.rpmalloc / "lib/librpmalloc.a"), "-o", str(binary),
        ], check=True)
        env = os.environ.copy()
        env.update(WINEPREFIX=str(directory / "prefix"), WINEARCH="win64",
                   WINEDEBUG="-all", WINEDLLOVERRIDES="mscoree,mshtml=;winedbg.exe=d")
        try:
            subprocess.run([args.wine, str(binary)], env=env, check=True, timeout=60)
        finally:
            server = Path(args.wine).resolve().with_name("wineserver")
            subprocess.run([str(server), "-w"], env=env, check=True, timeout=60)


if __name__ == "__main__":
    main()
