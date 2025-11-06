#!/bin/bash
# Build script for cross-compiling SkyrimCoop with MinGW-w64 on Linux

set -e

echo "========================================"
echo "SkyrimCoop MinGW Cross-Compilation"
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
echo "  Toolchain: MinGW-w64"
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

# Configure XMake for MinGW cross-compilation
echo "Configuring XMake for MinGW..."
xmake f \
    -p mingw \
    --arch=x86_64 \
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
echo "Output files should be in: build/mingw/x86_64/$BUILD_MODE/"
echo ""
echo "Note: Test on Windows. If ABI issues occur, we'll add:"
echo "  - extern \"C\" wrappers for SKSE exports"
echo "  - -fseh-exceptions for exception compatibility"
echo "  - ABI compatibility shims as needed"
echo ""

# Show what was built
echo "Built files:"
find build/mingw -type f -name "*.dll" -o -name "*.exe" 2>/dev/null || echo "No output files found yet"
