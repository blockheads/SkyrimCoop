#!/usr/bin/env bash
# xmake for the clang-cl (MSVC ABI) SKSE plugin build. Same arguments as xmake;
# keeps its own configuration so the MinGW/native configs are untouched.
#   scripts/xmake-msvc.sh f -m dev        # configure (first time)
#   scripts/xmake-msvc.sh build -j16 SkyrimTogetherClientDLL
# Configures with the test harness and hot reload (scripts/coop_test.py needs both); a player build
# turns them off: scripts/xmake-msvc.sh f -m release --test_harness=n
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
T="$ROOT/build/toolchains"
[[ -d "$T/msvc-bin" ]] || "$ROOT/scripts/toolchain/setup_msvc_toolchain.sh"
export PATH="$T/msvc-bin:$PATH"
export XMAKE_CONFIGDIR="$ROOT/build/msvc-config"
cd "$ROOT"
if [[ "${1:-}" == "f" || "${1:-}" == "config" ]]; then
    shift
    exec xmake f -P "$ROOT" -p windows -a x64 --toolchain=clang-cl --sdk="$T/winsysroot" --test_harness=y "$@"
fi
# Always this checkout's project: in a git worktree (inside the main checkout) xmake would take the parent's.
if [[ $# -gt 0 && "$1" != -* ]]; then
    set -- "$1" -P "$ROOT" "${@:2}"
fi
exec xmake "$@"
