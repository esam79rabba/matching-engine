# DR Architecture — Design Document

**Date**: 2026-03-26
**Status**: Decision-ready design — not approved for implementation
**Scope**: Journal replication, failover sequence, RPO/RTO targets
**Prerequisite**: Production journal (DirectIOJournal) operational, persistent FIX message store operational

---

## 1. Problem Statement

The FR-1 platform currently operates as a single-site system with warm restart from local journal + snapshot. If the host fails:
- **RPO**: Last fsync'd journal entry (near-zero for Regulatory-Grade mode)
- **RTO**: Warm restart time (< 60s for 100K orders)
- **Failure mode**: Full outage until the same host recovers

For production deployment, a DR site must be able to take over if the primary site fails.

---

## 2. Replication Model

### Recommended: Active-Passive with Synchronous Journal Replication

```
PRIMARY SITE                          DR SITE
┌────────────────────┐                ┌────────────────────┐
│ Partition           │                │ Partition (standby) │
│   ↓                │                │   ↑                │
│ Journal append     │ ──sync──────→  │ Journal receive    │
│   ↓                │                │   (append locally) │
│ Output bus         │                │                    │
│   ↓                │                │ Snapshot (periodic) │
│ Market data        │                │                    │
│ Drop copy          │                │ Ready for promote  │
└────────────────────┘                └────────────────────┘
```

| Property | Value |
|----------|-------|
| Replication unit | Journal entry (same binary format as DirectIOJournal) |
| Replication mode | Synchronous (primary waits for DR ack before processing) |
| Replication transport | TCP with TLS between sites |
| DR site state | Cold standby — receives journal entries, does NOT process them |
| Failover trigger | Manual operator decision (automated failover is Phase 3+) |

### Why Synchronous

- **Regulatory-Grade durability** requires zero data loss. The journal entry must be durable at both sites before the matching engine processes the order.
- Synchronous replication adds latency (~1-5ms for same-region, ~10-50ms cross-region). This is acceptable for Profile B/C deployments. Profile A (ultra-low-latency) may use asynchronous replication with documented RPO > 0.

### Why NOT Active-Active

- Active-active matching across sites requires distributed consensus (Raft/Paxos) or split-brain resolution. This adds enormous complexity for near-zero benefit in a single-partition venue.
- Active-active is a Phase 4+ consideration for 24/7 global venues.

---

## 3. Journal Format Requirements for DR

The DirectIOJournal record format must support replication:

| Requirement | Current Status | DR Need |
|------------|:--------------:|---------|
| Self-contained records | Yes (header + payload + CRC32) | No change |
| Sequence continuity | Yes (monotonic per partition) | DR site validates sequence continuity |
| CRC32 integrity | Yes | DR site verifies CRC on receive |
| Segment boundaries | Yes (rotation by size) | DR site follows same rotation |
| Timestamp | Yes (in header) | Used for lag monitoring |

**No journal format changes needed for DR.** The existing record format is replication-ready.

---

## 4. Replication Components (To Build)

| Component | Location | Purpose |
|-----------|----------|---------|
| **JournalReplicator** | Primary site | Reads new journal entries, sends to DR via TCP/TLS |
| **JournalReceiver** | DR site | Receives entries, appends to local journal, sends ack |
| **ReplicationMonitor** | Both sites | Tracks lag (entries behind), alerts on threshold |
| **FailoverController** | DR site | Promotes DR to primary on operator command |

### JournalReplicator (Primary)

```
Tail the journal file (or receive from write-ahead callback)
  → Serialize entry to wire format (same as file format)
  → Send over TLS connection to DR
  → Wait for ack (synchronous mode)
  → If ack timeout: alert, optionally switch to async mode
```

### JournalReceiver (DR)

```
Listen on replication port (TLS)
  → Receive entry
  → Verify CRC32
  → Verify sequence continuity (expected = last_received + 1)
  → Append to local DirectIOJournal
  → Send ack
```

### Replication Lag Monitoring

| Metric | Source | Alert Threshold |
|--------|--------|:---------------:|
| Entries behind | primary.last_seq - dr.last_acked_seq | > 100 |
| Replication latency (ms) | Round-trip ack time | > 50ms (same region) |
| DR connection status | TCP/TLS heartbeat | Disconnected > 5s |

---

## 5. Failover Sequence

### Automatic Detection (Phase 3+)

For Phase 2, failover is **operator-initiated only**. Automatic detection is deferred.

### Manual Failover Procedure

