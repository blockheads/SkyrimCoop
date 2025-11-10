#!/bin/bash
# Build SkyrimCoop with real MSVC via Wine
# Uses mstorsjo/msvc-wine to run Microsoft compiler on Linux

set -e

# Cleanup function to remove xmake lock files on error
cleanup_locks() {
    echo "Cleaning up xmake lock files..."
    sudo docker-compose -f docker-compose.msvc-wine.yml run --rm msvc-wine \
        sh -c "rm -f .xmake/.lock* .xmake/cache/.lock* 2>/dev/null || true"
}

# Set trap to cleanup on error
trap cleanup_locks ERR

echo "Building SkyrimCoop with MSVC via Wine..."
echo "This uses the real Microsoft compiler and produces true MSVC binaries"
echo ""

# Build the Docker image (downloads MSVC on first run - takes ~10-15 minutes)
echo "Step 1: Building Docker image..."
echo "  (First run downloads ~2GB of MSVC components from Microsoft)"
sudo docker-compose -f docker-compose.msvc-wine.yml build

# Run the configuration
echo ""
echo "Step 2: Configuring build..."
sudo docker-compose -f docker-compose.msvc-wine.yml run --rm msvc-wine \
    ./configure-msvc-wine.sh

# Build the project
echo ""
echo "Step 3: Building project..."
sudo docker-compose -f docker-compose.msvc-wine.yml run --rm msvc-wine \
    xmake -y

# Install to distrib
echo ""
echo "Step 4: Installing to distrib/..."
sudo docker-compose -f docker-compose.msvc-wine.yml run --rm msvc-wine \
    xmake install -o distrib

echo ""
echo "✓ Build complete!"
echo "  Output in distrib/"
echo "  Built with real MSVC - will work with Skyrim!"

# Clean up lock files on success too
cleanup_locks
