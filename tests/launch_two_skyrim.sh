#!/bin/bash
# launch_two_skyrim.sh -- Developer tool: launch two Skyrim instances for manual co-op testing
# NOT an automated test. For quick manual iteration on co-op gameplay.
#
# Usage:
#   ./tests/launch_two_skyrim.sh              # Launch host + join
#   ./tests/launch_two_skyrim.sh --host-only  # Launch only host instance
#
# Requirements: Skyrim SE, SKSE, Proton Experimental, deployed relay DLL + native binary
set -euo pipefail

# ── Configuration ──
SKYRIM_DIR="/media/bighass/1f640250-798d-4f2f-87f2-c442109fc5d0/SteamLibrary/steamapps/common/Skyrim Special Edition"
PROTON_DIR="/media/bighass/1f640250-798d-4f2f-87f2-c442109fc5d0/SteamLibrary/steamapps/common/Proton - Experimental"
COMPAT_DATA="/media/bighass/1f640250-798d-4f2f-87f2-c442109fc5d0/SteamLibrary/steamapps/compatdata/489830"
STEAM_DIR="/home/bighass/.steam/steam"
PLUGINS="$SKYRIM_DIR/Data/SKSE/Plugins"
RELAY_INFO="$PLUGINS/skyrim_coop_relay.json"
GAME_SERVER_PORT=10578

# Steam environment
export SteamAppId=489830
export SteamGameId=489830
export STEAM_COMPAT_CLIENT_INSTALL_PATH="$STEAM_DIR"

# Host Skyrim.ini path
HOST_INI="$COMPAT_DATA/pfx/drive_c/users/steamuser/Documents/My Games/Skyrim Special Edition/Skyrim.ini"

# Join prefix (set during clone)
COMPAT_DATA_JOIN=""

# PIDs for cleanup
HOST_SKYRIM_PID=""
HOST_NATIVE_PID=""
JOIN_SKYRIM_PID=""
JOIN_NATIVE_PID=""

# ── Helpers ──
info()  { echo "[INFO]  $*"; }
pass()  { echo "[PASS]  $*"; }
fail()  { echo "[FAIL]  $*"; }
warn()  { echo "[WARN]  $*"; }

# ── INI Injection ──
inject_windowed_ini() {
    local ini_path="$1"
    # Backup
    cp "$ini_path" "${ini_path}.two_player_backup"

    # Remove existing display settings to avoid duplicates
    sed -i '/^bFull Screen=/d; /^iSize H=/d; /^iSize W=/d; /^bAlwaysActive=/d; /^bBorderless=/d; /^sStartingCell=/d' "$ini_path"

    # Inject windowed mode + starting cell after [General]
    sed -i '/^\[General\]/a sStartingCell=Riverwood' "$ini_path"

    # Inject display settings after [Display]
    sed -i '/^\[Display\]/a bFull Screen=0\niSize H=720\niSize W=1280\nbBorderless=0' "$ini_path"

    # Inject interface settings after [Interface] (create section if needed)
    if ! grep -q '^\[Interface\]' "$ini_path"; then
        echo -e "\n[Interface]" >> "$ini_path"
    fi
    sed -i '/^\[Interface\]/a bAlwaysActive=1' "$ini_path"

    info "Injected windowed 1280x720 + bAlwaysActive=1 into $ini_path"
}

# ── Proton Prefix Cloning ──
clone_proton_prefix() {
    COMPAT_DATA_JOIN="/tmp/skyrimcoop_join_prefix_$$"
    info "Cloning Proton prefix for join instance (~200MB)..."
    cp -a "$COMPAT_DATA" "$COMPAT_DATA_JOIN"
    info "Join prefix: $COMPAT_DATA_JOIN"
}

