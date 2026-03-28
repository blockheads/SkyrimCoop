#!/bin/bash
# deploy_and_test.sh — Build, deploy, and launch Skyrim with SkyrimCoop relay mod via Proton + SKSE
# The DLL writes a relay info file with port/pid; this script picks it up and launches the native binary.
set -e

# ── Paths ──
SKYRIM="/media/bighass/1f640250-798d-4f2f-87f2-c442109fc5d0/SteamLibrary/steamapps/common/Skyrim Special Edition"
PLUGINS="$SKYRIM/Data/SKSE/Plugins"
PROTON="/media/bighass/1f640250-798d-4f2f-87f2-c442109fc5d0/SteamLibrary/steamapps/common/Proton - Experimental"
COMPAT_DATA="/media/bighass/1f640250-798d-4f2f-87f2-c442109fc5d0/SteamLibrary/steamapps/compatdata/489830"
STEAM_DIR="/home/bighass/.steam/steam"
SKSE_LOG="$COMPAT_DATA/pfx/drive_c/users/steamuser/Documents/My Games/Skyrim Special Edition/SKSE"
DLL_LOG="$PLUGINS/skyrim_coop_hooks.log"
RELAY_INFO="$PLUGINS/skyrim_coop_relay.json"
PROJECT_DIR="$(cd "$(dirname "$0")" && pwd)"

# Skyrim SE Steam AppID
export SteamAppId=489830
export SteamGameId=489830
export STEAM_COMPAT_DATA_PATH="$COMPAT_DATA"
export STEAM_COMPAT_CLIENT_INSTALL_PATH="$STEAM_DIR"
export WINEPREFIX="$COMPAT_DATA/pfx"

usage() {
    echo "Usage: $0 [OPTIONS]"
    echo ""
    echo "Options:"
    echo "  --skip-build    Skip build step, just deploy + launch"
    echo "  --build-only    Build and deploy but don't launch"
    echo "  --launch-only   Just launch (no build, no deploy)"
    echo "  --kill          Kill running Skyrim + native process"
    echo "  --logs          Just show DLL + SKSE logs (no launch)"
    echo ""
    echo "Examples:"
    echo "  $0                    # Full: build, deploy, launch, connect native"
    echo "  $0 --skip-build      # Deploy last build + launch"
    echo "  $0 --kill            # Kill everything"
    echo "  $0 --logs            # Check DLL and SKSE logs"
}

DO_BUILD=true
DO_DEPLOY=true
DO_LAUNCH=true

for arg in "$@"; do
    case "$arg" in
        --skip-build)  DO_BUILD=false ;;
        --build-only)  DO_LAUNCH=false ;;
        --launch-only) DO_BUILD=false; DO_DEPLOY=false ;;
        --kill)
            echo "Killing Skyrim and skyrim-coop..."
            pkill -f "SkyrimSE.exe" 2>/dev/null && echo "  Killed SkyrimSE" || echo "  SkyrimSE not running"
            pkill -f "skse64_loader" 2>/dev/null || true
            pkill -f "skyrim-coop" 2>/dev/null && echo "  Killed skyrim-coop" || echo "  skyrim-coop not running"
            rm -f "$RELAY_INFO"
            exit 0
            ;;
        --logs)
            echo "=== DLL Log ==="
            if [ -f "$DLL_LOG" ]; then cat "$DLL_LOG"; else echo "(not found)"; fi
            echo ""
            echo "=== SKSE Log (plugin lines) ==="
            if [ -f "$SKSE_LOG/skse64.log" ]; then
                grep -i "skyrimcoop\|coop_hooks\|skyrim_coop\|loaded correctly\|couldn't load" "$SKSE_LOG/skse64.log" || echo "(no matches)"
            else echo "(not found)"; fi
            echo ""
            echo "=== Relay Info ==="
            if [ -f "$RELAY_INFO" ]; then cat "$RELAY_INFO"; else echo "(not found)"; fi
            exit 0
            ;;
        --help|-h) usage; exit 0 ;;
    esac
done

echo "=== SkyrimCoop Relay Deploy & Test ==="
echo ""

# ── Step 1: Build ──
if $DO_BUILD; then
    echo "[1/5] Building relay DLL (MinGW)..."
    cd "$PROJECT_DIR"
    xmake config -p mingw -m releasedbg -y >/dev/null 2>&1
    xmake build SkyrimCoopHooksDLL 2>&1 | grep -E "(error|warning|build ok)" || true

    echo "       Building native client (Linux)..."
    xmake config -p linux -m release -y >/dev/null 2>&1
    xmake build SkyrimCoopNative 2>&1 | grep -E "(error|warning|build ok)" || true
    echo ""
else
    echo "[1/5] Skipping build"
    echo ""
fi

