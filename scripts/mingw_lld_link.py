#!/usr/bin/env python3
"""g++-compatible link driver that links MinGW binaries with lld instead of GNU ld.

Debian's MinGW GCC hardcodes GNU ld, so -fuse-ld=lld and -B are silently ignored.
This asks GCC for its exact linker invocation (-###: same CRT objects, libraries
and search paths) and runs lld with it. Linking the client DLL drops from ~44s
to under 1s. Used by the dev mode toolset in xmake.lua.

Overrides: COOP_MINGW_GXX (GCC driver), COOP_LLD (lld binary).
"""
import os
import shlex
import shutil
import subprocess
import sys

gxx = os.environ.get("COOP_MINGW_GXX", "x86_64-w64-mingw32-g++-posix")
lld = os.environ.get("COOP_LLD") or shutil.which("ld.lld-18") or shutil.which("ld.lld")
if not lld:
    sys.exit("mingw_lld_link: ld.lld not found (install lld or set COOP_LLD)")

plan = subprocess.run([gxx, *sys.argv[1:], "-###"], capture_output=True, text=True)
if plan.returncode != 0:
    sys.stderr.write(plan.stderr)
    sys.exit(plan.returncode)
collect = [line for line in plan.stderr.splitlines() if "collect2" in line]
if not collect:
    # Not a link (e.g. a version query): run GCC unchanged.
    os.execvp(gxx, [gxx, *sys.argv[1:]])

arguments = []
tokens = iter(shlex.split(collect[-1])[1:])
for token in tokens:
    # GCC's LTO plugin options are GNU ld specific; LTO is not used here.
    if token == "-plugin":
        next(tokens, None)
    elif not token.startswith("-plugin-opt"):
        arguments.append(token)
os.execv(lld, [lld, *arguments])