# ── Cleanup ──
cleanup() {
    info "Cleaning up..."

    # Kill native processes
    if [ -n "$HOST_NATIVE_PID" ] && kill -0 "$HOST_NATIVE_PID" 2>/dev/null; then
        kill "$HOST_NATIVE_PID" 2>/dev/null || true
        wait "$HOST_NATIVE_PID" 2>/dev/null || true
    fi
    if [ -n "$JOIN_NATIVE_PID" ] && kill -0 "$JOIN_NATIVE_PID" 2>/dev/null; then
        kill "$JOIN_NATIVE_PID" 2>/dev/null || true
        wait "$JOIN_NATIVE_PID" 2>/dev/null || true
    fi

    # Kill Skyrim instances
    pkill -f "SkyrimSE.exe" 2>/dev/null || true
    pkill -f "skse64_loader" 2>/dev/null || true

    # Restore host INI
    if [ -f "${HOST_INI}.two_player_backup" ]; then
        cp "${HOST_INI}.two_player_backup" "$HOST_INI"
        rm -f "${HOST_INI}.two_player_backup"
        info "Restored host Skyrim.ini"
    fi

    # Remove cloned join prefix
    if [ -n "$COMPAT_DATA_JOIN" ] && [ -d "$COMPAT_DATA_JOIN" ]; then
        info "Removing cloned join prefix..."
        rm -rf "$COMPAT_DATA_JOIN"
    fi

    # Remove relay info
    rm -f "$RELAY_INFO"

    info "Cleanup done"
}

# ── Window Positioning ──
position_windows() {
    info "Positioning Skyrim windows side-by-side..."
    sleep 5  # Wait for windows to appear

    # Try wmctrl first, fall back to xdotool
    if command -v wmctrl >/dev/null 2>&1; then
        local windows
        windows=$(wmctrl -l | grep -i "skyrim" | awk '{print $1}')
        local count=0
        for wid in $windows; do
            if [ $count -eq 0 ]; then
                wmctrl -i -r "$wid" -e '0,0,0,1280,720'
            else
                wmctrl -i -r "$wid" -e '0,1280,0,1280,720'
            fi
            count=$((count + 1))
        done
        if [ $count -ge 2 ]; then
            pass "Positioned $count Skyrim windows side-by-side"
            return 0
        fi
    fi

    if command -v xdotool >/dev/null 2>&1; then
        local windows
        windows=$(xdotool search --name "Skyrim" 2>/dev/null || true)
        local count=0
        for wid in $windows; do
            if [ $count -eq 0 ]; then
                xdotool windowmove "$wid" 0 0
                xdotool windowsize "$wid" 1280 720
            else
                xdotool windowmove "$wid" 1280 0
                xdotool windowsize "$wid" 1280 720
            fi
            count=$((count + 1))
        done
        if [ $count -ge 2 ]; then
            pass "Positioned $count Skyrim windows (xdotool)"
            return 0
        fi
    fi

    warn "Could not position windows automatically (install wmctrl or xdotool)"
}

# ── Wait for relay_info.json ──
wait_for_relay_info() {
    local timeout="${1:-90}"
    local expect_port="${2:-}"   # If set, wait for a DIFFERENT port than this
    info "Waiting for relay_info.json (timeout: ${timeout}s)..."

    for i in $(seq 1 "$timeout"); do
        if [ -f "$RELAY_INFO" ]; then
            local port
            local pid
            port=$(grep -o '"port":[0-9]*' "$RELAY_INFO" | grep -o '[0-9]*' || true)
            pid=$(grep -o '"pid":[0-9]*' "$RELAY_INFO" | grep -o '[0-9]*' || true)
            if [ -n "$port" ] && [ -n "$pid" ]; then
                # If we're expecting a different port, skip if same
                if [ -n "$expect_port" ] && [ "$port" = "$expect_port" ]; then
                    sleep 1
                    continue
                fi
                echo "$port $pid"
                return 0
            fi
        fi
        sleep 1
    done

    fail "relay_info.json not ready within ${timeout}s"
    return 1
}

# ── Wait for GameServer UDP port ──
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
    fail "GameServer not listening on port $port after ${timeout}s"
    return 1
}

