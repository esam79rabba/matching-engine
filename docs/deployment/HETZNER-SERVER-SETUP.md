# Hetzner Server Setup — Matching Engine Deployment

## Server Details

| Item | Value |
|------|-------|
| Provider | Hetzner Cloud |
| Server Type | CX23 (2 vCPU, 4GB RAM, 40GB SSD) |
| OS | Ubuntu 24.04 LTS |
| IP Address | 46.224.174.75 |
| Hostname | matching-engine-pilot |
| SSH User | root |
| SSH Key | `~/.ssh/id_ed25519_hetzner` |

## Step 1: Server Provisioning

Created via Hetzner Cloud Console (https://console.hetzner.cloud):
1. New Project → Create Server
2. Location: Falkenstein (or nearest to target users)
3. Image: Ubuntu 24.04
4. Type: CX23 (2 vCPU, 4GB RAM)
5. SSH Key: Added `id_ed25519_hetzner.pub`
6. Name: `matching-engine-pilot`

## Step 2: Firewall Configuration

Hetzner Cloud Firewall: `matching-engine-fw`

| Rule Name | Protocol | Port | Source | Description |
|-----------|----------|:----:|--------|-------------|
| SSH | TCP | 22 | Any | SSH access |
| HTTP (certbot) | TCP | 80 | Any | Portal + Let's Encrypt |
| HTTPS | TCP | 443 | Any | HTTPS (future) |
| FIX TLS | TCP | 19876 | Any | FIX Gateway |
| Market Data | TCP | 19877 | Any | Market Data TCP |
| Ops API | TCP | 18080 | Any | REST API |
| WebSocket | TCP | 18081 | Any | WebSocket push |

Applied to server via Hetzner Console → Firewalls → Resources → Apply to server.

## Step 3: Initial Server Setup

```bash
# Connect
ssh root@46.224.174.75

# Update system
apt update && apt upgrade -y

# Docker is pre-installed on Ubuntu 24.04
docker --version  # Should show 28.x
docker compose version  # Should show v2.x
```

## Step 4: Deploy Key for GitHub

```bash
# Generate deploy key on server
ssh-keygen -t ed25519 -f ~/.ssh/id_ed25519 -N '' -q
cat ~/.ssh/id_ed25519.pub
```

Add the public key as a deploy key in GitHub:
1. Go to: https://github.com/esam79rabba/matching-engine/settings/keys
2. Click "Add deploy key"
3. Title: `Hetzner Server`
4. Paste the public key
5. Click "Add key"

## Step 5: Clone Repository

```bash
cd /opt
GIT_SSH_COMMAND='ssh -o StrictHostKeyChecking=accept-new' \
  git clone git@github.com:esam79rabba/matching-engine.git
cd matching-engine
```

## Step 6: Configure Platform

```bash
# Disable TLS for pilot (no certs on server)
sed -i 's/tls.enabled=true/tls.enabled=false/' deploy/docker-config/platform.conf

# Set 6 partitions
grep -q 'partition.count' deploy/docker-config/platform.conf || \
  sed -i 's/partition.id=1/partition.count=6/' deploy/docker-config/platform.conf
```

## Step 7: Build and Start

```bash
cd /opt/matching-engine

# Set required environment variables
export OPS_API_KEY='your-secret-api-key'
export JWT_SIGNING_KEY='your-jwt-signing-secret'

# Build Docker image (takes ~2 minutes)
docker compose build --no-cache

# Start
docker compose up -d

# Verify
docker ps
curl http://localhost:8080/api/v1/health
# Expected: {"status":"ok","partitions":6,"total_instruments":100}
```

## Step 8: Verify External Access

From your local machine:

```bash
# Portal
curl http://46.224.174.75:80/
# Should return HTML

# REST API
curl http://46.224.174.75:18080/api/v1/health
# Expected: {"status":"ok","partitions":6,"total_instruments":100}

# FIX port
nc -z -w 3 46.224.174.75 19876 && echo "FIX OPEN" || echo "FIX BLOCKED"

# Instruments
curl http://46.224.174.75:18080/api/v1/instruments | python3 -c "import sys,json; print(f'{len(json.load(sys.stdin))} instruments')"
# Expected: 100 instruments
```

## Step 9: Connect Participant Terminal (Local)

From your local development machine:

```bash
cd participant-terminal

# Install dependencies (first time only)
cd server && npm install && cd ../ui && npm install && cd ..

# Start pointing at the server
FIX_HOST=46.224.174.75 \
FIX_PORT=19876 \
EXCHANGE_URL=http://46.224.174.75:18080 \
SENDER_COMP_ID=PARTICIPANT1 \
npm run dev

# Open http://localhost:5174
# Click "Connect" to establish FIX session
```

## Updating the Deployment

```bash
ssh root@46.224.174.75

cd /opt/matching-engine
git pull

export OPS_API_KEY='your-secret-api-key'
export JWT_SIGNING_KEY='your-jwt-signing-secret'

# Rebuild and restart
docker compose build --no-cache
docker compose down
docker compose up -d

# Verify
sleep 10
curl http://localhost:8080/api/v1/health
docker logs matching-engine | tail -20
```

## Troubleshooting

### FIX port blocked
Check Hetzner firewall rules in the Cloud Console. Port 19876 TCP must be open for inbound.

### FIX acceptor failed to start
Check for TLS cert errors: `docker logs matching-engine | grep TLS`
Fix: set `tls.enabled=false` in `deploy/docker-config/platform.conf` and rebuild.

### Health check fails
The daemon takes 5-10 seconds to start with 100 instruments. Wait and retry.
Check logs: `docker logs matching-engine | tail -30`

### Order book empty after restart
Snapshot files persist orders across restarts. If starting fresh:
```bash
docker compose down
docker volume rm matching-engine_snapshot-data
docker compose up -d
```

### Portal shows old content
The portal is baked into the Docker image. After code changes, you must rebuild:
```bash
docker compose build --no-cache
docker compose down
docker compose up -d
```

## Port Mapping Summary

```
Host Port → Container Port → Service
  80      →  80             → Nginx (Portal + API proxy)
  18080   →  8080           → C++ Daemon (REST API)
  18081   →  8081           → WebSocket Server
  19876   →  9876           → FIX Gateway
  19877   →  9877           → Market Data TCP
  19878   →  9878           → Market Data Multicast (UDP)
```

## Credentials

| Service | Username | Password |
|---------|----------|----------|
| Exchange Portal (admin) | admin | `$OPS_API_KEY` |
| Exchange Portal (operator) | operator | `$OPS_API_KEY` |
| Exchange Portal (viewer) | viewer | viewer |
| SSH | root | Key-based (`~/.ssh/id_ed25519_hetzner`) |

## Docker Volumes

| Volume | Container Path | Purpose |
|--------|---------------|---------|
| journal-data | /opt/matching-engine/data/journal | Order journal (segmented) |
| snapshot-data | /opt/matching-engine/data/snapshots | Snapshot files (order persistence) |
| eod-data | /opt/matching-engine/data/eod | End-of-day reports |
| fix-sessions | /opt/matching-engine/data/fix-sessions | FIX session store |
| msg-store | /opt/matching-engine/data/msg-store | FIX message store |
| quickfix-data | /opt/matching-engine/data/quickfix | QuickFIX session data |
| session-config | /opt/matching-engine/data/session-config | Session configurator |
| audit-data | /opt/matching-engine/data/audit | Audit trail files |
| logs | /opt/matching-engine/logs | Application logs |
