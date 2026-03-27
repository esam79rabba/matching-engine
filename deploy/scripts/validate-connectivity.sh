#!/usr/bin/env bash
# ============================================================
# Matching Engine — Participant Connectivity Validation
# ============================================================
# Usage: ./validate-connectivity.sh [host] [fix_port] [md_port] [ops_port]
#
# Validates that a participant can reach the matching engine and
# complete basic technical handshakes. Run from the participant's
# network, or from the venue host for self-check.
#
# Exit code: 0 = all checks pass, 1 = one or more failures
# ============================================================
set -euo pipefail

HOST="${1:-127.0.0.1}"
FIX_PORT="${2:-9876}"
MD_PORT="${3:-9877}"
OPS_PORT="${4:-8080}"
REPORT_FILE="${5:-connectivity-report.txt}"

PASS=0
FAIL=0
TOTAL=0

# ─── Helpers ──────────────────────────────────────────────
check() {
    local name="$1"
    local result="$2"
    local detail="$3"
    TOTAL=$((TOTAL + 1))
    if [ "$result" = "PASS" ]; then
        PASS=$((PASS + 1))
        echo "  [PASS] ${name}"
    else
        FAIL=$((FAIL + 1))
        echo "  [FAIL] ${name} — ${detail}"
    fi
    echo "${name}|${result}|${detail}" >> "${REPORT_FILE}"
}

# ─── Start ────────────────────────────────────────────────
echo "============================================"
echo "  Connectivity Validation"
echo "============================================"
echo "  Host:      ${HOST}"
echo "  FIX port:  ${FIX_PORT}"
echo "  MD port:   ${MD_PORT}"
echo "  Ops port:  ${OPS_PORT}"
echo "  Report:    ${REPORT_FILE}"
echo "============================================"
echo ""

# Clear report file
echo "# Connectivity Validation Report" > "${REPORT_FILE}"
echo "# Host: ${HOST}" >> "${REPORT_FILE}"
echo "# Date: $(date -u '+%Y-%m-%dT%H:%M:%SZ')" >> "${REPORT_FILE}"
echo "# Format: check|result|detail" >> "${REPORT_FILE}"

# ─── 1. TCP Connectivity ──────────────────────────────────
echo "1. TCP Connectivity"

# FIX port
if timeout 5 bash -c "echo > /dev/tcp/${HOST}/${FIX_PORT}" 2>/dev/null; then
    check "FIX TCP (${HOST}:${FIX_PORT})" "PASS" "Port reachable"
else
    check "FIX TCP (${HOST}:${FIX_PORT})" "FAIL" "Port unreachable or timeout"
fi

# Market data port
if timeout 5 bash -c "echo > /dev/tcp/${HOST}/${MD_PORT}" 2>/dev/null; then
    check "Market Data TCP (${HOST}:${MD_PORT})" "PASS" "Port reachable"
else
    check "Market Data TCP (${HOST}:${MD_PORT})" "FAIL" "Port unreachable or timeout"
fi

# Ops HTTP port
if timeout 5 bash -c "echo > /dev/tcp/${HOST}/${OPS_PORT}" 2>/dev/null; then
    check "Ops HTTP TCP (${HOST}:${OPS_PORT})" "PASS" "Port reachable"
else
    check "Ops HTTP TCP (${HOST}:${OPS_PORT})" "FAIL" "Port unreachable or timeout"
fi

echo ""

# ─── 2. HTTP Health Check ─────────────────────────────────
echo "2. HTTP Health Check"

HEALTH_RESP=$(curl -s -o /dev/null -w "%{http_code}" --connect-timeout 5 "http://${HOST}:${OPS_PORT}/health" 2>/dev/null || echo "000")
if [ "$HEALTH_RESP" = "200" ]; then
    check "Health endpoint" "PASS" "HTTP 200"
else
    check "Health endpoint" "FAIL" "HTTP ${HEALTH_RESP}"
fi

echo ""

# ─── 3. Instrument List ───────────────────────────────────
echo "3. Reference Data"

INST_RESP=$(curl -s --connect-timeout 5 "http://${HOST}:${OPS_PORT}/api/v1/instruments" 2>/dev/null || echo "")
if [ -n "$INST_RESP" ] && echo "$INST_RESP" | grep -q "instrument"; then
    INST_COUNT=$(echo "$INST_RESP" | grep -o "instrument_id" | wc -l | tr -d ' ')
    check "Instrument list" "PASS" "${INST_COUNT} instruments found"
else
    check "Instrument list" "FAIL" "No instruments or endpoint unreachable"
fi

echo ""

# ─── 4. Metrics Endpoint ──────────────────────────────────
echo "4. Metrics"

METRICS_RESP=$(curl -s --connect-timeout 5 "http://${HOST}:${OPS_PORT}/api/v1/metrics" 2>/dev/null || echo "")
if [ -n "$METRICS_RESP" ] && echo "$METRICS_RESP" | grep -q "TYPE\|counter\|gauge"; then
    check "Metrics endpoint" "PASS" "Prometheus metrics available"
else
    check "Metrics endpoint" "FAIL" "No metrics or endpoint unreachable"
fi

echo ""

# ─── 5. FIX Logon (if certification harness available) ────
echo "5. FIX Session"

BUILD_DIR="../../build"
CERT_BIN="${BUILD_DIR}/certification/cert-harness-tests"

if [ -x "${CERT_BIN}" ] && [ "${HOST}" = "127.0.0.1" ] || [ "${HOST}" = "localhost" ]; then
    # Run just the logon test
    if "${CERT_BIN}" --gtest_filter="*Logon*" > /tmp/cert_logon.log 2>&1; then
        check "FIX Logon (cert harness)" "PASS" "Logon ack received"
    else
        check "FIX Logon (cert harness)" "FAIL" "See /tmp/cert_logon.log"
    fi
else
    check "FIX Logon (cert harness)" "SKIP" "Cert harness not available or remote host"
fi

echo ""

# ─── 6. TLS Check (if openssl available) ──────────────────
echo "6. TLS"

if command -v openssl >/dev/null 2>&1; then
    TLS_RESULT=$(echo | timeout 5 openssl s_client -connect "${HOST}:${FIX_PORT}" 2>/dev/null | head -1 || echo "")
    if echo "$TLS_RESULT" | grep -qi "CONNECTED"; then
        check "TLS handshake" "PASS" "TLS connection established"
    else
        check "TLS handshake" "INFO" "No TLS (plain TCP) — expected if TLS not enabled"
    fi
else
    check "TLS handshake" "SKIP" "openssl not installed"
fi

echo ""

# ─── Summary ──────────────────────────────────────────────
echo "============================================"
echo "  Results: ${PASS} passed, ${FAIL} failed, ${TOTAL} total"
echo "  Report:  ${REPORT_FILE}"
echo "============================================"

if [ "${FAIL}" -gt 0 ]; then
    echo ""
    echo "WARNING: ${FAIL} check(s) failed. Review before pilot."
    exit 1
fi

echo ""
echo "All checks passed. Connectivity validated."
exit 0
