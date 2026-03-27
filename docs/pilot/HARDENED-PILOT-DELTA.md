# Hardened-Platform Pilot Delta Summary

**Date**: 2026-03-26
**Baseline**: Last pilot validated 18/18 scenarios (pre-Phase 2B)
**Current**: Phase 2B complete — 420 tests, production-hardened platform

---

## 1. What Changed Since Last Pilot

| Area | Before (Pilot Baseline) | After (Phase 2B) |
|------|------------------------|-------------------|
| TLS | Server TLS only | **mTLS available** — client cert verification supported |
| Journal | stdio FileJournal + SegmentedJournal | **DirectIOJournal** — O_DIRECT/F_NOCACHE, production-grade |
| FIX message store | In-memory (lost on restart) | **File-backed** — persistent across restarts |
| FIX replay | Replay from memory only; old msgs gap-filled | **Full replay with PossDupFlag=Y** from persistent store |
| Auth | API key + JWT login | **Per-endpoint RBAC** — viewer/operator/admin enforced |
| DR | Warm restart only; no replication | **Journal replication** — sync mode, simulated failover proven |
| Tests | 370 | **420** (+50 tests) |

## 2. Participant-Visible Improvements

### For the pilot participant to know:

**mTLS (if enabled)**:
- Participant can now present a client certificate
- Venue verifies client identity at transport layer
- Config: `tls.require_client_cert=true` + `tls.ca_file`

**Persistent FIX replay**:
- If participant disconnects and reconnects, venue can replay stored messages
- PossDupFlag (tag 43=Y) correctly set on replayed messages
- Messages survive daemon restart

**RBAC-protected ops**:
- Health, instruments, metrics: readable by all
- Halt/resume, kill switch, EOD: requires operator role
- Instrument/participant CRUD: requires admin role
- Viewer role for read-only portal access

**DR replication (operational awareness)**:
- Journal entries replicated to standby site in real-time
- Zero RPO in synchronous mode
- Simulated failover proven — standby journal produces identical partition state
- Manual failover path documented

### For the pilot participant NOT to worry about:

- DirectIOJournal internals (transparent behind IJournal interface)
- Replication wire protocol details
- RBAC role assignment (operator handles this)

## 3. Updated Pilot Readiness Note

### Go Criteria (Same 13 as Before + 4 New)

| # | Check | Status |
|:-:|-------|:------:|
| 1-13 | All original external pilot delta checks | Verified in previous pilot |
| 14 | mTLS tested (if enabled) | Verify with `openssl s_client -cert` |
| 15 | RBAC: viewer gets 403 on halt | Verify with viewer JWT |
| 16 | Persistent message store configured | `fix.message_store_dir` in config |
| 17 | Replication running (if DR active) | Check replicator stats |

### Known Limitations Update

| Limitation | Previous | Current |
|-----------|:--------:|:-------:|
| No mTLS | Yes | **Resolved** (configurable) |
| FIX replay bounded (memory only) | Yes | **Resolved** (persistent store) |
| No RBAC | Yes | **Resolved** (3 roles enforced) |
| No DR | Yes | **Resolved** (journal replication) |
| EOD via API trigger only | Still manual | Unchanged |
| Top-of-book qty was 0 | Fixed in 2A | Fixed |
| No auction | Still deferred | Unchanged |
| Multicast MD | TCP/JSON only | Unchanged |
| Automated failover | Manual only | Unchanged |
