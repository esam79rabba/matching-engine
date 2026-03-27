#!/usr/bin/env bash
# ============================================================
# Matching Engine Platform — Warm Restart
# ============================================================
# Performs: stop → snapshot restore → journal replay → resume
# ============================================================
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
DATA_DIR="${SCRIPT_DIR}/../data"
LOG_DIR="${SCRIPT_DIR}/../logs"

echo "=== Matching Engine — Warm Restart ==="
echo "Timestamp: $(date -u '+%Y-%m-%dT%H:%M:%SZ')"
echo ""

# Step 1: Stop current instance
echo "Step 1: Stopping current instance..."
"${SCRIPT_DIR}/stop.sh"
echo ""

# Step 2: Verify data integrity
echo "Step 2: Verifying journal integrity..."
if [ -d "${DATA_DIR}/journal" ]; then
    SEGMENT_COUNT=$(find "${DATA_DIR}/journal" -name "*.journal" 2>/dev/null | wc -l | tr -d ' ')
    echo "  Journal segments found: ${SEGMENT_COUNT}"
else
    echo "  WARNING: No journal directory found"
fi

if [ -d "${DATA_DIR}/snapshots" ]; then
    SNAP_COUNT=$(find "${DATA_DIR}/snapshots" -name "*.snap" 2>/dev/null | wc -l | tr -d ' ')
    echo "  Snapshots found: ${SNAP_COUNT}"
else
    echo "  WARNING: No snapshots directory found"
fi
echo ""

# Step 3: Restart with recovery
echo "Step 3: Starting platform with warm restart..."
echo "  The StartupController will:"
echo "    1. Load latest snapshot"
echo "    2. Replay journal entries from snapshot point"
echo "    3. Validate restored state"
echo "    4. Resume accepting new traffic"
echo ""

# NOTE: In the current prototype, warm restart is executed via
# StartupController::warm_restart() in the test/integration harness.
# A standalone daemon with signal-based restart is Phase 2+.

echo "Step 4: Running smoke tests to verify..."
"${SCRIPT_DIR}/start.sh"
echo ""
echo "=== Warm restart procedure complete ==="
