#!/bin/bash
# launch_two_player.sh -- Phase 10: Two-player co-op launch with SKYRIM_COOP_PORT
#
# Port Assignment Strategy (D-11):
#   Instead of relying on relay_info.json for port discovery (which races when two
#   instances share the same Skyrim directory), each instance gets a deterministic
#   TCP relay port via the SKYRIM_COOP_PORT environment variable:
#     Host:   SKYRIM_COOP_PORT=9667
#     Client: SKYRIM_COOP_PORT=9668
#   The relay DLL (modified in Plan 02) reads this env var at load time and binds
#   to the specified port. The native_client then connects to the known port.
#
# Dual-Instance Setup (D-10, D-12, D-15):
#   Two separate Proton prefixes ensure completely isolated Wine environments.
#   Host prefix is the normal Steam compat data; client prefix is a copy.
#
# Connection Model (D-13, D-14):
#   Host runs embedded GameServer on UDP port 10578.
#   Client connects via --connect 127.0.0.1:10578.
#
# Usage:
#   ./tests/launch_two_player.sh                       # Launch host + client
#   ./tests/launch_two_player.sh --host-only           # Launch only host instance
#   ./tests/launch_two_player.sh --record session      # Record hook events + enet6 traffic
#                                                      #   Creates: session_host.ndjson, session_client.ndjson,
#                                                      #            session_host_enet/ (per-peer network msgs)
#
# Requirements: Skyrim SE, SKSE, Proton Experimental, built relay DLL + native binary
set -euo pipefail

LOG_DIR="$(cd "$(dirname "$0")/.." && pwd)/logs"
mkdir -p "$LOG_DIR"
LOG_FILE="$LOG_DIR/two_player_$(date +%Y%m%d_%H%M%S).log"
exec > >(tee -a "$LOG_FILE") 2>&1
echo "=== Log: $LOG_FILE ==="

# ── Configuration (edit these for your system) ──
SKYRIM_DIR="/media/bighass/1f640250-798d-4f2f-87f2-c442109fc5d0/SteamLibrary/steamapps/common/Skyrim Special Edition"
PROTON_DIR="/media/bighass/1f640250-798d-4f2f-87f2-c442109fc5d0/SteamLibrary/steamapps/common/Proton - Experimental"
COMPAT_HOST="/media/bighass/1f640250-798d-4f2f-87f2-c442109fc5d0/SteamLibrary/steamapps/compatdata/489830"
COMPAT_CLIENT="${COMPAT_HOST}_client"
STEAM_DIR="/home/bighass/.steam/steam"
PROJECT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="${PROJECT_DIR}/build"
NATIVE_BIN="${BUILD_DIR}/linux/x86_64/release/skyrim-coop"
DLL_NAME="skyrim_coop_hooks.dll"
PLUGINS="$SKYRIM_DIR/Data/SKSE/Plugins"

# Port assignments per D-11 -- deterministic, no relay_info.json race
HOST_RELAY_PORT=9667
CLIENT_RELAY_PORT=9668
GAMESERVER_PORT=10578

# Steam environment
export SteamAppId=489830
export SteamGameId=489830
export STEAM_COMPAT_CLIENT_INSTALL_PATH="$STEAM_DIR"

# PIDs for cleanup
HOST_SKYRIM_PID=""
HOST_NATIVE_PID=""
CLIENT_SKYRIM_PID=""
CLIENT_NATIVE_PID=""

# ── Helpers ──
info()  { echo "[INFO]  $*"; }
pass()  { echo "[PASS]  $*"; }
fail()  { echo "[FAIL]  $*"; }
warn()  { echo "[WARN]  $*"; }

# ── INI Injection ──
inject_windowed_ini() {
    local ini_path="$1"
    local label="$2"

    if [ ! -f "$ini_path" ]; then
        warn "Skyrim.ini not found at $ini_path -- skipping INI injection for $label"
        return 0
    fi

    # Backup
    cp "$ini_path" "${ini_path}.two_player_backup"

    # Remove existing settings to avoid duplicates
    sed -i '/^bFull Screen=/d; /^iSize H=/d; /^iSize W=/d; /^bAlwaysActive=/d; /^bBorderless=/d; /^sStartingCell=/d' "$ini_path"

    # Inject sStartingCell=Riverwood after [General] (per D-21)
    sed -i '/^\[General\]/a sStartingCell=Riverwood' "$ini_path"

    # Inject display settings after [Display] for windowed mode
    sed -i '/^\[Display\]/a bFull Screen=0\niSize H=720\niSize W=1280\nbBorderless=0' "$ini_path"

    # Inject bAlwaysActive after [Interface] (create section if needed)
    if ! grep -q '^\[Interface\]' "$ini_path"; then
        echo -e "\n[Interface]" >> "$ini_path"
    fi
    sed -i '/^\[Interface\]/a bAlwaysActive=1' "$ini_path"

    info "$label: Injected windowed 1280x720 + sStartingCell=Riverwood into $(basename "$ini_path")"
}

