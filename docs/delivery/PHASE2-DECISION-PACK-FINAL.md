# Phase 2 Decision Pack — Final

**Date**: 2026-03-26
**Basis**: 6 pilot sessions, 18/18 best result, 8 issues found and resolved, zero open P1/P2
**Status**: Decision-ready

---

## Category A: Must-Do Next

Items that would block or embarrass a broader rollout. Evidence-backed.

| # | Item | Why It Matters | Pilot Evidence | Urgency | Complexity | Recommendation |
|:-:|------|---------------|----------------|:-------:|:----------:|:---------------|
| A1 | **Automatic EOD trigger** | Operators must remember to trigger EOD via API; missed trigger = missing reports | EOD worked but required manual POST in every session | High | S | **Implement: session-close hook triggers EOD automatically** |
| A2 | **Top-of-book quantity** | Qty shows 0 in market data; cosmetic but confusing for participants | Visible in all MD events across all sessions | Medium | S | **Fix: track qty at top price level in OrderBook** |
| A3 | **FIX multi-message sessions** | Each TCP connection handles one message; real FIX clients keep connections open | All pilot scenarios used reconnect-per-message pattern | High | M | **Implement: persistent FIX session with message loop** |

## Category B: Strong Candidates for Phase 2

Justified by pilot experience and needed for production path.

| # | Item | Why It Matters | Pilot Evidence | Complexity | Risk | Recommendation |
|:-:|------|---------------|----------------|:----------:|:----:|:---------------|
| B1 | **Production journal (NVMe Direct I/O)** | Current stdio journal is not production-grade I/O | Journal works but not benchmarked for production latency | L | Medium | **Phase 2 priority — needed for production hardware validation** |
| B2 | **Full FIX message replay from journal** | ResendRequest only replays in-memory stored messages; old messages gap-filled | Bounded recovery worked for pilot; production needs full replay | L | Medium | **Phase 2 — requires journal-to-FIX message reconstruction** |
| B3 | **DR / journal replication** | Single-site only; host failure = downtime | Warm restart worked; no DR tested | XL | High | **Phase 2 — plan architecture early, implement incrementally** |
| B4 | **mTLS enforcement** | Server TLS only; no client cert verification | TLS v1.3 worked; mTLS config exists but not enforced | M | Low | **Phase 2 — low risk, high trust signal for institutional participants** |
| B5 | **Full RBAC on ops console** | API key auth is single-key; no user/role separation | API key auth worked; insufficient for multi-operator production | M | Low | **Phase 2** |
| B6 | **Multicast UDP market data** | TCP/JSON is not suitable for colocated HFT participants | MD TCP feed validated with 8 events; not production transport | L | Medium | **Phase 2 — needed for production market data** |
| B7 | **Daemon integration test expansion** | 6 tests caught both P1 bugs; more coverage needed | Integration tests proved invaluable | S | Low | **Phase 2 — continuous expansion** |
| B8 | **Persistent FIX message store** | In-memory store lost on restart | Message store works in-memory; restart loses replay capability | M | Low | **Phase 2** |

## Category C: Nice to Have

Valuable but not blocking production path.

| # | Item | Why It Matters | Urgency | Recommendation |
|:-:|------|---------------|:-------:|:---------------|
| C1 | Container / Docker packaging | Portability for different deployment environments | Low | **Defer until deployment target requires it** |
| C2 | Prometheus/Grafana dashboards | Metrics endpoint exists; dashboards are ops work | Low | **Ops team can build from existing endpoint** |
| C3 | Config validation at startup | Daemon accepts invalid config silently | Low | **Nice to have** |
| C4 | Participant self-service onboarding | Manual CompID assignment works | Low | **Defer** |
| C5 | Structured log file output | Currently stdout/stderr; no file rotation | Low | **Defer — systemd journal captures logs** |

## Category D: Defer

No pilot evidence justifies building these now.

| # | Item | Why Defer | Revisit Trigger |
|:-:|------|-----------|-----------------|
| D1 | Auction mechanism | No regulatory requirement for pilot scope; continuous trading validated | FSRA requires auctions for specific instruments |
| D2 | Pro-rata / parity matching | Required for derivatives; not for spot digital assets | Derivatives listing planned |
| D3 | Iceberg / pegged / stop orders | Advanced order types; no participant requested | Participant demand |
| D4 | CCP clearing integration | Post-trade clearing not needed for pilot/early production | Regulatory requirement or participant demand |
| D5 | WebSocket / REST gateway | No participant requested; FIX 4.4 sufficient | Retail or API-first participant demand |

## Category E: Avoid for Now

| # | Item | Why Avoid | Bar for Reconsideration |
|:-:|------|-----------|------------------------|
| E1 | Hardware sequencer (FPGA) | No evidence software sequencing is insufficient | Production benchmarks prove need |
| E2 | Multi-partition daemon | Single partition handled all pilot scenarios | Instrument count exceeds capacity |
| E3 | Architecture redesign | Architecture held up across 6 sessions; all bugs were wiring | Fundamental correctness or scalability failure |
| E4 | Full FIX 5.0 / FIXT migration | FIX 4.4 works; no participant requested upgrade | Participant requires 5.0 |

## Architecture Assessment

| Question | Pilot Answer | Evidence |
|----------|:------------:|---------|
| Single-threaded-per-partition holds? | **Yes** | No contention across 6 sessions |
| Integer price encoding works? | **Yes** | All prices converted correctly |
| Journal model sufficient for FR-1? | **Yes** | Segmented journal operated correctly |
| Transport abstraction works? | **Yes** | TLS plugged in cleanly |
| Config-driven startup works? | **Yes** | JSON loading correct across sessions |
| Any architecture changes needed? | **No** | All 8 issues were wiring, not design |

## Recommended Phase 2 Starting Scope

**Phase 2A (Immediate — 2-4 weeks):**
- A1: Automatic EOD trigger
- A2: Top-of-book quantity fix
- A3: FIX multi-message sessions
- B7: Integration test expansion

**Phase 2B (Next — 4-8 weeks):**
- B1: Production journal
- B4: mTLS
- B5: RBAC
- B8: Persistent message store

**Phase 2C (Following — 8-16 weeks):**
- B2: Full FIX replay
- B3: DR/replication
- B6: Multicast market data

## Decision Summary

| Decision | Recommended | Rationale |
|----------|:----------:|-----------|
| Freeze FR-1 pilot-validated state? | **Yes** | Tag `fr1-pilot-validated-20260326` |
| Start Phase 2A immediately? | **Yes** | 3 items directly from pilot evidence |
| Architecture changes? | **No** | Architecture validated across 6 sessions |
| Invite real external participant? | **Yes, in parallel with Phase 2A** | Platform proven; process proven |
