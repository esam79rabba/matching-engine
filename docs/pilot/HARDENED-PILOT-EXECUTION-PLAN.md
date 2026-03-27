# Hardened-Platform Wider Pilot — Execution Plan

**Date**: 2026-03-26
**Platform**: Phase 2B complete (420 tests, mTLS, DR replication, RBAC, persistent replay)
**Purpose**: Validate hardened platform; collect evidence for Phase 2C decision

---

## Pilot Scope

### Expanded Scenario Set (22 Scenarios)

Previous pilot: 18 scenarios. This pilot adds 4 new scenarios testing Phase 2B capabilities.

| Phase | Scenarios | New |
|-------|:---------:|:---:|
| 1. Startup & Auth | 4 | — |
| 2. FIX Session (TLS) | 1 | — |
| 3. BTC-USD Order Lifecycle | 2 | — |
| 4. ETH-USD Order Lifecycle | 2 | — |
| 5. Cancel Flow | 3 | — |
| 6. Rejection Paths | 1 | — |
| 7. Ops Control (RBAC) | 3 | — |
| 8. Market Data + EOD | 2 | — |
| **9. RBAC Enforcement** | **2** | **New** |
| **10. Persistent Replay** | **1** | **New** |
| **11. DR Replication Health** | **1** | **New** |

### New Scenarios Detail

| # | Scenario | Expected Outcome |
|:-:|----------|-----------------|
| 19 | RBAC: viewer cannot halt | HTTP 403 Forbidden |
| 20 | RBAC: operator can halt, viewer cannot | Role enforcement verified |
| 21 | Persistent replay: disconnect, reconnect, verify stored messages retrievable | Messages persist across connection |
| 22 | DR replication: verify replicator stats show entries sent = acked | Zero lag confirmed |

## Execution Sequence

### T-0: Pilot Day

```
Phase 1: Startup (5 min)
  - Start daemon with DirectIOJournal + mTLS + RBAC + persistent store
  - Verify health, instruments, metrics
  - Verify auth: login as admin, operator, viewer

Phase 2: FIX Session (5 min)
  - Logon over TLS (or mTLS if enabled)
  - Verify persistent session (multi-message)

Phase 3-6: Order Lifecycle (10 min)
  - Same 11 lifecycle scenarios as previous pilot
  - BTC-USD + ETH-USD trades, cancels, rejects

Phase 7-8: Ops + Market Data + EOD (5 min)
  - Halt/resume with RBAC
  - Market data events
  - EOD trigger

Phase 9: RBAC Enforcement (5 min)
  - Viewer token: attempt halt → expect 403
  - Operator token: halt → expect 200
  - Admin token: create instrument → expect 201

Phase 10: Persistent Replay (5 min)
  - Submit orders, disconnect
  - Verify message store has entries
  - Reconnect, verify stored messages available

Phase 11: DR Replication (5 min)
  - Check replicator stats via API or metrics
  - Verify entries_sent = entries_acked
  - Verify zero replication lag

Evidence capture: 10 min
```

**Total estimated time: 45 minutes**

## Evidence Needed for Phase 2C Decision

### Must Collect

| Evidence | Purpose | How |
|----------|---------|-----|
| All 22 scenario results (pass/fail) | Baseline validation | Pilot script output |
| RBAC enforcement proof | Security posture | 403 responses for viewer |
| Persistent replay proof | Recovery readiness | Message retrieval after disconnect |
| Replication stats snapshot | DR readiness | Metrics: entries_sent, entries_acked, lag |
| DirectIOJournal performance (if measurable) | Journal readiness | Journal append latency observation |
| Total test count | Quality signal | `ctest` output |

### Must Answer (Post-Pilot)

| Question | Informs Phase 2C Decision |
|----------|--------------------------|
| Did any Phase 2B feature fail during pilot? | If yes: fix before 2C |
| Was mTLS transparent to participant? | If painful: simplify before wider rollout |
| Was RBAC correctly enforced without breaking flow? | If brittle: harden before 2C |
| Did persistent replay work as expected? | If not: fix before relying on it |
| Was replication lag zero in sync mode? | If not: investigate before DR goes live |
| Are there any new participant-reported issues? | Feed into 2C backlog |

## Phase 2C Go/No-Go Criteria

### Go Criteria (All Must Be Yes)

| # | Criterion | Source |
|:-:|-----------|--------|
| 1 | 22/22 pilot scenarios pass | Pilot results |
| 2 | Zero new P1 issues | Issue log |
| 3 | RBAC correctly enforced (403 for viewer on mutating) | Pilot evidence |
| 4 | Persistent replay works after disconnect | Pilot evidence |
| 5 | Replication lag = 0 in sync mode | Replication stats |
| 6 | 420+ tests pass | `ctest` output |
| 7 | No architecture issues exposed by Phase 2B features | Pilot observation |

### No-Go Triggers

- Any Phase 2B feature fails during pilot
- Replication lag > 0 in sync mode without explanation
- RBAC bypass discovered
- New P1 issue found

### What Phase 2C Would Include (If Go)

| Item | Effort | Why |
|------|:------:|-----|
| Automated failover detection | L | Manual failover proven; auto-detection next |
| Multicast UDP market data | L | TCP/JSON insufficient for colocated participants |
| Participant reconnection support after failover | M | Currently manual re-logon |
| Container/Docker packaging | M | If deployment target requires |
