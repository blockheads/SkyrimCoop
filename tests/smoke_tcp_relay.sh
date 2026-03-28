#!/bin/bash
set -e
echo "=== Smoke Test: TCP Relay Build & Protocol ==="

# Resolve xmake project root (build artifacts go to projectdir/build, which may
# differ from cwd when running from a git worktree)
PROJECT_ROOT=$(xmake show 2>&1 | sed 's/\x1b\[[0-9;]*m//g' | awk '/projectdir/{print $2}')
if [ -z "$PROJECT_ROOT" ]; then
    PROJECT_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
fi
BUILD_DIR="$PROJECT_ROOT/build"
echo "Project root: $PROJECT_ROOT"
echo "Build dir: $BUILD_DIR"

echo "Building relay DLL (MinGW)..."
xmake config -p mingw -m releasedbg -y
xmake build SkyrimCoopHooksDLL

DLL_PATH=$(find "$BUILD_DIR" -name "skyrim_coop_hooks.dll" -type f 2>/dev/null | head -1)
if [ -z "$DLL_PATH" ]; then
    echo "FAIL: skyrim_coop_hooks.dll not found"
    exit 1
fi
echo "DLL found: $DLL_PATH"

if x86_64-w64-mingw32-objdump -x "$DLL_PATH" | grep -q "SKSEPlugin_Version"; then
    echo "PASS: SKSEPlugin_Version exported"
else
    echo "FAIL: SKSEPlugin_Version not exported"
    exit 1
fi

if x86_64-w64-mingw32-objdump -x "$DLL_PATH" | grep -q "SKSEPlugin_Load"; then
    echo "PASS: SKSEPlugin_Load exported"
else
    echo "FAIL: SKSEPlugin_Load not exported"
    exit 1
fi

echo "Cleaning build artifacts before platform switch..."
xmake clean -a

echo "Building native client (Linux)..."
xmake config -p linux -m release -y
xmake build SkyrimCoopNative

NATIVE_PATH=$(find "$BUILD_DIR" -name "skyrim-coop" -type f -executable 2>/dev/null | head -1)
if [ -z "$NATIVE_PATH" ]; then
    echo "FAIL: skyrim-coop binary not found"
    exit 1
fi
echo "Native binary found: $NATIVE_PATH"

echo "Running all relay unit tests..."
xmake build TPTests
# Catch2 uses comma-separated tags for OR matching
xmake run TPTests "[RelayProtocol],[CommandQueue],[PointerTable],[ProcMem]"
echo "PASS: All unit tests pass"

echo "Running clang-format check on relay_dll sources..."
UNFORMATTED=$(find Code/relay_dll Code/native_client -name "*.cpp" -o -name "*.h" | xargs clang-format --dry-run --Werror 2>&1 || true)
if [ -n "$UNFORMATTED" ]; then
    echo "WARNING: Some files need clang-format:"
    echo "$UNFORMATTED" | head -20
else
    echo "PASS: All source files pass clang-format"
fi

echo "=== TCP Relay smoke test PASSED ==="
