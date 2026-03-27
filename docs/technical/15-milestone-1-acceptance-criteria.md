# Milestone 1 — Acceptance Criteria

**Milestone**: Single-instrument deterministic order lifecycle skeleton operational
**Target**: Month 3 (2 weeks after D-01 closes, per bootstrap plan)
**Owner**: Engine Lead + Gateway Lead

---

## Scope

A single instrument, running on a single partition, can accept orders via FIX, match them deterministically, and return execution reports — with the full order lifecycle journaled and replayable.

## Required Components

| Component | State Required | Notes |
|-----------|---------------|-------|
| Order book (single instrument) | Insert, cancel, modify (price + qty), best bid/ask | Price-time FIFO only |
| Matching logic | Limit-vs-limit matching; multi-level sweep; partial fills | Market orders included |
| Market-order residual | Cancel-residual policy implemented | Collar option: implemented or stubbed |
| IOC / FOK | IOC residual cancel; FOK pre-check and reject | |
| Day order cleanup | Day orders cancelled at session-end event | |
| STP check | Cancel-newest mode | Other modes: stubbed |
| Price-band validation | Static band check; reject if outside | Dynamic: may be stubbed |
| Tick-size validation | Reject orders not aligned to tick | |
| Instrument state machine | At minimum: PRE_OPEN → CONTINUOUS → CLOSED | Auction states: stubbed OK |
| Journal | Interface implemented; in-memory stub or real impl | Must support replay |
| Snapshot | Capture full book state to serialized format | |
| Replay | Load snapshot + replay journal → identical state | Determinism proven |
| Sequencer | Monotonic sequence assignment (simple counter OK) | PTP/HW: later |
| FIX gateway | Logon, logout, heartbeat, NewOrderSingle, ExecutionReport, OrderCancelRequest | Session management functional |
| Binary gateway | Not required for M1 | Stub OK |
| Risk service | Stub (pass-through) or basic fat-finger check | |
| Market data | Not required for M1 (log output OK) | |
| Drop copy | Not required for M1 | |

## Required Behaviors

| # | Behavior | Verification Method |
|---|----------|-------------------|
| B-01 | Limit buy order accepted and resting in book | Unit test + FIX integration test |
| B-02 | Limit sell order matches resting buy at buy's price | Unit test |
| B-03 | Partial fill: remaining quantity correct; resting order retains priority | Unit test |
| B-04 | Cancel: order removed from book; cancel confirmed | Unit test + FIX test |
| B-05 | Modify price: order moved to new level; loses time priority | Unit test |
| B-06 | Modify qty (reduce): in-place; retains priority | Unit test |
| B-07 | Market order: sweeps multiple levels | Unit test |
| B-08 | Market order residual: cancelled (default policy) | Unit test |
| B-09 | IOC: partial fill + residual cancel | Unit test |
| B-10 | FOK: rejected if full qty not available | Unit test |
| B-11 | Price band: reject order outside static band | Unit test |
| B-12 | Tick size: reject order not aligned to tick | Unit test |
| B-13 | STP cancel-newest: aggressive order cancelled on self-trade | Unit test |
| B-14 | Instrument state: reject new order when instrument is CLOSED | Unit test |
| B-15 | Deterministic replay: journal replay produces bit-identical book state | Simulation test |
| B-16 | FIX logon/logout: session established and terminated cleanly | FIX integration test |
| B-17 | FIX order submit → engine → ExecutionReport returned | End-to-end integration test |
| B-18 | FIX cancel → engine → cancel confirmed | End-to-end integration test |

## Required Tests

| Category | Count | Coverage Target |
|----------|:-----:|:---------------:|
| Unit tests (engine core) | ≥ 40 | ≥ 95% line coverage on matching + book logic |
| Simulation tests (deterministic replay) | ≥ 5 | Replay 5 different input sequences; verify identical output |
| FIX integration tests | ≥ 10 | Cover B-16, B-17, B-18 + basic error cases |
| Total | ≥ 55 | |

## Intentionally Out of Scope for M1

| Feature | Why Deferred |
|---------|-------------|
| Auctions (opening, closing, volatility) | Complex; Phase 1 stretch goal |
| Iceberg / pegged / stop orders | Phase 2 order types |
| Pro-rata / parity allocation | Phase 2 priority models |
| Binary gateway | Can start in parallel; not needed for M1 proof |
| Market data publication | Log output sufficient for M1 |
| Drop copy | Not needed for M1 |
| Pre-trade risk (real implementation) | Stub sufficient for M1 |
| Post-trade / fee calculation | Not needed for M1 |
| Surveillance export | Not needed for M1 |
| Cancel-on-disconnect | Requires session state maturity; Phase 1 post-M1 |
| PTP timestamping | Simple counter sufficient for M1 determinism proof |
| Performance benchmarking | Functional correctness first; performance from M2 |

## Evidence Required to Claim M1 Complete

| Evidence | Format |
|----------|--------|
| All 18 behaviors (B-01 through B-18) passing | Test report (CI output) |
| ≥ 55 tests passing, ≥ 95% engine core coverage | Coverage report |
| Deterministic replay proven (5 scenarios) | Simulation test log showing input → replay → identical state |
| FIX gateway functional (logon, order, cancel, ack) | Integration test log |
| Build pipeline green on CI | CI dashboard screenshot |
| Code reviewed (all engine/core changes) | PR review log |

## M1 → M2 Transition

After M1 is accepted, the following work activates:
- Binary gateway development
- Pre-trade risk integration (real checks, not stub)
- Market data publisher
- Drop copy gateway
- Performance benchmarking (first latency measurements)
- Auction implementation (if in FR-1 scope)
- Cancel-on-disconnect
- Multi-instrument testing
