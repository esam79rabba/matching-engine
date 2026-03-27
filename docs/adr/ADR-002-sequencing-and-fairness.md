# ADR-002: Partition-Local Sequencing and Fairness Boundary

**Status**: DECIDED
**Date**: 2026-03-24
**Deciders**: Chief Architect, CTO, Market Microstructure Lead

## Context

Multiple gateways feed messages into the matching engine. The order in which these messages are processed determines execution priority. A fair and deterministic ordering mechanism is essential for regulatory compliance, participant trust, and provable replay. The sequencing model also determines whether a global ordering bottleneck exists.

## Decision

1. **Sequencing is partition-local.** Each partition has its own sequencer that assigns a monotonically increasing sequence number to every inbound message for that partition. There is no global total ordering across partitions.

2. **Global event identity** is formed as a composite: `[partition_id]:[local_sequence]`, which is unique across the platform without requiring a global sequencer.

3. **Fairness guarantees apply at the venue-controlled sequencing boundary** — the point where the sequencer assigns a sequence number. Network path variability outside the venue boundary (participant LAN, WAN, cross-connects) is outside the venue's fairness guarantee.

4. **Fairness targets are deployment-profile-dependent**: < 1 µs (Profile A, hardware sequencer), < 5 µs (Profile A/B, software time-based), < 50 µs (Profile C).

## Rationale

- **No global bottleneck**: Partition-local sequencing avoids a single global sequencer that would limit total platform throughput and create a single point of failure.
- **Deterministic per partition**: Within a partition, the sequence number defines the exact processing order. Replay from the journal reproduces the same sequence.
- **Fairness at the right boundary**: The venue controls the sequencer and the gateway infrastructure. It cannot control participant network paths. Publishing per-gateway latency statistics provides transparency for regulatory and participant review.
- **Hardware vs. software is a deployment choice**: The sequencing strategy (D-04, still open) determines which hardware is used, but the architectural model is the same either way.

## Consequences

- No total ordering across partitions. Consumers needing cross-partition ordering must use timestamps (with PTP accuracy tolerance).
- Cross-partition operations (mass cancel) are independently sequenced per partition — there is no atomic cross-partition operation.
- Fairness measurement requires hardware NIC timestamps at gateway ingress for accurate per-gateway statistics.
- The venue must publish fairness statistics for regulatory transparency (NFR-FAR-005).

## Alternatives Considered

| Alternative | Why Rejected |
|------------|-------------|
| Global sequencer (single point for all partitions) | Creates throughput bottleneck; single point of failure; adds cross-partition latency |
| No sequencer (rely on TCP arrival order) | Non-deterministic under multi-gateway; unfair if gateways have different latency |
| Timestamp-only ordering (no sequence numbers) | Clock drift between hosts introduces non-determinism; not provably replayable |

## Implementation Implications

- Sequencer must be co-located with (or on same host as) the matching engine partition
- PTP clock synchronization is mandatory for timestamp accuracy
- The sequencer-to-engine path is on the hot path; must be lock-free and sub-microsecond
- Hardware sequencer (FPGA) is an optional enhancement for Profile A deployments (D-04 open)

## References

- §11.2, §15.8 (NFR-FAR-001 through FAR-005), §21.1
