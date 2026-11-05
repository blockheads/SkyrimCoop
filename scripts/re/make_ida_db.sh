#!/usr/bin/env bash
# Build a separate runtime-specific IDA database headlessly: unpack SteamStub DRM (Steamless under
# Wine), auto-analyse, apply annotations and replay the RE journals (ida_replay.py). Takes a while (full analysis of a 37MB exe).
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
RE="$ROOT/build/re"
IDA_DIR="${IDA_DIR:-/media/bighass/4d2c4781-34de-41a5-8939-e35551bc9a5d1/apps/aur-ida-pro}"
GAME="${SKYRIM_GAME_DIR:-/media/bighass/1f640250-798d-4f2f-87f2-c442109fc5d0/SteamLibrary/steamapps/common/Skyrim Special Edition}"
VERSION="$(python3 -c 'import sys; sys.path.insert(0, sys.argv[1]); from skyrim_runtime import runtime_version; print(runtime_version().removesuffix(".0"))' "$ROOT/scripts")"
EXE="$RE/SkyrimSE_${VERSION}_unpacked.exe"
DB="$RE/SkyrimSE_${VERSION}.i64"
mkdir -p "$RE/tools"

if [[ ! -f "$EXE" ]]; then
    if [[ ! -f "$RE/tools/steamless/Steamless.CLI.exe" ]]; then
        curl -sfL -o "$RE/tools/steamless.zip" \
            "https://github.com/atom0s/Steamless/releases/download/v3.1.0.5/Steamless.v3.1.0.5.-.by.atom0s.zip"
        unzip -o -q "$RE/tools/steamless.zip" -d "$RE/tools/steamless"
        cp "$RE/tools/steamless/Plugins/"{Steamless.API.dll,SharpDisasm.dll} "$RE/tools/steamless/"
    fi
    SOURCE="$(python3 -c 'import sys; sys.path.insert(0, sys.argv[1]); from skyrim_runtime import game_executable; print(game_executable(sys.argv[2]))' "$ROOT/scripts" "$GAME")"
    cp "$SOURCE" "$RE/SkyrimSE_${VERSION}.exe"
    (cd "$RE/tools/steamless" && WINEPREFIX="$RE/wineprefix" WINEDEBUG=-all \
        wine Steamless.CLI.exe "Z:${RE//\//\\}\\SkyrimSE_${VERSION}.exe")
    mv "$RE/SkyrimSE_${VERSION}.exe.unpacked.exe" "$EXE"
fi

python3 "$ROOT/scripts/re/build_re_data.py"
[[ -e "${DB%.i64}.id0" ]] && { echo "Database is open or analysis is running: $DB" >&2; exit 1; }
[[ -e "$DB" ]] && { echo "$DB exists; open it in IDA and run scripts/re/ida_apply.py to refresh."; exit 0; }
echo "Analysing $EXE (log: $RE/ida.log)..."
COOP_IDA_BATCH=1 TERM=xterm "$IDA_DIR/idat" -A -c -L"$RE/ida-${VERSION}.log" -o"$DB" \
    -S"$ROOT/scripts/re/ida_replay.py" "$EXE" </dev/null >/dev/null
echo "Done: $DB"
