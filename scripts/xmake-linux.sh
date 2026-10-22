#!/usr/bin/env bash
# xmake for the native Linux build (BridgeTests, TPTests, coop_peer): clang and lld from LLVM 18. Same
# arguments as xmake; keeps its own configuration so the Windows config is untouched.
#   scripts/xmake-linux.sh f                     # configure (first time)
#   scripts/xmake-linux.sh build -j8 BridgeTests && build/linux/x86_64/debug/BridgeTests
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
export PATH="${LLVM_DIR:-/usr/lib/llvm-18}/bin:$PATH"
export XMAKE_CONFIGDIR="$ROOT/build/linux-config"
cd "$ROOT"
if [[ "${1:-}" == "f" || "${1:-}" == "config" ]]; then
    shift
    # clang takes the newest GCC for libstdc++, headers or not: the newest one with its headers.
    GCC_VERSION="$(ls /usr/include/c++ | sort -V | tail -1)"
    GCC="--gcc-install-dir=/usr/lib/gcc/x86_64-linux-gnu/$GCC_VERSION"
    exec xmake f -p linux -a x86_64 -m debug --toolchain=clang --cxflags="$GCC" \
        --ldflags="-fuse-ld=lld $GCC" --shflags="-fuse-ld=lld $GCC" -y "$@"
fi
exec xmake "$@"
