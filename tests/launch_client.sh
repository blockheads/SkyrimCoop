#!/bin/bash
# launch_client.sh -- Launch client Skyrim + native client connecting to host
set -euo pipefail

LOG_DIR="$(cd "$(dirname "$0")/.." && pwd)/logs"
mkdir -p "$LOG_DIR"
LOG_FILE="$LOG_DIR/client_$(date +%Y%m%d_%H%M%S).log"
exec > >(tee -a "$LOG_FILE") 2>&1
echo "=== Log: $LOG_FILE ==="

SKYRIM_DIR="/media/bighass/1f640250-798d-4f2f-87f2-c442109fc5d0/SteamLibrary/steamapps/common/Skyrim Special Edition"
PROTON_DIR="/media/bighass/1f640250-798d-4f2f-87f2-c442109fc5d0/SteamLibrary/steamapps/common/Proton - Experimental"
COMPAT_HOST="/media/bighass/1f640250-798d-4f2f-87f2-c442109fc5d0/SteamLibrary/steamapps/compatdata/489830"
COMPAT_CLIENT="${COMPAT_HOST}_client"
PROJECT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
NATIVE_BIN="${PROJECT_DIR}/build/linux/x86_64/releasedbg/skyrim-coop"

CLIENT_RELAY_PORT=9668
HOST_ADDRESS="${1:-127.0.0.1:10578}"

SKYRIM_INI="$COMPAT_CLIENT/pfx/drive_c/users/steamuser/Documents/My Games/Skyrim Special Edition/Skyrim.ini"

export SteamAppId=489830
export SteamGameId=489830
export STEAM_COMPAT_CLIENT_INSTALL_PATH="/home/bighass/.steam/steam"
export STEAM_COMPAT_DATA_PATH="$COMPAT_CLIENT"
export WINEPREFIX="$COMPAT_CLIENT/pfx"
export SKYRIM_COOP_PORT="$CLIENT_RELAY_PORT"

echo "=== CLIENT: Launching Skyrim (relay port $CLIENT_RELAY_PORT, connecting to $HOST_ADDRESS) ==="

# Create client prefix if needed
if [ ! -d "$COMPAT_CLIENT" ]; then
    echo "[INFO] Creating client Proton prefix (copying from host)..."
    cp -a "$COMPAT_HOST" "$COMPAT_CLIENT"
    echo "[PASS] Client prefix created"
fi

# --- INI auto-configuration (bAlwaysActive + windowed mode) ---
if [[ -f "$SKYRIM_INI" ]]; then
    cp "$SKYRIM_INI" "${SKYRIM_INI}.client_backup"
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

trap 'if [[ -f "${SKYRIM_INI}.client_backup" ]]; then cp "${SKYRIM_INI}.client_backup" "$SKYRIM_INI"; rm -f "${SKYRIM_INI}.client_backup"; echo "[INFO] Restored Skyrim.ini"; fi' EXIT

# Launch Skyrim via SKSE
cd "$SKYRIM_DIR"
"$PROTON_DIR/proton" run "$SKYRIM_DIR/skse64_loader.exe" &
LAUNCHER_PID=$!
echo "[INFO] SKSE launcher PID: $LAUNCHER_PID"

# Wait for relay DLL to listen
echo "[INFO] Waiting for relay DLL on port $CLIENT_RELAY_PORT..."
for i in $(seq 1 120); do
    if ss -tln 2>/dev/null | grep -q ":${CLIENT_RELAY_PORT} "; then
        echo "[PASS] Relay DLL listening on port $CLIENT_RELAY_PORT"
        break
    fi
    if [ "$i" -eq 120 ]; then
        echo "[FAIL] Relay DLL not listening after 120s"
        exit 1
    fi
    sleep 1
done

# Find client Skyrim PID (the one on the client port, not the host port)
SKYRIM_PID=$(ss -tlnp 2>/dev/null | grep ":${CLIENT_RELAY_PORT} " | grep -oP 'pid=\K[0-9]+' | head -1)
if [ -z "$SKYRIM_PID" ]; then
    # Fallback: find SkyrimSE.exe that isn't the host
    HOST_PID=$(ss -tlnp 2>/dev/null | grep ":9667 " | grep -oP 'pid=\K[0-9]+' | head -1)
    for pid in $(pgrep -f "SkyrimSE.exe" 2>/dev/null); do
        if [ "$pid" != "$HOST_PID" ]; then
            SKYRIM_PID="$pid"
            break
        fi
    done
fi
echo "[INFO] Skyrim PID: $SKYRIM_PID"

# Launch native client in join mode
echo "[INFO] Launching skyrim-coop in CLIENT mode (connecting to $HOST_ADDRESS)..."
"$NATIVE_BIN" --pid "$SKYRIM_PID" --port "$CLIENT_RELAY_PORT" --connect "$HOST_ADDRESS"
