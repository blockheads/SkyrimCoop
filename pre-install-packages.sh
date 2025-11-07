#!/bin/bash
# Pre-install XMake packages using Linux XMake
# Then we'll use these packages with MSVC via Wine for the actual build

set -e

echo "=== Pre-installing XMake packages with Linux XMake ==="
echo ""

# Temporarily use Linux XMake (not Wine version)
# Install it if not already present
if ! command -v /root/.local/bin/xmake-linux &> /dev/null; then
    echo "Installing Linux XMake for package installation..."
    curl -fsSL https://xmake.io/shget.text | bash
    mv /root/.local/bin/xmake /root/.local/bin/xmake-linux
fi

# Use Linux XMake to install all packages for Windows platform
echo "Installing packages for Windows x64 platform..."
echo "(Using Linux XMake with cross-compilation mode)"
echo ""

# Configure for Windows cross-compilation to download Windows packages
/root/.local/bin/xmake-linux f -c -y \
  -p windows \
  -a x64 \
  -m releasedbg

# Install all required packages
echo ""
echo "Fetching and building packages..."
/root/.local/bin/xmake-linux require -y

echo ""
echo "✓ All packages pre-installed!"
echo "✓ Package cache located at: ~/.xmake/packages/"
echo ""
echo "Now you can run build-with-msvc-wine.sh which will use these packages"