# ── Prerequisites Check ──
check_prerequisites() {
    local all_ok=true

    info "Checking prerequisites..."

    if [ ! -d "$SKYRIM_DIR" ] || [ ! -f "$SKYRIM_DIR/SkyrimSE.exe" ]; then
        fail "Skyrim SE not found at $SKYRIM_DIR"
        all_ok=false
    fi

    if [ ! -d "$PROTON_DIR" ] || [ ! -f "$PROTON_DIR/proton" ]; then
        fail "Proton Experimental not found at $PROTON_DIR"
        all_ok=false
    fi

    if [ ! -f "$SKYRIM_DIR/skse64_loader.exe" ]; then
        fail "SKSE loader not found"
        all_ok=false
    fi

    if [ ! -f "$PLUGINS/skyrim_coop_hooks.dll" ]; then
        fail "Relay DLL not deployed (run deploy_and_test.sh)"
        all_ok=false
    fi

    if [ ! -f "$PLUGINS/skyrim-coop" ] || [ ! -x "$PLUGINS/skyrim-coop" ]; then
        fail "Native client (skyrim-coop) not deployed"
        all_ok=false
    fi

    if [ ! -f "$HOST_INI" ]; then
        fail "Skyrim.ini not found (run Skyrim once to generate)"
        all_ok=false
    fi

    if [ "$all_ok" = "false" ]; then
        fail "Prerequisites check FAILED"
        return 1
    fi

    pass "All prerequisites met"
    return 0
}

