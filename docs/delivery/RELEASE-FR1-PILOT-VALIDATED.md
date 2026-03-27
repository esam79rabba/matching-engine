# Release Package — FR-1 Pilot-Validated

**Release**: FR-1.0-pilot-validated
**Date**: 2026-03-26
**Status**: Pilot-validated, not production-certified
**Recommended tag**: `fr1-pilot-validated-20260326`

---

## 1. Validated State Summary

The FR-1 matching engine prototype has been validated through 6 pilot sessions with progressively stricter conditions, culminating in an 18-scenario simulated external pilot with a separate participant CompID, TLS v1.3 encryption, auth-protected ops endpoints, multi-instrument trading, and EOD processing.

| Metric | Value |
|--------|:-----:|
| Source lines (C++) | ~18,800 |
| Test suite | 334 tests, 100% passing |
| Modules | 13 |
| Pilot sessions completed | 6 |
| Best pilot result | 18/18 scenarios, zero issues |
| Evidence directories | 4 |

## 2. Validated Capabilities

| # | Capability | Pilot Evidence |
|:-:|-----------|---------------|
| 1 | Deterministic price-time FIFO matching | Crossing trades at correct price across all 6 sessions |
| 2 | FIX 4.4 order entry over TLS v1.3 | All sessions from external pilot onward |
| 3 | Multi-instrument (BTC-USD + ETH-USD) | Wider + sim-alpha pilots |
| 4 | String and numeric ClOrdID | All sessions post-blocker-fix |
| 5 | Composite venue OrderID (cancel-safe) | All sessions post-blocker-fix |
| 6 | Real engine-populated fills | All sessions post-hardening-sprint |
| 7 | Order cancel with explicit reject on unknown | Sim-alpha pilot (scenario 12) |
| 8 | Auth-protected ops (API key, open health/metrics) | All sessions from external pilot onward |
| 9 | Instrument halt/resume | All sessions |
| 10 | Market data feed (trade + top-of-book) | 8 events in sim-alpha pilot |
| 11 | EOD trigger via API | 2 trades captured in sim-alpha pilot |
| 12 | Invalid instrument rejection | Sim-alpha pilot (scenario 13) |
| 13 | Graceful startup and shutdown | All sessions |
| 14 | JSON config-driven loading | All sessions from M13 onward |
| 15 | FIX session persistence | Validated in M13 tests |
| 16 | Segmented journal with CRC32 | Operational across all sessions |
| 17 | Warm restart (snapshot + replay) | Validated in M10-M13 tests |
| 18 | Structured FIX processing logging | All sessions post-hardening |
| 19 | Daemon integration test suite | 6 tests catching P1-class bugs |
| 20 | Transport abstraction (TCP/TLS) | TLS plugged in cleanly |

## 3. Known Limitations

| # | Limitation | Impact | Disposition |
|:-:|-----------|--------|-------------|
| 1 | Top-of-book qty shows 0 | Cosmetic | P3 |
| 2 | Self-signed TLS certificate | Pilot only | CA-signed for production |
| 3 | No mTLS enforcement | Server TLS only | Phase 2 |
| 4 | API key auth only (no RBAC) | Single-key model | Phase 2 |
| 5 | No DR/failover | Warm restart only | Phase 2 |
| 6 | No auction mechanism | Continuous only | Phase 2 |
| 7 | Bounded FIX gap recovery | Stored msgs only | Phase 2 |
| 8 | EOD via API trigger, not automatic | Manual | Phase 2 |
| 9 | Single-partition daemon | Sufficient for pilot | Phase 2 |
| 10 | Dev hardware only | Not production-benchmarked | Phase 2 |

## 4. Evidence Directory Index

| Directory | Date | Type | Scenarios | Result |
|-----------|------|------|:---------:|:------:|
| `pilot-evidence-20260325/` | 2026-03-25 | Self-pilot + blocker fixes | 11 | 11/11 |
| `pilot-evidence-20260326/` | 2026-03-26 | External pilot (TLS+auth) | 11 | 11/11 |
| `pilot-evidence-20260327/` | 2026-03-27 | Wider external pilot | 15 | 15/15 |
| `pilot-evidence-sim-alpha/` | 2026-03-26 | Simulated external (strictest) | 18 | 18/18 |

## 5. Release Notes

### What Ships
- Standalone daemon (`matching-engine-daemon`) with signal handling
- FIX 4.4 gateway with TLS v1.3 support
- Price-time FIFO matching engine (multi-instrument)
- Pre-trade risk (fat-finger, notional, rate limit)
- Session scheduler, halt/resume, kill switch
- Market data publisher (TCP/JSON)
- Operations console with API key auth and Prometheus metrics
- Reference data service with HTTP handler
- EOD processor with API trigger
- Segmented journal with CRC32 checksums
- Snapshot capture and restore
- Warm restart and cold start controllers
- FIX session persistence
- Bounded FIX gap recovery (ResendRequest/SequenceReset)
- Client certification harness with JSON/CSV report output
- Systemd service unit and installer
- Complete pilot documentation package (9 pilot docs, 7 runbooks)

### What Does NOT Ship
- Production-grade journal (NVMe Direct I/O)
- DR/replication
- Auction mechanism
- mTLS enforcement
- Full RBAC
- Multicast market data
- Container packaging
- Production performance certification

## 6. Artifacts to Freeze

| Category | Files | Action |
|----------|-------|--------|
| Engine core | `engine/core/` | Tag and freeze |
| Gateway | `gateway/fix/`, `gateway/sbe/` | Tag and freeze |
| Daemon | `daemon/main.cpp`, `daemon/CMakeLists.txt` | Tag and freeze |
| Risk | `risk/` | Tag and freeze |
| All modules | `market-data/`, `dropcopy/`, `ops/`, `refdata/`, `eod/`, `common/`, `certification/` | Tag and freeze |
| Config | `deploy/pilot-env/` | Snapshot (config may evolve per participant) |
| Pilot docs | `docs/pilot/` | Tag and freeze |
| Runbooks | `docs/runbooks/` | Tag and freeze |
| Build | `CMakeLists.txt` (all levels) | Tag and freeze |
| Evidence | `pilot-evidence-*/` | Archive permanently |