```
1. DETECT:  Primary site unresponsive (operator confirms)
2. DECIDE:  Operator decides to failover (not automated)
3. FENCE:   Ensure primary cannot resume writing (network fence or shutdown)
4. VERIFY:  DR site verifies journal integrity (CRC + sequence)
5. PROMOTE: DR site replays journal to rebuild partition state
            (StartupController::cold_start from DR journal)
6. RESUME:  DR site starts accepting FIX connections
7. NOTIFY:  Participants reconnect to DR site
            (DNS update or failover IP)
```

### Estimated Failover Timeline

| Step | Duration | Notes |
|------|:--------:|-------|
| Detection | 2-30 seconds | Operator must notice + confirm |
| Decision | 30-300 seconds | Human decision time |
| Fencing | 5-10 seconds | Network-level |
| Verification | 5-30 seconds | Journal integrity check |
| Promotion (replay) | 10-60 seconds | Depends on journal size since last snapshot |
| Resume | 5-10 seconds | Start acceptor, open ports |
| **Total RTO** | **1-7 minutes** | Dominated by human decision time |

---

## 6. RPO / RTO Targets

| Mode | RPO | RTO | Use Case |
|------|:---:|:---:|----------|
| Synchronous replication | **Zero** | 1-7 minutes | Regulatory-Grade (default) |
| Asynchronous replication | Last batch interval (1-10ms) | 1-7 minutes | Reduced-Latency (opt-in) |
| No replication (current) | Zero (local) | 30-60 seconds (warm restart) | Pilot / dev |

---

## 7. Network Architecture

```
PRIMARY SITE                         DR SITE
┌──────────────────────┐             ┌──────────────────────┐
│ Matching Engine       │             │ Matching Engine       │
│   ├─ FIX port 9876   │             │   ├─ FIX port 9876   │
│   ├─ MD port 9877    │             │   ├─ MD port 9877    │
│   ├─ Ops port 8180   │             │   ├─ Ops port 8180   │
│   └─ Repl port 9878  │──TLS──────→│   └─ Repl port 9878  │
│                       │             │                       │
│ Journal (NVMe)        │             │ Journal (NVMe)        │
│ Snapshots             │             │ Snapshots (from repl) │
└──────────────────────┘             └──────────────────────┘
         │                                     │
    ┌────┴────┐                          ┌─────┴────┐
    │ VIP/DNS │                          │ VIP/DNS  │
    │ (active)│                          │(standby) │
    └─────────┘                          └──────────┘
```

| Port | Protocol | Purpose |
|:----:|----------|---------|
| 9876 | FIX/TLS | Participant order entry |
| 9877 | TCP | Market data |
| 9878 | TLS | Journal replication (new) |
| 8180 | HTTP | Ops console |

---

## 8. What to Build When

| Phase | Item | Effort |
|:-----:|------|:------:|
| **2B-M3** | JournalReplicator + JournalReceiver (core replication) | L |
| **2B-M3** | Replication monitoring metrics | S |
| **2C** | FailoverController (manual promote) | M |
| **2C** | Participant reconnection support | M |
| **Phase 3** | Automatic failure detection | L |
| **Phase 3** | Automated failover (no human decision) | XL |
| **Phase 4** | Active-active (if 24/7 required) | XL |

---

## 9. Decision Points

| Decision | Options | Recommended | Rationale |
|----------|---------|:-----------:|-----------|
| Replication mode | Sync / Async / Both | **Both** (config-selectable) | Regulatory-Grade = sync; Reduced-Latency = async |
| Failover trigger | Manual / Auto | **Manual** (Phase 2) | Simpler, safer; auto in Phase 3 |
| Snapshot replication | Replicate / Rebuild | **Rebuild** from journal | Simpler; snapshot capture on DR after promote |
| FIX session state | Replicate / Re-logon | **Re-logon** | Participants reconnect after failover |
| Market data | Replicate / Rebuild | **Rebuild** on DR from book state | No MD replication needed |

---

## 10. Risks

| Risk | Impact | Mitigation |
|------|--------|-----------|
| Synchronous replication latency | Adds 1-50ms per order | Async mode available; same-region deployment minimizes |
| Split-brain (both sites think they're primary) | Data divergence | Network fencing; operator-only failover; sequence validation |
| DR journal divergence | Inconsistent state | CRC + sequence verification on every received entry |
| Large journal replay on promote | Long RTO | Regular snapshots reduce replay window |
| Replication link failure | DR falls behind | Alert on lag; buffer entries; reconnect with catch-up |

---

## 11. Summary

The DR architecture is **active-passive with synchronous journal replication, manual failover, and operator-initiated promotion**. No journal format changes are needed. The replication transport uses TLS on a dedicated port. RPO is zero (synchronous mode). RTO is 1-7 minutes (dominated by operator decision time).

**Implementation can begin in Phase 2B-M3** after the full FIX replay and RBAC are complete. The first deliverable is the JournalReplicator + JournalReceiver pair with replication monitoring metrics.
