#!/bin/bash
# Configure XMake to use MSVC via Wine (mstorsjo/msvc-wine)

set -e

echo "Configuring XMake for MSVC via Wine..."
echo ""

# Source the msvc-wine environment
echo "Step 1: Loading MSVC environment from Wine..."
source /opt/msvc-wine/msvcenv-native.sh /opt/msvc

echo ""
echo "Step 2: Configuring XMake for Windows with MSVC..."
xmake f -p windows -a x64 -m releasedbg -y

echo ""
echo "✓ Configuration complete!"
echo "  Using real MSVC compiler via Wine"
echo "  This produces true MSVC binaries compatible with Skyrim"
echo ""
echo "Now run: xmake -y"
