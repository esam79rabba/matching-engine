# ADR-001: Single-Threaded Per Partition Model

**Status**: DECIDED
**Date**: 2026-03-24
**Deciders**: Chief Architect, CTO

## Context

The matching engine must process orders deterministically — identical input sequences must always produce identical output. This property is essential for journal replay, audit reconstruction, regulatory proof, and disaster recovery. The threading model is the most fundamental architectural choice because it directly determines whether determinism is trivially provable or requires complex verification.

## Decision

The matching engine uses a **single-threaded-per-partition** processing model. Each partition is assigned one or more instruments (order books). A single dedicated thread processes all messages for its assigned instruments in strict sequence order. High-activity instruments may be isolated to a dedicated partition.

There is **no cross-partition matching** — each instrument belongs to exactly one partition at any time.

## Rationale

- **Determinism is trivial**: A single thread with no shared mutable state and no external non-deterministic inputs is deterministic by construction. No locks, no race conditions, no ordering ambiguity.
- **Simplicity**: Reasoning about correctness, testing, and debugging are dramatically simpler with single-threaded execution.
- **Scalability via partitioning**: Horizontal scaling is achieved by adding partitions (more CPU cores), not by making individual partitions multi-threaded. This trades per-partition throughput ceiling for system-wide scalability.
- **Proven pattern**: Major exchanges (LSEG Millennium, Nasdaq INET) use single-threaded-per-book or per-partition models for the same reasons.

## Consequences

- Per-partition throughput is bounded by single-core performance (~1M msg/s on modern hardware). This is sufficient for most instruments.
- Extremely high-activity instruments may require dedicated partitions, reducing instrument density.
- Cross-instrument operations (e.g., mass cancel for a participant across all instruments) require fan-out messages to each affected partition, adding latency for those operations.
- Instrument reassignment between partitions requires a maintenance window.

## Alternatives Considered

| Alternative | Why Rejected |
|------------|-------------|
| Multi-threaded per partition with deterministic scheduling | Theoretically higher throughput, but verification of determinism is extremely complex; adds locks or coordination overhead; not worth the risk for FR-1 |
| Single-threaded per instrument (one thread per book) | Too many threads for large instrument universes; OS scheduling overhead; similar determinism benefit but worse resource utilization |
| Lock-free multi-threaded | Requires formal verification of ordering guarantees; extremely difficult to test and debug; unsuitable for a new platform |

## Implementation Implications

- CPU core pinning is mandatory for partition threads (eliminate context switches)
- Each partition pre-allocates its own memory pool (NUMA-local)
- The partition owns its own journal segment
- Inter-partition communication is via message passing only (no shared state)

## References

- §06 TC-01, §07 MS-01, §11.1, §11.7, §27.2.2
