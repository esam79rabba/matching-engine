# Pre-Pilot Readiness Checklist

**Version**: 2.0 | **Date**: 2026-03-25
**Purpose**: Go/no-go gate for FR-1 pilot with external participants
**Status**: Template — fill in before each pilot engagement

---

## Instructions

- Complete all **Mandatory** items before pilot start
- Complete all **Recommended** items where practical
- Document all **Deferred** items with acknowledgment from pilot participants
- Each item requires: check owner, pass/fail/NA, evidence or notes

---

## Mandatory Before Pilot

| # | Check | Owner | Status | Evidence / Notes |
|:-:|-------|-------|:------:|------------------|
| 1 | Platform builds clean with zero warnings | Engineering | | `make -j8` exit code 0 |
| 2 | Full test suite passes (318+ tests) | Engineering | | `ctest --output-on-failure` 100% pass |
| 3 | Daemon starts and displays startup banner | Engineering | | Startup banner screenshot/log |
| 4 | FIX port 9876 accepting connections | Engineering | | Telnet/cert harness logon success |
| 5 | Market data port 9877 streaming | Engineering | | JSON event received on connect |
| 6 | Ops console port 8080 responding | Engineering | | `curl /health` returns 200 |
| 7 | Instruments loaded from config | Engineering | | `/api/v1/instruments` shows expected list |
| 8 | Certification harness passes all 7 scenarios | Engineering | | `cert-harness-tests` 100% pass |
| 9 | Certification report generated (JSON + CSV) | Engineering | | Report file exists with pass/fail |
| 10 | Participant config present in sessions.json | Operations | | SenderCompID, risk limits configured |
| 11 | API key configured for ops console | Operations | | Auth test: valid key → 200, missing → 401 |
| 12 | Journal directory writable | Operations | | `ls -la data/journal/` |
| 13 | Snapshot directory writable | Operations | | `ls -la data/snapshots/` |
| 14 | EOD output directory writable | Operations | | `ls -la data/eod/` |
| 15 | FIX session store path writable | Operations | | `data/fix-sessions/` exists |
| 16 | Warm restart tested | Engineering | | Stop, restart, verify state recovered |
| 17 | Kill switch tested | Engineering | | Activate, verify orders rejected, deactivate |
| 18 | Halt/resume tested | Engineering | | Halt instrument, verify reject, resume |
| 19 | Graceful shutdown tested | Engineering | | SIGTERM, verify clean shutdown log |
| 20 | Participant onboarding guide distributed | Operations | | Guide sent to pilot participants |

## Recommended Before Pilot

| # | Check | Owner | Status | Evidence / Notes |
|:-:|-------|-------|:------:|------------------|
| 21 | TLS enabled on FIX port | Security | | Server cert provisioned, TLS handshake verified |
| 22 | TLS certificate valid and not expired | Security | | `openssl s_client` output |
| 23 | Metrics endpoint returning data | Engineering | | `/api/v1/metrics` shows counters |
| 24 | EOD processing tested | Engineering | | Trade summary CSV generated after session |
| 25 | Session scheduler tested | Engineering | | Automatic PRE_OPEN → CONTINUOUS transition observed |
| 26 | Drop copy feed verified | Engineering | | Participant receives own order/trade events |
| 27 | Risk limits configured per participant | Operations | | Fat-finger, rate limit values reviewed |
| 28 | Backup of config files taken | Operations | | Config snapshot stored off-host |
| 29 | Runbooks reviewed by operations | Operations | | All 7 runbooks read and understood |
| 30 | Stress test run with pilot-scale load | Engineering | | 10K+ orders, no crashes or memory issues |
| 31 | Credentials provisioned per guide | Security | | See CERTIFICATE-AND-CREDENTIAL-PROVISIONING.md |
| 32 | Pilot env config pack deployed | Operations | | deploy/pilot-env/ files copied and edited |
| 33 | Connectivity validation script passes | Engineering | | `validate-connectivity.sh` all PASS |
| 34 | Operator dry-run completed | Operations | | PILOT-DAY-DRY-RUN.md executed end-to-end |
| 35 | Systemd service unit installed | Operations | | `systemctl status matching-engine` |

## Deferred — Documented and Acknowledged

| # | Item | Status | Participant Acknowledgment |
|:-:|------|:------:|---------------------------|
| D1 | Full FIX gap recovery (journal replay) | Bounded — stored messages replayed, older gap-filled | |
| D2 | mTLS enforcement | Not enabled — server TLS only | |
| D3 | Full RBAC on ops console | API key auth only | |
| D4 | DR / failover | Single-site warm restart only | |
| D5 | Multicast market data | TCP/JSON only | |
| D6 | Auction mechanism | Not implemented — continuous trading only | |
| D7 | Advanced order types | No iceberg, pegged, stop orders | |
| D8 | Process supervisor | Daemon runs standalone, no systemd/watchdog in prototype | |
| D9 | Production hardware benchmarks | Dev hardware only — not production-certified | |
| D10 | Clearing/settlement integration | Not implemented | |

## Sign-off

| Role | Name | Date | Signature |
|------|------|------|-----------|
| Engineering Lead | | | |
| Operations Lead | | | |
| Security Lead | | | |
| Pilot Participant Contact | | | |

---

## Incident Response (Pilot)

| Scenario | Action | Owner |
|----------|--------|-------|
| Daemon crash | Check logs, warm restart | Engineering |
| Participant cannot connect | Check port, TLS, CompID config | Operations |
| Orders rejected unexpectedly | Check instrument state, risk limits, kill switch | Operations |
| Journal corruption | Truncate at last valid entry, warm restart | Engineering |
| Performance degradation | Check system load, reduce order rate | Engineering |
| Security concern | Disable participant via kill switch, investigate | Security |

## Post-Pilot Review

After pilot completion, document:
1. Issues encountered and resolutions
2. Feature gaps that blocked participant workflows
3. Performance observations
4. Stability/uptime metrics
5. Recommendations for next phase