# ── Step 2: Deploy ──
if $DO_DEPLOY; then
    echo "[2/5] Deploying to SKSE plugins..."
    DLL_SRC=$(find "$PROJECT_DIR/build" -name "skyrim_coop_hooks.dll" -type f 2>/dev/null | head -1)
    NATIVE_SRC=$(find "$PROJECT_DIR/build" -name "skyrim-coop" -type f -executable 2>/dev/null | head -1)

    if [ -z "$DLL_SRC" ]; then echo "  ERROR: skyrim_coop_hooks.dll not found"; exit 1; fi
    if [ -z "$NATIVE_SRC" ]; then echo "  ERROR: skyrim-coop not found"; exit 1; fi

    cp "$DLL_SRC" "$PLUGINS/skyrim_coop_hooks.dll"
    cp "$NATIVE_SRC" "$PLUGINS/skyrim-coop"
    chmod +x "$PLUGINS/skyrim-coop"

    # Clean stale relay info from previous run
    rm -f "$RELAY_INFO" "$DLL_LOG"

    echo "  skyrim_coop_hooks.dll: $(numfmt --to=iec $(stat -c%s "$PLUGINS/skyrim_coop_hooks.dll"))"
    echo "  skyrim-coop:           $(numfmt --to=iec $(stat -c%s "$PLUGINS/skyrim-coop"))"
    echo ""

    echo "[3/5] Verifying DLL exports..."
    if x86_64-w64-mingw32-objdump -x "$PLUGINS/skyrim_coop_hooks.dll" 2>/dev/null | grep -q "SKSEPlugin_Version"; then
        echo "  PASS: SKSEPlugin_Version"
    else
        echo "  FAIL: SKSEPlugin_Version not exported"; exit 1
    fi
    if x86_64-w64-mingw32-objdump -x "$PLUGINS/skyrim_coop_hooks.dll" 2>/dev/null | grep -q "SKSEPlugin_Load"; then
        echo "  PASS: SKSEPlugin_Load"
    else
        echo "  FAIL: SKSEPlugin_Load not exported"; exit 1
    fi
    echo ""
else
    echo "[2/5] Skipping deploy"
    echo "[3/5] Skipping verify"
    echo ""
fi

# ── Step 4: Launch Skyrim via Proton + SKSE ──
if ! $DO_LAUNCH; then
    echo "[4/5] Skipping launch"
    echo "[5/5] Skipping native connect"
    echo ""
    echo "Done. Run with no flags to build+deploy+launch."
    exit 0
fi

# Kill any existing instances first
pkill -f "SkyrimSE.exe" 2>/dev/null || true
pkill -f "skyrim-coop" 2>/dev/null || true
sleep 1

echo "[4/5] Launching Skyrim via Proton + SKSE..."
echo "  Proton: Experimental"
echo "  SKSE:   skse64_loader.exe"
echo ""

# Launch SKSE via Proton in background
cd "$SKYRIM"
"$PROTON/proton" run "$SKYRIM/skse64_loader.exe" >/dev/null 2>&1 &
PROTON_PID=$!
echo "  Proton PID: $PROTON_PID"

# Wait for Skyrim to start
echo "  Waiting for SkyrimSE.exe..."
SKYRIM_PID=""
for i in $(seq 1 30); do
    SKYRIM_PID=$(pgrep -f "SkyrimSE.exe" 2>/dev/null | head -1 || true)
    if [ -n "$SKYRIM_PID" ]; then
        echo "  SkyrimSE.exe running (PID: $SKYRIM_PID)"
        break
    fi
    sleep 1
    printf "."
done
echo ""

if [ -z "$SKYRIM_PID" ]; then
    echo "  ERROR: SkyrimSE.exe not detected after 30s"
    echo ""
    echo "  DLL log:"
    cat "$DLL_LOG" 2>/dev/null || echo "  (no log file)"
    exit 1
fi

# ── Step 5: Wait for DLL relay info, then launch native process ──
echo "[5/5] Waiting for DLL to write relay info..."
RELAY_PORT=""
RELAY_PID=""
for i in $(seq 1 30); do
    if [ -f "$RELAY_INFO" ]; then
        # Parse JSON (simple grep, no jq dependency)
        RELAY_PORT=$(grep -o '"port":[0-9]*' "$RELAY_INFO" | grep -o '[0-9]*')
        RELAY_PID=$(grep -o '"pid":[0-9]*' "$RELAY_INFO" | grep -o '[0-9]*')
        if [ -n "$RELAY_PORT" ] && [ -n "$RELAY_PID" ]; then
            echo "  Relay info found: port=$RELAY_PORT pid=$RELAY_PID"
            break
        fi
    fi
    sleep 1
    printf "."
done
echo ""

if [ -z "$RELAY_PORT" ]; then
    echo "  ERROR: DLL did not write relay info after 30s"
    echo ""
    echo "  DLL log:"
    cat "$DLL_LOG" 2>/dev/null || echo "  (no log file)"
    echo ""
    echo "  SKSE log (plugin lines):"
    grep -i "skyrimcoop\|coop_hooks\|skyrim_coop\|loaded\|couldn't" "$SKSE_LOG/skse64.log" 2>/dev/null || echo "  (none)"
    exit 1
fi

# Use the real Linux PID (from pgrep), not the Wine PID from the relay file.
# The Wine PID from GetCurrentProcessId() won't work for /proc/pid/mem.
REAL_PID="$SKYRIM_PID"
echo "  Launching native process: skyrim-coop --pid $REAL_PID --port $RELAY_PORT"
echo "  (Using Linux PID $REAL_PID, DLL reported Wine PID $RELAY_PID)"
"$PLUGINS/skyrim-coop" --pid "$REAL_PID" --port "$RELAY_PORT" &
NATIVE_PID=$!
echo "  skyrim-coop PID: $NATIVE_PID"
sleep 2

# Check if native process is still running
if kill -0 "$NATIVE_PID" 2>/dev/null; then
    echo "  skyrim-coop is running"
else
    echo "  WARNING: skyrim-coop exited early"
fi

echo ""
echo "=== Launched ==="
echo "  SkyrimSE.exe  PID: $SKYRIM_PID"
echo "  skyrim-coop   PID: $NATIVE_PID"
echo "  TCP port:      $RELAY_PORT"
echo "  Game PID:      $RELAY_PID"
echo ""
echo "=== DLL Log ==="
cat "$DLL_LOG" 2>/dev/null || echo "(no log file yet)"
echo ""
echo "Quick commands:"
echo "  $0 --kill          # Kill everything"
echo "  $0 --logs          # Check logs"
echo "  tail -f '$DLL_LOG' # Watch DLL log"
echo "  ps aux | grep -E 'skyrim|skse'  # Check processes"
