#!/usr/bin/env bash
# ============================================================
# Matching Engine Platform — Pilot Start Script
# ============================================================
# Usage: ./start.sh [config_dir]
# Default config_dir: ../config
# ============================================================
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CONFIG_DIR="${1:-${SCRIPT_DIR}/../config}"
BUILD_DIR="${SCRIPT_DIR}/../../build"
DATA_DIR="${SCRIPT_DIR}/../data"
LOG_DIR="${SCRIPT_DIR}/../logs"
PID_DIR="${SCRIPT_DIR}/../run"

# ─── Preflight checks ─────────────────────────────────────
echo "=== Matching Engine Pilot — Startup ==="
echo "Config:  ${CONFIG_DIR}"
echo "Build:   ${BUILD_DIR}"
echo "Data:    ${DATA_DIR}"
echo "Logs:    ${LOG_DIR}"
echo ""

if [ ! -f "${CONFIG_DIR}/platform.conf" ]; then
    echo "ERROR: ${CONFIG_DIR}/platform.conf not found"
    exit 1
fi

if [ ! -f "${CONFIG_DIR}/instruments.json" ]; then
    echo "ERROR: ${CONFIG_DIR}/instruments.json not found"
    exit 1
fi

# ─── Create directories ───────────────────────────────────
mkdir -p "${DATA_DIR}/journal"
mkdir -p "${DATA_DIR}/snapshots"
mkdir -p "${DATA_DIR}/eod"
mkdir -p "${DATA_DIR}/fix-sessions"
mkdir -p "${LOG_DIR}"
mkdir -p "${PID_DIR}"

# ─── Verify build exists ──────────────────────────────────
echo "Checking build artifacts..."

REQUIRED_TESTS=(
    "engine/core/engine-core-tests"
    "risk/risk-service-tests"
    "gateway/fix/fix-gateway-tests"
)

for test_bin in "${REQUIRED_TESTS[@]}"; do
    if [ ! -x "${BUILD_DIR}/${test_bin}" ]; then
        echo "WARNING: ${test_bin} not found in build dir"
        echo "Run: cd ${BUILD_DIR} && cmake .. && make -j\$(nproc)"
    fi
done

# ─── Run smoke tests ──────────────────────────────────────
echo ""
echo "Running smoke tests..."
SMOKE_OK=true

for test_bin in "${REQUIRED_TESTS[@]}"; do
    if [ -x "${BUILD_DIR}/${test_bin}" ]; then
        echo -n "  ${test_bin}... "
        if "${BUILD_DIR}/${test_bin}" --gtest_brief=1 > "${LOG_DIR}/smoke_$(basename ${test_bin}).log" 2>&1; then
            echo "PASS"
        else
            echo "FAIL (see ${LOG_DIR}/smoke_$(basename ${test_bin}).log)"
            SMOKE_OK=false
        fi
    fi
done

if [ "${SMOKE_OK}" = false ]; then
    echo ""
    echo "ERROR: Smoke tests failed. Resolve before starting."
    exit 1
fi

echo ""
echo "=== Smoke tests passed ==="
echo ""

# ─── Display startup summary ──────────────────────────────
echo "Platform configuration:"
echo "  Partition ID:     1"
echo "  FIX port:         9876"
echo "  Market data port: 9877"
echo "  Ops HTTP port:    8080"
echo "  Journal:          ${DATA_DIR}/journal (segmented, sync)"
echo "  Snapshots:        ${DATA_DIR}/snapshots"
echo "  EOD output:       ${DATA_DIR}/eod"
echo ""
echo "=== Platform ready ==="
echo ""

# ─── Launch daemon if available ────────────────────────────
DAEMON_BIN="${BUILD_DIR}/daemon/matching-engine-daemon"
if [ -x "${DAEMON_BIN}" ]; then
    echo "Starting matching-engine-daemon..."
    "${DAEMON_BIN}" "${CONFIG_DIR}/platform.conf" &
    DAEMON_PID=$!
    echo "${DAEMON_PID}" > "${PID_DIR}/daemon.pid"
    echo "  Daemon PID: ${DAEMON_PID}"
    echo "  PID file:   ${PID_DIR}/daemon.pid"
    echo ""
    echo "To stop: ./stop.sh  or  kill ${DAEMON_PID}"
    echo ""
    echo "To run certification:"
    echo "  cd ${BUILD_DIR} && ./certification/cert-harness-tests"
else
    echo "NOTE: Daemon binary not found at ${DAEMON_BIN}"
    echo "The platform can still be exercised via test harnesses."
    echo ""
    echo "To run the full test suite:"
    echo "  cd ${BUILD_DIR} && ctest --output-on-failure"
    echo ""
    echo "To run certification tests:"
    echo "  cd ${BUILD_DIR} && ./certification/cert-harness-tests"
fi
