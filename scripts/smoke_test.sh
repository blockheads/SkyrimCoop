#!/usr/bin/env bash
# smoke_test.sh - Headless connection smoke test for skyrim-coop
#
# Tests:
#   1. Server starts successfully in --host-only mode
#   2. Replay client processes events from a session ndjson file (if available)
#
# Usage: bash scripts/smoke_test.sh
# Exit:  0 = PASS, 1 = FAIL

set -euo pipefail

# ---------------------------------------------------------------------------
# Setup
# ---------------------------------------------------------------------------
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"

# Binary preference: project root first, then build output (build output may
# have newer features such as --replay support).
if [[ -x "${PROJECT_ROOT}/skyrim-coop" ]]; then
    BINARY="${PROJECT_ROOT}/skyrim-coop"
elif [[ -x "${PROJECT_ROOT}/build/linux/x86_64/releasedbg/skyrim-coop" ]]; then
    BINARY="${PROJECT_ROOT}/build/linux/x86_64/releasedbg/skyrim-coop"
else
    echo "FAIL: skyrim-coop binary not found at project root or build output"
    exit 1
fi

# Replay fixture: prefer a hook-event session capture if available, then fall
# back to the enet auth_flow fixture (which replays 0 hook events but still
# exercises the replay code path without crashing).
if [[ -f "${PROJECT_ROOT}/session.ndjson" ]]; then
    REPLAY_FILE="${PROJECT_ROOT}/session.ndjson"
elif [[ -f "${PROJECT_ROOT}/tests/fixtures/replay/enet/auth_flow.ndjson" ]]; then
    REPLAY_FILE="${PROJECT_ROOT}/tests/fixtures/replay/enet/auth_flow.ndjson"
else
    REPLAY_FILE=""
fi

SERVER_PORT=10579  # Use non-default port to avoid conflicts

LOG_DIR="$(mktemp -d /tmp/smoke_test_XXXXXX)"
SERVER_LOG="${LOG_DIR}/server.log"
CLIENT_LOG="${LOG_DIR}/client.log"
SERVER_PID=""
CLIENT_PID=""
PASS=0

# ---------------------------------------------------------------------------
# Cleanup trap
# ---------------------------------------------------------------------------
cleanup() {
    if [[ -n "${SERVER_PID}" ]]; then
        kill -TERM "${SERVER_PID}" 2>/dev/null || true
        wait "${SERVER_PID}" 2>/dev/null || true
    fi
    if [[ -n "${CLIENT_PID}" ]]; then
        kill -TERM "${CLIENT_PID}" 2>/dev/null || true
        wait "${CLIENT_PID}" 2>/dev/null || true
    fi
    if [[ "${PASS}" -eq 1 ]]; then
        echo ""
        echo "--- Server log (${SERVER_LOG}) ---"
        cat "${SERVER_LOG}" 2>/dev/null || true
        if [[ -n "${REPLAY_FILE}" && -f "${CLIENT_LOG}" ]]; then
            echo ""
            echo "--- Client log (${CLIENT_LOG}) ---"
            cat "${CLIENT_LOG}" 2>/dev/null || true
        fi
    fi
    rm -rf "${LOG_DIR}"
}
trap cleanup EXIT

# ---------------------------------------------------------------------------
# Banner
# ---------------------------------------------------------------------------
echo "================================================================"
echo "  skyrim-coop smoke test"
echo "  Binary : ${BINARY}"
echo "  Replay : ${REPLAY_FILE:-<skipped>}"
echo "  Logs   : ${LOG_DIR}"
echo "================================================================"
echo ""

# ---------------------------------------------------------------------------
# TEST 1: Server starts in --host-only mode
# ---------------------------------------------------------------------------
echo "[1/2] Starting server in --host-only mode on port ${SERVER_PORT}..."

"${BINARY}" --host-only --server-port "${SERVER_PORT}" \
    >"${SERVER_LOG}" 2>&1 &
SERVER_PID=$!

