# Linux VM Setup — Matching Engine Deployment

## Server Requirements

| Item | Value |
|------|-------|
| Provider | Any (AWS EC2, GCP, DigitalOcean, Hetzner, bare metal, ...) |
| Size | 2 vCPU, 4 GB RAM, 40 GB SSD (minimum for pilot) |
| OS | Ubuntu 24.04 LTS |
| IP Address | `<server-public-ip>` |
| Hostname | `your-server` |
| SSH User | root (or a sudo-capable user) |
| SSH Key | `~/.ssh/id_ed25519` (or `~/.ssh/your_ssh_key`) |

## Step 1: Server Provisioning

Provision a Linux VM with any cloud provider (AWS EC2, GCP, DigitalOcean, Hetzner, bare metal, ...):
1. Choose a region close to the target users.
2. Image: Ubuntu 24.04 LTS.
3. Size: 2 vCPU, 4 GB RAM.
4. Add your SSH public key (`~/.ssh/id_ed25519.pub` or equivalent) to the instance.
5. Name the instance `your-server`.

## Step 2: Firewall Configuration

Configure the provider's firewall (security group, cloud firewall, or host firewall such as `ufw`) to allow inbound TCP on:

| Rule Name | Protocol | Port | Source | Description |
|-----------|----------|:----:|--------|-------------|
| SSH | TCP | 22 | Admin IPs (recommended) | SSH access |
| HTTP (certbot) | TCP | 80 | Any | Portal + Let's Encrypt |
| HTTPS | TCP | 443 | Any | HTTPS (future) |
| FIX TLS | TCP | 19876 | Any | FIX Gateway |
| Market Data | TCP | 19877 | Any | Market Data TCP |
| Ops API | TCP | 18080 | Any | REST API |
| WebSocket | TCP | 18081 | Any | WebSocket push |

Apply the rules to the instance through your provider's console or CLI.

## Step 3: Initial Server Setup

```bash
# Connect
ssh -i ~/.ssh/id_ed25519 root@<server-public-ip>

# Update system
apt update && apt upgrade -y

# Install Docker Engine and the Compose plugin (Ubuntu 24.04 packages or Docker's apt repo)
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
1. Go to: `https://github.com/<org>/matching-engine/settings/keys`
2. Click "Add deploy key"
3. Title: `your-server`
4. Paste the public key
5. Click "Add key"

## Step 5: Clone Repository

```bash
cd /opt
GIT_SSH_COMMAND='ssh -o StrictHostKeyChecking=accept-new' \
  git clone git@github.com:<org>/matching-engine.git
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
curl http://<server-public-ip>:80/
# Should return HTML

# REST API
curl http://<server-public-ip>:18080/api/v1/health
# Expected: {"status":"ok","partitions":6,"total_instruments":100}

# FIX port
nc -z -w 3 <server-public-ip> 19876 && echo "FIX OPEN" || echo "FIX BLOCKED"

# Instruments
curl http://<server-public-ip>:18080/api/v1/instruments | python3 -c "import sys,json; print(f'{len(json.load(sys.stdin))} instruments')"
# Expected: 100 instruments
```

## Step 9: Connect Participant Terminal (Local)

From your local development machine:

```bash
cd participant-terminal

# Install dependencies (first time only)
cd server && npm install && cd ../ui && npm install && cd ..

# Start pointing at the server
FIX_HOST=<server-public-ip> \
FIX_PORT=19876 \
EXCHANGE_URL=http://<server-public-ip>:18080 \
SENDER_COMP_ID=PARTICIPANT1 \
npm run dev

# Open http://localhost:5174
# Click "Connect" to establish FIX session
```

## Updating the Deployment

```bash
ssh root@<server-public-ip>

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
Check the provider's firewall rules (security group / cloud firewall / host firewall). Port 19876 TCP must be open for inbound.

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
| SSH | root | Key-based (`~/.ssh/id_ed25519`) |

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
