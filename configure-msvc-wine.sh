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
# Use windows platform so xmake.lua conditionals work correctly
# This ensures Windows-specific packages (CEF, Discord SDK, etc.) are included
# Pass Windows 10 defines via cxflags to fix cpp-httplib "Windows 8 or lower" error
xmake f -p windows --sdk=/opt/msvc -a x64 -m releasedbg -y \
  --toolchain=msvc \
  --cc=/opt/msvc/bin/x64/cl \
  --cxx=/opt/msvc/bin/x64/cl \
  --ld=/opt/msvc/bin/x64/link \
  --sh=/opt/msvc/bin/x64/link \
  --ar=/opt/msvc/bin/x64/lib \
  --mrc=/opt/msvc/bin/x64/rc \
  --cxflags='/D_WIN32_WINNT=0x0A00' \
  --cxflags='/DWINVER=0x0A00'

echo ""
echo "✓ Configuration complete!"
echo "  Using real MSVC compiler via Wine"
echo "  This produces true MSVC binaries compatible with Skyrim"
echo ""
echo "Build with: xmake -y"
echo "  (Make sure to run in same shell or source msvcenv.sh first)"
