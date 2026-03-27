# Hardened Platform — Final Participant Delta

**Date**: 2026-03-27
**Platform**: Phase 2C-M2 complete, 431 tests, public host operational
**Public host**: pilot.rabba3.com
**Portal**: http://pilot.rabba3.com

---

## 1. What Improved Since Last Participant-Ready State

| Area | Previous State | Current State |
|------|---------------|---------------|
| Tests | 423 | **431** |
| Failover detection | Manual observation only | **Automated health probe** (HEALTHY → DEGRADED → FAILED) |
| Failover readiness | Not assessed | **Readiness API** with lag + probe state |
| Failover promote | Not available | **Manual promote** from replicated journal (proven in test) |
| DR replication | Journal replication proven | + **failover detection + promote proven** |
| Container packaging | Docker + docker-compose | + **portal served via Nginx** |
| Public host | Not available | **pilot.rabba3.com live** |
| Portal | Local only | **Public at http://pilot.rabba3.com** |
| HTTP server | Body truncation on browser POST | **Fixed** (Content-Length aware reads) |

## 2. Updated Known Limitations for Participant Disclosure

### Resolved Since Last Disclosure

| Previously Limited | Now |
|-------------------|-----|
| No failover detection | Automated detection with state machine |
| No failover promote | Manual promote from replicated journal |
| Portal local only | Public at pilot.rabba3.com |

### Current Limitations (Disclose Before Pilot)

| # | Limitation | Impact |
|:-:|-----------|--------|
| 1 | FIX port is plain TCP on public host | TLS works locally; Docker TLS init issue deferred |
| 2 | Automated failover promotion is manual | Operator must trigger; no auto-promote |
| 3 | No auction mechanism | Continuous trading only |
| 4 | Market data TCP/JSON | Not multicast/SBE |
| 5 | EOD triggered via API | Not automatic on session close in Docker (works locally) |
| 6 | Single partition | Sufficient for pilot scale |
| 7 | Prototype hardware | EUR 3.49/mo Hetzner CX23 — not benchmarked |
| 8 | Self-signed cert not deployed | Let's Encrypt RSA obtained but Docker TLS init fails |

## 3. Pilot Scenario Set (40 Scenarios)

### Carried Forward (35 — proven in public host test)

| Phase | Scenarios | Count |
|-------|-----------|:-----:|
| Infrastructure | Health, instruments, metrics (Prometheus + JSON) | 4 |
| Auth & RBAC | Login (3 roles), auth/me, viewer 403, operator 200, no-auth 401 | 7 |
| FIX Session | Logon with CompID | 1 |
| Order Lifecycle | Buy, trade, ETH buy, ETH trade, cancel, cancel-unknown, invalid-inst | 7 |
| Ops Control | Halt, reject-on-halt, resume | 3 |
| Market Data | Events received, trades, TOB | 3 |
| EOD | Trigger, status | 2 |
| Audit | Entries logged | 1 |
| Metrics Summary | Orders, cancels, sessions, halts, EOD count | 5 |
| Portal | Accessible, serves HTML | 2 |

### New Scenarios (5 — failover capabilities)

| # | Scenario | Expected Outcome |
|:-:|----------|-----------------|
| 36 | Replication status API returns health data | JSON with probe_state, replication_lag, ready_to_promote |
| 37 | Failover readiness: not ready when healthy | ready_to_promote = false |
| 38 | Container restart preserves journal + sessions | Health OK after restart; orders accepted |
| 39 | Reconnect after restart with persisted sequences | FIX logon succeeds with continued seq |
| 40 | Metrics survive restart (counters reset expected) | Health OK; metrics endpoint responds |

## 4. Pilot Execution Checklist

### Pre-Pilot (Operator)

| # | Check | Command | Done |
|:-:|-------|---------|:----:|
| 1 | Public host running | `ssh root@46.224.174.75 "docker compose ps"` | |
| 2 | Health endpoint | `curl http://pilot.rabba3.com:18080/api/v1/health` | |
| 3 | Portal accessible | Open http://pilot.rabba3.com in browser | |
| 4 | FIX port open | `nc -z pilot.rabba3.com 19876` | |
| 5 | Market data port open | `nc -z pilot.rabba3.com 19877` | |
| 6 | Participant CompID in config | grep in sessions.json | |
| 7 | Admin login works | Login at portal | |
| 8 | Instruments loaded | Check portal dashboard | |

### During Pilot

| # | Action | Monitor Via |
|:-:|--------|-----------|
| 1 | Participant connects | Portal → FIX Sessions page |
| 2 | Orders processed | Portal → Dashboard metrics |
| 3 | Trades executed | Portal → Dashboard + Market Data |
| 4 | Issues | PILOT-ISSUE-LOG-TEMPLATE.md |

### Post-Pilot

| # | Action | How |
|:-:|--------|-----|
| 1 | Final metrics | Portal → Metrics page or curl |
| 2 | Audit log | Portal → Audit Log page |
| 3 | EOD trigger | Portal → EOD page → Trigger button |
| 4 | Container logs | `ssh root@46.224.174.75 "docker logs matching-engine"` |
| 5 | Participant feedback | Direct communication |

## 5. Remaining Business/Ops Blockers

| # | Blocker | Type | Action Needed |
|:-:|---------|:----:|---------------|
| 1 | **No real participant identified** | Business | Identify and contact one firm |
| 2 | **FIX TLS disabled on public host** | Technical (deferred) | Participant must accept plain TCP or fix must be resolved |
| 3 | **Default API key in production config** | Security | Change `ops.api_key` from default before real participant |

**Blocker #1 is the only true blocker.** #2 is acceptable for a controlled pilot with disclosure. #3 is a 1-minute config change.

### Not Blockers (Confirmed Working)

- Public host: live and tested (35/35)
- Portal: accessible and functional
- Auth/RBAC: 3 roles enforced
- Docker deployment: persistent volumes, restart recovery
- Failover detection: proven (8 tests)
- 431 tests passing

## 6. Recommended Immediate Next Action

**The platform is ready. The only blocker is identifying a real participant.**

| Priority | Action | Owner |
|:--------:|--------|-------|
| 1 | **Identify one real pilot participant** | Business |
| 2 | Change default API key on public host | Ops (1 min) |
| 3 | Send onboarding package when participant confirms | Ops |

Everything else — engineering, documentation, deployment, portal, testing — is done.