# ── Wait for TCP port (relay DLL listening) ──
wait_for_port() {
    local port="$1"
    local label="$2"
    local timeout=90
    local elapsed=0
    info "Waiting for $label relay DLL on TCP port $port (timeout: ${timeout}s)..."
    while [ $elapsed -lt $timeout ]; do
        if ss -tln 2>/dev/null | grep -q ":${port} "; then
            pass "$label relay DLL listening on port $port"
            return 0
        fi
        sleep 1
        elapsed=$((elapsed + 1))
    done
    fail "$label relay DLL not listening on port $port after ${timeout}s"
    return 1
}

# ── Find PID owning a TCP port ──
get_pid_for_port() {
    local port="$1"
    # Use ss to find the PID listening on this port
    local pid
    pid=$(ss -tlnp 2>/dev/null | grep ":${port} " | grep -oP 'pid=\K[0-9]+' | head -1)
    echo "$pid"
}

# ── Wait for GameServer UDP port ──
wait_for_gameserver_port() {
    local port="$1"
    local timeout="${2:-30}"
    info "Waiting for GameServer on UDP port $port (timeout: ${timeout}s)..."
    for i in $(seq 1 "$timeout"); do
        if ss -ulnp 2>/dev/null | grep -q ":${port} "; then
            pass "GameServer listening on UDP port $port"
            return 0
        fi
        sleep 1
    done
    fail "GameServer not listening on UDP port $port after ${timeout}s"
    return 1
}

# ── Launch Skyrim under Proton with SKYRIM_COOP_PORT ──
launch_skyrim() {
    local compat_path="$1"
    local label="$2"
    local relay_port="$3"

    export STEAM_COMPAT_DATA_PATH="$compat_path"
    export WINEPREFIX="$compat_path/pfx"
    export SKYRIM_COOP_PORT="$relay_port"  # Per D-11: DLL reads this to bind specific port

    cd "$SKYRIM_DIR"
    "$PROTON_DIR/proton" run "$SKYRIM_DIR/skse64_loader.exe" &
    local launcher_pid=$!
    info "$label Skyrim launcher PID: $launcher_pid (SKYRIM_COOP_PORT=$relay_port)"
    echo "$launcher_pid"
}

