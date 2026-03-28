#!/bin/bash
# test_ingame_sync.sh -- Integration test: fake_player connects to embedded GameServer
# inside a real Skyrim instance and verifies remote actor position via /proc/pid/mem.
#
# Usage:
#   ./tests/test_ingame_sync.sh              # Full in-game test
#   ./tests/test_ingame_sync.sh --check-only # Just verify prerequisites
#
# Exit codes:
#   0 = PASS (full test or prerequisites check)
#   1 = FAIL (test failure)
#   2 = Prerequisites not met (Skyrim not installed, binary not built, etc.)
set -euo pipefail

# ── Configuration ──
SKYRIM_DIR="/media/bighass/1f640250-798d-4f2f-87f2-c442109fc5d0/SteamLibrary/steamapps/common/Skyrim Special Edition"
PROTON_DIR="/media/bighass/1f640250-798d-4f2f-87f2-c442109fc5d0/SteamLibrary/steamapps/common/Proton - Experimental"
COMPAT_DATA="/media/bighass/1f640250-798d-4f2f-87f2-c442109fc5d0/SteamLibrary/steamapps/compatdata/489830"
STEAM_DIR="/home/bighass/.steam/steam"
PLUGINS="$SKYRIM_DIR/Data/SKSE/Plugins"
RELAY_INFO="$PLUGINS/skyrim_coop_relay.json"
DLL_LOG="$PLUGINS/skyrim_coop_hooks.log"

# Determine project root (script lives in tests/ subdir)
PROJECT_DIR="$(cd "$(dirname "$0")/.." && pwd)"

# Build artifact location (use xmake show to handle worktrees)
FAKE_PLAYER=""
BUILD_DIR=""

# Steam environment
export SteamAppId=489830
export SteamGameId=489830
export STEAM_COMPAT_DATA_PATH="$COMPAT_DATA"
export STEAM_COMPAT_CLIENT_INSTALL_PATH="$STEAM_DIR"
export WINEPREFIX="$COMPAT_DATA/pfx"

# Test parameters
DURATION=15
SERVER_PORT=10578
TIMEOUT_SKYRIM=60
TIMEOUT_SERVER=30

# ── Helpers ──
info()  { echo "[INFO]  $*"; }
pass()  { echo "[PASS]  $*"; }
fail()  { echo "[FAIL]  $*"; }
warn()  { echo "[WARN]  $*"; }

cleanup() {
    info "Cleaning up..."
    # Kill fake_player if running
    if [ -n "${FAKE_PLAYER_PID:-}" ] && kill -0 "$FAKE_PLAYER_PID" 2>/dev/null; then
        kill "$FAKE_PLAYER_PID" 2>/dev/null || true
        wait "$FAKE_PLAYER_PID" 2>/dev/null || true
    fi
    # Kill Skyrim if we launched it
    if [ "${LAUNCHED_SKYRIM:-false}" = "true" ]; then
        pkill -f "SkyrimSE.exe" 2>/dev/null || true
        pkill -f "skse64_loader" 2>/dev/null || true
        pkill -f "skyrim-coop" 2>/dev/null || true
        rm -f "$RELAY_INFO"
    fi
    info "Cleanup done"
}

# ── Find build artifacts ──
find_build_artifacts() {
    # Try xmake show for worktree-safe path, fall back to find
    if command -v xmake >/dev/null 2>&1; then
        BUILD_DIR=$(cd "$PROJECT_DIR" && xmake show 2>/dev/null | grep -o 'projectdir:.*' | cut -d: -f2 | tr -d ' ' || echo "")
        if [ -n "$BUILD_DIR" ]; then
            BUILD_DIR="$BUILD_DIR/build"
        fi
    fi

    if [ -z "$BUILD_DIR" ]; then
        BUILD_DIR="$PROJECT_DIR/build"
    fi

    # Find fake_player binary
    FAKE_PLAYER=$(find "$BUILD_DIR" -name "fake_player" -type f -executable 2>/dev/null | head -1 || true)
}

# ── Step 1: Prerequisites Check ──
check_prerequisites() {
    local all_ok=true

    info "Checking prerequisites..."

    # 1a. Check fake_player binary
    find_build_artifacts
    if [ -n "$FAKE_PLAYER" ] && [ -x "$FAKE_PLAYER" ]; then
        pass "fake_player binary found: $FAKE_PLAYER"
    else
        fail "fake_player binary not found (run: xmake build fake_player)"
        all_ok=false
    fi

    # 1b. Check ptrace_scope
    local scope
    scope=$(cat /proc/sys/kernel/yama/ptrace_scope 2>/dev/null || echo "N/A")
    if [ "$scope" = "0" ] || [ "$scope" = "1" ]; then
        pass "ptrace_scope=$scope (OK for /proc/pid/mem)"
    else
        warn "ptrace_scope=$scope (may need CAP_SYS_PTRACE or scope=0)"
    fi

    # 1c. Check Skyrim installation
    if [ -d "$SKYRIM_DIR" ] && [ -f "$SKYRIM_DIR/SkyrimSE.exe" ]; then
        pass "Skyrim SE installed: $SKYRIM_DIR"
    else
        fail "Skyrim SE not found at $SKYRIM_DIR"
        all_ok=false
    fi

    # 1d. Check Proton
    if [ -d "$PROTON_DIR" ] && [ -f "$PROTON_DIR/proton" ]; then
        pass "Proton Experimental found"
    else
        fail "Proton Experimental not found at $PROTON_DIR"
        all_ok=false
    fi

    # 1e. Check SKSE
    if [ -f "$SKYRIM_DIR/skse64_loader.exe" ]; then
        pass "SKSE loader found"
    else
        fail "SKSE loader not found (install SKSE into Skyrim directory)"
        all_ok=false
    fi

    # 1f. Check relay DLL deployed
    if [ -f "$PLUGINS/skyrim_coop_hooks.dll" ]; then
        pass "Relay DLL deployed: skyrim_coop_hooks.dll"
    else
        fail "Relay DLL not found (run deploy_and_test.sh first)"
        all_ok=false
    fi

    # 1g. Check native client deployed
    if [ -f "$PLUGINS/skyrim-coop" ] && [ -x "$PLUGINS/skyrim-coop" ]; then
        pass "Native client deployed: skyrim-coop"
    else
        fail "Native client not found (run deploy_and_test.sh first)"
        all_ok=false
    fi

    if [ "$all_ok" = "false" ]; then
        fail "Prerequisites check FAILED"
        return 2
    fi

    pass "All prerequisites met"
    return 0
}

