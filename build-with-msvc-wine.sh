#!/bin/bash
# Build SkyrimCoop with MSVC via Wine
# Uses Linux XMake for package management, MSVC via Wine for compilation

set -e

echo "=== Building SkyrimCoop with MSVC via Wine ==="
echo ""

# Step 1: Pre-fetch cross-platform packages (Windows-only packages will be installed during build)
if [ ! -d "/root/.xmake/packages" ] || [ -z "$(ls -A /root/.xmake/packages 2>/dev/null)" ]; then
    echo "Step 1: Pre-fetching cross-platform XMake packages..."
    echo "(Windows-only packages like minhook, directxtk, cef will be fetched during build)"
    echo ""

    # Install only cross-platform packages
    # Windows-only packages (minhook, directxtk, cef, discord, imgui) will be installed by XMake during build
    xrepo install -y \
        "entt v3.10.0" \
        "recastnavigation v1.6.0" \
        "cryptopp 8.9.0" \
        "spdlog v1.13.0" \
        "cpp-httplib 0.14.0" \
        "gtest v1.14.0" \
        "mem 1.0.0" \
        "glm 0.9.9+8" \
        "sentry-native 0.7.1" \
        "zlib v1.3.1" \
        "mimalloc" \
        "hopscotch-map v2.3.1" \
        "snappy 1.1.10" \
        "gamenetworkingsockets v1.4.1" \
        "libuv v1.48.0" \
        "xbyak v7.06" \
        "catch2 2.13.9" || true  # Don't fail if some packages can't install

    echo ""
    echo "✓ Cross-platform packages fetched"
    echo ""
else
    echo "✓ Packages already installed (skipping)"
    echo ""
fi

# Step 2: Source the MSVC environment
echo "Step 2: Loading MSVC Wine environment..."
source /opt/msvc/bin/x64/msvcenv.sh

# Set Wine temp directories (MSVC needs writable temp)
export TEMP=/tmp/wine-temp
export TMP=/tmp/wine-temp
mkdir -p /tmp/wine-temp

echo "✓ MSVC environment loaded (BINDIR=$BINDIR)"
echo "✓ Temp directories set"
echo ""

# Step 3: Configure XMake with MSVC toolchain
echo "Step 3: Configuring XMake with MSVC toolchain..."
# Don't use -c (clean) here to preserve installed packages
xmake f -y \
  -p windows \
  -a x64 \
  -m releasedbg \
  --sdk=/opt/msvc \
  --toolchain=msvc \
  --cc=/opt/msvc/bin/x64/cl \
  --cxx=/opt/msvc/bin/x64/cl \
  --ld=/opt/msvc/bin/x64/link \
  --sh=/opt/msvc/bin/x64/link \
  --ar=/opt/msvc/bin/x64/lib

echo ""
echo "✓ Configuration complete"
echo ""

# Step 4: Build with MSVC via Wine
echo "Step 4: Building project with MSVC via Wine..."
echo "(This will take a while - Wine overhead + compilation)"
echo ""

xmake build -y

echo ""
echo "✓ Build complete!"
echo "Output should be in build/ directory"
