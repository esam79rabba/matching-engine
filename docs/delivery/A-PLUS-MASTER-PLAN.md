# A+ Production Exchange — Master Tracking Plan

**Created**: 2026-03-27
**Last Updated**: 2026-03-27
**Current Grade**: C+ (Working Prototype)
**Target Grade**: A+ (Regulatory-Certified Production Exchange)
**Baseline**: 482 tests, ~30,375 LOC, 15 modules

---

## Progress Dashboard

| Phase | Items | Done | In Progress | Blocked | % Complete |
|-------|:-----:|:----:|:-----------:|:-------:|:----------:|
| 1 — Core Engine | 20 | 0 | 0 | 0 | 0% |
| 2 — Risk & Surveillance | 17 | 0 | 0 | 0 | 0% |
| 3 — Post-Trade & Settlement | 10 | 0 | 0 | 0 | 0% |
| 4 — Market Data & Gateways | 11 | 0 | 0 | 0 | 0% |
| 5 — Operations & Compliance | 10 | 0 | 0 | 0 | 0% |
| 6 — Infrastructure & Performance | 14 | 0 | 0 | 0 | 0% |
| 7 — Documentation & Certification | 6 | 0 | 0 | 0 | 0% |
| **TOTAL** | **88** | **0** | **0** | **0** | **0%** |

**Launch Blockers Remaining**: 5 of 5

---

## PHASE 1: Core Engine Completeness (8-10 weeks)

### 1.1 Order Types (4 weeks)

| # | Item | Size | Depends On | Status | Tests | Notes |
|:-:|------|:----:|:----------:|:------:|:-----:|-------|
| 1 | Stop orders — trigger queue, price monitoring, activation on trade | L | — | PENDING | — | |
| 2 | Stop-Limit orders — stop trigger + limit placement | M | #1 | PENDING | — | |
| 3 | Iceberg/Reserve — hidden_qty, display_qty, replenishment on fill | L | — | PENDING | — | LAUNCH BLOCKER |
| 4 | Pegged orders — primary/mid/market peg, reprice on BBO change | L | — | PENDING | — | |
| 5 | Post-Only enforcement — reject if would cross (flag exists, logic partial) | S | — | PENDING | — | |
| 6 | Reduce-Only orders — position-aware, reject if would increase | M | #22 | PENDING | — | Needs position tracking |
| 7 | AuctionOnly TIF — accept only during auction phases | S | #13 | PENDING | — | |
| 8 | Mass Quote — two-sided quoting, atomic insert/replace | L | — | PENDING | — | |

### 1.2 STP Modes (1 week)

| # | Item | Size | Depends On | Status | Tests | Notes |
|:-:|------|:----:|:----------:|:------:|:-----:|-------|
| 9 | CancelOldest — cancel resting, let incoming proceed | S | — | PENDING | — | |
| 10 | CancelBoth — cancel both sides, no fill | S | — | PENDING | — | |
| 11 | Decrement — reduce qty to avoid crossing, partial rest | M | — | PENDING | — | |

### 1.3 Matching Algorithms (3 weeks)

| # | Item | Size | Depends On | Status | Tests | Notes |
|:-:|------|:----:|:----------:|:------:|:-----:|-------|
| 12 | Pro-rata allocation — proportional fill within price level | L | — | PENDING | — | |
| 13 | Closing auction — order collection, price determination, execution | L | — | PENDING | — | |
| 14 | Volatility auction — circuit breaker trigger → pause → auction → resume | L | #13 | PENDING | — | |
| 15 | Intraday scheduled auction — configurable periodic auctions | M | #13 | PENDING | — | |
| 16 | Auction imbalance publication — indicative price + side + size | M | #13 | PENDING | — | |
| 17 | Random auction end — jitter window to prevent gaming | S | #13 | PENDING | — | |

### 1.4 Market Order Improvement (1 week)

| # | Item | Size | Depends On | Status | Tests | Notes |
|:-:|------|:----:|:----------:|:------:|:-----:|-------|
| 18 | Track actual last execution price (not synthetic INT64_MAX) | M | — | PENDING | — | |
| 19 | Slippage reporting — deviation from reference/last traded price | S | #18 | PENDING | — | |
| 20 | Dynamic reference price update — update static_ref_price after trades | S | #18 | PENDING | — | |

---

## PHASE 2: Risk & Surveillance (8-12 weeks)

### 2.1 Pre-Trade Risk (4 weeks)

| # | Item | Size | Depends On | Status | Tests | Notes |
|:-:|------|:----:|:----------:|:------:|:-----:|-------|
| 21 | Credit/exposure limits — net debit tracking, cross-instrument margin | L | — | PENDING | — | LAUNCH BLOCKER |
| 22 | Position limits — per-instrument long/short caps, real-time tracking | L | — | PENDING | — | LAUNCH BLOCKER |
| 23 | Dynamic price bands — recalculate bands on trade/auction/time | M | — | PENDING | — | |
| 24 | Wire rate limiter into engine — exists but not invoked | S | — | PENDING | — | |
| 25 | Notional value check strengthening — price × qty vs limit | S | — | PENDING | — | |
| 26 | Market maker protection — bulk cancel on adverse move | L | — | PENDING | — | |
| 27 | Cancel-on-disconnect enforcement — flag exists, needs gateway wiring | M | — | PENDING | — | |
| 28 | Risk limit hot-reload — update limits without restart | M | — | PENDING | — | |