# ── Main ──
main() {
    echo "=== SkyrimCoop Two-Instance Developer Tool ==="
    echo ""

    local host_only=false
    for arg in "$@"; do
        case "$arg" in
            --host-only) host_only=true ;;
            --help|-h)
                echo "Usage: $0 [--host-only] [--help]"
                echo "  --host-only   Launch only the host instance"
                exit 0
                ;;
        esac
    done

    # Check prerequisites
    if ! check_prerequisites; then
        exit 1
    fi
    echo ""

    # Set up cleanup trap
    trap cleanup EXIT

    # ── Step 1: Kill existing instances ──
    info "Killing any existing Skyrim/native processes..."
    pkill -f "SkyrimSE.exe" 2>/dev/null || true
    pkill -f "skse64_loader" 2>/dev/null || true
    pkill -f "skyrim-coop" 2>/dev/null || true
    rm -f "$RELAY_INFO"
    sleep 1

    # ── Step 2: Inject windowed INI for host ──
    info "Configuring host instance for windowed mode..."
    inject_windowed_ini "$HOST_INI"
    echo ""

    # ── Step 3: Launch host Skyrim via Proton ──
    info "Launching HOST Skyrim via Proton + SKSE..."
    export STEAM_COMPAT_DATA_PATH="$COMPAT_DATA"
    export WINEPREFIX="$COMPAT_DATA/pfx"
    cd "$SKYRIM_DIR"
    "$PROTON_DIR/proton" run "$SKYRIM_DIR/skse64_loader.exe" &
    HOST_SKYRIM_PID=$!
    info "Host Skyrim launcher PID: $HOST_SKYRIM_PID"

    # Wait for SkyrimSE.exe to appear
    info "Waiting for host SkyrimSE.exe..."
    local host_game_pid=""
    for i in $(seq 1 120); do
        host_game_pid=$(pgrep -f "SkyrimSE.exe" 2>/dev/null | head -1 || true)
        if [ -n "$host_game_pid" ]; then
            pass "Host SkyrimSE.exe running (PID: $host_game_pid)"
            break
        fi
        sleep 1
    done
    if [ -z "$host_game_pid" ]; then
        fail "Host SkyrimSE.exe did not start within 120s"
        exit 1
    fi
    echo ""

    # ── Step 4: Wait for relay_info.json, capture host's TCP port + PID ──
    local relay_result
    relay_result=$(wait_for_relay_info 90) || exit 1
    local host_relay_port host_relay_pid
    host_relay_port=$(echo "$relay_result" | awk '{print $1}')
    host_relay_pid=$(echo "$relay_result" | awk '{print $2}')
    pass "Host relay: port=$host_relay_port pid=$host_relay_pid"
    echo ""

    # ── Step 5: Launch host native process (embedded GameServer) ──
    info "Launching host skyrim-coop (embedded GameServer)..."
    "$PLUGINS/skyrim-coop" --pid "$host_relay_pid" --port "$host_relay_port" &
    HOST_NATIVE_PID=$!
    sleep 2
    if kill -0 "$HOST_NATIVE_PID" 2>/dev/null; then
        pass "Host skyrim-coop running (PID: $HOST_NATIVE_PID)"
    else
        fail "Host skyrim-coop exited early"
        exit 1
    fi
    echo ""

    # ── Step 6: Wait for GameServer on UDP port ──
    if ! wait_for_gameserver_port "$GAME_SERVER_PORT" 30; then
        fail "Host GameServer not ready"
        exit 1
    fi
    echo ""

    if [ "$host_only" = "true" ]; then
        info "Host-only mode. Host is running with GameServer on port $GAME_SERVER_PORT."
        info "Press Enter to shut down."
        read -r
        exit 0
    fi

    # ── Step 7: Clone Proton prefix for join instance ──
    clone_proton_prefix
    echo ""

    # ── Step 8: Inject windowed INI for join prefix ──
    local join_ini="$COMPAT_DATA_JOIN/pfx/drive_c/users/steamuser/Documents/My Games/Skyrim Special Edition/Skyrim.ini"
    info "Configuring join instance for windowed mode..."
    inject_windowed_ini "$join_ini"
    echo ""

    # ── Step 9: Launch join Skyrim via Proton with cloned prefix ──
    info "Launching JOIN Skyrim via Proton + SKSE..."
    # Remove host's relay_info.json so join instance writes a fresh one
    rm -f "$RELAY_INFO"

    STEAM_COMPAT_DATA_PATH="$COMPAT_DATA_JOIN" WINEPREFIX="$COMPAT_DATA_JOIN/pfx" \
        "$PROTON_DIR/proton" run "$SKYRIM_DIR/skse64_loader.exe" &
    JOIN_SKYRIM_PID=$!
    info "Join Skyrim launcher PID: $JOIN_SKYRIM_PID"

    # Wait for a second SkyrimSE.exe process
    info "Waiting for join SkyrimSE.exe..."
    local join_game_pid=""
    for i in $(seq 1 120); do
        # Find SkyrimSE.exe PIDs that are NOT the host
        local all_pids
        all_pids=$(pgrep -f "SkyrimSE.exe" 2>/dev/null || true)
        for pid in $all_pids; do
            if [ "$pid" != "$host_game_pid" ]; then
                join_game_pid="$pid"
                break
            fi
        done
        if [ -n "$join_game_pid" ]; then
            pass "Join SkyrimSE.exe running (PID: $join_game_pid)"
            break
        fi
        sleep 1
    done
    if [ -z "$join_game_pid" ]; then
        fail "Join SkyrimSE.exe did not start within 120s"
        exit 1
    fi
    echo ""

    # ── Step 10: Wait for NEW relay_info.json (different port from host) ──
    local join_relay_result
    join_relay_result=$(wait_for_relay_info 90 "$host_relay_port") || exit 1
    local join_relay_port join_relay_pid
    join_relay_port=$(echo "$join_relay_result" | awk '{print $1}')
    join_relay_pid=$(echo "$join_relay_result" | awk '{print $2}')
    pass "Join relay: port=$join_relay_port pid=$join_relay_pid"
    echo ""

    # ── Step 11: Launch join native process (connects to host GameServer) ──
    info "Launching join skyrim-coop (connecting to host at 127.0.0.1:$GAME_SERVER_PORT)..."
    "$PLUGINS/skyrim-coop" --pid "$join_relay_pid" --port "$join_relay_port" \
        --connect "127.0.0.1:$GAME_SERVER_PORT" &
    JOIN_NATIVE_PID=$!
    sleep 2
    if kill -0 "$JOIN_NATIVE_PID" 2>/dev/null; then
        pass "Join skyrim-coop running (PID: $JOIN_NATIVE_PID)"
    else
        fail "Join skyrim-coop exited early"
        exit 1
    fi
    echo ""

    # ── Step 12: Position windows side-by-side ──
    position_windows
    echo ""

    # ── Step 13: Wait for user ──
    echo "=========================================="
    echo "  Both instances running!"
    echo "  Host: PID $host_game_pid (GameServer on port $GAME_SERVER_PORT)"
    echo "  Join: PID $join_game_pid (connected via JoinClient)"
    echo "=========================================="
    echo ""
    info "Press Enter to shut down both instances."
    read -r

    info "Shutting down..."
}

main "$@"