# ── Window Positioning ──
position_windows() {
    info "Positioning Skyrim windows side-by-side..."
    sleep 5

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

# ── Cleanup ──
cleanup() {
    info "Cleaning up..."

    # Kill native processes
    for pid_var in HOST_NATIVE_PID CLIENT_NATIVE_PID; do
        local pid="${!pid_var}"
        if [ -n "$pid" ] && kill -0 "$pid" 2>/dev/null; then
            kill "$pid" 2>/dev/null || true
            wait "$pid" 2>/dev/null || true
        fi
    done

    # Kill Skyrim instances
    pkill -f "SkyrimSE.exe" 2>/dev/null || true
    pkill -f "skse64_loader" 2>/dev/null || true

    # Restore INI backups
    local host_ini="$COMPAT_HOST/pfx/drive_c/users/steamuser/Documents/My Games/Skyrim Special Edition/Skyrim.ini"
    if [ -f "${host_ini}.two_player_backup" ]; then
        cp "${host_ini}.two_player_backup" "$host_ini"
        rm -f "${host_ini}.two_player_backup"
        info "Restored host Skyrim.ini"
    fi

    local client_ini="$COMPAT_CLIENT/pfx/drive_c/users/steamuser/Documents/My Games/Skyrim Special Edition/Skyrim.ini"
    if [ -f "${client_ini}.two_player_backup" ]; then
        cp "${client_ini}.two_player_backup" "$client_ini"
        rm -f "${client_ini}.two_player_backup"
        info "Restored client Skyrim.ini"
    fi

    info "Cleanup done"
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
        fail "SKSE loader not found at $SKYRIM_DIR/skse64_loader.exe"
        all_ok=false
    fi

    if [ ! -f "$PLUGINS/$DLL_NAME" ]; then
        fail "Relay DLL not found at $PLUGINS/$DLL_NAME"
        all_ok=false
    fi

    if [ ! -f "$NATIVE_BIN" ]; then
        fail "Native client not found at $NATIVE_BIN"
        all_ok=false
    fi

    if [ "$all_ok" = "false" ]; then
        fail "Prerequisites check FAILED"
        return 1
    fi

    pass "All prerequisites met"
    return 0
}

# ── Deploy DLL ──
deploy_dll() {
    local dll_src="${BUILD_DIR}/mingw/x86_64/release/${DLL_NAME}"
    if [ -f "$dll_src" ]; then
        mkdir -p "$PLUGINS"
        cp "$dll_src" "$PLUGINS/$DLL_NAME"
        info "Deployed $DLL_NAME to $PLUGINS/"
    else
        info "No build artifact at $dll_src -- assuming DLL already deployed"
    fi
}

# ── Main ──
main() {
    echo "=== Phase 10: Two-Player Co-op Launch ==="
    echo "Host relay:   SKYRIM_COOP_PORT=$HOST_RELAY_PORT"
    echo "Client relay: SKYRIM_COOP_PORT=$CLIENT_RELAY_PORT"
    echo "GameServer:   UDP port $GAMESERVER_PORT"
    echo "Client connects to: 127.0.0.1:$GAMESERVER_PORT"
    echo ""

    local host_only=false
    local record_prefix=""
    while [[ $# -gt 0 ]]; do
        case "$1" in
            --host-only) host_only=true; shift ;;
            --record) record_prefix="$2"; shift 2 ;;
            --help|-h)
                echo "Usage: $0 [--host-only] [--record <prefix>] [--help]"
                echo "  --host-only        Launch only the host instance"
                echo "  --record <prefix>  Record hook events + enet6 traffic"
                echo "                     Creates: <prefix>_host.ndjson, <prefix>_client.ndjson,"
                echo "                              <prefix>_host_enet/ (per-peer network messages)"
                exit 0
                ;;
            *) shift ;;
        esac
    done

    # Resolve to absolute path before cd changes working directory
    [[ -n "$record_prefix" && "$record_prefix" != /* ]] && record_prefix="$(pwd)/$record_prefix"

    # Check prerequisites
    if ! check_prerequisites; then
        exit 1
    fi
    echo ""

    # Deploy DLL if build artifact exists
    deploy_dll
    echo ""

    # Set up cleanup trap
    trap cleanup EXIT

    # ── Step 1: Kill existing instances ──
    info "Killing any existing Skyrim/native processes..."
    pkill -f "SkyrimSE.exe" 2>/dev/null || true
    pkill -f "skse64_loader" 2>/dev/null || true
    pkill -f "skyrim-coop" 2>/dev/null || true
    sleep 1

    # ── Step 2: Create client Proton prefix if needed (per D-10) ──
    if [ ! -d "$COMPAT_CLIENT" ]; then
        info "Creating client Proton prefix (copying from host)..."
        cp -a "$COMPAT_HOST" "$COMPAT_CLIENT"
        pass "Client prefix created at $COMPAT_CLIENT"
    else
        info "Client Proton prefix already exists at $COMPAT_CLIENT"
    fi
    echo ""

    # ── Step 3: Inject windowed INI + sStartingCell=Riverwood for both (per D-21) ──
    local host_ini="$COMPAT_HOST/pfx/drive_c/users/steamuser/Documents/My Games/Skyrim Special Edition/Skyrim.ini"
    local client_ini="$COMPAT_CLIENT/pfx/drive_c/users/steamuser/Documents/My Games/Skyrim Special Edition/Skyrim.ini"
    inject_windowed_ini "$host_ini" "HOST"
    inject_windowed_ini "$client_ini" "CLIENT"
    echo ""

    # ── Step 4: Launch host Skyrim with SKYRIM_COOP_PORT=9667 ──
    info "Launching HOST Skyrim via Proton + SKSE (SKYRIM_COOP_PORT=$HOST_RELAY_PORT)..."
    HOST_SKYRIM_PID=$(launch_skyrim "$COMPAT_HOST" "HOST" "$HOST_RELAY_PORT")

    # Wait for host relay DLL to start listening on known port
    wait_for_port "$HOST_RELAY_PORT" "HOST" || exit 1
    HOST_PID=$(get_pid_for_port "$HOST_RELAY_PORT")
    if [ -z "$HOST_PID" ]; then
        # Fallback: use pgrep
        HOST_PID=$(pgrep -f "SkyrimSE.exe" 2>/dev/null | head -1 || true)
    fi
    pass "Host Skyrim PID: $HOST_PID (relay on port $HOST_RELAY_PORT)"
    echo ""

    # ── Step 5: Launch host native_client (embedded GameServer) ──
    local host_record_arg=""
    if [[ -n "$record_prefix" ]]; then
        host_record_arg="--record ${record_prefix}_host.ndjson"
        info "Recording host hooks to: ${record_prefix}_host.ndjson"
        info "Recording host enet6 to: ${record_prefix}_host_enet/"
    fi
    info "Launching host skyrim-coop (embedded GameServer)..."
    "$NATIVE_BIN" --pid "$HOST_PID" --port "$HOST_RELAY_PORT" --server-port "$GAMESERVER_PORT" $host_record_arg &
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
    if ! wait_for_gameserver_port "$GAMESERVER_PORT" 30; then
        fail "Host GameServer not ready"
        exit 1
    fi
    echo ""

    if [ "$host_only" = "true" ]; then
        info "Host-only mode. Host is running with GameServer on port $GAMESERVER_PORT."
        info "Press Enter to shut down."
        read -r
        exit 0
    fi

    # ── Step 7: Launch client Skyrim with SKYRIM_COOP_PORT=9668 ──
    info "Launching CLIENT Skyrim via Proton + SKSE (SKYRIM_COOP_PORT=$CLIENT_RELAY_PORT)..."
    CLIENT_SKYRIM_PID=$(launch_skyrim "$COMPAT_CLIENT" "CLIENT" "$CLIENT_RELAY_PORT")

    # Wait for client relay DLL to start listening on known port
    wait_for_port "$CLIENT_RELAY_PORT" "CLIENT" || exit 1
    CLIENT_PID=$(get_pid_for_port "$CLIENT_RELAY_PORT")
    if [ -z "$CLIENT_PID" ]; then
        # Fallback: find second SkyrimSE.exe that is not the host
        local all_pids
        all_pids=$(pgrep -f "SkyrimSE.exe" 2>/dev/null || true)
        for pid in $all_pids; do
            if [ "$pid" != "$HOST_PID" ]; then
                CLIENT_PID="$pid"
                break
            fi
        done
    fi
    pass "Client Skyrim PID: $CLIENT_PID (relay on port $CLIENT_RELAY_PORT)"
    echo ""

    # ── Step 8: Launch client native_client in join mode ──
    local client_record_arg=""
    if [[ -n "$record_prefix" ]]; then
        client_record_arg="--record ${record_prefix}_client.ndjson"
        info "Recording client hooks to: ${record_prefix}_client.ndjson"
    fi
    info "Launching client skyrim-coop (connecting to host at 127.0.0.1:$GAMESERVER_PORT)..."
    "$NATIVE_BIN" --pid "$CLIENT_PID" --port "$CLIENT_RELAY_PORT" --connect "127.0.0.1:$GAMESERVER_PORT" $client_record_arg &
    CLIENT_NATIVE_PID=$!
    sleep 2
    if kill -0 "$CLIENT_NATIVE_PID" 2>/dev/null; then
        pass "Client skyrim-coop running (PID: $CLIENT_NATIVE_PID)"
    else
        fail "Client skyrim-coop exited early"
        exit 1
    fi
    echo ""

    # ── Step 9: Position windows side-by-side ──
    position_windows
    echo ""

    # ── Step 10: Summary and wait ──
    echo "=========================================="
    echo "  Two-Player Co-op Session Active!"
    echo ""
    echo "  HOST:   Skyrim PID $HOST_PID"
    echo "          Relay port $HOST_RELAY_PORT"
    echo "          GameServer UDP $GAMESERVER_PORT"
    echo "          Native PID $HOST_NATIVE_PID"
    echo ""
    echo "  CLIENT: Skyrim PID $CLIENT_PID"
    echo "          Relay port $CLIENT_RELAY_PORT"
    echo "          Connected to 127.0.0.1:$GAMESERVER_PORT"
    echo "          Native PID $CLIENT_NATIVE_PID"
    echo "=========================================="
    echo ""
    if [[ -n "$record_prefix" ]]; then
        echo ""
        echo "  RECORDING: --record $record_prefix"
        echo "    Host hooks:  ${record_prefix}_host.ndjson"
        echo "    Host enet6:  ${record_prefix}_host_enet/"
        echo "    Client hooks: ${record_prefix}_client.ndjson"
    fi
    echo ""
    info "Press Enter to shut down both instances."
    read -r

    info "Shutting down..."
}

main "$@"
