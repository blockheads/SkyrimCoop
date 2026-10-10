#!/usr/bin/env bash
# xmake for the clang-cl (MSVC ABI) SKSE plugin build. Same arguments as xmake;
# keeps its own configuration so the MinGW/native configs are untouched.
#   scripts/xmake-msvc.sh f -m dev        # configure (first time)
#   scripts/xmake-msvc.sh build -j16 SkyrimTogetherClientDLL
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
T="$ROOT/build/toolchains"
[[ -d "$T/msvc-bin" ]] || "$ROOT/scripts/toolchain/setup_msvc_toolchain.sh"
export PATH="$T/msvc-bin:$PATH"
export XMAKE_CONFIGDIR="$ROOT/build/msvc-config"
cd "$ROOT"
if [[ "${1:-}" == "f" || "${1:-}" == "config" ]]; then
    shift
    exec xmake f -p windows -a x64 --toolchain=clang-cl --sdk="$T/winsysroot" "$@"
fi
exec xmake "$@"
