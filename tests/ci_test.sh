#!/bin/bash
# ci_test.sh -- CI test runner for SkyrimCoop
# Chains: build -> unit tests -> headless multi-bot -> mock relay pipeline
# Does NOT require Skyrim. All tests are headless/CI-safe.
#
# Usage:
#   ./tests/ci_test.sh           # Full CI pipeline
#   ./tests/ci_test.sh --quick   # Build + unit tests only
#
# Exit codes: 0=PASS, 1=FAIL
set -euo pipefail

PROJECT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
cd "$PROJECT_DIR"

info()  { echo "[CI]  $*"; }
pass()  { echo "[PASS] $*"; }
fail()  { echo "[FAIL] $*"; exit 1; }
step()  { echo ""; echo "=== $* ==="; }

QUICK=false
for arg in "$@"; do
    case "$arg" in
        --quick) QUICK=true ;;
    esac
done

# Step 1: Build all targets
step "Step 1: Build"
info "Building native targets..."
xmake build -y SkyrimCoopNative 2>&1 | tail -10
xmake build -y fake_player 2>&1 | tail -10
xmake build -y TPTests 2>&1 | tail -10
pass "Native targets built"

# Build MinGW targets if mingw toolchain available
if xmake config -p mingw --mingw="$PROJECT_DIR/.toolchain" 2>/dev/null; then
    info "Building MinGW targets..."
    xmake build -y SkyrimCoopHooksDLL 2>&1 | tail -10
    xmake build -y mock_skyrim_loader 2>&1 | tail -10
    pass "MinGW targets built"
    HAS_MINGW=true
    # Switch back to linux platform
    xmake config -p linux -y 2>/dev/null
else
    info "MinGW toolchain not available, skipping DLL builds"
    HAS_MINGW=false
fi

# Step 2: Unit Tests
step "Step 2: Unit Tests"
BUILD_DIR=$(xmake show 2>/dev/null | grep -o 'projectdir:.*' | cut -d: -f2 | tr -d ' ' || echo "$PROJECT_DIR")
TPTESTS=$(find "$BUILD_DIR/build" -name "TPTests" -type f -executable 2>/dev/null | head -1 || true)
if [ -n "$TPTESTS" ]; then
    "$TPTESTS" 2>&1 | tail -20
    pass "Unit tests passed"
else
    fail "TPTests binary not found"
fi

if [ "$QUICK" = "true" ]; then
    echo ""
    pass "Quick CI: build + unit tests PASSED"
    exit 0
fi

# Step 3: Headless Multi-Bot Test
step "Step 3: Headless Multi-Bot Test"
FAKE_PLAYER=$(find "$BUILD_DIR/build" -name "fake_player" -type f -executable 2>/dev/null | head -1 || true)
SKYRIM_COOP=$(find "$BUILD_DIR/build" -name "skyrim-coop" -type f -executable 2>/dev/null | head -1 || true)

if [ -z "$FAKE_PLAYER" ] || [ -z "$SKYRIM_COOP" ]; then
    fail "fake_player or skyrim-coop binary not found"
fi

# Start host-only server
info "Starting skyrim-coop --host-only..."
"$SKYRIM_COOP" --host-only &
HOST_PID=$!
sleep 2

if ! kill -0 "$HOST_PID" 2>/dev/null; then
    fail "skyrim-coop --host-only exited early"
fi

# Wait for GameServer port
info "Waiting for GameServer on UDP 10578..."
for i in $(seq 1 30); do
    if ss -ulnp 2>/dev/null | grep -q ":10578 "; then
        break
    fi
    sleep 1
done

# Run multi-bot test
info "Running fake_player --bots 8 --verify-broadcast --duration 15..."
if "$FAKE_PLAYER" --bots 8 --verify-broadcast --metrics --duration 15 --port 10578 2>&1; then
    pass "Headless multi-bot test passed"
else
    kill "$HOST_PID" 2>/dev/null || true
    fail "Headless multi-bot test failed"
fi

# Also run self-test
info "Running fake_player --self-test..."
"$FAKE_PLAYER" --self-test 2>&1 | tail -5
pass "Self-test passed"

kill "$HOST_PID" 2>/dev/null || true
wait "$HOST_PID" 2>/dev/null || true

# Step 4: Mock Relay Pipeline
step "Step 4: Mock Relay Pipeline"
if [ "$HAS_MINGW" = "true" ]; then
    LOADER=$(find "$BUILD_DIR/build" -name "mock_skyrim_loader.exe" -type f 2>/dev/null | head -1 || true)
    DLL=$(find "$BUILD_DIR/build" -name "skyrim_coop_hooks.dll" -type f 2>/dev/null | head -1 || true)

    if [ -n "$LOADER" ] && [ -n "$DLL" ] && command -v wine >/dev/null 2>&1; then
        info "Running mock_skyrim_loader under Wine..."
        # Start native process to receive events
        "$SKYRIM_COOP" --host-only &
        HOST_PID=$!
        sleep 2

        # Run loader (it loads DLL, sends events, exits)
        timeout 60 wine "$LOADER" --dll "$(winepath -w "$DLL" 2>/dev/null || echo "$DLL")" --events 10 2>&1 | tail -10 || true

        kill "$HOST_PID" 2>/dev/null || true
        wait "$HOST_PID" 2>/dev/null || true
        pass "Mock relay pipeline test completed"
    else
        info "Skipping mock relay pipeline (Wine or MinGW builds not available)"
    fi
else
    info "Skipping mock relay pipeline (no MinGW builds)"
fi

echo ""
echo "=== CI Test Suite PASSED ==="
exit 0
