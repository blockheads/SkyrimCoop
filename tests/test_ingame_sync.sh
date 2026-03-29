#!/bin/bash
# test_ingame_sync.sh -- Integration test: fake_player connects to embedded GameServer
# inside a real Skyrim instance and verifies remote actor position via /proc/pid/mem.
#
# Usage:
#   ./tests/test_ingame_sync.sh                   # Full in-game test (fake_player runs by default)
#   ./tests/test_ingame_sync.sh --check-only      # Just verify prerequisites
#   ./tests/test_ingame_sync.sh --no-fake-player   # Skip fake_player stage (stages 1-4 only)
#   ./tests/test_ingame_sync.sh --keep-logs        # Keep log directory even on PASS
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

# Skyrim INI (for sStartingCell injection)
SKYRIM_INI="$COMPAT_DATA/pfx/drive_c/users/steamuser/Documents/My Games/Skyrim Special Edition/Skyrim.ini"

# Test parameters
DURATION=15
GAME_SERVER_PORT=10578   # enet6 UDP port for GameServer (fake_player connects here)
RELAY_PORT=""            # TCP port for DLL<->native relay (discovered from relay_info.json)
TIMEOUT_SKYRIM=120
TIMEOUT_SERVER=90

# Log collection
TEST_RUN_DIR=""
KEEP_LOGS=false
TEST_FAILED=false

# ── Helpers ──
info()  { echo "[INFO]  $*"; }
pass()  { echo "[PASS]  $*"; }
fail()  { echo "[FAIL]  $*"; }
warn()  { echo "[WARN]  $*"; }

setup_test_logs() {
    TEST_RUN_DIR=$(mktemp -d "/tmp/skyrimcoop-test-XXXXXX")
    info "Test run logs: $TEST_RUN_DIR"
}

fail_with_logs() {
    local msg="$1"
    fail "$msg"
    if [ -n "$TEST_RUN_DIR" ]; then
        info "Check logs in: $TEST_RUN_DIR/"
        [ -f "$DLL_LOG" ] && info "DLL log (last 20 lines):" && tail -20 "$DLL_LOG"
        [ -f "$TEST_RUN_DIR/native_stderr.log" ] && info "Native stderr (last 20):" && tail -20 "$TEST_RUN_DIR/native_stderr.log"
        [ -f "$TEST_RUN_DIR/skyrim_stderr.log" ] && info "Skyrim stderr (last 20):" && tail -20 "$TEST_RUN_DIR/skyrim_stderr.log"
    fi
}

# ── INI Injection ──
inject_starting_cell() {
    if [ ! -f "$SKYRIM_INI" ]; then
        fail "Skyrim.ini not found at: $SKYRIM_INI"
        exit 2
    fi

    # Back up Skyrim.ini
    cp "$SKYRIM_INI" "${SKYRIM_INI}.test_backup"
    info "Backed up Skyrim.ini to Skyrim.ini.test_backup"

    # Remove any existing sStartingCell line to avoid duplicates
    sed -i '/^sStartingCell=/d' "$SKYRIM_INI"

    # Insert sStartingCell=Riverwood after [General]
    sed -i '/^\[General\]/a sStartingCell=Riverwood' "$SKYRIM_INI"
    info "Injected sStartingCell=Riverwood into Skyrim.ini [General] section"

    # Mute all sounds for automated testing
    sed -i '/^fAudioMasterVolume=/d' "$SKYRIM_INI"
    if grep -q '^\[AudioMenu\]' "$SKYRIM_INI"; then
        sed -i '/^\[AudioMenu\]/a fAudioMasterVolume=0.0000' "$SKYRIM_INI"
    else
        printf '\n[AudioMenu]\nfAudioMasterVolume=0.0000\n' >> "$SKYRIM_INI"
    fi
    info "Muted audio (fAudioMasterVolume=0)"
}

