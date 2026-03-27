# Phase 2C-M3 Pilot Readiness

**Date**: 2026-03-27
**Platform**: 442 tests, automated failover promotion proven
**Public host**: pilot.rabba3.com (rehearsal)

---

## 1. Participant Delta Summary

| Area | Before (2C-M2) | After (2C-M3) |
|------|----------------|---------------|
| Tests | 431 | **442** |
| Failover detection | Automated (HEALTHY → DEGRADED → FAILED) | Unchanged |
| Failover promotion | Manual operator action | **Automated** with safety checks |
| Split-brain protection | Not addressed | **Generation counter** — stale primary rejected |
| Failover readiness | API available | + **pre-promote validation hooks** |
| Failover audit | Detection logged | + **every promotion decision logged** |
| Failover metrics | Detection metrics | + `failover.promotions`, `failover.fencing_events` |
| Operator override | N/A | **Master switch** to disable auto-promote |

### What This Means for Participants

- If the primary site fails, the standby **automatically detects and promotes** within seconds
- No operator intervention required for promotion (though operators can disable auto-promote)
- After promotion, the standby has identical order book state (proven in end-to-end test)
- Participants would need to **reconnect** to the promoted site (manual re-logon)
- Stale primary cannot accept orders after promotion (generation fencing)

## 2. Updated Known Limitations

### Resolved Since Last Disclosure

| Previously Limited | Now |
|-------------------|-----|
| Manual promote only | **Automated promotion** with safety validation |
| No split-brain protection | **Generation counter fencing** |
| No failover audit trail | **All decisions logged** |

### Current Limitations (Disclose Before Pilot)

| # | Limitation | Impact |
|:-:|-----------|--------|
| 1 | FIX TLS disabled on public Docker host | Transport encryption deferred; plain TCP operational |
| 2 | Participant reconnect is manual | After failover, participants must re-logon |
| 3 | No DNS failover | Participant must know DR host or operator updates DNS manually |
| 4 | No auction mechanism | Continuous trading only |
| 5 | Market data TCP/JSON | Not multicast/SBE |
| 6 | Single partition | Sufficient for pilot |
| 7 | Prototype hardware | EUR 3.49/mo Hetzner CX23 |
| 8 | Failover tested in simulation only | Not tested with real two-site deployment yet |

## 3. Failover-Focused Pilot Scenario Set

### Standard Scenarios (35 — carried forward, proven)

All scenarios from the 35/35 public host test remain unchanged.

### New Failover Scenarios (7)

| # | Scenario | Setup | Expected Outcome |
|:-:|----------|-------|-----------------|
| 36 | Health probe detects healthy primary | Probe running, primary up | state = HEALTHY |
| 37 | Failover readiness when healthy | Query readiness API | ready_to_promote = false, reason = "primary is healthy" |
| 38 | Replication status shows zero lag | After replication cycle | entries_sent == entries_acked |
| 39 | Auto-promote fires on primary failure | Stop primary, probe detects FAILED | Promotion occurs, generation increments |
| 40 | Promoted partition has correct state | After auto-promote | Order book matches pre-failure state |
| 41 | Stale generation rejected | Old primary tries to claim current | is_stale = true |
| 42 | Auto-promote disabled respects override | Set enabled=false, primary fails | Detection fires, no promotion |

**Total: 42 scenarios**

## 4. Pilot Execution Checklist

### Pre-Pilot

| # | Check | How | Done |
|:-:|-------|-----|:----:|
| 1 | Host running | `ssh root@HOST "docker compose ps"` | |
| 2 | Health OK | `curl HOST:18080/api/v1/health` | |
| 3 | Portal accessible | Browser → http://HOST | |
| 4 | FIX port open | `nc -z HOST 19876` | |
| 5 | Participant in config | grep sessions.json | |
| 6 | API key changed from default | Verify in platform.conf | |
| 7 | 442 tests pass locally | `ctest` | |

### Pilot Execution

| Phase | Scenarios | Method |
|-------|:---------:|--------|
| Infrastructure + Auth | 1-11 | curl + Python |
| FIX Session | 12 | Raw TCP |
| Order Lifecycle | 13-21 | Raw TCP FIX |
| Ops Control | 22-25 | JWT + curl |
| Market Data + EOD | 26-30 | TCP + curl |
| Audit + Metrics | 31-35 | curl |
| **Failover Chain** | **36-42** | **Replication tests / simulation** |

### Post-Pilot

| # | Action | How |
|:-:|--------|-----|
| 1 | Final metrics | Portal or curl |
| 2 | Audit log | Portal → Audit page |
| 3 | Evidence capture | Per evidence checklist |
| 4 | Container logs | `docker logs matching-engine` |

## 5. Remaining Business/Ops Blockers

| # | Blocker | Type | Status |
|:-:|---------|:----:|:------:|
| 1 | **No real participant identified** | Business | **Only true blocker** |
| 2 | Default API key on public host | Ops | 1-minute fix |
| 3 | FIX TLS on Docker | Technical (deferred) | Acceptable with disclosure |

**Engineering blockers: zero.** The platform has 442 tests, automated failover, generation fencing, full audit trail, and a proven public deployment.

## 6. Recommended Immediate Next Action

**Identify one real participant.** Everything else is done:

- Platform: 442 tests, Phases 2A through 2C-M3 complete
- Deployment: Docker containerized, public host playbook ready (45 min to recreate)
- Portal: live at pilot.rabba3.com
- Documentation: 16 pilot docs, 7 runbooks, onboarding templates
- Failover: automated detection + promotion + fencing + audit
- Process: proven across 10 pilot sessions, best result 35/35 on public host

The matching engine is **ready for a real external participant pilot**. The only remaining step is a business decision.
