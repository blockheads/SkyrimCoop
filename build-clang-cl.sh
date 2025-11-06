#!/bin/bash
# Build script for cross-compiling SkyrimCoop with clang-cl on Linux

set -e

echo "========================================"
echo "SkyrimCoop Cross-Compilation with clang-cl"
echo "========================================"

# Check if we're in the project directory
if [ ! -f "xmake.lua" ]; then
    echo "Error: xmake.lua not found. Are you in the project root?"
    exit 1
fi

# Configuration
BUILD_MODE="${1:-debug}"  # debug or release

echo ""
echo "Build Configuration:"
echo "  Mode: $BUILD_MODE"
echo "  Windows SDK: /opt/winsdk"
echo "  CEF/UI: Disabled (not needed for debug builds)"
echo ""

# Apply patch to disable CEF if not already applied
if [ -f "patches/disable-cef.patch" ]; then
    echo "Applying patch to disable CEF..."
    if git apply --check patches/disable-cef.patch 2>/dev/null; then
        git apply patches/disable-cef.patch
        echo "Patch applied successfully"
    else
        echo "Patch already applied or not needed"
    fi
    echo ""
fi

# Configure XMake for clang-cl cross-compilation
echo "Configuring XMake..."

# On Linux, we use regular clang with Windows target, not clang-cl
# Configure XMake for Windows cross-compilation
# Use gcc for host tools (lua, ninja), clang for Windows targets
xmake f \
    --plat=windows \
    --arch=x64 \
    --sdk=/opt/winsdk \
    --toolchain=clang \
    --toolchain_host=gcc \
    --cc=clang \
    --cxx=clang \
    --ld=lld \
    --sh=clang \
    --ar=llvm-ar \
    --cxflags="--target=x86_64-pc-windows-msvc -fms-extensions -fms-compatibility -fdelayed-template-parsing" \
    --ldflags="--target=x86_64-pc-windows-msvc -fuse-ld=lld" \
    -m $BUILD_MODE \
    -c \
    -y

echo ""
echo "Configuration complete. Starting build..."
echo ""

# Build the project
xmake -y

echo ""
echo "========================================"
echo "Build complete!"
echo "========================================"
echo ""
echo "Output files should be in: build/windows/x64/$BUILD_MODE/"
echo ""
echo "Note: You'll need to test the DLL on an actual Windows system with Skyrim."
echo ""

# Show what was built
echo "Built files:"
find build/windows -type f -name "*.dll" -o -name "*.exe" 2>/dev/null || echo "No output files found yet"
