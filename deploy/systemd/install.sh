#!/usr/bin/env bash
# ============================================================
# Matching Engine — Systemd Service Installer
# ============================================================
# Usage: sudo ./install.sh [install_dir]
# Default install_dir: /opt/matching-engine
# ============================================================
set -euo pipefail

INSTALL_DIR="${1:-/opt/matching-engine}"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="${SCRIPT_DIR}/../.."
SERVICE_NAME="matching-engine"

echo "=== Matching Engine Service Installer ==="
echo "Install dir: ${INSTALL_DIR}"
echo ""

# Check for root
if [ "$(id -u)" -ne 0 ]; then
    echo "ERROR: Must run as root (sudo)"
    exit 1
fi

# Create service user if not exists
if ! id -u ${SERVICE_NAME} >/dev/null 2>&1; then
    echo "Creating service user: ${SERVICE_NAME}"
    useradd --system --no-create-home --shell /usr/sbin/nologin ${SERVICE_NAME}
fi

# Create directory structure
echo "Creating directory structure..."
mkdir -p "${INSTALL_DIR}/bin"
mkdir -p "${INSTALL_DIR}/config"
mkdir -p "${INSTALL_DIR}/config/tls"
mkdir -p "${INSTALL_DIR}/data/journal"
mkdir -p "${INSTALL_DIR}/data/snapshots"
mkdir -p "${INSTALL_DIR}/data/eod"
mkdir -p "${INSTALL_DIR}/data/fix-sessions"
mkdir -p "${INSTALL_DIR}/logs"
mkdir -p "${INSTALL_DIR}/docs/runbooks"

# Copy binary
echo "Installing binary..."
if [ -f "${PROJECT_ROOT}/build/daemon/matching-engine-daemon" ]; then
    cp "${PROJECT_ROOT}/build/daemon/matching-engine-daemon" "${INSTALL_DIR}/bin/"
    chmod 755 "${INSTALL_DIR}/bin/matching-engine-daemon"
else
    echo "WARNING: Binary not found at ${PROJECT_ROOT}/build/daemon/matching-engine-daemon"
    echo "Build first: cd build && cmake .. && make -j"
fi

# Copy config
echo "Installing configuration..."
cp "${PROJECT_ROOT}/deploy/config/platform.conf" "${INSTALL_DIR}/config/"
cp "${PROJECT_ROOT}/deploy/config/instruments.json" "${INSTALL_DIR}/config/"
cp "${PROJECT_ROOT}/deploy/config/sessions.json" "${INSTALL_DIR}/config/"

# Create environment file template
if [ ! -f "${INSTALL_DIR}/config/environment" ]; then
    cat > "${INSTALL_DIR}/config/environment" << 'ENVEOF'
# Matching Engine Environment Variables
# Loaded by systemd EnvironmentFile directive
#
# OPS_API_KEY=your-secret-api-key-here
# TLS_CERT_FILE=/opt/matching-engine/config/tls/server.crt
# TLS_KEY_FILE=/opt/matching-engine/config/tls/server.key
ENVEOF
fi

# Copy runbooks
echo "Installing runbooks..."
cp "${PROJECT_ROOT}/docs/runbooks/"*.md "${INSTALL_DIR}/docs/runbooks/" 2>/dev/null || true

# Copy pilot docs
if [ -d "${PROJECT_ROOT}/docs/pilot" ]; then
    mkdir -p "${INSTALL_DIR}/docs/pilot"
    cp "${PROJECT_ROOT}/docs/pilot/"*.md "${INSTALL_DIR}/docs/pilot/" 2>/dev/null || true
fi

# Set ownership
chown -R ${SERVICE_NAME}:${SERVICE_NAME} "${INSTALL_DIR}"

# Update platform.conf paths for installed location
sed -i.bak \
    -e "s|./data/journal|${INSTALL_DIR}/data/journal|g" \
    -e "s|./data/snapshots|${INSTALL_DIR}/data/snapshots|g" \
    -e "s|./data/eod|${INSTALL_DIR}/data/eod|g" \
    -e "s|./data/fix-sessions|${INSTALL_DIR}/data/fix-sessions|g" \
    -e "s|./logs|${INSTALL_DIR}/logs|g" \
    -e "s|./config/|${INSTALL_DIR}/config/|g" \
    "${INSTALL_DIR}/config/platform.conf"
rm -f "${INSTALL_DIR}/config/platform.conf.bak"

# Install systemd service
echo "Installing systemd service..."
cp "${SCRIPT_DIR}/matching-engine.service" /etc/systemd/system/
systemctl daemon-reload

echo ""
echo "=== Installation Complete ==="
echo ""
echo "Next steps:"
echo "  1. Edit config:    sudo vim ${INSTALL_DIR}/config/platform.conf"
echo "  2. Edit secrets:   sudo vim ${INSTALL_DIR}/config/environment"
echo "  3. Enable service: sudo systemctl enable ${SERVICE_NAME}"
echo "  4. Start service:  sudo systemctl start ${SERVICE_NAME}"
echo "  5. Check status:   sudo systemctl status ${SERVICE_NAME}"
echo "  6. View logs:      sudo journalctl -u ${SERVICE_NAME} -f"
echo ""
echo "Service management:"
echo "  Start:   sudo systemctl start ${SERVICE_NAME}"
echo "  Stop:    sudo systemctl stop ${SERVICE_NAME}"
echo "  Restart: sudo systemctl restart ${SERVICE_NAME}"
echo "  Status:  sudo systemctl status ${SERVICE_NAME}"
echo "  Logs:    sudo journalctl -u ${SERVICE_NAME} --since '1 hour ago'"
