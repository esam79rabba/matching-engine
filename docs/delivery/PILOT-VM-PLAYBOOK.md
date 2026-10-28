# Pilot VM Playbook

**Date**: 2026-03-27
**Current VM**: pilot.example.com (203.0.113.10) — REHEARSAL ONLY
**Purpose**: Repeatable recipe for creating a fresh pilot host in under 1 hour

---

## 1. What to Preserve (In Local Repo)

These are already in your local codebase and survive VM destruction:

| Item | Location | Why |
|------|----------|-----|
| Dockerfile | `Dockerfile` | Builds the engine image |
| docker-compose.yml | `docker-compose.yml` | Full stack definition with portal |
| .dockerignore | `.dockerignore` | Keeps image clean |
| Docker config | `deploy/docker-config/` | Container-internal platform.conf, instruments, sessions |
| Pilot-env config | `deploy/pilot-env/` | Local dev config + TLS cert directory |
| Portal | `portal/index.html` | Single-file ops dashboard |
| All source code | Everything in repo | 431 tests, all milestones |
| Pilot docs | `docs/pilot/` | Onboarding, checklists, runbooks, outreach |
| Deployment guide | `docs/pilot/CONTAINER-DEPLOYMENT-GUIDE.md` | Step-by-step |
| This playbook | `docs/delivery/PILOT-VM-PLAYBOOK.md` | Repeatable recipe |

**Preserve the pilot evidence directories locally** — they document validation history:

| Evidence | Location |
|----------|----------|
| pilot-evidence-20260325/ | Self-pilot |
| pilot-evidence-20260326/ | External pilot |
| pilot-evidence-20260327/ | Wider pilot |
| pilot-evidence-hardened/ | Hardened pilot |
| pilot-evidence-sim-alpha/ | Simulated external |
| pilot-evidence-gamma/ | Docker rehearsal |
| pilot-evidence-delta/ | Docker rehearsal |
| pilot-evidence-public/ | Public host test |

## 2. What to Discard / Not Care About

| Item | Why |
|------|-----|
| Current VM state (data volumes, journals, sessions) | Rehearsal data, not production |
| VM-specific Let's Encrypt cert | Re-issue on new host (2 minutes) |
| Default API key (`change-this-in-production`) | Generate fresh per deployment |
| Orphan Docker containers on current VM | Cleanup artifact |
| VM SSH host keys | New VM gets new keys |
| Provider server ID (e.g., 125070446) | New server gets new ID |

**The current VM can be destroyed at any time.** Nothing on it is irreplaceable.

## 3. Fresh Pilot VM Recreation Checklist

Total time: ~45 minutes from zero to validated.

### Step 1: Provision VM (5 min)

```bash
# Using provider API (or console)
PROVIDER_TOKEN=$(cat ~/.config/matching-engine/provider-token)

# Upload SSH key (skip if already uploaded)
# Create server
curl -s -X POST "https://<provider-api>/v1/servers" \
  -H "Authorization: Bearer ${PROVIDER_TOKEN}" \
  -H "Content-Type: application/json" \
  -d '{
    "name": "matching-engine-pilot",
    "server_type": "cx23",
    "image": "ubuntu-24.04",
    "location": "fsn1",
    "ssh_keys": [YOUR_SSH_KEY_ID],
    "start_after_create": true
  }'

# Note the IP address from the response
```

### Step 2: Create Firewall (2 min)

```bash
curl -s -X POST "https://<provider-api>/v1/firewalls" \
  -H "Authorization: Bearer ${PROVIDER_TOKEN}" \
  -H "Content-Type: application/json" \
  -d '{
    "name": "matching-engine-fw",
    "rules": [
      {"direction":"in","protocol":"tcp","port":"22","source_ips":["0.0.0.0/0","::/0"]},
      {"direction":"in","protocol":"tcp","port":"80","source_ips":["0.0.0.0/0","::/0"]},
      {"direction":"in","protocol":"tcp","port":"443","source_ips":["0.0.0.0/0","::/0"]},
      {"direction":"in","protocol":"tcp","port":"19876","source_ips":["0.0.0.0/0","::/0"]},
      {"direction":"in","protocol":"tcp","port":"19877","source_ips":["0.0.0.0/0","::/0"]},
      {"direction":"in","protocol":"tcp","port":"18080","source_ips":["0.0.0.0/0","::/0"]}
    ],
    "apply_to": [{"type":"server","server":{"id":SERVER_ID}}]
  }'
```

