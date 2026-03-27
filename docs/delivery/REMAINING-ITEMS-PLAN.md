# Remaining Items Plan — Prioritized

**Date**: 2026-03-27
**Baseline**: Engineering complete, 444 tests, 0 critical/high issues
**Source**: Codebase analysis (4 medium, 9 low, 4 info findings) + 8 TODOs + deferred items

---

## Priority Tiers

### Tier 1: Do Before Any Real Participant Pilot (Security + Correctness)

These items directly affect participant trust and basic security posture.

| # | Item | Source | Effort | Why Now |
|:-:|------|--------|:------:|---------|
| 1.1 | **Replace hex JWT with signed tokens** | Analysis: Medium security | M | Current tokens are replay-able; any intercepted token works forever |
| 1.2 | **Move API key out of plaintext config** | Analysis: Medium security | S | Environment variable or encrypted file; 1-hour fix |
| 1.3 | **Restrict CORS to specific origins** | Analysis: Low security | S | Change `*` to configurable origin list; prevents cross-site API abuse |
| 1.4 | **Rate limit auth/login endpoint** | Analysis: Low security | S | Prevent brute-force; simple counter per IP per minute |
| 1.5 | **Resolve participant_id hardcoded to 1** | Analysis: Low quality (TODO) | S | CompID → participant_id mapping from ref data; 3 locations |

**Estimated total: 1-2 weeks**

### Tier 2: Do Before Production Deployment (Performance + Scalability)

These items affect production viability under real load.

| # | Item | Source | Effort | Why Before Production |
|:-:|------|--------|:------:|----------------------|
| 2.1 | **Multi-threaded FIX accept loop** | Analysis: Medium performance | L | Single-threaded blocks all connections during one session; production needs concurrent sessions |
| 2.2 | **HTTP server upgrade (epoll/kqueue)** | Analysis: Low performance | M | Current select()-based server is prototype; replace with proper HTTP framework (Boost.Beast, httplib, or Crow) |
| 2.3 | **Async market data broadcast** | Analysis: Low performance | M | Slow subscriber blocks publisher; use non-blocking writes or per-client threads |
| 2.4 | **FOK pre-check (read-only book scan)** | TODO in matching.cpp | M | FOK currently relies on IOC residual handling; true FOK needs pre-check to avoid partial state mutation |
| 2.5 | **Sliding-window rate limiter** | TODO in risk_service.cpp | M | Current rate limiter is a placeholder; need real sliding window or token bucket |
| 2.6 | **Open order count tracking** | TODO in risk_service.cpp | S | Track per-participant open orders for limit enforcement |

**Estimated total: 4-6 weeks**

### Tier 3: Do Before Exchange Certification (Completeness + Compliance)

These items are required for regulatory certification and institutional confidence.

| # | Item | Source | Effort | Why Before Certification |
|:-:|------|--------|:------:|--------------------------|
| 3.1 | **Market data snapshot recovery** | TODO in publisher.h | L | New MD subscribers need initial state; currently no snapshot delivery |
| 3.2 | **Session → participant_id mapping** | TODO in fix_parser.cpp | S | FIX session must resolve to correct participant for risk limits |
| 3.3 | **Password hashing in AuthService** | Analysis: Info security | S | Store bcrypt/argon2 hashes, not plaintext |
| 3.4 | **CI/CD pipeline with automated benchmark** | Analysis: Info performance | M | Run benchmarks on every commit; detect regressions |
| 3.5 | **Multi-partition daemon support** | Deferred item | L | Single partition limits instrument count; production needs N partitions |
| 3.6 | **Multicast UDP market data** | Deferred item | L | Required for colocated HFT participants |
| 3.7 | **Auction mechanism (opening/closing)** | Deferred item | L | Required by some regulators for price discovery |

**Estimated total: 8-12 weeks**

### Tier 4: Do When Demand Justifies (Nice-to-Have + Future)

No urgency. Build only when a participant, regulator, or deployment target requires it.

| # | Item | Source | Effort | Trigger |
|:-:|------|--------|:------:|---------|
| 4.1 | Portal migration to React/framework | Architecture: Info | L | Current single-file HTML exceeds maintainability threshold |
| 4.2 | Pro-rata/parity matching | Deferred item | L | Derivatives listing planned |
| 4.3 | Iceberg/pegged/stop orders | Deferred item | M each | Participant demand |
| 4.4 | CCP clearing integration | Deferred item | XL | Regulatory requirement |
| 4.5 | WebSocket/REST gateway | Deferred item | L | Retail or API-first participants |
| 4.6 | Automated participant reconnect after failover | Deferred item | M | Production DR deployment |
| 4.7 | DNS-based failover routing | Deferred item | M | Two-site production deployment |
| 4.8 | Hardware sequencer (FPGA) | Deferred item | XL | Benchmark proves software insufficient |
| 4.9 | FIX 5.0 / FIXT migration | Deferred item | L | Participant requires 5.0 |
| 4.10 | Replace catch(...) with specific exception handling | Analysis: Low quality | S | Code quality improvement |

---

## Recommended Execution Sequence

```
NOW (engineering paused)
  │
  ▼
Tier 1 (1-2 weeks) ← Resume when participant identified OR before any external exposure
  │
  ├─ 1.1 Signed JWT
  ├─ 1.2 API key from env var
  ├─ 1.3 CORS restriction
  ├─ 1.4 Auth rate limit
  └─ 1.5 Participant_id mapping
  │
  ▼
Real Participant Pilot ← Execute with Tier 1 complete
  │
  ▼
Tier 2 (4-6 weeks) ← Resume after pilot feedback
  │
  ├─ 2.1 Multi-threaded FIX (highest impact)
  ├─ 2.2 HTTP server upgrade
  ├─ 2.3 Async market data
  ├─ 2.4 FOK pre-check
  ├─ 2.5 Real rate limiter
  └─ 2.6 Open order tracking
  │
  ▼
Production Deployment Decision ← Assess with Tier 2 complete
  │
  ▼
Tier 3 (8-12 weeks) ← Build toward certification
  │
  ├─ 3.1 MD snapshot recovery
  ├─ 3.2 Session mapping
  ├─ 3.3 Password hashing
  ├─ 3.4 CI/CD + benchmarks
  ├─ 3.5 Multi-partition
  ├─ 3.6 Multicast MD
  └─ 3.7 Auctions
  │
  ▼
Exchange Certification ← Submit with Tier 3 complete
  │
  ▼
Tier 4 (demand-driven) ← Build only when required
```

---

## Effort Summary

| Tier | Items | Effort | Gate |
|:----:|:-----:|:------:|------|
| 1 | 5 | 1-2 weeks | Before real participant |
| 2 | 6 | 4-6 weeks | Before production |
| 3 | 7 | 8-12 weeks | Before certification |
| 4 | 10 | Demand-driven | When justified |
| **Total** | **28** | **~20 weeks + on-demand** | |

---

## Decision Rules

| Question | Answer |
|----------|--------|
| Start Tier 1 now? | Only if a participant is imminent. Otherwise, engineering is paused. |
| Start Tier 2 without pilot? | No. Pilot feedback should shape priorities. |
| Start Tier 3 without Tier 2? | No. Foundation must be solid before certification work. |
| Skip any Tier 4 item permanently? | Yes — build only on evidence of demand. |
| Can tiers overlap? | Tier 1 → Pilot can overlap. Tier 2 → Tier 3 can overlap on independent items. |
