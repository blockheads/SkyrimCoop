#!/usr/bin/env python3
"""Compile real shared libraries and test the resident loader on Linux or Wine."""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--wine", help="Wine executable; tests Windows DLLs instead of Linux SOs")
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="skyrimcoop-reload-test-") as temp:
        directory = Path(temp)
        compiler = "x86_64-w64-mingw32-g++-posix" if args.wine else "g++"
        common = [compiler, "-std=c++17", "-g", "-O1", "-pthread", "-I", str(ROOT / "Code/hotreload")]
        if args.wine:
            common += ["-static-libgcc", "-static-libstdc++", "-static"]
        else:
            common += ["-fPIC", "-fno-gnu-unique"]
        suffix = ".dll" if args.wine else ".so"
        libraries = []
        for variant in range(5):
            library = directory / (f"core-{variant}" + suffix)
            source = ROOT / ("Code/hotreload/MovementCore.cpp" if variant == 0 else "Code/tests/hot_reload/fixture.cpp")
            subprocess.run(common + ["-shared", f"-DTEST_VARIANT={variant}", str(source), "-o", str(library)], check=True)
            libraries.append(library)
        binary = directory / ("loader_tests.exe" if args.wine else "loader_tests")
        subprocess.run(common + [str(ROOT / "Code/hotreload/MovementLoader.cpp"),
                                str(ROOT / "Code/tests/hot_reload/loader_tests.cpp"),
                                "-o", str(binary)] + ([] if args.wine else ["-ldl"]), check=True)
        env = os.environ.copy()
        if args.wine:
            env.update(WINEPREFIX=str(directory / "prefix"), WINEARCH="win64", WINEDEBUG="-all",
                       WINEDLLOVERRIDES="mscoree,mshtml=")
            command = [args.wine, str(binary)] + [str(path.name) for path in libraries]
        else:
            command = [str(binary)] + [str(path) for path in libraries]
        try:
            subprocess.run(command, cwd=directory, env=env, check=True, timeout=60)
        finally:
            if args.wine:
                # Let this isolated prefix flush before TemporaryDirectory removes
                # it. Never terminate the user's unrelated Wine/Proton processes.
                server = Path(args.wine).resolve().with_name("wineserver")
                subprocess.run([str(server), "-w"], env=env, check=True, timeout=60)


if __name__ == "__main__":
    main()
