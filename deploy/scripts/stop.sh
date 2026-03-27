#!/usr/bin/env bash
# ============================================================
# Matching Engine Platform — Pilot Stop Script
# ============================================================
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PID_DIR="${SCRIPT_DIR}/../run"

echo "=== Matching Engine Pilot — Shutdown ==="

# Currently the prototype runs as test harnesses / in-process.
# When a standalone daemon is implemented, this script will:
# 1. Send graceful shutdown signal
# 2. Wait for session draining
# 3. Trigger EOD processing if during trading hours
# 4. Capture final snapshot
# 5. Close journal cleanly
# 6. Stop all services

if [ -d "${PID_DIR}" ] && ls "${PID_DIR}"/*.pid 1>/dev/null 2>&1; then
    for pidfile in "${PID_DIR}"/*.pid; do
        pid=$(cat "${pidfile}")
        name=$(basename "${pidfile}" .pid)
        if kill -0 "${pid}" 2>/dev/null; then
            echo "Stopping ${name} (PID ${pid})..."
            kill -TERM "${pid}"
            sleep 1
            if kill -0 "${pid}" 2>/dev/null; then
                echo "  Force killing ${name}..."
                kill -9 "${pid}"
            fi
        fi
        rm -f "${pidfile}"
    done
else
    echo "No running processes found."
fi

echo "=== Shutdown complete ==="
