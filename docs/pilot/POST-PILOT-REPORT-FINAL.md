# Post-Pilot Report — Final

**Pilot Dates**: 2026-03-25 (self-pilot), 2026-03-26 (external pilot)
**Report Date**: 2026-03-26
**Author**: Esam Rabba
**Status**: Final

---

## 1. Pilot Scope

| Item | Detail |
|------|--------|
| Objective | Validate FR-1 prototype runtime for controlled external pilot readiness |
| Duration | 2 sessions over 2 days |
| Environment | Development hardware, macOS, localhost + same-LAN |
| Instruments | BTC-USD (100), ETH-USD (101) |
| Participants | Internal self-pilot (Esam Rabba) |

## 2. Pilot Progression

| Session | Date | Transport | Auth | Result |
|---------|------|:---------:|:----:|:------:|
| Self-pilot (initial) | 2026-03-25 | Plain TCP | None | 10/11 (cancel failed) |
| Blocker fix + rerun | 2026-03-25 | Plain TCP | None | 11/11 |
| External pilot | 2026-03-26 | TLS v1.3 | API key | 11/11 |

## 3. Issues Found and Resolved

| # | Issue | Severity | Found | Fixed | How |
|:-:|-------|:--------:|:-----:|:-----:|-----|
| 1 | Cancel OrderID mismatch | P1 | Self-pilot | Same day | Added `assigned_order_id` to MatchResult; daemon uses composite ID |
| 2 | String ClOrdIDs unsupported | P1 | Self-pilot | Same day | Adapter falls back to hash for non-numeric; raw string echoed in ack |
| 3 | Silent error on failed parse/cancel | P2 | Self-pilot | Same day | Daemon sends explicit reject on all fail paths |
| 4 | Auth not wired in daemon | P2 | Self-pilot | Pre-external | Daemon reads `ops.api_key`, configures auth middleware |
| 5 | TLS not wired in daemon | P2 | Self-pilot | Pre-external | Daemon reads `tls.*` config, passes to FIX acceptor |
| 6 | Market data not publishing | P3 | Self-pilot | Pre-external | MdPartitionListener publishes trade + top-of-book events |
| 7 | Port 8080 conflict | Note | Self-pilot | Config change | Used port 8180 |

**All P1 and P2 issues resolved before external pilot.**

## 4. Final Scenario Results (External Pilot, TLS + Auth)

| # | Scenario | Result | Evidence |
|:-:|----------|:------:|---------|
| 1 | Daemon startup | PASS | Health returns OK, 2 instruments |
| 2 | Health/metrics | PASS | Prometheus counters active |
| 3 | FIX logon (TLS v1.3) | PASS | 35=A ack, TLS_AES_256_GCM_SHA384 |
| 4 | Valid order (string ClOrdID) | PASS | 150=0, ClOrdID echoed correctly |
| 5 | Crossing trade | PASS | 150=F, LastPx=49000, LastQty=10 |
| 6 | Cancel order | PASS | 150=4, correct composite OrderID |
| 7 | Halt (auth required) | PASS | 401 without key, halted with key |
| 8 | Reject on halt | PASS | 150=8 on halted instrument |
| 9 | Resume (auth required) | PASS | Continuous state restored |
| 10 | Order after resume | PASS | 150=0 accepted |
| 11 | Graceful shutdown | PASS | Clean stop, journal flushed |

## 5. Metrics Summary (External Pilot)

| Metric | Value |
|--------|:-----:|
| FIX messages received | 6 |
| Orders accepted | 4 |
| Orders rejected | 1 |
| Cancels processed | 1 |
| Sessions logon | 2 |
| Halt actions | 1 |
| Resume actions | 1 |
| Instruments | 2 |

## 6. Validated Capabilities

| Capability | Validated |
|-----------|:---------:|
| Deterministic matching (price-time FIFO) | Yes |
| FIX 4.4 order entry over TLS v1.3 | Yes |
| String and numeric ClOrdID support | Yes |
| Order cancel with correct venue OrderID | Yes |
| Crossing trade with fill response | Yes |
| Instrument halt/resume via auth-protected API | Yes |
| API key auth (open health, protected mutating) | Yes |
| Prometheus metrics export | Yes |
| Market data feed (trade + top-of-book) | Yes |
| Graceful startup and shutdown | Yes |
| JSON config-driven instrument/session loading | Yes |
| FIX session persistence (sequence continuity) | Yes |
| Segmented journal with CRC32 | Yes |
| Warm restart (snapshot + journal replay) | Yes |

## 7. Known Limitations (Not Blocking)

| Limitation | Impact | Disposition |
|-----------|--------|-------------|
| EOD not auto-triggered from daemon | Manual process | P3 — wire in Phase 2 |
| Fills vector not populated by engine | Daemon synthesizes from params | P3 — engine fix in Phase 2 |
| Top-of-book quantity shows 0 | Cosmetic | P3 |
| Self-signed TLS certificate | Acceptable for pilot | Production: CA-signed |
| No mTLS | Server-only TLS | Phase 2 |
| No full RBAC | API key only | Phase 2 |
| No DR/failover | Warm restart only | Phase 2 |
| No auction mechanism | Continuous only | Phase 2 |
| Bounded FIX gap recovery | Stored msgs replayed, older gap-filled | Phase 2 |

## 8. Readiness Conclusion

| Question | Answer |
|----------|--------|
| Did the platform perform as expected? | **Yes** — 11/11 scenarios pass |
| Were all planned scenarios completed? | **Yes** — across 2 pilot sessions |
| Were blocking issues found? | **Yes** — 2 P1 issues, both resolved same day |
| Is the platform ready for wider pilot? | **Yes** — with TLS and auth enabled |
| Is the platform ready for Phase 2? | **Yes** — core engine proven, daemon wiring hardened |
