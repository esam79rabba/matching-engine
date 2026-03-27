# External Pilot Delta Checklist

**Purpose**: Move from localhost self-pilot to one external participant
**Baseline**: Self-pilot completed 2026-03-25 (11/11 pass, all P1 blockers resolved)

---

## Delta Items (Beyond Self-Pilot)

### Mandatory Before External Participant

| # | Check | Owner | Status | Notes |
|:-:|-------|-------|:------:|-------|
| 1 | `ops.api_key` set in platform.conf | Security | | Generate unique key per deployment |
| 2 | Auth verified: protected endpoints return 401 without key | Engineering | | Tested in self-pilot |
| 3 | Auth verified: open endpoints (health, instruments, metrics) accessible | Engineering | | Tested in self-pilot |
| 4 | TLS certificate generated for FIX port | Security | | See CERTIFICATE-AND-CREDENTIAL-PROVISIONING.md |
| 5 | TLS enabled in platform.conf (`tls.enabled=true`) | Security | | Uncomment TLS lines |
| 6 | TLS handshake verified (`openssl s_client -connect host:9876`) | Security | | |
| 7 | Participant CompID provisioned in sessions.json | Operations | | Unique SenderCompID |
| 8 | Participant risk limits configured | Operations | | fat_finger, rate_limit, notional |
| 9 | Participant received: CompID, venue cert, FIX host/port, onboarding guide | Operations | | |
| 10 | Firewall rules: participant IP → ports 9876, 9877, 8180 | Infrastructure | | |
| 11 | Connectivity validation passes from participant network | Engineering | | `validate-connectivity.sh` from their side |
| 12 | Participant FIX logon succeeds | Operations | | Monitor daemon logs |
| 13 | Dry-run completed with participant connected | Operations | | PILOT-DAY-DRY-RUN.md |

### Recommended

| # | Check | Owner | Status | Notes |
|:-:|-------|-------|:------:|-------|
| 14 | Market data subscriber tested by participant | Engineering | | Port 9877 |
| 15 | API key distributed securely to ops team only | Security | | Not shared with participant |
| 16 | Warm restart tested with participant's session state | Engineering | | Session persists across restart |
| 17 | Kill switch tested (on test account, not participant) | Operations | | |

### Known Limitations — Participant Must Acknowledge

| # | Limitation | Impact |
|:-:|-----------|--------|
| 1 | Prototype on dev hardware — not production-certified | Latency not guaranteed |
| 2 | No DR/failover — single-site warm restart only | Downtime on failure |
| 3 | FIX gap recovery is bounded (stored messages only) | Old messages gap-filled |
| 4 | No auction mechanism — continuous trading only | No opening/closing auction |
| 5 | Market data is TCP/JSON — not multicast/SBE | Higher latency than production |
| 6 | EOD not auto-triggered from daemon | Manual process via operator |
| 7 | ClOrdID must be numeric OR alphanumeric (both supported) | |
| 8 | Cancel uses venue OrderID (tag 37 from ack) in tag 41 | Must echo exact value |

### Go / No-Go for External Participant

| Criterion | Status |
|-----------|:------:|
| All mandatory items pass | |
| Participant acknowledged limitations | |
| Rollback plan confirmed | |
| Incident log and evidence tools ready | |

**Decision**: GO / NO-GO

Signed: _________________ Date: _________