# Wait up to 5 seconds for the server to write its startup message
SERVER_OK=0
for i in $(seq 1 10); do
    sleep 0.5
    if grep -q "GameServer listening on port" "${SERVER_LOG}" 2>/dev/null; then
        SERVER_OK=1
        break
    fi
    # Also accept the enet networking line as proof of startup
    if grep -q "SERVER: Listening on port" "${SERVER_LOG}" 2>/dev/null; then
        SERVER_OK=1
        break
    fi
done

if [[ "${SERVER_OK}" -eq 0 ]]; then
    echo ""
    echo "--- Server log ---"
    cat "${SERVER_LOG}" 2>/dev/null || true
    echo ""
    echo "FAIL: Server did not report startup within 5 seconds"
    exit 1
fi

echo "  Server started OK (PID=${SERVER_PID})"

# Shut server down before starting replay (avoids port conflicts)
kill -TERM "${SERVER_PID}" 2>/dev/null || true
wait "${SERVER_PID}" 2>/dev/null || true
SERVER_PID=""

# ---------------------------------------------------------------------------
# TEST 2: Replay client processes events (if replay binary supports --replay)
# ---------------------------------------------------------------------------
REPLAY_OK=0
REPLAY_SKIP=0

if [[ -z "${REPLAY_FILE}" ]]; then
    echo ""
    echo "[2/2] Replay test: SKIPPED (no replay fixture found)"
    REPLAY_SKIP=1
else
    # Check if this binary supports --replay by checking help output
    REPLAY_SUPPORTED=0
    if "${BINARY}" --help 2>&1 | grep -q "\-\-replay"; then
        REPLAY_SUPPORTED=1
    fi

    if [[ "${REPLAY_SUPPORTED}" -eq 0 ]]; then
        echo ""
        echo "[2/2] Replay test: SKIPPED (binary does not support --replay; rebuild to enable)"
        REPLAY_SKIP=1
    else
        echo ""
        echo "[2/2] Running replay client against: ${REPLAY_FILE}"

        # Replay mode exits on its own; cap with timeout just in case
        timeout 30 "${BINARY}" --replay "${REPLAY_FILE}" \
            >"${CLIENT_LOG}" 2>&1 &
        CLIENT_PID=$!

        # Wait for replay to finish (replay mode exits on its own)
        wait "${CLIENT_PID}" 2>/dev/null || true
        REPLAY_EXIT=$?
        CLIENT_PID=""

        if grep -q "Replayed" "${CLIENT_LOG}" 2>/dev/null; then
            # Extract event count from "Replayed N events from ..."
            COUNT=$(grep "Replayed" "${CLIENT_LOG}" | grep -oP 'Replayed \K[0-9]+' || echo "0")
            echo "  Replayed ${COUNT} events"
            # Success if the replay ran without crashing (exit 0), even with 0
            # events (enet fixtures are not hook-event captures).
            if [[ "${REPLAY_EXIT}" -eq 0 || "${COUNT}" -gt 0 ]]; then
                REPLAY_OK=1
            else
                echo "  WARNING: Replay exited non-zero (${REPLAY_EXIT}) and processed 0 events"
            fi
        else
            echo "  WARNING: 'Replayed' not found in client log"
        fi

        if [[ "${REPLAY_OK}" -eq 0 ]]; then
            PASS=1  # Enable log dump in cleanup
            echo ""
            echo "--- Client log ---"
            cat "${CLIENT_LOG}" 2>/dev/null || true
            echo ""
            echo "FAIL: Replay client did not complete successfully"
            exit 1
        fi

        echo "  Replay client completed OK"
    fi
fi

# ---------------------------------------------------------------------------
# Result
# ---------------------------------------------------------------------------
echo ""
echo "================================================================"
if [[ "${REPLAY_SKIP}" -eq 1 ]]; then
    echo "  SUCCESS: Server startup test passed. Replay skipped."
else
    echo "  SUCCESS: Server startup and replay client both passed."
fi
echo "================================================================"
exit 0
