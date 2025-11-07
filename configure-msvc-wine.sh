#!/bin/bash
# Configure XMake to use MSVC via Wine (mstorsjo/msvc-wine)

set -e

echo "Configuring XMake for MSVC via Wine..."
echo ""

# Source the MSVC environment setup
echo "Step 1: Setting up MSVC Wine environment..."
source /opt/msvc/bin/x64/msvcenv.sh

echo "  MSVC environment loaded"
echo "  BINDIR=$BINDIR"
echo "  PATH updated to include MSVC tools"

echo ""
echo "Step 2: Configuring XMake for Windows with MSVC..."
# Use cross compilation mode with explicit toolchain
# This bypasses XMake's Visual Studio detection
xmake f -p cross --sdk=/opt/msvc -a x64 -m releasedbg -y \
  --toolchain=msvc \
  --cc=/opt/msvc/bin/x64/cl \
  --cxx=/opt/msvc/bin/x64/cl \
  --ld=/opt/msvc/bin/x64/link \
  --sh=/opt/msvc/bin/x64/link \
  --ar=/opt/msvc/bin/x64/lib

echo ""
echo "✓ Configuration complete!"
echo "  Using real MSVC compiler via Wine"
echo "  This produces true MSVC binaries compatible with Skyrim"
echo ""
echo "Build with: xmake -y"
echo "  (Make sure to run in same shell or source msvcenv.sh first)"