### 2.2 Market Surveillance (6-8 weeks)

| # | Item | Size | Depends On | Status | Tests | Notes |
|:-:|------|:----:|:----------:|:------:|:-----:|-------|
| 29 | Surveillance framework — rule engine, event bus, alert pipeline | XL | — | PENDING | — | LAUNCH BLOCKER |
| 30 | Wash trade detection — same participant on both sides | L | #29 | PENDING | — | |
| 31 | Layering/spoofing detection — rapid cancel patterns | L | #29 | PENDING | — | |
| 32 | Order-to-trade ratio — per participant, per instrument, alerting | M | #29 | PENDING | — | |
| 33 | Quote stuffing detection — high-frequency message anomaly | M | #29 | PENDING | — | |
| 34 | Price manipulation detection — coordinated activity | L | #29 | PENDING | — | |
| 35 | Cross-market surveillance hooks — external data feed integration | L | #29 | PENDING | — | |
| 36 | Automated alert escalation — severity classification, notification | M | #29 | PENDING | — | |
| 37 | Surveillance dashboard — real-time views for surveillance officer | L | #29 | PENDING | — | |

---

## PHASE 3: Post-Trade & Settlement (6-8 weeks)

### 3.1 Trade Services (3 weeks)

| # | Item | Size | Depends On | Status | Tests | Notes |
|:-:|------|:----:|:----------:|:------:|:-----:|-------|
| 38 | Trade confirmation service — bilateral matching, FIX trade capture | L | — | PENDING | — | |
| 39 | Real-time fee calculation — maker/taker per trade, in exec reports | M | — | PENDING | — | |
| 40 | Trade bust / correction — four-eyes approval, reverse entries | L | #65 | PENDING | — | |
| 41 | Trade inquiry API — history per participant, instrument, date range | M | — | PENDING | — | |
| 42 | Order inquiry API — order status, history, open orders | M | — | PENDING | — | |

### 3.2 Settlement & Clearing (4-5 weeks)

| # | Item | Size | Depends On | Status | Tests | Notes |
|:-:|------|:----:|:----------:|:------:|:-----:|-------|
| 43 | CCP clearing adapter — abstract interface + one concrete integration | XL | — | PENDING | — | |
| 44 | Settlement instruction generation — delivery/payment legs per trade | L | — | PENDING | — | |
| 45 | Position calculation service — real-time per participant × instrument | L | — | PENDING | — | |
| 46 | EOD settlement files — regulatory format, reconciliation-ready | M | — | PENDING | — | |
| 47 | P&L reporting — realized/unrealized per participant | M | #45 | PENDING | — | |

---

## PHASE 4: Market Data & Gateways (4-6 weeks)

### 4.1 Market Data (3 weeks)

| # | Item | Size | Depends On | Status | Tests | Notes |
|:-:|------|:----:|:----------:|:------:|:-----:|-------|
| 48 | Full depth (L2) publication — per-level aggregated qty + order count | L | — | PENDING | — | |
| 49 | Order-level (L3) feed — individual order events | L | — | PENDING | — | |
| 50 | OHLC/VWAP real-time statistics — intra-day, per session | M | — | PENDING | — | |
| 51 | Snapshot recovery service — full book state on subscriber connect | L | — | PENDING | — | |
| 52 | Market data gap detection + recovery — sequence numbering, retransmit | M | — | PENDING | — | |
| 53 | Reference/opening/closing price publication | S | — | PENDING | — | |

### 4.2 Gateways (2-3 weeks)

| # | Item | Size | Depends On | Status | Tests | Notes |
|:-:|------|:----:|:----------:|:------:|:-----:|-------|
| 54 | QuickFIX integration OR FIX conformance certification | XL | — | PENDING | — | LAUNCH BLOCKER |
| 55 | WebSocket/REST gateway — for web-based participants | L | — | PENDING | — | |
| 56 | FIX 5.0 SP2 support — modern FIX version | L | #54 | PENDING | — | |
| 57 | Mass cancel — by participant, by instrument, by criteria | M | — | PENDING | — | |
| 58 | Binary (SBE) gateway TCP transport — codec exists, needs listener | M | — | PENDING | — | |

---

## PHASE 5: Operations & Compliance (4-6 weeks)

### 5.1 Audit & Compliance (3 weeks)

| # | Item | Size | Depends On | Status | Tests | Notes |
|:-:|------|:----:|:----------:|:------:|:-----:|-------|
| 59 | Complete audit trail — every order/cancel/fill/reject with hash chain | L | — | PENDING | — | LAUNCH BLOCKER |
| 60 | Audit trail query API — search by participant, instrument, time, export | L | #59 | PENDING | — | |
| 61 | Tamper-evident chain — SHA-256 chaining per spec §18 | M | #59 | PENDING | — | |
| 62 | Regulatory reporting — automated daily/monthly reports | L | #59 | PENDING | — | |
| 63 | Data retention — 90 days online, 7 years archive | M | — | PENDING | — | |

