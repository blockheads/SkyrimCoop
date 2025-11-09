#!/bin/bash
# Build SkyrimCoop with real MSVC via Wine
# Uses mstorsjo/msvc-wine to run Microsoft compiler on Linux

set -e

echo "Building SkyrimCoop with MSVC via Wine..."
echo "This uses the real Microsoft compiler and produces true MSVC binaries"
echo ""

# Build the Docker image (downloads MSVC on first run - takes ~10-15 minutes)
echo "Step 1: Building Docker image..."
echo "  (First run downloads ~2GB of MSVC components from Microsoft)"
docker-compose -f docker-compose.msvc-wine.yml build

# Run the configuration
echo ""
echo "Step 2: Configuring build..."
docker-compose -f docker-compose.msvc-wine.yml run --rm msvc-wine \
    ./configure-msvc-wine.sh

# Build the project
echo ""
echo "Step 3: Building project..."
docker-compose -f docker-compose.msvc-wine.yml run --rm msvc-wine \
    xmake -y

# Install to distrib
echo ""
echo "Step 4: Installing to distrib/..."
docker-compose -f docker-compose.msvc-wine.yml run --rm msvc-wine \
    xmake install -o distrib

echo ""
echo "✓ Build complete!"
echo "  Output in distrib/"
echo "  Built with real MSVC - will work with Skyrim!"
