#!/usr/bin/env bash
# Fast, game-free UI gate. Uses private Xvfb displays; never connects to or modifies a Skyrim process.
set -euo pipefail
TASK_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$TASK_ROOT"
BUILD=1
SHOTS=0
for argument in "$@"; do
    case "$argument" in
        --no-build) BUILD=0 ;;
        --shots) SHOTS=1 ;;
        *) echo "usage: scripts/check-ui.sh [--no-build] [--shots]" >&2; exit 2 ;;
    esac
done
START=$SECONDS
mkdir -p build/check-ui
if (( BUILD )); then
    if ! scripts/xmake-linux.sh build -j8 UiWorkbench > build/check-ui/build.log 2>&1; then
        cat build/check-ui/build.log
        exit 1
    fi
fi
for size in 1280x720 1920x1080 2560x1440; do
    if ! build/linux/x86_64/debug/UiWorkbench --check --size "$size" > "build/check-ui/$size.log" 2>&1; then
        cat "build/check-ui/$size.log"
        exit 1
    fi
    printf 'UI %s: fixtures, input, colors and HUD checks passed\n' "$size"
done
if (( SHOTS )); then
    mkdir -p build/ui-previews
    for fixture in party-colors party-mood-dead join-receiving-world; do
        build/linux/x86_64/debug/UiWorkbench --shot "$fixture" "build/ui-previews/$fixture.png" --size 1920x1080
    done
    echo 'Previews: build/ui-previews/'
fi
echo "UI smoke checks passed in $((SECONDS - START))s"