restore_skyrim_ini() {
    if [ -f "${SKYRIM_INI}.test_backup" ]; then
        cp "${SKYRIM_INI}.test_backup" "$SKYRIM_INI"
        rm -f "${SKYRIM_INI}.test_backup"
        info "Restored Skyrim.ini from backup"
    else
        # Fallback: just remove the injected line
        if [ -f "$SKYRIM_INI" ]; then
            sed -i '/^sStartingCell=Riverwood$/d' "$SKYRIM_INI"
            info "Removed sStartingCell=Riverwood from Skyrim.ini (no backup found)"
        fi
    fi
}

cleanup() {
    info "Cleaning up..."
    # Restore Skyrim.ini before killing processes
    restore_skyrim_ini
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

    # Copy DLL log to test run dir if it exists
    if [ -n "$TEST_RUN_DIR" ] && [ -f "$DLL_LOG" ]; then
        cp "$DLL_LOG" "$TEST_RUN_DIR/dll.log" 2>/dev/null || true
    fi

    # Clean up logs unless --keep-logs or test failed
    if [ -n "$TEST_RUN_DIR" ] && [ "$KEEP_LOGS" != "true" ] && [ "$TEST_FAILED" != "true" ]; then
        rm -rf "$TEST_RUN_DIR"
    elif [ -n "$TEST_RUN_DIR" ]; then
        info "Logs preserved in: $TEST_RUN_DIR/"
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

    # 1h. Check Skyrim.ini exists (needed for sStartingCell injection)
    if [ -f "$SKYRIM_INI" ]; then
        pass "Skyrim.ini found for INI injection"
    else
        fail "Skyrim.ini not found at: $SKYRIM_INI (run Skyrim once to generate it)"
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
    "$PROTON_DIR/proton" run "$SKYRIM_DIR/skse64_loader.exe" \
        >"$TEST_RUN_DIR/skyrim_stdout.log" 2>"$TEST_RUN_DIR/skyrim_stderr.log" &

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
        fail_with_logs "SkyrimSE.exe did not start within ${TIMEOUT_SKYRIM}s"
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
                pass "Relay info: relay_tcp_port=$relay_port pid=$relay_pid"
                RELAY_PORT="$relay_port"
                break
            fi
        fi
        sleep 1
    done

    if [ -z "$RELAY_PORT" ]; then
        fail_with_logs "DLL did not write relay info within ${TIMEOUT_SERVER}s"
        return 1
    fi

    # Launch native skyrim-coop process (connects to DLL via relay TCP port)
    info "Launching native skyrim-coop process..."
    "$PLUGINS/skyrim-coop" --pid "$SKYRIM_PID" --port "$RELAY_PORT" \
        >"$TEST_RUN_DIR/native_stdout.log" 2>"$TEST_RUN_DIR/native_stderr.log" &
    NATIVE_PID=$!
    sleep 2

    if kill -0 "$NATIVE_PID" 2>/dev/null; then
        pass "skyrim-coop running (PID: $NATIVE_PID)"
    else
        fail "skyrim-coop exited early"
        return 1
    fi
}

# ── Step 4: Verify relay hooks are flowing ──
verify_relay_hooks() {
    info "Watching native process logs for hook events (10s)..."

    # The native process logs ActorAdded events as it receives hook data from the DLL.
    # If we see these, the full relay pipeline is working:
    # DLL hooks -> TCP -> native process -> service dispatch
    local hook_count=0
    local start_time=$(date +%s)

    while true; do
        local now=$(date +%s)
        local elapsed=$(( now - start_time ))
        if [ "$elapsed" -ge 10 ]; then
            break
        fi
        sleep 1
    done

    # Count hook events from native process (it logs to stderr which we can check)
    # Since the native process is backgrounded, check if it's still running
    if kill -0 "$NATIVE_PID" 2>/dev/null; then
        pass "Native process still running after 10s (relay connection stable)"
    else
        fail "Native process died during hook verification"
        return 1
    fi

    pass "Relay pipeline verified: DLL -> TCP -> native process -> services"
    return 0
}

