#!/usr/bin/env bash
# Build build/re/SkyrimSE_1.6.1170.i64 headlessly: unpack SteamStub DRM (Steamless under
# Wine), auto-analyse, apply annotations. Takes a while (full analysis of a 37MB exe).
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
RE="$ROOT/build/re"
IDA_DIR="${IDA_DIR:-/media/bighass/4d2c4781-34de-41a5-8939-e35551bc9a5d1/apps/aur-ida-pro}"
GAME="${SKYRIM_GAME_DIR:-/media/bighass/BlackHole/SteamLibrary/steamapps/common/Skyrim Special Edition}"
EXE="$RE/SkyrimSE_1.6.1170_unpacked.exe"
DB="$RE/SkyrimSE_1.6.1170.i64"
mkdir -p "$RE/tools"

if [[ ! -f "$EXE" ]]; then
    if [[ ! -f "$RE/tools/steamless/Steamless.CLI.exe" ]]; then
        curl -sfL -o "$RE/tools/steamless.zip" \
            "https://github.com/atom0s/Steamless/releases/download/v3.1.0.5/Steamless.v3.1.0.5.-.by.atom0s.zip"
        unzip -o -q "$RE/tools/steamless.zip" -d "$RE/tools/steamless"
        cp "$RE/tools/steamless/Plugins/"{Steamless.API.dll,SharpDisasm.dll} "$RE/tools/steamless/"
    fi
    cp "$GAME/SkyrimSE.exe" "$RE/SkyrimSE_1.6.1170.exe"
    (cd "$RE/tools/steamless" && WINEPREFIX="$RE/wineprefix" WINEDEBUG=-all \
        wine Steamless.CLI.exe "Z:${RE//\//\\}\\SkyrimSE_1.6.1170.exe")
    mv "$RE/SkyrimSE_1.6.1170.exe.unpacked.exe" "$EXE"
fi

python3 "$ROOT/scripts/re/build_re_data.py"
[[ -e "$DB" ]] && { echo "$DB exists; open it in IDA and run scripts/re/ida_apply.py to refresh."; exit 0; }
echo "Analysing $EXE (log: $RE/ida.log)..."
COOP_IDA_BATCH=1 TERM=xterm "$IDA_DIR/idat" -A -c -L"$RE/ida.log" -o"$DB" \
    -S"$ROOT/scripts/re/ida_apply.py" "$EXE" </dev/null >/dev/null
echo "Done: $DB"
