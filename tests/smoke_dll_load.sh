#!/bin/bash
# smoke_dll_load.sh — Deploy SkyrimTogetherClient.dll and verify SKSE loads it
#
# v2.0 smoke test: no relay, no native binary — just the single DLL.
# Deploys the MinGW-built DLL to SKSE plugins, launches Skyrim via Proton,
# checks SKSE log for plugin load confirmation, then kills Skyrim.
#
# Usage:
#   ./smoke_dll_load.sh                # Full: deploy + launch + verify + kill
#   ./smoke_dll_load.sh --skip-deploy  # Launch with whatever DLL is already deployed
#   ./smoke_dll_load.sh --check-only   # Just check exports and DLL size (no Skyrim)
#   ./smoke_dll_load.sh --kill         # Kill running Skyrim instances
set -euo pipefail

# ── Paths ──
SKYRIM_DIR="/media/bighass/1f640250-798d-4f2f-87f2-c442109fc5d0/SteamLibrary/steamapps/common/Skyrim Special Edition"
PLUGINS_DIR="$SKYRIM_DIR/Data/SKSE/Plugins"
PROTON_DIR="/media/bighass/1f640250-798d-4f2f-87f2-c442109fc5d0/SteamLibrary/steamapps/common/Proton - Experimental"
COMPAT_DATA="/media/bighass/1f640250-798d-4f2f-87f2-c442109fc5d0/SteamLibrary/steamapps/compatdata/489830"
STEAM_DIR="/home/bighass/.steam/steam"
SKSE_LOG_DIR="$COMPAT_DATA/pfx/drive_c/users/steamuser/Documents/My Games/Skyrim Special Edition/SKSE"
PROJECT_DIR="$(cd "$(dirname "$0")/.." && pwd)"

# Build output — prefer releasedbg (self-contained), fall back to devbuild
if [ -f "$PROJECT_DIR/build/mingw/x86_64/releasedbg/SkyrimTogetherClient.dll" ]; then
    DLL_SRC="$PROJECT_DIR/build/mingw/x86_64/releasedbg/SkyrimTogetherClient.dll"
elif [ -f "$PROJECT_DIR/build/mingw/x86_64/devbuild/SkyrimTogetherClient.dll" ]; then
    DLL_SRC="$PROJECT_DIR/build/mingw/x86_64/devbuild/SkyrimTogetherClient.dll"
    echo "  WARN: Using devbuild DLL (has shared lib deps — may fail to load in SKSE)"
else
    DLL_SRC="$PROJECT_DIR/build/mingw/x86_64/releasedbg/SkyrimTogetherClient.dll"
fi

# Skyrim SE Steam AppID
export SteamAppId=489830
export SteamGameId=489830
export STEAM_COMPAT_DATA_PATH="$COMPAT_DATA"
export STEAM_COMPAT_CLIENT_INSTALL_PATH="$STEAM_DIR"
export WINEPREFIX="$COMPAT_DATA/pfx"

# How long to wait for Skyrim to start (seconds)
SKYRIM_WAIT=60
# How long to wait for SKSE log after Skyrim starts (seconds)
LOG_WAIT=30

# ── Argument parsing ──
DO_DEPLOY=true
DO_LAUNCH=true
CHECK_ONLY=false

for arg in "$@"; do
    case "$arg" in
        --skip-deploy) DO_DEPLOY=false ;;
        --check-only)  CHECK_ONLY=true; DO_LAUNCH=false ;;
        --kill)
            echo "Killing Skyrim instances..."
            pkill -f "SkyrimSE.exe" 2>/dev/null && echo "  Killed SkyrimSE" || echo "  SkyrimSE not running"
            pkill -f "skse64_loader" 2>/dev/null || true
            exit 0
            ;;
        --help|-h)
            head -13 "$0" | tail -10
            exit 0
            ;;
    esac
done

echo "=== SkyrimCoop v2.0 DLL Smoke Test ==="
echo ""

# ── Step 1: Check DLL exists and verify exports ──
echo "[1/4] Checking DLL..."

if [ ! -f "$DLL_SRC" ]; then
    echo "  FAIL: DLL not found at $DLL_SRC"
    echo "  Run: xmake f -p mingw -m devbuild -y && xmake build SkyrimTogetherClientDLL"
    exit 1
fi

DLL_SIZE=$(stat -c%s "$DLL_SRC")
DLL_SIZE_HUMAN=$(numfmt --to=iec "$DLL_SIZE")
echo "  DLL: $DLL_SRC ($DLL_SIZE_HUMAN)"

# Check SKSE exports via objdump export table (works on both debug and stripped DLLs)
EXPORTS_OK=true
EXPORT_DUMP=$(x86_64-w64-mingw32-objdump -x "$DLL_SRC" 2>/dev/null | grep -A 5 "Ordinal/Name Pointer" || true)
for EXPORT in SKSEPlugin_Version SKSEPlugin_Load; do
    if echo "$EXPORT_DUMP" | grep -qw "$EXPORT"; then
        echo "  PASS: $EXPORT exported"
    else
        echo "  FAIL: $EXPORT NOT exported"
        EXPORTS_OK=false
    fi
done

if [ "$EXPORTS_OK" = false ]; then
    echo ""
    echo "  DLL exports are broken. Check skse_entry.cpp and link flags."
    exit 1
fi

if $CHECK_ONLY; then
    echo ""
    echo "=== Check-only complete. DLL looks good. ==="
    exit 0
fi

