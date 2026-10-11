#!/usr/bin/env bash
# Install the clang-cl (MSVC ABI) cross toolchain into build/toolchains:
#   LLVM (clang-cl, lld-link, llvm-lib/rc/ml), Windows SDK + MSVC CRT via xwin
#   (accepts Microsoft's SDK license), and msvc-bin/ with the tool names xmake expects.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
T="$ROOT/build/toolchains"
LLVM_VERSION="${LLVM_VERSION:-23.1.3}"
XWIN_VERSION="${XWIN_VERSION:-0.10.0}"
mkdir -p "$T"

if [[ ! -x "$T/LLVM/bin/clang-cl" ]]; then
    echo "Downloading LLVM $LLVM_VERSION..."
    curl -sfL "https://github.com/llvm/llvm-project/releases/download/llvmorg-$LLVM_VERSION/LLVM-$LLVM_VERSION-Linux-X64.tar.xz" | tar xJ -C "$T"
    mv "$T/LLVM-$LLVM_VERSION-Linux-X64" "$T/LLVM"
fi
if [[ ! -x "$T/xwin" ]]; then
    curl -sfL "https://github.com/Jake-Shadle/xwin/releases/download/$XWIN_VERSION/xwin-$XWIN_VERSION-x86_64-unknown-linux-musl.tar.gz" \
        | tar xz -C "$T" --strip-components=1 "xwin-$XWIN_VERSION-x86_64-unknown-linux-musl/xwin"
fi
if [[ ! -d "$T/winsysroot/VC" ]]; then
    echo "Downloading Windows SDK + MSVC CRT (xwin, accepting Microsoft's license)..."
    "$T/xwin" --accept-license --cache-dir "$T/xwin-cache" --arch x86_64 --include-debug-runtime \
        splat --include-debug-libs --use-winsysroot-style --output "$T/winsysroot"
fi
# Visual Studio layout uses x64, xwin writes x86_64.
for d in "$T"/winsysroot/VC/Tools/MSVC/*/lib "$T"/winsysroot/Windows\ Kits/10/Lib/*/um "$T"/winsysroot/Windows\ Kits/10/Lib/*/ucrt; do
    ln -sfn x86_64 "$d/x64"
done

B="$T/msvc-bin"
rm -rf "$B" && mkdir -p "$B"
for tool in lld-link llvm-lib llvm-rc llvm-ml llvm-dlltool llvm-ar llvm-mt; do
    ln -s "../LLVM/bin/$tool" "$B/$tool"
done
# Fixes up the dependency files xmake reads (see the script).
cp "$ROOT/scripts/toolchain/clang_cl_wrapper.sh" "$B/clang-cl"
chmod +x "$B/clang-cl"
ln -s clang-cl "$B/clang-cl.exe"
ln -s ../LLVM/bin/lld-link "$B/lld-link.exe"
ln -s ../LLVM/bin/llvm-dlltool "$B/llvm-dlltool.exe"
ln -s ../LLVM/bin/llvm-lib "$B/lib.exe"
ln -s ../LLVM/bin/llvm-rc "$B/rc.exe"
ln -s ../LLVM/bin/llvm-ml "$B/ml64.exe"
ln -s ../LLVM/bin/llvm-mt "$B/mt.exe"
# xmake's CMake package builder looks for the msvc toolchain's cl.exe.
printf '#!/bin/sh\nexec "$(dirname "$(readlink -f "$0")")/../LLVM/bin/clang-cl" "$@"\n' > "$B/cl.exe"
cp "$ROOT/scripts/toolchain/link_shim.py" "$B/link.exe"
chmod +x "$B/cl.exe" "$B/link.exe"
# CMake also probes the bare names; without these it finds coreutils' /usr/bin/link.
for tool in cl link lib rc mt ml64; do ln -s "$tool.exe" "$B/$tool"; done
# CMake's Ninja generator wraps resource compiles in cmcldeps, which Linux lacks (see the script).
cp "$ROOT/scripts/toolchain/cmcldeps_rc_shim.sh" "$B/RC"
chmod +x "$B/RC"
echo "Toolchain ready: $("$T/LLVM/bin/clang-cl" --version | head -1)"
