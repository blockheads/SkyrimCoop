#!/bin/bash
# launch_host.sh -- Launch host Skyrim + native client with embedded GameServer
#
# Usage:
#   ./launch_host.sh                     # Normal host mode (launches Skyrim)
#   ./launch_host.sh --record out.ndjson # Host mode + record hook events + enet6 traffic
#                                        #   Creates: out.ndjson (hooks) + out_enet/ (per-peer network)
#   ./launch_host.sh --replay  in.ndjson # Replay mode (no Skyrim needed)
#   ./launch_host.sh --replay  in.ndjson --real-time  # Replay at recorded speed
set -euo pipefail

LOG_DIR="$(cd "$(dirname "$0")/.." && pwd)/logs"
mkdir -p "$LOG_DIR"
LOG_FILE="$LOG_DIR/host_$(date +%Y%m%d_%H%M%S).log"
exec > >(tee -a "$LOG_FILE") 2>&1
echo "=== Log: $LOG_FILE ==="

SKYRIM_DIR="/media/bighass/1f640250-798d-4f2f-87f2-c442109fc5d0/SteamLibrary/steamapps/common/Skyrim Special Edition"
PROTON_DIR="/media/bighass/1f640250-798d-4f2f-87f2-c442109fc5d0/SteamLibrary/steamapps/common/Proton - Experimental"
COMPAT_HOST="/media/bighass/1f640250-798d-4f2f-87f2-c442109fc5d0/SteamLibrary/steamapps/compatdata/489830"
PROJECT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
NATIVE_BIN="${PROJECT_DIR}/build/linux/x86_64/releasedbg/skyrim-coop"

HOST_RELAY_PORT=9667
GAMESERVER_PORT=10578

# --- Replay mode: skip Skyrim entirely ---
REPLAY_FILE=""
RECORD_FILE=""
REAL_TIME=""
PASSTHROUGH_ARGS=()

while [[ $# -gt 0 ]]; do
    case "$1" in
        --replay)  REPLAY_FILE="$2"; shift 2 ;;
        --record)  RECORD_FILE="$2"; shift 2 ;;
        --real-time) REAL_TIME="--real-time"; shift ;;
        *)         PASSTHROUGH_ARGS+=("$1"); shift ;;
    esac
done

# Resolve to absolute paths before cd changes the working directory
[[ -n "$RECORD_FILE" && "$RECORD_FILE" != /* ]] && RECORD_FILE="$(pwd)/$RECORD_FILE"
[[ -n "$REPLAY_FILE" && "$REPLAY_FILE" != /* ]] && REPLAY_FILE="$(pwd)/$REPLAY_FILE"

if [[ -n "$REPLAY_FILE" ]]; then
    if [[ ! -f "$REPLAY_FILE" ]]; then
        echo "[FAIL] Replay file not found: $REPLAY_FILE"
        exit 1
    fi
    echo "=== REPLAY MODE: $REPLAY_FILE ==="
    exec "$NATIVE_BIN" --replay "$REPLAY_FILE" $REAL_TIME
fi

SKYRIM_INI="$COMPAT_HOST/pfx/drive_c/users/steamuser/Documents/My Games/Skyrim Special Edition/Skyrim.ini"

# --- INI auto-configuration (bAlwaysActive + windowed mode) ---
if [[ -f "$SKYRIM_INI" ]]; then
    cp "$SKYRIM_INI" "${SKYRIM_INI}.host_backup"
    echo "[INFO] Backed up Skyrim.ini"

    sed -i '/^bFull Screen=/d; /^iSize H=/d; /^iSize W=/d; /^bAlwaysActive=/d; /^bBorderless=/d' "$SKYRIM_INI"
    sed -i '/^\[Display\]/a bFull Screen=0\niSize H=720\niSize W=1280\nbBorderless=0' "$SKYRIM_INI"

    if grep -q '^\[Interface\]' "$SKYRIM_INI"; then
        sed -i '/^\[Interface\]/a bAlwaysActive=1' "$SKYRIM_INI"
    else
        echo -e "\n[Interface]\nbAlwaysActive=1" >> "$SKYRIM_INI"
    fi

    echo "[INFO] Injected windowed 1280x720 + bAlwaysActive=1 into Skyrim.ini"
fi

trap 'if [[ -f "${SKYRIM_INI}.host_backup" ]]; then cp "${SKYRIM_INI}.host_backup" "$SKYRIM_INI"; rm -f "${SKYRIM_INI}.host_backup"; echo "[INFO] Restored Skyrim.ini"; fi' EXIT

export SteamAppId=489830
export SteamGameId=489830
export STEAM_COMPAT_CLIENT_INSTALL_PATH="/home/bighass/.steam/steam"
export STEAM_COMPAT_DATA_PATH="$COMPAT_HOST"
export WINEPREFIX="$COMPAT_HOST/pfx"
export SKYRIM_COOP_PORT="$HOST_RELAY_PORT"

echo "=== HOST: Launching Skyrim (relay port $HOST_RELAY_PORT, GameServer port $GAMESERVER_PORT) ==="

# Launch Skyrim via SKSE
cd "$SKYRIM_DIR"
"$PROTON_DIR/proton" run "$SKYRIM_DIR/skse64_loader.exe" &
LAUNCHER_PID=$!
echo "[INFO] SKSE launcher PID: $LAUNCHER_PID"

# Wait for relay DLL to listen
echo "[INFO] Waiting for relay DLL on port $HOST_RELAY_PORT..."
for i in $(seq 1 120); do
    if ss -tln 2>/dev/null | grep -q ":${HOST_RELAY_PORT} "; then
        echo "[PASS] Relay DLL listening on port $HOST_RELAY_PORT"
        break
    fi
    if [ "$i" -eq 120 ]; then
        echo "[FAIL] Relay DLL not listening after 120s"
        exit 1
    fi
    sleep 1
done

# Find Skyrim PID
SKYRIM_PID=$(ss -tlnp 2>/dev/null | grep ":${HOST_RELAY_PORT} " | grep -oP 'pid=\K[0-9]+' | head -1)
if [ -z "$SKYRIM_PID" ]; then
    SKYRIM_PID=$(pgrep -f "SkyrimSE.exe" | head -1)
fi
echo "[INFO] Skyrim PID: $SKYRIM_PID"

# Launch native client in host mode
RECORD_ARG=""
if [[ -n "$RECORD_FILE" ]]; then
    RECORD_ARG="--record $RECORD_FILE"
    ENET_DIR="${RECORD_FILE%.*}_enet"
    echo "[INFO] Recording hook events to: $RECORD_FILE"
    echo "[INFO] Recording enet6 traffic to: $ENET_DIR/"
fi

echo "[INFO] Launching skyrim-coop in HOST mode..."
"$NATIVE_BIN" --pid "$SKYRIM_PID" --port "$HOST_RELAY_PORT" --server-port "$GAMESERVER_PORT" $RECORD_ARG
