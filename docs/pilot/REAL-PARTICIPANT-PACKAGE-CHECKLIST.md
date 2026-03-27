# Real-Participant Pilot — Deployment Package Checklist

---

## Venue Operator Prepares (Before Sending to Participant)

| # | Item | Action | Done |
|:-:|------|--------|:----:|
| 1 | Docker image built | `docker build -t matching-engine .` | |
| 2 | TLS certificate generated (CA-signed preferred) | See credential provisioning guide | |
| 3 | Participant CompID assigned | Add to `deploy/docker-config/sessions.json` | |
| 4 | Participant risk limits configured | In sessions.json | |
| 5 | API key set in platform.conf | `ops.api_key=<generated>` | |
| 6 | Portal auth users configured | admin/operator/viewer in daemon | |
| 7 | Docker-compose tested locally | `docker-compose up -d && curl localhost:8080/api/v1/health` | |
| 8 | Firewall rules submitted | Participant IP → ports 9876, 9877, 8080 | |
| 9 | Container deployed on pilot host | docker-compose running on target machine | |

## Send to Participant

| # | Item | File / Source | Sent |
|:-:|------|-------------|:----:|
| 1 | Participant onboarding guide | `docs/pilot/PARTICIPANT-ONBOARDING.md` | |
| 2 | Server TLS certificate | `deploy/pilot-env/tls/server.crt` | |
| 3 | Connection details sheet | Prepared per participant (host, ports, CompID) | |

### Connection Details Template

```
Matching Engine Pilot — Connection Details
==========================================
Participant:    [Name]
SenderCompID:   [ASSIGNED_COMPID]
TargetCompID:   VENUE
FIX Host:       [HOST_IP]
FIX Port:       9876
Transport:      TLS v1.2+ (certificate attached)
Heartbeat:      30 seconds

Market Data:    [HOST_IP]:9877 (TCP/JSON)
Ops Console:    [HOST_IP]:8080 (HTTP, read-only endpoints open)

Instruments:
  BTC-USD (ID: 100)
  ETH-USD (ID: 101)

Reconnection:
  After venue restart, reconnect with your current MsgSeqNum.
  Venue will resume from persisted sequence state.
```

## Do NOT Send

- API key
- platform.conf
- Docker credentials
- Other participants' CompIDs
- Server private key

## Pre-Send Verification

| # | Check | Done |
|:-:|-------|:----:|
| 1 | Container running on pilot host | |
| 2 | Health endpoint responding | |
| 3 | FIX port accepting TLS connections | |
| 4 | Participant CompID in config | |
| 5 | Connection details sheet complete (no placeholders) | |
