#!/usr/bin/env python3
# link.exe shim for xmake's clang-cl toolchain on Linux: "-lib" mode -> llvm-lib
# (dropping linker-only flags MSVC's lib.exe ignores), everything else -> lld-link.
import os, sys
LLVM = os.environ.get("COOP_LLVM_BIN") or os.path.join(os.path.dirname(os.path.realpath(__file__)), "..", "LLVM", "bin")
args = sys.argv[1:]
# CMake uses link.exe as its archiver without "/lib"; a .lib output means archive mode.
archive_out = any(a.lower().startswith(("/out:", "-out:")) and a.lower().endswith(".lib") for a in args)
if archive_out and not (args and args[0].lower() in ("-lib", "/lib")):
    args = ["/lib", *args]
if args and args[0].lower() in ("-lib", "/lib"):
    linker_only = ("/opt:", "-opt:", "/debug", "-debug", "/incremental", "-incremental", "/pdb", "-pdb", "/dynamicbase", "-dynamicbase", "/fixed", "-fixed")
    args = [a for a in args[1:] if not a.lower().startswith(linker_only)]
    os.execv(LLVM + "/llvm-lib", ["llvm-lib", *args])
# MSVC's link.exe infers the subsystem in cases lld-link refuses (e.g. main() in a .lib).
lowered = [a.lower() for a in args]
if not any(a.startswith(("/subsystem", "-subsystem", "/dll", "-dll")) for a in lowered):
    args.append("/subsystem:console")
os.execv(LLVM + "/lld-link", ["lld-link", *args])
