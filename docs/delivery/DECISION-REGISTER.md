# Decision Register

**Last updated**: 2026-03-25

---

## Closed Decisions

| ID | Decision | Choice | Decided By | Date | Status |
|----|----------|--------|------------|------|:------:|
| D-01 | Hot-path programming language | **C++** | Founder | 2026-03-25 | **CLOSED** |
| D-02 | FR-1 asset class | **Digital assets** (spot order-book) | Founder | 2026-03-25 | **CLOSED** |
| D-03 | Primary jurisdiction | **ADGM / FSRA** | Founder | 2026-03-25 | **CLOSED** |

## Closed Decisions (Formerly Working Assumptions)

| ID | Decision | Choice | Rationale | Status |
|----|----------|--------|-----------|:------:|
| D-04 | Sequencing strategy | **Software time-based with PTP** | Lower risk for FR-1; hardware sequencing is Phase 2+ option | **CLOSED** |
| D-05 | Hosting approach | **Cloud dev/test now; ADGM-aligned production hosting in parallel** | Avoid blocking dev on colo procurement | **CLOSED** |
| D-06 | Journal implementation | **Custom local append-only journal (C++, segmented rotation, CRC32)** | Best alignment with C++ hot path | **CLOSED** |

## Previously Frozen Architecture Decisions

| ID | Decision | Status |
|----|----------|:------:|
| ADR-001 | Single-threaded per partition | FROZEN |
| ADR-002 | Partition-local monotonic sequencing + fairness boundary | FROZEN |
| ADR-003 | Regulatory-Grade / Reduced-Latency durability modes | FROZEN |
| ADR-004 | Market-order residual policy (cancel / collar) | FROZEN |
| ADR-005 | FR-1 vs Phase 2+ scope boundaries | FROZEN |

## Implementation Status

All 6 founder decisions are closed. Milestones M1–M14 complete (318 tests, 21 modules).
The FR-1 prototype is pilot-ready with TLS, auth, metrics, and bounded FIX gap recovery.
See `IMPLEMENTATION-STATUS-M14.md` for full runtime capability summary.

## Remaining Open Items

| Item | Description | Owner | Target | Status |
|------|------------|-------|--------|:------:|
| Fee model | Maker-taker / flat / tiered for digital assets | Product | Month 2 | Prototype EOD fees implemented |
| ADGM license timeline | Engagement with FSRA; application planning | Compliance | Month 1-2 | Pre-application brief prepared |
| Production hosting site | ADGM-aligned facility recommendation | Infrastructure | Month 2-3 | Cloud dev active; colo TBD |
| Market maker partnerships | Digital-asset MM recruitment | BizDev | Month 2-4 | Pitch materials ready |
| Production NVMe journal | Segmented journal implemented; kernel-bypass Direct I/O deferred to Phase 2+ | Engineering | Phase 2+ | Deferred |
| TLS on transports | TLS implemented on FIX path via OpenSSL; mTLS enforcement Phase 2+ | Security | Phase 2+ | FIX TLS operational |
| Auction mechanism | Opening/closing auctions | Engineering | Phase 2 | State hooks implemented |
