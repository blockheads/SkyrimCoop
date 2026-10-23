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
    # clang takes the newest GCC for libstdc++, headers or not: the newest one with its headers. The flags go in
    # clang config files, one argument each: xmake hands a flags string with a space to CMake packages as one.
    GCC_VERSION="$(ls /usr/include/c++ | sort -V | tail -1)"
    GCC="--gcc-install-dir=/usr/lib/gcc/x86_64-linux-gnu/$GCC_VERSION"
    mkdir -p "$XMAKE_CONFIGDIR/clang"
    printf '%s\n' "$GCC" > "$XMAKE_CONFIGDIR/clang/compile.cfg"
    printf '%s\n' "$GCC" -fuse-ld=lld > "$XMAKE_CONFIGDIR/clang/link.cfg"
    exec xmake f -p linux -a x86_64 -m debug --toolchain=clang --cxflags="--config=$XMAKE_CONFIGDIR/clang/compile.cfg" \
        --ldflags="--config=$XMAKE_CONFIGDIR/clang/link.cfg" --shflags="--config=$XMAKE_CONFIGDIR/clang/link.cfg" -y "$@" < /dev/null
fi
exec xmake "$@"
