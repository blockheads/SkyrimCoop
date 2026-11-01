#!/usr/bin/env bash
# xmake for the clang-cl (MSVC ABI) SKSE plugin build. Same arguments as xmake;
# keeps its own configuration so the MinGW/native configs are untouched.
#   scripts/xmake-msvc.sh f -m dev        # configure (first time)
#   scripts/xmake-msvc.sh build -j16 SkyrimTogetherClientDLL
# Configures with the test harness, hot reload and Tracy (scripts/coop_test.py needs them); a player build
# turns them off: scripts/xmake-msvc.sh f -m release --test_harness=n --profiler=n
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
T="$ROOT/build/toolchains"
export XMAKE_CONFIGDIR="$ROOT/build/msvc-config"
# On Windows (Git Bash): build/toolchains/LLVM's clang-cl with the installed Visual Studio's SDK and CRT.
case "$(uname -s)" in MINGW*|MSYS*) WINDOWS=1 ;; *) WINDOWS= ;; esac
if [[ -n "$WINDOWS" ]]; then
    [[ -x "$T/LLVM/bin/clang-cl.exe" ]] || { echo "Unpack LLVM's clang+llvm-*-x86_64-pc-windows-msvc release into $T/LLVM"; exit 1; }
    export PATH="$T/LLVM/bin:$PATH"
    # Packages beside the build (COOP_XMAKE_GLOBALDIR to move them).
    export XMAKE_GLOBALDIR="${COOP_XMAKE_GLOBALDIR:-$ROOT/build/xmake-global}"
else
    [[ -d "$T/msvc-bin" ]] || "$ROOT/scripts/toolchain/setup_msvc_toolchain.sh"
    export PATH="$T/msvc-bin:$PATH"
fi
cd "$ROOT"
if [[ "${1:-}" == "f" || "${1:-}" == "config" ]]; then
    shift
    if [[ -n "$WINDOWS" ]]; then
        exec xmake f -P "$ROOT" -p windows -a x64 --toolchain=clang-cl --test_harness=y --profiler=y "$@"
    fi
    exec xmake f -P "$ROOT" -p windows -a x64 --toolchain=clang-cl --sdk="$T/winsysroot" --test_harness=y --profiler=y "$@"
fi
# Always this checkout's project: in a git worktree (inside the main checkout) xmake would take the parent's.
if [[ $# -gt 0 && "$1" != -* ]]; then
    set -- "$1" -P "$ROOT" "${@:2}"
fi
exec xmake "$@"
