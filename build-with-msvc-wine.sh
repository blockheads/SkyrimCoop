#!/bin/bash
# Build SkyrimCoop with MSVC via Wine
# Uses Linux XMake for package management, MSVC via Wine for compilation

set -e

echo "=== Building SkyrimCoop with MSVC via Wine ==="
echo ""

# Step 1: Source the MSVC environment
echo "Step 1: Loading MSVC Wine environment..."
source /opt/msvc/bin/x64/msvcenv.sh

# Set Wine temp directories (MSVC needs writable temp)
export TEMP=/tmp/wine-temp
export TMP=/tmp/wine-temp
mkdir -p /tmp/wine-temp

# Create Wine C: drive structure to match expected paths
# The build expects c:/projects/SkyrimCoop to exist
# Wine maps C: to ~/.wine/drive_c by default
mkdir -p "$HOME/.wine/drive_c/projects"
ln -sf /workspace "$HOME/.wine/drive_c/projects/SkyrimCoop" 2>/dev/null || true

echo "✓ MSVC environment loaded (BINDIR=$BINDIR)"
echo "✓ Temp directories set"
echo "✓ Wine C: drive: c:/projects/SkyrimCoop -> /workspace"
echo ""

# Step 2: Configure XMake with MSVC toolchain
echo "Step 2: Configuring XMake with MSVC toolchain..."
echo "(This may take a while - installing Windows packages with MSVC...)"
# Don't use -c (clean) here to preserve installed packages
xmake f -y -v \
  -p windows \
  -a x64 \
  -m releasedbg \
  --sdk=/opt/msvc \
  --toolchain=msvc \
  --cc=/opt/msvc/bin/x64/cl \
  --cxx=/opt/msvc/bin/x64/cl \
  --ld=/opt/msvc/bin/x64/link \
  --sh=/opt/msvc/bin/x64/link \
  --ar=/opt/msvc/bin/x64/lib \
  --cxflags='/D_WIN32_WINNT=0x0A00' \
  --cxflags='/DWINVER=0x0A00'

echo ""
echo "✓ Configuration complete"
echo ""

# Step 3: Build with MSVC via Wine
echo "Step 3: Building project with MSVC via Wine..."
echo "(This will take a while - Wine overhead + compilation)"
echo ""

xmake build -y

echo ""
echo "✓ Build complete!"
echo "Output should be in build/ directory"