### Step 3: DNS (2 min)

Point `pilot.yourdomain.com` A record to the new server IP. Cloudflare: DNS only (grey cloud).

### Step 4: Install Docker (5 min)

```bash
ssh root@NEW_IP "apt-get update -qq && apt-get install -y -qq docker.io docker-compose-v2 git curl certbot && systemctl enable docker && systemctl start docker"
```

### Step 5: Deploy Source (5 min)

```bash
cd /path/to/MatchingEngine
rsync -az --exclude='build/' --exclude='pilot-data/' --exclude='pilot-evidence-*/' --exclude='external repos/' -e ssh . root@NEW_IP:/opt/matching-engine-src/
```

### Step 6: Generate API Key (1 min)

```bash
NEW_KEY=$(openssl rand -hex 16)
ssh root@NEW_IP "sed -i 's/change-this-in-production/${NEW_KEY}/' /opt/matching-engine-src/deploy/docker-config/platform.conf"
echo "API key: ${NEW_KEY}"
```

### Step 7: Add Participant to Config (2 min)

```bash
# Edit on server or locally then rsync
ssh root@NEW_IP "python3 -c \"
import json
with open('/opt/matching-engine-src/deploy/docker-config/sessions.json') as f: cfg=json.load(f)
cfg['participants'].append({
    'participant_id': 10,
    'name': 'PARTICIPANT_NAME',
    'sender_comp_id': 'COMPID',
    'max_order_rate': 500,
    'fat_finger_price_pct': 10.0,
    'fat_finger_max_notional': 5000000,
    'sessions': [{'session_id': 10, 'target_comp_id': 'VENUE'}]
})
with open('/opt/matching-engine-src/deploy/docker-config/sessions.json', 'w') as f: json.dump(cfg, f, indent=2)
print('Done')
\""
```

### Step 8: TLS Certificate (5 min)

```bash
# Issue Let's Encrypt RSA cert
ssh root@NEW_IP "certbot certonly --standalone -d pilot.yourdomain.com --non-interactive --agree-tos --email you@email.com --key-type rsa"

# Copy to deployment
ssh root@NEW_IP "
cp /etc/letsencrypt/live/pilot.yourdomain.com/fullchain.pem /opt/matching-engine-src/deploy/pilot-env/tls/server.crt
cp /etc/letsencrypt/live/pilot.yourdomain.com/privkey.pem /opt/matching-engine-src/deploy/pilot-env/tls/server.key
chmod 644 /opt/matching-engine-src/deploy/pilot-env/tls/server.crt
chmod 644 /opt/matching-engine-src/deploy/pilot-env/tls/server.key"
```

Note: Docker TLS init issue exists. Set `tls.enabled=false` in docker config if needed.

### Step 9: Build and Start (15 min)

```bash
ssh root@NEW_IP "cd /opt/matching-engine-src && docker build -t matching-engine . && docker compose up -d"
```

### Step 10: Validate (5 min)

```bash
# Health
curl http://pilot.yourdomain.com:18080/api/v1/health

# Portal
curl -s http://pilot.yourdomain.com/ | head -3

# FIX
echo | nc -z pilot.yourdomain.com 19876 && echo "FIX: OPEN"

# Login
curl -s -X POST -H "Content-Type: application/json" \
  -d '{"username":"admin","password":"NEW_KEY"}' \
  http://pilot.yourdomain.com:18080/api/v1/auth/login
```

---

## Quick Reference

| What | Where |
|------|-------|
| Provider API token | `~/.config/matching-engine/provider-token` |
| SSH key ID | `<your-provider-ssh-key-id>` |
| Server type | 2 vCPU, 4GB RAM (~EUR 3.49/mo on a CX23-class plan; use your provider equivalent) |
| OS | Ubuntu 24.04 LTS |
| Ports | 22, 80, 443, 19876, 19877, 18080 |
| Docker compose | `docker compose up -d` |
| Portal | Nginx on port 80 (in docker-compose) |
