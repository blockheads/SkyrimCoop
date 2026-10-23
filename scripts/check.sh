#!/usr/bin/env bash
# The gate every change passes (docs/design/testing.md, tiers 1-2): build and run the native tests, and build the
# plugin. About 10-20 s incremental; the pre-push hook (scripts/hooks/pre-push) runs it.
#   scripts/check.sh            # everything
#   scripts/check.sh --no-dll   # the native tests only (no Windows toolchain)
# Skyrim's scenarios are not part of it: run the affected ones before a feature push (coop_test.py scenario).
set -uo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

DLL=1
for argument in "$@"; do
    case "$argument" in
        --no-dll) DLL=0 ;;
        *) echo "unknown argument: $argument" >&2; exit 2 ;;
    esac
done

LOGS="$ROOT/build/check"
mkdir -p "$LOGS"
FAILED=()
START=$SECONDS

# One step: its name, then the command; its output goes to build/check/<name>.log, shown only if it fails.
step() {
    local name="$1"; shift
    local log="$LOGS/${name// /-}.log"
    local begin=$SECONDS
    if "$@" > "$log" 2>&1; then
        printf '  %-24s ok    %3ds  %s\n' "$name" $((SECONDS - begin)) "$(grep -E "^All tests passed" "$log" | tail -n 1)"
    else
        printf '  %-24s FAIL  %3ds  (%s)\n' "$name" $((SECONDS - begin)) "${log#"$ROOT"/}"
        # The failed checks or the compiler's errors, else the end of the log.
        if grep -qE "FAILED:|error:|never happened" "$log"; then
            grep -E -A6 "FAILED:|error:|never happened" "$log" | head -n 40
        else
            tail -n 40 "$log"
        fi | sed 's/^/      /'
        FAILED+=("$name")
    fi
}

echo "check: tiers 1-2"
for target in ClientTests BridgeTests TPTests GameTests; do
    step "build $target" scripts/xmake-linux.sh build -j8 "$target"
    [[ " ${FAILED[*]-} " == *" build $target "* ]] || step "run $target" "build/linux/x86_64/debug/$target"
done
if (( DLL )); then
    step "build plugin" scripts/xmake-msvc.sh build -j8 SkyrimTogetherClientDLL
fi

if (( ${#FAILED[@]} )); then
    echo "check: FAILED (${FAILED[*]}) in $((SECONDS - START))s"
    exit 1
fi
echo "check: passed in $((SECONDS - START))s"
