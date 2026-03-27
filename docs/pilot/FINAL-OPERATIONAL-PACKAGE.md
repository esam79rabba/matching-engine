# Final Operational Package — Real Participant Pilot

**Date**: 2026-03-26
**Platform**: Phase 2C-M1 complete, 423 tests, Docker deployment validated
**Validation**: Docker dry-run 13/13 pass, hardened pilot 22/22 pass
**Status**: Ready for real participant — no engineering blockers

---

## 1. Pilot-Ready Release Freeze Summary

| Item | Value |
|------|-------|
| Recommended tag | `fr1-docker-pilot-20260326` |
| Tests | 423, 100% passing |
| Docker image | `matching-engine` (multi-stage, Ubuntu 24.04 runtime) |
| Pilot validations completed | 8 sessions, best 22/22 (hardened) + 13/13 (Docker) |
| Issues found and resolved | 8 (all in earlier sessions, zero open) |
| Modules | 14 (engine, gateway, risk, refdata, ops, market-data, dropcopy, eod, common, certification, daemon, replication, portal) |

### Freeze scope

| Freeze | Files |
|--------|-------|
| Docker artifacts | `Dockerfile`, `docker-compose.yml`, `.dockerignore` |
| Container config | `deploy/docker-config/` |
| Engine + all modules | All `src/`, `include/`, `CMakeLists.txt` |
| Pilot documentation | `docs/pilot/` (all files) |
| Runbooks | `docs/runbooks/` |

**Do not modify frozen artifacts until the pilot is complete and evidence is captured.**

---

## 2. Real Participant Deployment Package Checklist

| # | Step | Owner | Done |
|:-:|------|-------|:----:|
| 1 | Docker and Docker Compose installed on pilot host | Ops | |
| 2 | Source code cloned to pilot host | Ops | |
| 3 | `docker build -t matching-engine .` succeeds | Ops | |
| 4 | TLS certificate placed in `deploy/pilot-env/tls/` | Security | |
| 5 | Participant added to `deploy/docker-config/sessions.json` | Ops | |
| 6 | API key set in `deploy/docker-config/platform.conf` | Security | |
| 7 | `docker-compose up -d` starts cleanly | Ops | |
| 8 | `curl http://HOST:18080/api/v1/health` returns OK | Ops | |
| 9 | TLS handshake verified: `openssl s_client -connect HOST:19876` | Ops | |
| 10 | Firewall: participant IP allowed to ports 19876, 19877, 18080 | Infra | |

---

## 3. Real Participant Configuration Checklist

### sessions.json — Add participant entry

```json
{
  "participant_id": <UNIQUE_INT>,
  "name": "<Participant Organization Name>",
  "sender_comp_id": "<ASSIGNED_COMPID>",
  "max_order_rate": 500,
  "fat_finger_price_pct": 10.0,
  "fat_finger_max_notional": 5000000,
  "sessions": [
    { "session_id": <UNIQUE_INT>, "target_comp_id": "VENUE" }
  ]
}
```

### platform.conf — Verify these values

| Key | Action |
|-----|--------|
| `ops.api_key` | Change from default to a generated key: `openssl rand -hex 16` |
| `tls.enabled` | `true` |
| `tls.cert_file` | Path to server certificate |
| `tls.key_file` | Path to server private key |
| `fix.listen_port` | 9876 (container internal; host maps to 19876 or custom) |
| `journal.flush_mode` | `sync` for Regulatory-Grade |

### Port mapping — docker-compose.yml

| Container Port | Default Host Port | Override Env Var |
|:--------------:|:-----------------:|:----------------:|
| 9876 (FIX) | 19876 | `FIX_PORT` |
| 9877 (MD) | 19877 | `MD_PORT` |
| 8080 (Ops) | 18080 | `OPS_PORT` |

Custom: `FIX_PORT=9876 docker-compose up -d`

---

## 4. Certificate Decision Guide

### Self-Signed Is Acceptable When

- Participant is internal or closely trusted
- Pilot is on localhost or same LAN
- Participant's FIX client can disable cert verification
- Purpose is technical validation, not security certification
- Both parties accept prototype-grade security

**How**: Already generated at `deploy/pilot-env/tls/server.crt`. Share with participant.

### CA-Signed Is Required When

- Participant is an external institution with compliance requirements
- Participant's FIX client requires trusted CA chain
- Pilot is over the public internet
- Participant's infosec team must approve connectivity
- Venue wants to demonstrate production-grade security posture

**How**:

