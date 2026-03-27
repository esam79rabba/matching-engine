# ADR-003: Regulatory-Grade and Reduced-Latency Durability Modes

**Status**: DECIDED
**Date**: 2026-03-24
**Deciders**: Chief Architect, CTO, Compliance Lead

## Context

The journal is the single source of truth for the matching engine. Every input message and every output event must be recorded for audit, replay, and recovery. The durability mode — when the journal write is confirmed as durable — directly trades off between latency and data-loss risk. Different venue types have different regulatory requirements for data loss.

## Decision

The platform supports two journal durability modes, selectable per deployment:

1. **Regulatory-Grade (default)**: The journal entry is durably written (fsync or equivalent) before the matching engine processes the message and before any acknowledgment is sent. This guarantees zero data loss on crash. Required for all regulated exchange, MTF, OTF, and ATS deployments.

2. **Reduced-Latency (opt-in)**: Journal entries are batched within a configurable window (e.g., 10–100 µs) before durable write. This reduces journal-related latency but introduces a data-loss window equal to the batch interval on crash. Suitable only for venues where the operator accepts this risk and the regulatory framework permits it (e.g., internal crossing networks, non-regulated digital-asset venues).

The durability mode is configured per deployment, not per instrument or per partition.

## Rationale

- **Regulatory default**: Most target venue types (exchanges, MTFs, ATSs) require complete audit trails with no data loss. Synchronous durability is the only way to guarantee this.
- **Latency cost is acceptable**: Synchronous fsync on NVMe adds approximately 2 µs. This is a small fraction of the total wire-to-wire latency and is acceptable for the Regulatory-Grade target.
- **Optionality for lower-control venues**: Some venue types (broker crossing, startup venues) may prioritize latency over absolute durability. The Reduced-Latency mode serves this need without requiring a separate codebase.
- **Single journal implementation**: Both modes use the same journal code path. The only difference is when fsync is called. This minimizes code divergence and testing surface.

## Consequences

- Regulatory-Grade mode adds ~2 µs per message to the hot path (fsync on NVMe).
- Reduced-Latency mode accepts data loss of up to the batch interval on crash.
- FR-1 baseline assumes Regulatory-Grade. Reduced-Latency is available but not the default.
- DR journal replication is a Phase 2+ capability. FR-1 uses local durable journal + warm restart (single site).

## Alternatives Considered

| Alternative | Why Rejected |
|------------|-------------|
| Always synchronous (no reduced-latency option) | Unnecessarily constrains venue types that don't need regulatory-grade durability |
| Always asynchronous (batch by default) | Unacceptable for regulated venues; data-loss risk too high |
| Per-partition durability mode | Adds complexity; a single deployment should have one durability posture |
| Separate journal implementations for each mode | Code divergence; double the testing burden; unnecessary since only fsync timing differs |

## Implementation Implications

- Journal write interface has a single `write(entry)` method; fsync strategy is injected via configuration
- Regulatory-Grade: `write()` calls `fsync()` before returning
- Reduced-Latency: `write()` appends to buffer; background thread calls `fsync()` at batch interval
- Both modes produce identical journal format; replay is the same
- Recovery from crash in Reduced-Latency mode replays to the last durable batch boundary (not the last written entry)

## References

- §06 TC-05, §11.3, §21.3.3, §27.2.1
