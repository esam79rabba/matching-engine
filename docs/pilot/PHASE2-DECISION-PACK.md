# Phase 2 Decision Pack

**Date**: 2026-03-26
**Basis**: Pilot evidence from self-pilot (2026-03-25) and external pilot (2026-03-26)
**Status**: Decision-ready

---

## Category A: Must-Do Before Wider Pilot

| # | Item | Why It Matters | Pilot Evidence | Effort | Risk | Recommendation |
|:-:|------|---------------|----------------|:------:|:----:|:--------------|
| A1 | Daemon integration test suite | Would have caught both P1 blockers before pilot | OrderID mismatch and string ClOrdID bugs found live | S | Low | **Do immediately** |
| A2 | Populate fills vector in engine | Current workaround synthesizes fills in daemon; fragile | Fill events had no trade_id, buyer/seller info | S | Low | **Do immediately** |
| A3 | Wire EOD into daemon | Operators cannot generate trade reports without test framework | EOD tested only via unit tests | M | Low | **Do before wider pilot** |
| A4 | Log all FIX processing decisions | Silent drops made debugging difficult during pilot | Cancel timeout with no error response | S | Low | **Do immediately** |

## Category B: Strong Candidates for Phase 2

| # | Item | Why It Matters | Pilot Evidence | Effort | Risk | Recommendation |
|:-:|------|---------------|----------------|:------:|:----:|:--------------|
| B1 | Production journal (NVMe Direct I/O) | Current segmented journal uses stdio, not kernel-bypass I/O | Journal works but not production-grade performance | L | Medium | **Phase 2 priority** |
| B2 | DR / journal replication | Single-site only; any host failure = downtime | Warm restart works but no remote recovery | XL | High | **Phase 2, plan early** |
| B3 | Full FIX message replay from journal | ResendRequest responses use in-memory store only; old messages gap-filled | Bounded gap recovery sufficient for pilot | L | Medium | **Phase 2** |
| B4 | mTLS enforcement | Server TLS only; no client cert verification | TLS v1.3 works; mTLS config exists but not enforced | M | Low | **Phase 2** |
| B5 | Full RBAC on ops console | API key auth only; no user/role separation | Auth works but single-key model | M | Low | **Phase 2** |
| B6 | Multicast UDP market data | TCP/JSON only; not suitable for colocated HFT participants | Market data TCP feed validated | L | Medium | **Phase 2** |
| B7 | FIX session multi-message support | Each TCP connection handles one message (prototype limitation) | Works for pilot; not for persistent FIX sessions | L | Medium | **Phase 2** |

## Category C: Nice-to-Have / Defer

| # | Item | Why It Matters | Urgency | Recommendation |
|:-:|------|---------------|:-------:|:--------------|
| C1 | Auction mechanism | Opening/closing auctions for regulated markets | Low for pilot | **Defer to Phase 2+** |
| C2 | Pro-rata / parity matching | Required for derivatives, not for spot | None for FR-1 | **Defer to Phase 3** |
| C3 | Iceberg / pegged / stop orders | Advanced order types | None for pilot | **Defer to Phase 2+** |
| C4 | CCP clearing integration | Post-trade clearing | Not needed for pilot | **Defer to Phase 3** |
| C5 | Container / Docker packaging | Systemd is sufficient for single host | Low | **Defer** |
| C6 | Monitoring stack (Grafana) | Prometheus endpoint exists; full dashboards are ops work | Low | **Defer** |
| C7 | Top-of-book quantity tracking | Currently reports 0 | Cosmetic | **Defer** |

## Category D: Avoid for Now

| # | Item | Why Avoid | Revisit Trigger |
|:-:|------|-----------|-----------------|
| D1 | Hardware sequencer (FPGA) | No evidence from pilot that software sequencing is insufficient | Production latency benchmarks show need |
| D2 | Multi-partition daemon | Single partition handled all pilot load | Instrument count exceeds single-partition capacity |
| D3 | Architecture redesign | Architecture held up well — all bugs were wiring, not design | Fundamental performance or correctness issue found |
| D4 | WebSocket/REST gateway | No participant demand | External participant requests it |
| D5 | Real-time surveillance | File-based export sufficient for pilot/early regulatory | Regulator requires real-time |

## Architecture Change Assessment

| Question | Answer | Evidence |
|----------|--------|----------|
| Does single-threaded-per-partition hold? | **Yes** | No contention issues, deterministic matching correct |
| Does integer price encoding work? | **Yes** | All prices converted correctly from FIX decimal strings |
| Does the journal model need changes? | **No for FR-1; yes for production** | Segmented journal works; Direct I/O needed for production |
| Does the transport abstraction work? | **Yes** | TLS plugged in cleanly via ISocket/IServerSocket |
| Does config-driven startup work? | **Yes** | JSON instruments/sessions loaded correctly |
| Unexpected performance bottlenecks? | **None observed** | Pilot was not a load test |

## Decision Summary

| Decision | Recommended Choice | Rationale |
|----------|-------------------|-----------|
| Execute Category A items? | **Yes, immediately** | Low effort, high impact, directly from pilot evidence |
| Begin Phase 2 (Category B)? | **Yes, after Category A** | Core engine proven; daemon needs hardening for production |
| Wider external pilot? | **Yes, after Category A** | Platform validated; need real external participant feedback |
| Architecture changes? | **No** | Architecture held up well; bugs were all wiring-level |
