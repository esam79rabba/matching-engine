# Journal Implementation Comparison — Decision Support

**Status**: Evaluation artifact — all options are candidates, not approved selections
**Purpose**: Support D-06 (journal implementation) decision
**Source**: §11.3 Journal/WAL (frozen baseline)

---

## Requirements from Baseline

The journal must support:
- Append-only, immutable writes
- Regulatory-Grade mode: durable write (fsync) before engine processes message
- Reduced-Latency mode (optional): batched writes within configurable window
- Sequential read for replay from any sequence number
- Segment rotation (hourly or by size)
- Checksums per entry and per segment
- Retention: 90 days online, 7 years archived
- Replication to DR standby (Phase 2)

---

## Candidate Options

### Option A: Custom Local Journal (NVMe + Direct I/O)

| Factor | Assessment |
|--------|-----------|
| **Approach** | Custom append-only file on NVMe SSD. Direct I/O bypasses page cache. Application manages write buffering and fsync. |
| **Language fit** | C++ or Rust: excellent. Java: possible but less natural for Direct I/O. |
| **Durability** | Regulatory-Grade: fsync per entry (~2 µs on NVMe). Reduced-Latency: batch + periodic fsync. |
| **Performance** | Best possible — no abstraction overhead; direct NVMe access |
| **Replay** | Sequential file read; offset index by sequence number |
| **Segment rotation** | Must implement (rotate by time or size; background compression) |
| **Checksums** | Must implement (CRC32 per entry; SHA-256 per segment) |
| **Replication** | Must build (file-based replication or custom TCP stream) |
| **Complexity** | Medium-High — must handle corruption recovery, segment management, compaction |
| **Development effort** | 4–6 weeks for core; 2–4 weeks for segment management and checksums |
| **Pros** | Maximum control; best latency; no external dependency; fits C++/Rust naturally |
| **Cons** | Must build everything; replication is extra work; more testing burden |
| **Recommendation condition** | Best choice if hot-path language is C++ or Rust |

### Option B: Aeron Archive

| Factor | Assessment |
|--------|-----------|
| **Approach** | Aeron messaging library with Archive extension for persistent replay. |
| **Language fit** | Java: native. C++: Aeron has a C++ client (aeron-archive-client) but Archive server is Java. |
| **Durability** | Aeron Archive persists to disk; can configure for sync or async. |
| **Performance** | Very good — designed for financial messaging; sub-microsecond IPC |
| **Replay** | Built-in replay from any position; designed for this use case |
| **Segment rotation** | Built-in |
| **Checksums** | Built-in (Aeron uses CRC) |
| **Replication** | Aeron Cluster provides replicated state; or Archive can be file-replicated |
| **Complexity** | Medium — learning curve for Aeron model; but well-documented |
| **Development effort** | 2–4 weeks to integrate with engine |
| **Pros** | Battle-tested in financial systems; built-in replay; built-in IPC for engine ↔ services |
| **Cons** | Java dependency for Archive server; Aeron model requires understanding; less control than custom |
| **Recommendation condition** | Best choice if Java is the hot-path language, or if engine ↔ gateway IPC also uses Aeron |

### Option C: Chronicle Queue

| Factor | Assessment |
|--------|-----------|
| **Approach** | Java library for low-latency, persisted messaging. Memory-mapped files. |
| **Language fit** | Java: native. C++/Rust: not practical (Java-only library). |
| **Durability** | Memory-mapped files with OS-level fsync; configurable sync mode. |
| **Performance** | Good — designed for financial journaling; microsecond latency |
| **Replay** | Built-in sequential replay; tailer API |
| **Segment rotation** | Built-in (daily rolling by default) |
| **Checksums** | Partial — relies on OS page cache integrity |
| **Replication** | Chronicle Queue Enterprise has replication; open-source does not |
| **Complexity** | Low — straightforward API |
| **Development effort** | 1–2 weeks to integrate |
| **Pros** | Simplest integration; proven for financial journaling; fast development |
| **Cons** | Java-only; open-source lacks replication; Enterprise version has license cost; less control over sync behavior |
| **Recommendation condition** | Only if Java is the hot-path language AND simplicity is prioritized over control |

## Comparison Matrix

| Factor | Custom (NVMe) | Aeron Archive | Chronicle Queue |
|--------|:-------------:|:-------------:|:---------------:|
| C++ / Rust fit | Excellent | Partial (C++ client, Java server) | Not practical |
| Java fit | Possible | Excellent | Excellent |
| Latency (Reg-Grade) | ~2 µs | ~3–5 µs | ~3–5 µs |
| Latency (Reduced) | < 500 ns amortized | ~1 µs | ~1 µs |
| Replay built-in | No (build) | Yes | Yes |
| Replication built-in | No (build) | Yes (Cluster) | Enterprise only |
| Checksums built-in | No (build) | Yes | Partial |
| Segment rotation | No (build) | Yes | Yes |
| Development effort | 4–6 weeks | 2–4 weeks | 1–2 weeks |
| External dependency | None | Aeron (OSS) | Chronicle (OSS/Enterprise) |
| Control level | Full | High | Medium |
| FR-1 risk | Medium (must test thoroughly) | Low | Low |

## Provisional Recommendation

| If D-01 = ... | Recommended journal approach |
|:-------------:|------------------------------|
| **C++** | **Custom (NVMe + Direct I/O)** — natural fit; maximum control; no Java dependency |
| **Rust** | **Custom (NVMe + Direct I/O)** — same rationale; Rust's unsafe blocks handle Direct I/O well |
| **Java** | **Aeron Archive** — proven; built-in replay and IPC; fits naturally with Aeron for engine ↔ gateway messaging |

*All named products are candidate options for evaluation, not approved selections.*
