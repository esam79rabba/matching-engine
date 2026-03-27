# Participant Dry-Run Scheduling Checklist

**Purpose**: Ensure a clean connectivity test before the actual pilot day.

---

## Scheduling

| Item | Detail | Confirmed |
|------|--------|:---------:|
| Dry-run date | [Date — at least 1 day before pilot] | |
| Dry-run time | [Time — 15–30 min window] | |
| Participant contact available | [Name / phone / Slack] | |
| Operator available | [Name] | |
| Platform deployed and running | Daemon on pilot host | |

## Pre-Dry-Run (Operator)

| # | Check | Done |
|:-:|-------|:----:|
| 1 | Daemon running with TLS enabled | |
| 2 | Participant CompID in sessions.json | |
| 3 | Firewall open for participant IP | |
| 4 | Health endpoint responding | |
| 5 | FIX port 9876 accepting TLS connections | |
| 6 | Communication channel with participant open | |

## Dry-Run Steps

| # | Step | Who | Expected | Actual | Pass |
|:-:|------|:---:|----------|--------|:----:|
| 1 | Participant connects FIX client to host:9876 | Participant | TLS handshake succeeds | | |
| 2 | Participant sends Logon (35=A) | Participant | 35=A ack received | | |
| 3 | Participant sends a test order (35=D) | Participant | 150=0 (New) received | | |
| 4 | Participant sends cancel (35=F) | Participant | 150=4 (Cancelled) received | | |
| 5 | Participant connects to market data port 9877 | Participant | JSON events visible | | |
| 6 | Operator checks metrics | Operator | Counters incremented | | |

## Post-Dry-Run

| # | Action | Owner | Done |
|:-:|--------|-------|:----:|
| 1 | Record dry-run results in operator notes | Operator | |
| 2 | Address any issues found | Engineering / Ops | |
| 3 | Confirm pilot date is still on | Both | |
| 4 | If dry-run fails: reschedule after fix | Operator | |

## Dry-Run Pass Criteria

All 6 steps must pass. If any fail:
- **Steps 1–2 fail**: Network or TLS issue — check firewall, cert, CompID
- **Steps 3–4 fail**: FIX message issue — check onboarding guide compliance
- **Steps 5–6 fail**: Non-blocking for pilot, but investigate

**Dry-run pass = cleared for pilot day.**
