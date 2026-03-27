# Phase 2B Engineering Plan

**Date**: 2026-03-26
**Prerequisite**: Phase 2A complete (auto EOD, TOB qty, FIX multi-message, 12 daemon tests)
**Portal**: Paused at functionally complete state (9 pages, 6 write actions)
**Source of truth**: Phase 2 Decision Pack (Category B items)

---

## 1. Phase 2B Workstream Recommendation

Phase 2B targets the gap between "pilot-validated prototype" and "production-deployable platform." The work falls into three parallel tracks:

```
Track 1: Transport & Security Hardening
  B4 mTLS → B8 Persistent message store → B5 RBAC

Track 2: Journal & Recovery Hardening
  B1 Production journal → B2 Full FIX replay → B3 DR planning

Track 3: Continuous Quality
  B7 Test expansion (runs throughout)
```

**Track 1 and Track 2 are independent** — they touch different modules and can run in parallel with separate engineers.

**Track 3 runs alongside both** — every feature addition includes tests.

---

## 2. Prioritized Engineering Backlog

| Priority | Item | Track | Effort | Why This Order |
|:--------:|------|:-----:|:------:|---------------|
| **1** | B4: mTLS enforcement | T1 | M | Low risk, high trust signal. Transport abstraction already supports it. Immediate participant confidence. |
| **2** | B1: Production journal (Direct I/O) | T2 | L | Required for production hardware validation. Current stdio journal works but isn't benchmarkable. |
| **3** | B8: Persistent FIX message store | T1 | M | In-memory store lost on restart. File-backed store enables true gap recovery across restarts. |
| **4** | B7: Integration test expansion | T3 | S | Continuous. Add tests alongside every item. Target: 20+ daemon integration tests. |
| **5** | B2: Full FIX replay from journal | T2 | L | Depends on B1 (production journal) and B8 (persistent store). True message replay for ResendRequest. |
| **6** | B5: Full RBAC | T1 | M | AuthService exists with 3 roles. Expand to per-endpoint role checks, user management API. |
| **7** | B3: DR architecture planning | T2 | M (plan) | Architecture document only. No implementation yet. Defines journal replication model. |
| **8** | B6: Multicast UDP market data | — | L | Defer to Phase 2C unless colocated participant demands it. |

---

## 3. Dependencies and Parallel Tracks

```
Track 1 (Transport/Security)     Track 2 (Journal/Recovery)     Track 3 (Quality)
─────────────────────────────    ─────────────────────────────  ──────────────────
B4: mTLS enforcement             B1: Production journal          B7: Tests (ongoing)
  ↓                                ↓
B8: Persistent message store     B2: Full FIX replay
  ↓                                ↓
B5: Full RBAC                    B3: DR planning (doc only)
```

| Dependency | From → To | Type |
|-----------|-----------|:----:|
| B2 depends on B1 | Journal format must be stable before replay reads it | Hard |
| B2 depends on B8 | Persistent store provides the message source for replay | Hard |
| B5 depends on B4 | mTLS client certs can feed into RBAC identity | Soft |
| B3 depends on B1 | DR planning needs to know the production journal format | Hard |
| B8 is independent | File-backed store, no journal dependency | None |
| B4 is independent | Transport layer change only | None |

**Maximum parallelism**: B4 + B1 + B7 can all start simultaneously.

---

## 4. First Milestone to Execute: Phase 2B-M1

**Phase 2B-M1: mTLS + Production Journal Foundation (2-3 weeks)**

| # | Item | Track | Acceptance Criteria |
|:-:|------|:-----:|-------------------|
| 1 | **mTLS enforcement on FIX port** | T1 | Client cert required and validated; connection rejected without valid client cert; existing TLS still works when mTLS disabled |
| 2 | **Production journal interface (O_DIRECT / io_uring stub)** | T2 | New journal implementation with Direct I/O writes; segmented rotation; CRC32 checksums; passes existing journal tests via IJournal interface |
| 3 | **Persistent FIX message store** | T1 | File-backed store replacing in-memory; survives daemon restart; ResendRequest replays from file |
| 4 | **Integration test expansion to 16+** | T3 | Cover mTLS connection, persistent store replay, journal integrity after Direct I/O writes |

**Why these four together**: They are independent (different modules), high-value, and create the foundation for B2, B3, and B5 in Phase 2B-M2.

### Implementation Notes

**mTLS (B4)**:
- The `TlsServerSocket` already accepts `TlsConfig` with `require_client_cert` and `ca_file`
- Daemon already reads `tls.require_client_cert` from config (commented out)
- Work: uncomment, test with self-signed client certs, add test, add rejection path

**Production Journal (B1)**:
- Current `FileJournal` uses `fopen`/`fwrite`/`fflush`/`fsync` (stdio)
- Production: `open(O_DIRECT | O_SYNC)` with aligned writes
- Implement as a new class `DirectIOJournal : public IJournal` alongside existing `FileJournal`
- Swap via config: `journal.type=direct_io` vs `journal.type=segmented`
- macOS note: `O_DIRECT` is Linux-only. On macOS, use `fcntl(F_NOCACHE)` as equivalent. Or build the interface and test on Linux.

**Persistent Message Store (B8)**:
- Current `FixMessageStore` is in-memory with eviction
- Add file backing: write each stored message to a file, read on startup
- Simple format: one file per session, append-only, sequence-indexed
- On restart: load from file, populate in-memory map

---

## 5. Why Portal Work Should Pause Here

| Reason | Detail |
|--------|--------|
| **Portal is functionally complete** | 9 pages covering all pilot ops + admin CRUD. No user has requested more. |
| **No new backend APIs needed for portal** | All Gate 2 APIs are built and tested (370 tests). Portal consumes them correctly. |
| **Portal polish is low-ROI** | Replacing `prompt()` with inline forms, adding loading spinners, etc. are cosmetic improvements that don't improve platform capability. |
| **Engineering bottleneck is platform, not UI** | The production journal, mTLS, and persistent store are on the critical path to production. The portal is not. |
| **Portal can resume later** | The single-file HTML approach makes it easy to add pages. No build toolchain to maintain while paused. |
| **Phase 2B creates new APIs** | When production journal and persistent store are built, new portal pages may be needed. Building UI before backend is settled creates throwaway work. |

**Portal pause criteria**: Resume portal work when (a) a user reports a missing portal capability that blocks their workflow, or (b) Phase 2B creates new backend APIs that need UI exposure.

---

## Phase 2B Timeline

| Milestone | Duration | Items | Gate |
|:---------:|:--------:|-------|------|
| 2B-M1 | 2-3 weeks | mTLS, Direct I/O journal, persistent msg store, tests | 16+ daemon tests, mTLS verified, journal benchmarkable |
| 2B-M2 | 3-4 weeks | Full FIX replay, RBAC expansion, DR architecture doc | Replay from persistent store, role-gated endpoints, DR design reviewed |
| 2B-M3 | 2-3 weeks | DR journal replication (if approved), multicast MD (if demanded) | Replication operational or explicitly deferred |

**Total Phase 2B estimate**: 7-10 weeks.

---

## Immediate Next Action

**Start Phase 2B-M1.** Say "Start 2B-M1" when ready. The first engineering tasks are:

1. Enable mTLS in daemon config + add mTLS integration test
2. Create `DirectIOJournal` class implementing `IJournal`
3. Convert `FixMessageStore` to file-backed
4. Expand daemon integration tests to 16+