# ── Step 2: Launch Skyrim ──
launch_skyrim() {
    info "Killing any existing Skyrim instances..."
    pkill -f "SkyrimSE.exe" 2>/dev/null || true
    pkill -f "skyrim-coop" 2>/dev/null || true
    rm -f "$RELAY_INFO" "$DLL_LOG"
    sleep 1

    info "Launching Skyrim via Proton + SKSE..."
    LAUNCHED_SKYRIM=true
    cd "$SKYRIM_DIR"
    "$PROTON_DIR/proton" run "$SKYRIM_DIR/skse64_loader.exe" >/dev/null 2>&1 &

    # Wait for SkyrimSE.exe process
    info "Waiting for SkyrimSE.exe (timeout: ${TIMEOUT_SKYRIM}s)..."
    local skyrim_pid=""
    for i in $(seq 1 "$TIMEOUT_SKYRIM"); do
        skyrim_pid=$(pgrep -f "SkyrimSE.exe" 2>/dev/null | head -1 || true)
        if [ -n "$skyrim_pid" ]; then
            pass "SkyrimSE.exe running (PID: $skyrim_pid)"
            SKYRIM_PID="$skyrim_pid"
            break
        fi
        sleep 1
    done

    if [ -z "${SKYRIM_PID:-}" ]; then
        fail "SkyrimSE.exe did not start within ${TIMEOUT_SKYRIM}s"
        [ -f "$DLL_LOG" ] && info "DLL log:" && cat "$DLL_LOG"
        return 1
    fi
}

# ── Step 3: Wait for Server Ready ──
wait_for_server() {
    info "Waiting for relay info + embedded server (timeout: ${TIMEOUT_SERVER}s)..."
    local relay_port=""
    local relay_pid=""

    for i in $(seq 1 "$TIMEOUT_SERVER"); do
        if [ -f "$RELAY_INFO" ]; then
            relay_port=$(grep -o '"port":[0-9]*' "$RELAY_INFO" | grep -o '[0-9]*' || true)
            relay_pid=$(grep -o '"pid":[0-9]*' "$RELAY_INFO" | grep -o '[0-9]*' || true)
            if [ -n "$relay_port" ] && [ -n "$relay_pid" ]; then
                pass "Relay info: port=$relay_port pid=$relay_pid"
                SERVER_PORT="$relay_port"
                break
            fi
        fi
        sleep 1
    done

    if [ -z "$relay_port" ]; then
        fail "DLL did not write relay info within ${TIMEOUT_SERVER}s"
        [ -f "$DLL_LOG" ] && info "DLL log:" && cat "$DLL_LOG"
        return 1
    fi

    # Launch native skyrim-coop process
    info "Launching native skyrim-coop process..."
    "$PLUGINS/skyrim-coop" --pid "$SKYRIM_PID" --port "$SERVER_PORT" &
    NATIVE_PID=$!
    sleep 2

    if kill -0 "$NATIVE_PID" 2>/dev/null; then
        pass "skyrim-coop running (PID: $NATIVE_PID)"
    else
        fail "skyrim-coop exited early"
        return 1
    fi
}

# ── Step 4: Run fake_player ──
run_fake_player() {
    info "Running fake_player (duration: ${DURATION}s, port: ${SERVER_PORT})..."

    local verify_args=""
    if [ -n "${SKYRIM_PID:-}" ]; then
        verify_args="--verify-pid $SKYRIM_PID"
    fi

    "$FAKE_PLAYER" --port "$SERVER_PORT" --duration "$DURATION" $verify_args &
    FAKE_PLAYER_PID=$!

    # Wait for fake_player to complete
    if wait "$FAKE_PLAYER_PID"; then
        pass "fake_player exited successfully"
        return 0
    else
        local exit_code=$?
        fail "fake_player exited with code $exit_code"
        return 1
    fi
}

# ── Main ──
main() {
    echo "=== SkyrimCoop In-Game Sync Integration Test ==="
    echo ""

    local check_only=false
    for arg in "$@"; do
        case "$arg" in
            --check-only) check_only=true ;;
            --help|-h)
                echo "Usage: $0 [--check-only] [--help]"
                echo "  --check-only  Only verify prerequisites, don't launch"
                exit 0
                ;;
        esac
    done

    # Always check prerequisites first
    if ! check_prerequisites; then
        exit 2
    fi
    echo ""

    if [ "$check_only" = "true" ]; then
        info "Check-only mode, skipping in-game test"
        exit 0
    fi

    # Set up cleanup trap
    trap cleanup EXIT

    # Launch and test
    if ! launch_skyrim; then
        fail "Failed to launch Skyrim"
        exit 1
    fi
    echo ""

    if ! wait_for_server; then
        fail "Failed to start embedded server"
        exit 1
    fi
    echo ""

    if ! run_fake_player; then
        fail "In-game sync test FAILED"
        exit 1
    fi
    echo ""

    echo "=== In-Game Sync Integration Test PASSED ==="
    exit 0
}

main "$@"