### 5.2 Operations Portal (3 weeks)

| # | Item | Size | Depends On | Status | Tests | Notes |
|:-:|------|:----:|:----------:|:------:|:-----:|-------|
| 64 | Portal rewrite — React/Vue, proper state management | L | — | PENDING | — | |
| 65 | Four-eyes approval — sensitive ops require second operator | M | — | PENDING | — | |
| 66 | Replay viewer — historical book reconstruction from journal | L | — | PENDING | — | |
| 67 | Reconciliation tools — order/trade/position reconciliation | M | — | PENDING | — | |
| 68 | Real-time operational dashboard — < 5 sec refresh, alerting | M | — | PENDING | — | |

---

## PHASE 6: Infrastructure & Performance (4-6 weeks)

### 6.1 Performance (3 weeks)

| # | Item | Size | Depends On | Status | Tests | Notes |
|:-:|------|:----:|:----------:|:------:|:-----:|-------|
| 69 | Production benchmark suite — Profile A/B/C, p99/p99.9/p99.99 | L | — | PENDING | — | |
| 70 | Object pooling everywhere — MD events, trade records, journal entries | M | — | PENDING | — | |
| 71 | Backpressure testing — journal congestion, MD buffer saturation | M | — | PENDING | — | |
| 72 | Performance regression CI — automated baseline on every build | M | — | PENDING | — | |
| 73 | CPU pinning + NUMA awareness — per-partition thread affinity | M | — | PENDING | — | |

### 6.2 Resilience (2 weeks)

| # | Item | Size | Depends On | Status | Tests | Notes |
|:-:|------|:----:|:----------:|:------:|:-----:|-------|
| 74 | Snapshot versioning + checksum — CRC32/SHA, schema version | M | — | PENDING | — | |
| 75 | Incremental snapshots — delta-only for faster capture | M | — | PENDING | — | |
| 76 | Hot standby failover (< 5s RTO) — beyond warm restart | L | — | PENDING | — | |
| 77 | Network partition simulation testing | M | — | PENDING | — | |

### 6.3 Security Hardening (2 weeks)

| # | Item | Size | Depends On | Status | Tests | Notes |
|:-:|------|:----:|:----------:|:------:|:-----:|-------|
| 78 | HSM-backed certificate management — no file-based certs | L | — | PENDING | — | |
| 79 | Network segmentation — DMZ/App/Match/Data zones | M | — | PENDING | — | |
| 80 | Encryption at rest — AES-256 for journals, snapshots | M | — | PENDING | — | |
| 81 | Secrets management — vault integration | M | — | PENDING | — | |
| 82 | Penetration testing — independent security audit | EXT | — | PENDING | — | External vendor |

---

## PHASE 7: Documentation & Certification (4 weeks)

| # | Item | Size | Depends On | Status | Tests | Notes |
|:-:|------|:----:|:----------:|:------:|:-----:|-------|
| 83 | Documentation rewrite — match every claim to code, delete fiction | L | All phases | PENDING | — | |
| 84 | FIX conformance certification — FPL test suite proof | XL | #54 | PENDING | — | |
| 85 | Determinism certification — automated replay verification | M | — | PENDING | — | |
| 86 | Resilience test schedule — monthly failover, quarterly DR | M | #76, #77 | PENDING | — | |
| 87 | Independent code audit — third-party review | EXT | All phases | PENDING | — | External vendor |
| 88 | ADGM FSRA pre-application package — updated with real capabilities | L | All phases | PENDING | — | |

---

## Launch Blockers Checklist

These 5 items MUST be complete before any go-live:

- [ ] **#3** Iceberg orders (minimum viable order type coverage)
- [ ] **#29** Surveillance framework (REGULATORY REQUIREMENT)
- [ ] **#54** FIX certification (CONNECTIVITY TRUST)
- [ ] **#59** Complete audit trail (REGULATORY REQUIREMENT)
- [ ] **#21+#22** Position + credit limits (RISK MANAGEMENT)

---

## Critical Path

```
Phase 1 (Order Types) ──→ Phase 2 (Surveillance needs order types)
                      ──→ Phase 3 (Settlement needs trade lifecycle)
                                                    ──→ Phase 7 (Certification needs everything)
Phase 2 (Risk) ──────────────────────────────────────→ Phase 7
Phase 4 (Gateways) ─────────────────────────────────→ Phase 7
Phase 5 (Compliance) ───────────────────────────────→ Phase 7
Phase 6 (Infra) ────────────────────────────────────→ Phase 7
```

Phases 1-6 can run in parallel across team members. Phase 7 is the final gate.

---

## Effort Key

| Size | Typical Duration (1 dev) |
|:----:|:------------------------:|
| S | 1-2 days |
| M | 3-5 days |
| L | 1-2 weeks |
| XL | 2-4 weeks |
| EXT | External vendor |

---

## Change Log

| Date | Change | By |
|------|--------|----|
| 2026-03-27 | Initial plan created — 88 items across 7 phases | Claude |
