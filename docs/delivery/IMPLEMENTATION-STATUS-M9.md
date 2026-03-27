# Implementation Status — Post Milestone 9

**Date**: 2026-03-25
**Status**: FR-1 prototype feature-complete with real transport layer

---

## Current Runtime Capabilities

| Capability | Status | Module | Tests |
|-----------|:------:|--------|:-----:|
| Deterministic matching (price-time FIFO) | Operational | engine/core | 32 |
| Order types: Market, Limit, IOC, FOK, GTC, GTD, Day | Operational | engine/core | 28 |
| Market-order residual policy (cancel/collar) | Operational | engine/core | 4 |
| Self-trade prevention (cancel-newest) | Operational | engine/core | 3 |
| Pre-trade risk (fat finger, notional, price band) | Operational | risk | 9 |
| Rate throttling (sliding window) | Operational | risk | 7 |
| Kill switch (participant-level) | Operational | engine/core | 5 |
| Cancel-on-disconnect | Operational | engine/core | 7 |
| Session scheduler (automatic state transitions) | Operational | engine/core | 5 |
| Instrument state machine (PRE_OPEN→CONTINUOUS→HALTED→CLOSED) | Operational | engine/core | 13 |
| Instrument listing/delisting at runtime | Operational | engine/core | 7 |
| Session lifecycle (Day-order cleanup, GTC persistence) | Operational | engine/core | 6 |
| GTD periodic expiry | Operational | engine/core | 5 |
| FIX 4.4 message parsing (real tag-value) | Operational | gateway/fix | 11 |
| FIX TCP session acceptor | Operational | gateway/fix | TCP transport |
| Binary SBE codec (encode/decode) | Operational | gateway/sbe | 8 |
| Market data publisher | Operational | market-data | 7 |
| Market data TCP transport | Operational | market-data | TCP server |
| Drop copy service | Operational | dropcopy | 7 |
| Operations console API | Operational | ops | 12 |
| HTTP server (ops + ref data) | Operational | ops | TCP transport |
| Reference data service | Operational | refdata | 18 |
| Reference data HTTP handler | Operational | refdata | 9 |
| Ref data → gateway wiring | Operational | gateway/fix | 3 |
| Journal (file-backed with CRC32) | Operational | engine/core | 7 |
| Snapshot capture | Operational | engine/core | 5 |
| Snapshot restore into live partition | Operational | engine/core | 4 |
| EOD processing (OHLC, fees, file output) | Operational | eod | 12 |
| Deterministic replay proof | Operational | engine/core | 4 |
| Multi-partition independence | Operational | engine/core | 6 |
| Stress test (10K+ orders, multi-instrument) | Validated | engine/core | 2 |
| Full platform integration test | Validated | gateway/fix | 6 |

**Total: 235 tests, 16 modules, ~9,000 lines C++, 100% passing**

## Closed Decisions

| Decision | Choice | Status |
|----------|--------|:------:|
| D-01 Hot-path language | C++ | CLOSED |
| D-02 FR-1 asset class | Digital assets (spot) | CLOSED |
| D-03 Jurisdiction | ADGM/FSRA | CLOSED |
| D-04 Sequencing | Software PTP (working assumption) | CLOSED |
| D-05 Hosting | Cloud dev/test + ADGM production in parallel | CLOSED |
| D-06 Journal | Custom local append-only (C++) | CLOSED |

## FR-1 vs Phase 2 Boundary (Actual)

### FR-1 Implemented
- Continuous LOB with price-time FIFO
- Market, Limit, IOC, FOK, GTC, GTD, Day orders
- STP cancel-newest
- Fat-finger, notional, rate throttle, kill switch
- Session scheduler with automatic state transitions
- Day-order cleanup, GTC persistence, GTD expiry
- Instrument listing/delisting
- FIX 4.4 TCP sessions
- SBE binary codec
- Market data TCP transport
- Drop copy service
- Operations console with HTTP transport
- Reference data service with HTTP transport
- File-backed journal with CRC32 checksums
- Snapshot capture and restore
- EOD processing with file output
- Cancel-on-disconnect
- Kill switch (participant-level)

### Phase 2 Deferred
- Auction mechanism (opening/closing)
- Pro-rata/parity priority models
- Iceberg, pegged, stop orders
- CCP clearing integration
- DR site / journal replication
- TLS/mTLS on all transports
- Auth/RBAC
- Production NVMe Direct I/O journal
- Multicast UDP market data
- FIX sequence gap recovery (ResendRequest)

## Prototype Benchmark Results (Not Production Certification)

| Metric | Result | Caveat |
|--------|--------|--------|
| Matching insert p99 | ~3.9 µs | Development hardware, no CPU pinning, no DPDK |
| Matching match p99 | ~2.5 µs | Same caveats |
| Stress throughput | ~537K msg/s | Single partition, prototype journal, dev hardware |
| Test suite execution | ~1.5 seconds | 235 tests |

These are **prototype directional indicators only**, not production venue performance certification.