| Option | Effort | Cost |
|--------|:------:|:----:|
| Let's Encrypt (free, needs domain) | 1 hour | Free |
| Commercial CA (DigiCert, Sectigo) | 1-2 days | $50-200/yr |
| Internal CA (org-managed) | 1 hour | Free |

Steps for Let's Encrypt:
```bash
# Requires a domain pointing to the pilot host
certbot certonly --standalone -d pilot.yourvenue.com
cp /etc/letsencrypt/live/pilot.yourvenue.com/fullchain.pem deploy/pilot-env/tls/server.crt
cp /etc/letsencrypt/live/pilot.yourvenue.com/privkey.pem deploy/pilot-env/tls/server.key
```

### Decision Matrix

| Participant Type | Network | Recommendation |
|-----------------|---------|:--------------:|
| Internal team | Localhost/LAN | Self-signed |
| Known partner | VPN/LAN | Self-signed (share cert) |
| External institution | Internet | CA-signed |
| Regulatory demo | Any | CA-signed |

---

## 5. Exact Onboarding Handoff Pack Contents

### Send to participant (3 items)

| # | Item | Source | Format |
|:-:|------|--------|--------|
| 1 | Participant onboarding guide | `docs/pilot/PARTICIPANT-ONBOARDING.md` | PDF or Markdown |
| 2 | Server TLS certificate | `deploy/pilot-env/tls/server.crt` | PEM file |
| 3 | Connection details sheet | Prepared per participant | Text/PDF |

### Connection details sheet template

```
Matching Engine Pilot — Connection Details
==========================================
Participant:      [Name]
SenderCompID:     [ASSIGNED]
TargetCompID:     VENUE
FIX Host:         [HOST_IP_OR_DOMAIN]
FIX Port:         [19876 or custom]
Transport:        TLS v1.2+ (certificate attached)
Heartbeat:        30 seconds

Market Data:      [HOST]:19877 (TCP/JSON, no auth)
Ops Console:      [HOST]:18080 (HTTP, health/instruments/metrics open)

Instruments:      BTC-USD (100), ETH-USD (101)
Session Schedule: PRE_OPEN 08:00 → CONTINUOUS 09:00 → CLOSED 17:00 (GST)

Reconnection:     After venue restart, reconnect with your current
                  MsgSeqNum. Venue resumes from persisted state.
```

### Do NOT send

- API key, platform.conf, sessions.json, server private key
- Docker credentials, source code, internal pilot docs

---

## 6. Real Participant Dry-Run Checklist

Run this AFTER deployment, BEFORE inviting participant to connect.

| # | Check | Command | Expected | Pass |
|:-:|-------|---------|----------|:----:|
| 1 | Container healthy | `docker-compose ps` | Status: Up (healthy) | |
| 2 | Health endpoint | `curl HOST:18080/api/v1/health` | `{"status":"ok"}` | |
| 3 | Instruments loaded | `curl HOST:18080/api/v1/instruments` | 2 instruments | |
| 4 | TLS handshake | `openssl s_client -connect HOST:19876` | CONNECTED + cert chain | |
| 5 | FIX logon | Send 35=A with participant CompID | 35=A ack | |
| 6 | Order + trade | Buy then crossing sell | 150=0 then 150=F | |
| 7 | Cancel | Submit then cancel | 150=4 | |
| 8 | RBAC | Viewer token → halt → 403 | Forbidden | |
| 9 | Market data | Connect to MD port | JSON events | |
| 10 | Restart recovery | `docker-compose restart` → health check | OK after restart | |

**All 10 must pass. If any fail, do not invite participant.**

---

## 7. Top 5 Business/Ops Actions Required Next

| # | Action | Owner | Effort | Dependency |
|:-:|--------|-------|:------:|:----------:|
| 1 | **Identify real pilot participant** — reach out to one firm willing to do a controlled FIX integration test | Business | 1-2 weeks | Nothing |
| 2 | **Decide on TLS certificate type** — self-signed (share cert) or CA-signed (need domain) based on participant requirements | Security + Business | 1 day | Participant identified |
| 3 | **Deploy container on pilot host** — build image, configure sessions.json with participant CompID, start docker-compose | Operations | 1-2 hours | Participant confirmed, cert ready |
| 4 | **Run dry-run checklist** — all 10 checks pass before sending onboarding package | Operations | 30 min | Container deployed |
| 5 | **Send onboarding package and schedule call** — 3 items + 30-min call using existing agenda template | Operations | 1 hour | Dry-run passed |

**After these 5**: participant connects for dry-run, then pilot day execution using `PILOT-EXECUTION-PACK.md`.

**The platform is ready. The next move is a business decision, not an engineering one.**
