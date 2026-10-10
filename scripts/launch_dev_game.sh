#!/usr/bin/env bash
# Launch one SKSE game for scripts/hot_reload.py. No relay/native client needed.
set -euo pipefail

: "${SKYRIM_GAME_DIR:?Set SKYRIM_GAME_DIR to the Skyrim Special Edition directory}"
: "${SKYRIM_PROTON_DIR:?Set SKYRIM_PROTON_DIR to the Proton installation directory}"
: "${SKYRIM_COMPAT_DIR:?Set SKYRIM_COMPAT_DIR to your existing Steam compatdata/appid directory}"
: "${SKYRIM_STEAM_RUNTIME_DIR:?Set SKYRIM_STEAM_RUNTIME_DIR to the Steam Linux Runtime required by your Proton version}"

for required in "$SKYRIM_GAME_DIR/skse64_loader.exe" \
                "$SKYRIM_GAME_DIR/Data/SKSE/Plugins/SkyrimTogetherClient.dll" \
                "$SKYRIM_PROTON_DIR/proton" \
                "$SKYRIM_STEAM_RUNTIME_DIR/run"; do
    if [[ ! -f "$required" ]]; then
        echo "Missing: $required" >&2
        exit 1
    fi
done
if [[ ! -d "$SKYRIM_COMPAT_DIR/pfx" ]]; then
    echo "Use an existing Skyrim prefix containing your settings and saves." >&2
    exit 1
fi
if ! pgrep -x steam >/dev/null; then
    echo "Start Steam before launching SKSE through Proton." >&2
    exit 1
fi
if pgrep -ix 'SkyrimSE.exe' >/dev/null; then
    echo "Skyrim is already running. Use hot_reload.py instead of launching again." >&2
    exit 1
fi
if [[ -f "$SKYRIM_GAME_DIR/Data/SKSE/Plugins/skyrim_coop_hooks.dll" ]]; then
    echo "Move the old skyrim_coop_hooks.dll relay out of Plugins before this test." >&2
    exit 1
fi

export SteamAppId=489830 SteamGameId=489830
export STEAM_COMPAT_CLIENT_INSTALL_PATH="${STEAM_COMPAT_CLIENT_INSTALL_PATH:-$HOME/.steam/steam}"
export STEAM_COMPAT_DATA_PATH="$SKYRIM_COMPAT_DIR"
export WINEPREFIX="$SKYRIM_COMPAT_DIR/pfx"
cd "$SKYRIM_GAME_DIR"
exec "$SKYRIM_STEAM_RUNTIME_DIR/run" -- "$SKYRIM_PROTON_DIR/proton" run "$SKYRIM_GAME_DIR/skse64_loader.exe"