# ── Step 2: Deploy DLL ──
if $DO_DEPLOY; then
    echo ""
    echo "[2/4] Deploying to SKSE plugins..."
    mkdir -p "$PLUGINS_DIR"
    cp "$DLL_SRC" "$PLUGINS_DIR/SkyrimTogetherClient.dll"
    DEPLOYED_SIZE=$(stat -c%s "$PLUGINS_DIR/SkyrimTogetherClient.dll")
    echo "  Deployed: $PLUGINS_DIR/SkyrimTogetherClient.dll ($(numfmt --to=iec $DEPLOYED_SIZE))"
else
    echo ""
    echo "[2/4] Skipping deploy (--skip-deploy)"
    if [ ! -f "$PLUGINS_DIR/SkyrimTogetherClient.dll" ]; then
        echo "  WARN: No DLL deployed at $PLUGINS_DIR/SkyrimTogetherClient.dll"
    fi
fi

# ── Step 3: Launch Skyrim via Proton + SKSE ──
echo ""
echo "[3/4] Launching Skyrim via Proton + SKSE..."

# Kill any existing instances
pkill -f "SkyrimSE.exe" 2>/dev/null || true
pkill -f "skse64_loader" 2>/dev/null || true
sleep 1

# Clear old SKSE log so we get a fresh one
rm -f "$SKSE_LOG_DIR/skse64.log" 2>/dev/null || true

cd "$SKYRIM_DIR"
"$PROTON_DIR/proton" run "$SKYRIM_DIR/skse64_loader.exe" >/dev/null 2>&1 &
PROTON_PID=$!
echo "  Proton PID: $PROTON_PID"

# Wait for SkyrimSE.exe to appear
echo "  Waiting for SkyrimSE.exe (up to ${SKYRIM_WAIT}s)..."
SKYRIM_PID=""
for i in $(seq 1 "$SKYRIM_WAIT"); do
    SKYRIM_PID=$(pgrep -f "SkyrimSE.exe" 2>/dev/null | head -1 || true)
    if [ -n "$SKYRIM_PID" ]; then
        echo "  SkyrimSE.exe running (PID: $SKYRIM_PID) after ${i}s"
        break
    fi
    sleep 1
done

if [ -z "$SKYRIM_PID" ]; then
    echo "  FAIL: SkyrimSE.exe not detected after ${SKYRIM_WAIT}s"
    exit 1
fi

# ── Step 4: Check SKSE log for plugin load ──
echo ""
echo "[4/4] Waiting for SKSE log (up to ${LOG_WAIT}s)..."

PLUGIN_LOADED=false
PLUGIN_FAILED=false
for i in $(seq 1 "$LOG_WAIT"); do
    if [ -f "$SKSE_LOG_DIR/skse64.log" ]; then
        # Check for successful load
        if grep -qi "SkyrimTogether\|SkyrimTogetherClient" "$SKSE_LOG_DIR/skse64.log" 2>/dev/null; then
            echo "  Found plugin reference in SKSE log after ${i}s"

            # Check for explicit load failure
            if grep -qi "couldn't load.*SkyrimTogether\|failed.*SkyrimTogether" "$SKSE_LOG_DIR/skse64.log" 2>/dev/null; then
                PLUGIN_FAILED=true
            else
                PLUGIN_LOADED=true
            fi
            break
        fi

        # Also check if SKSE finished loading all plugins (even if ours isn't mentioned)
        if grep -qi "init complete" "$SKSE_LOG_DIR/skse64.log" 2>/dev/null; then
            echo "  SKSE init complete after ${i}s (checking for our plugin...)"
            if grep -qi "SkyrimTogether\|SkyrimTogetherClient" "$SKSE_LOG_DIR/skse64.log" 2>/dev/null; then
                PLUGIN_LOADED=true
            fi
            break
        fi
    fi
    sleep 1
done

# Show the relevant log lines
echo ""
echo "=== SKSE Log ==="
if [ -f "$SKSE_LOG_DIR/skse64.log" ]; then
    # Show full log (usually short)
    cat "$SKSE_LOG_DIR/skse64.log"
else
    echo "(no SKSE log found)"
fi

# Check if Skyrim is still running (didn't crash)
echo ""
STILL_RUNNING=false
if kill -0 "$SKYRIM_PID" 2>/dev/null; then
    STILL_RUNNING=true
    echo "SkyrimSE.exe still running (no crash) ✓"
else
    echo "SkyrimSE.exe has exited (possible crash)"
fi

# Kill Skyrim
echo ""
echo "Killing Skyrim..."
pkill -f "SkyrimSE.exe" 2>/dev/null || true
pkill -f "skse64_loader" 2>/dev/null || true
sleep 2

# ── Result ──
echo ""
echo "=== Result ==="
if $PLUGIN_FAILED; then
    echo "FAIL: SKSE found the DLL but couldn't load it"
    echo "Check the SKSE log above for details"
    exit 1
elif $PLUGIN_LOADED && $STILL_RUNNING; then
    echo "PASS: DLL loaded by SKSE, Skyrim did not crash"
    exit 0
elif $PLUGIN_LOADED && ! $STILL_RUNNING; then
    echo "WARN: DLL was loaded but Skyrim exited (may have crashed during init)"
    echo "Check SKSE log and Skyrim crash logs"
    exit 1
elif ! $PLUGIN_LOADED && $STILL_RUNNING; then
    echo "WARN: Skyrim running but plugin not confirmed in SKSE log"
    echo "The DLL may have loaded without logging, or SKSE may not have found it"
    exit 1
else
    echo "FAIL: Skyrim exited and plugin load not confirmed"
    exit 1
fi