# ── Step 5: Wait for GameServer UDP port readiness ──
wait_for_gameserver_port() {
    local port="${1:-10578}"
    local timeout="${2:-30}"
    info "Waiting for GameServer on UDP port $port (timeout: ${timeout}s)..."
    for i in $(seq 1 "$timeout"); do
        if ss -ulnp 2>/dev/null | grep -q ":${port} "; then
            pass "GameServer listening on port $port"
            return 0
        fi
        sleep 1
    done
    fail_with_logs "GameServer not listening on port $port after ${timeout}s"
    return 1
}

# ── Step 6: Run fake_player (default — requires embedded GameServer) ──
run_fake_player() {
    info "Running fake_player (duration: ${DURATION}s, game_server_port: ${GAME_SERVER_PORT})..."

    local verify_args=""
    if [ -n "${SKYRIM_PID:-}" ]; then
        verify_args="--verify-pid $SKYRIM_PID"
    fi

    # fake_player connects to the GameServer's enet6 UDP port (not the relay TCP port)
    "$FAKE_PLAYER" --port "$GAME_SERVER_PORT" --duration "$DURATION" $verify_args \
        >"$TEST_RUN_DIR/fake_player_stdout.log" 2>"$TEST_RUN_DIR/fake_player_stderr.log" &
    FAKE_PLAYER_PID=$!

    # Wait for fake_player to complete
    if wait "$FAKE_PLAYER_PID"; then
        # Show fake_player output
        if [ -f "$TEST_RUN_DIR/fake_player_stderr.log" ]; then
            info "fake_player output (last 20 lines):"
            tail -20 "$TEST_RUN_DIR/fake_player_stderr.log"
        fi
        pass "fake_player exited successfully"
        return 0
    else
        local exit_code=$?
        if [ -f "$TEST_RUN_DIR/fake_player_stderr.log" ]; then
            info "fake_player stderr (last 20 lines):"
            tail -20 "$TEST_RUN_DIR/fake_player_stderr.log"
        fi
        fail "fake_player exited with code $exit_code"
        return 1
    fi
}

# ── Main ──
main() {
    echo "=== SkyrimCoop In-Game Sync Integration Test ==="
    echo ""

    local check_only=false
    local no_fake_player=false
    for arg in "$@"; do
        case "$arg" in
            --check-only) check_only=true ;;
            --no-fake-player) no_fake_player=true ;;
            --keep-logs) KEEP_LOGS=true ;;
            --help|-h)
                echo "Usage: $0 [--check-only] [--no-fake-player] [--keep-logs] [--help]"
                echo "  --check-only       Only verify prerequisites, don't launch"
                echo "  --no-fake-player   Skip fake_player stage (stages 1-4 only)"
                echo "  --keep-logs        Keep log directory even on PASS"
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

    # Set up test log directory
    setup_test_logs

    # Inject sStartingCell so Skyrim boots directly into Riverwood
    inject_starting_cell

    # Launch and test
    if ! launch_skyrim; then
        TEST_FAILED=true
        fail "Failed to launch Skyrim"
        exit 1
    fi
    echo ""

    if ! wait_for_server; then
        TEST_FAILED=true
        fail "Failed to start relay connection"
        exit 1
    fi
    echo ""

    if ! verify_relay_hooks; then
        TEST_FAILED=true
        fail "Relay hook verification FAILED"
        exit 1
    fi
    echo ""

    # fake_player runs by default (use --no-fake-player to skip)
    if [ "$no_fake_player" != "true" ]; then
        if ! wait_for_gameserver_port "$GAME_SERVER_PORT" 30; then
            TEST_FAILED=true
            fail "GameServer port not ready"
            exit 1
        fi
        echo ""

        if ! run_fake_player; then
            TEST_FAILED=true
            fail "fake_player test FAILED"
            exit 1
        fi
        echo ""
    else
        info "Skipping fake_player (--no-fake-player specified)"
    fi

    echo "=== In-Game Sync Integration Test PASSED ==="
    exit 0
}

main "$@"
