#!/usr/bin/env bash
# The gate every change passes (docs/design/testing.md, tiers 1-2): build and run the native tests, and build the
# plugin. About 5 s incremental; the pre-push hook (scripts/hooks/pre-push) runs it.
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

# GameTests in shards at once: its games wait on real timers, not the CPU. Each shard has its own ports; it fails
# if a shard fails or a case did not run.
SHARDS=8
game_tests() {
    local binary="build/linux/x86_64/debug/GameTests" dir="$LOGS/GameTests" pids=() failed=0 shard
    rm -rf "$dir" && mkdir -p "$dir"
    # Catch2 reads a comma in a name as "or": escaped, as are brackets and backslashes.
    "$binary" --list-test-names-only | grep . | sed 's/[],[\\]/\\&/g' \
        | awk -v dir="$dir" -v n="$SHARDS" '{ print > (dir "/" (NR % n) ".cases") }'
    for shard in "$dir"/*.cases; do
        GAMETESTS_PORT=$((21578 + 16 * $(basename "$shard" .cases))) \
            "$binary" --input-file "$shard" > "${shard%.cases}.log" 2>&1 &
        pids+=($!)
    done
    for pid in "${pids[@]}"; do wait "$pid" || failed=1; done
    local listed ran
    listed=$(cat "$dir"/*.cases | wc -l)
    ran=$(cat "$dir"/*.log | grep -oE "in [0-9]+ test cases|test cases: +[0-9]+" | grep -oE "[0-9]+" | paste -sd+ | bc)
    if (( failed )); then
        cat "$dir"/*.log
        return 1
    fi
    if (( ran != listed )); then
        echo "ran ${ran:-0} of $listed cases"
        return 1
    fi
    cat "$dir"/*.log | grep -oE "[0-9]+ assertions" | grep -oE "[0-9]+" | paste -sd+ | bc \
        | xargs -I{} echo "All tests passed ({} assertions in $ran test cases, $SHARDS shards)"
}

echo "check: tiers 1-2"
for target in ClientTests HostAuthorityTests TPTests GameTests; do
    step "build $target" scripts/xmake-linux.sh build -j8 "$target"
    [[ " ${FAILED[*]-} " == *" build $target "* ]] && continue
    if [[ $target == GameTests ]]; then
        step "run $target" game_tests
    else
        step "run $target" "build/linux/x86_64/debug/$target"
    fi
done
step "check harness comparisons" python3 -m unittest discover -s scripts -p test_world_diff.py
step "check late-save baseline" python3 -m unittest discover -s scripts -p test_late_baseline.py
step "check ragdoll comparisons" python3 -m unittest discover -s scripts -p test_ragdoll_trace.py
step "build UiWorkbench" scripts/xmake-linux.sh build -j8 UiWorkbench
if [[ " ${FAILED[*]-} " != *" build UiWorkbench "* ]]; then
    step "check UI fixtures" build/linux/x86_64/debug/UiWorkbench --check
fi
if (( DLL )); then
    step "build plugin" scripts/xmake-msvc.sh build -j8 SkyrimTogetherClientDLL
    step "build launcher" scripts/xmake-msvc.sh build -j8 SkyrimCoopLauncher
fi

if (( ${#FAILED[@]} )); then
    echo "check: FAILED (${FAILED[*]}) in $((SECONDS - START))s"
    exit 1
fi
echo "check: passed in $((SECONDS - START))s"
