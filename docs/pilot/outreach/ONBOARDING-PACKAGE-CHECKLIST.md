# Onboarding Package — Contents Checklist

**Purpose**: Ensure everything the participant needs is included before sending.

---

## Package Contents

| # | Item | File / Source | Included |
|:-:|------|--------------|:--------:|
| 1 | Participant onboarding guide | `docs/pilot/PARTICIPANT-ONBOARDING.md` | |
| 2 | Server TLS certificate | `deploy/pilot-env/tls/server.crt` | |
| 3 | Connection details sheet (see below) | Prepared per participant | |
| 4 | Known limitations acknowledgment | Section 11 of onboarding guide | |

## Connection Details Sheet (Prepare Per Participant)

```
Matching Engine Pilot — Connection Details
==========================================
Participant:    [Name / Organization]
SenderCompID:   [ASSIGNED_COMPID]
TargetCompID:   VENUE
FIX Host:       [IP or hostname]
FIX Port:       9876
Transport:      TLS v1.2+ (certificate attached)
Heartbeat:      30 seconds
Market Data:    [IP]:9877 (TCP/JSON, no auth)
Ops Console:    [IP]:8180 (read-only endpoints, no auth)

Instruments:
  BTC-USD (ID: 100) — Bitcoin / USD Spot
  ETH-USD (ID: 101) — Ethereum / USD Spot

Session Schedule:
  PRE_OPEN:    08:00 GST
  CONTINUOUS:  09:00 GST
  CLOSED:      17:00 GST
```

## DO NOT Include

| Item | Reason |
|------|--------|
| Ops API key | Operator-only credential |
| platform.conf | Internal configuration |
| sessions.json | Contains all participant configs |
| Server private key | Security |
| Internal pilot docs (execution pack, issue log, retrospective) | Internal process |

## Delivery Method

- Send via encrypted email or secure file share
- Confirm receipt with participant
- Record delivery date in operator notes
