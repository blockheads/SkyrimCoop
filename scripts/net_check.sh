#!/usr/bin/env bash
# Tier 2 under network conditions (docs/design/testing.md, "Network conditions"): GameTests with each guest's traffic
# through a relay that delays, jitters and drops it (COOP_NET). Minutes, not seconds: before a push that touches
# messages or their order, not in the gate. Profiles run one at a time (the host listens on one port).
#   scripts/net_check.sh                 # both profiles, every flow
#   scripts/net_check.sh "[items]"       # Catch2 filters pass through
set -uo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

PROFILES=("delay=50,jitter=30,loss=5,seed=1" "delay=120,jitter=80,loss=10,seed=2")
LOGS="$ROOT/build/check"
mkdir -p "$LOGS"
scripts/xmake-linux.sh build -j8 GameTests > "$LOGS/net-build.log" 2>&1 || { cat "$LOGS/net-build.log"; exit 1; }

FAILED=0
for profile in "${PROFILES[@]}"; do
    log="$LOGS/net-${profile//[,=]/-}.log"
    begin=$SECONDS
    if COOP_NET="$profile" build/linux/x86_64/debug/GameTests "$@" > "$log" 2>&1; then
        printf '  %-40s ok    %3ds  %s\n' "$profile" $((SECONDS - begin)) "$(grep -E "^All tests passed" "$log" | tail -n 1)"
    else
        printf '  %-40s FAIL  %3ds  %s\n' "$profile" $((SECONDS - begin)) "$log"
        grep -E "never happened|FAILED" -A2 "$log" | head -n 20
        FAILED=1
    fi
done
exit $FAILED
