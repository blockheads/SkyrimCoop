#!/usr/bin/env bash
# xmake for the native Linux build (BridgeTests, TPTests): GCC, linked with lld from LLVM 18. Same
# arguments as xmake; keeps its own configuration so the Windows and MinGW configs are untouched.
#   scripts/xmake-linux.sh f                     # configure (first time)
#   scripts/xmake-linux.sh build -j16 BridgeTests && build/linux/x86_64/debug/BridgeTests
# Not clang yet: sol2 3.3.0 (the server's Lua) does not build with clang 18.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
export PATH="${LLVM_DIR:-/usr/lib/llvm-18}/bin:$PATH"
export XMAKE_CONFIGDIR="$ROOT/build/linux-config"
cd "$ROOT"
if [[ "${1:-}" == "f" || "${1:-}" == "config" ]]; then
    shift
    exec xmake f -p linux -a x86_64 -m debug --toolchain=gcc --ldflags=-fuse-ld=lld --shflags=-fuse-ld=lld -y "$@"
fi
exec xmake "$@"
