# Specification Package — Review, Normalization, and Revision Report

**Version**: 1.0
**Date**: 2026-03-24
**Reviewer Role**: Senior Capital-Markets Solution Architect, Exchange-Platform Reviewer, Technical Editor, Software-Specification Quality Auditor

---

## 1. Executive Review Summary

### 1.1 Overall Assessment

The 30-file specification package is **substantively strong**. It demonstrates genuine domain knowledge across market microstructure, exchange operations, regulatory frameworks, and low-latency systems engineering. The coverage is unusually complete for a first draft — most commercial exchange specification efforts at this stage omit half of what is present here.

### 1.2 Strengths

1. **Comprehensive scope**: Covers all layers from matching kernel through operations, compliance, testing, and deployment
2. **Market microstructure depth**: Auction mechanisms, priority models, STP, CoD, tick regimes are specified with real-world accuracy
3. **Multi-asset and multi-jurisdiction thinking**: Not locked into one asset class or regulatory regime
4. **Separation of concerns**: Clear layered architecture distinguishing hot path from control plane
5. **Professional data model**: Entity definitions, event taxonomy, and ID strategies are implementable
6. **Realistic tradeoff analysis**: The tradeoff section (§27) is honest about build choices

### 1.3 Weaknesses (Addressed in This Report)

1. **Architecture terminology inconsistency**: "single-threaded-per-book" vs. "single-threaded-per-partition" appears in different files
2. **Sequencing model confusion**: Global vs. partition-local sequencing is inconsistently described
3. **Performance numbers presented as universal guarantees**: Sub-µs matching, < 500ns journal writes treated as mandatory product specs rather than deployment-profile-dependent targets
4. **Journaling durability mode contradiction**: Synchronous durable write is both mandated (§6, §11) and then batched/async is discussed as optimization (§21) without reconciliation
5. **Market order residual handling is unsafe**: "Convert to limit at last fill price" is a dangerous default
6. **Overclaimed configurability**: "New market models configurable without code changes" is too broad
7. **MVP/MVR terminology inconsistency**: Both terms used; Phase 1 scope is too large for a true "minimum viable" anything
8. **Editorial voice drift**: Some sections read as product marketing; others read as engineering specification

### 1.4 Readiness Level

| Audience | Readiness | Notes |
|----------|-----------|-------|
| Stakeholder workshops | **Ready after corrections in this report** | Core content sufficient for productive workshops |
| Architecture baseline | **Ready after corrections** | Architecture is sound; needs terminology normalization |
| Regulator pre-review | **Not ready** | Requires jurisdiction-specific compliance overlays and formal rule-book draft |
| Implementation kickoff | **Not ready** | Requires design-decision resolution (§29.3 open questions) and detailed interface specifications |
| Vendor evaluation | **Ready after corrections** | Strong enough for technology assessment and scoping |

---

## 2. Package-Wide Issues

| Issue ID | Category | Severity | Description | Affected Files | Recommended Fix |
|----------|----------|----------|-------------|---------------|----------------|
| PWI-001 | Architecture | **Critical** | "Single-threaded-per-book" (01, 06, 07) contradicts "single-threaded-per-partition" (11, 27). These are different models. | 01, 06, 07, 11, 15, 27 | Normalize to "single-threaded per partition; each partition manages one or more order books" |
| PWI-002 | Sequencing | **Critical** | REQ-SEQ-001 says "globally unique, monotonically increasing sequence number" implying a global sequencer, while §11.7 and §17 describe partition-local sequences. | 11, 15, 17 | Normalize to partition-local monotonic sequencing; global identity = `partition_id:local_sequence` composite |
| PWI-003 | Performance | **High** | Latency targets (p50 < 1µs, p99 < 5µs matching; < 500ns journal) presented as mandatory NFRs without deployment-profile qualification | 01, 02, 11, 15, 21, 26, 29 | Introduce three deployment profiles with profile-specific targets; change "Mandatory" to "Target for Profile A" |
| PWI-004 | Durability | **High** | §6 TC-05 mandates "Journal write must be durable before execution acknowledgment (fsync)." §21.3.3 lists "Write batching: Batch journal entries within configurable window (trade latency vs. durability)." §27.2.1 recommends sync. These are contradictory. | 06, 11, 21, 27 | Define two durability modes: Regulatory-Grade (sync, default) and Reduced-Latency (async, opt-in with documented risk). State which venues may use which. |
| PWI-005 | Market Orders | **High** | §11.4.3 REQ-OT-001 allows "convert to limit at last fill price" as residual handling for market orders. This is dangerous: in a thin book, the last fill price can be far from fair value. | 11, 14 | Replace with three safe options: (a) cancel residual, (b) convert to market-to-limit with protection collar, (c) venue-rulebook-defined policy. Remove "last fill price" as a named option. |
| PWI-006 | Configurability | **High** | Multiple files (01, 02, 07, 15) claim new market models, order types, and matching algorithms can be deployed "without code changes." This is only true for parameter changes and feature-flag enablement, not for materially new logic. | 01, 02, 07, 15 | Distinguish: "configuration" (parameters, feature flags, entitlements) vs. "controlled software release" (new algorithms, new order semantics, new workflow logic) |
| PWI-007 | Terminology | **Medium** | "MVP" appears in some files, "MVR" (Minimal Viable Release) in others. Phase 1 scope (opening/closing auctions, FIX + binary gateways, full audit trail) exceeds what most teams would call "minimal viable." | 00, 01, 06, 26, 27, 28, 29 | Standardize on "Foundation Release" (FR-1) for Phase 1. Reserve "MVP" for informal conversation only. |
| PWI-008 | Fairness NFR | **Medium** | NFR-FAR-001 requires "< 1 µs" fairness delta between gateways. This is extremely aggressive and may be unachievable without hardware sequencing. It should be a profile-dependent target. | 15, 21 | Restate as deployment-profile-dependent: < 1µs for hardware-sequenced profile, < 5µs for software time-based, "best-effort with published statistics" for standard exchange. |
| PWI-009 | Phase 1 scope | **Medium** | Phase 1 includes opening/closing auctions, which are complex to implement correctly (random end, equilibrium algorithm, surplus handling). Many real exchange MVPs launch with continuous-only and add auctions in the next phase. | 26, 29 | Flag auctions as "Foundation Release stretch goal" or move to Phase 2 with explicit decision point. |
| PWI-010 | Stakeholder persona | **Low** | §4.3 states Market Maker gateway latency target "< 1 µs gateway" which is a wire-to-wire target achievable only with FPGA, contradicting the recommendation to defer FPGA to Phase 4. | 04, 15, 27 | Change to "ultra-low (deployment-profile-dependent; see §15.1)" |
| PWI-011 | Cost claims | **Low** | §5.4 claims "cuts technology cost by 50%+" without qualification. This is marketing, not specification. | 05 | Soften to "intended to be competitive with or below tier-1 vendor licensing cost" |
| PWI-012 | GC pause target | **Low** | §21.2.2 states "GC pauses (if JVM) < 1 µs." Sub-microsecond GC pauses are not achievable on any current JVM. Even Azul C4 targets sub-millisecond. | 21 | Change to "< 100 µs for P99 GC pause" or "no GC on hot path (off-heap allocation)" |

---

## 3. Normalization Decisions

The following canonical standards shall apply across all 30 files.

### 3.1 Canonical Architecture Wording

**Use this wording everywhere**:

> The matching engine core uses a **single-threaded-per-partition** processing model. Each partition is assigned one or more instruments (order books). A single dedicated thread processes all messages for its assigned instruments in strict sequence order. High-activity instruments may be isolated to a dedicated partition; lower-activity instruments may share a partition. There is no cross-partition matching — each instrument belongs to exactly one partition at any time.

**Do not use**: "single-threaded-per-book" (ambiguous — implies one thread per instrument, which is unnecessarily granular and does not reflect the partition model).

### 3.2 Canonical Sequencing Wording

**Use this wording everywhere**:

> Sequencing is **partition-local**. Each partition has its own sequencer (or a shared sequencer with partition-routing) that assigns a monotonically increasing sequence number to every inbound message for that partition. There is no global total ordering across partitions. Global event identity is formed as a composite: `[partition_id]:[local_sequence]`. Cross-partition operations (e.g., cancel-all for a participant) are issued as independent messages to each affected partition and processed independently.

**Do not use**: "globally monotonic sequence number" without the partition qualifier.

### 3.3 Canonical Durability Wording

**Use this wording everywhere**:

> The platform supports two journal durability modes:
>
> - **Regulatory-Grade (Synchronous)** — Default. The journal entry is durably written (fsync or equivalent) before the matching engine processes the message and before any acknowledgment is sent. This mode guarantees zero data loss and is required for all regulated exchange, MTF, OTF, and ATS deployments.
>
> - **Reduced-Latency (Batched/Asynchronous)** — Optional. Journal entries are batched within a configurable window (e.g., 10–100 µs) before durable write. Reduces journal-related latency but introduces a data-loss window equal to the batch interval on crash. Suitable only for venues where the operator accepts this risk (e.g., internal crossing networks, non-regulated digital-asset venues) and where the deployment's regulatory framework permits it.
>
> The durability mode is configured per deployment, not per instrument.

### 3.4 Canonical Performance Wording

**Use this wording everywhere when stating latency or throughput targets**:

> Performance targets are stated per **deployment profile**. The platform defines three reference profiles:
>
> | Profile | Infrastructure | Use Case | Latency Class |
> |---------|---------------|----------|---------------|
> | **Profile A — Ultra-Low-Latency Colocation** | Bare metal, kernel bypass (DPDK), CPU pinning, PTP, NIC timestamping, hardware sequencer optional | Tier-1 regulated exchange competing on latency | Engine p99 < 5 µs; wire-to-wire p99 < 25 µs |
> | **Profile B — Standard Exchange-Grade** | Bare metal or dedicated hardware, standard kernel networking, PTP, software sequencer | Most regulated venues, MTFs, digital-asset exchanges | Engine p99 < 20 µs; wire-to-wire p99 < 100 µs |
> | **Profile C — Hybrid / Cloud-Compatible** | Cloud bare-metal instances or dedicated VMs, standard networking, NTP or PTP | Broker crossing, commodity/carbon venues, startup venues | Engine p99 < 100 µs; wire-to-wire p99 < 1 ms |
>
> All latency targets assume measurement under sustained load, not idle conditions. Targets are engineering goals validated through performance testing, not contractual guarantees.

### 3.5 Canonical Terminology — Release Names

| Old Term | New Canonical Term | Abbreviation |
|----------|-------------------|-------------|
| MVP | Foundation Release | FR-1 |
| MVR (Minimal Viable Release) | Foundation Release | FR-1 |
| Phase 1 — MVR | Phase 1 — Foundation Release | FR-1 |
| Phase 2 — Production Exchange | Phase 2 — Exchange-Grade Release | EGR |
| Phase 3 — Multi-Market Expansion | Phase 3 — Multi-Asset Release | MAR |
| Phase 4 — Advanced Resilience | Phase 4 — Advanced Operations Release | AOR |
| Phase 5 — Ecosystem | Phase 5 — Ecosystem Release | ECO |

### 3.6 Canonical Configurability Wording

**Use this wording everywhere**:

> The following are managed by **configuration** (no code change, no software release):
> - Instrument creation, listing, delisting
> - Trading session schedules and phase timing
> - Tick size and lot size tables
> - Price band parameters
> - Enabling or disabling existing, already-implemented order types per instrument or market segment
> - Participant entitlements and permissions
> - Risk limit values
> - Fee schedule parameters
> - Venue rule parameters (e.g., STP mode, CoD grace period, auction durations)
>
> The following require a **controlled software release** (code change, test cycle, deployment):
> - Materially new matching algorithms (e.g., a new priority model not already implemented)
> - Materially new order types with novel execution semantics
> - New surveillance detection logic that touches the execution path
> - New protocol message types
> - Structural changes to the journal format or event schema

### 3.7 Canonical Market Order Residual Wording

**Use this wording everywhere**:

> When a market order sweeps all available liquidity and a residual quantity remains, the venue rule-book configuration determines the handling. The available options are:
>
> 1. **Cancel residual** (default) — The unfilled portion is immediately cancelled. An unsolicited cancel event is published.
> 2. **Market-to-limit conversion** — The residual is converted to a limit order priced at the last fill price, subject to a configurable protection collar. If the collar would be breached, the residual is cancelled instead.
> 3. **Venue-rulebook-defined policy** — The venue operator configures custom handling via the rule-parameter framework.
>
> The option "rest at last fill price without a protection collar" is explicitly **not supported** due to the risk of resting an order at a price that does not reflect fair value after a book sweep.

---

## 4. File-by-File Review

### File 00 — Index
- **Status**: Needs minor edit
- **Issue**: References "MVR" in file 29 title
- **Fix**: Change to "Foundation Release vs Enterprise Release"

### File 01 — Executive Summary
- **Status**: Needs significant edits
- **Issues**:
  1. §1.2 bullet 1: "single-threaded-per-book" → must use canonical partition wording
  2. §1.2 bullet 1: "sub-microsecond internal latency" is overclaimed as product description
  3. §1.2 bullet 1: "millions of messages per second" — correct but should reference partition-level
  4. §1.6: "MVR" → "Foundation Release"
  5. §1.7: Performance metrics stated as universal targets without deployment-profile context
  6. §1.7: "< 4 weeks (configuration, not code)" for new asset class is overclaimed
  7. §1.8: References "MVP-vs-enterprise"

### File 02 — Vision and Goals
- **Status**: Needs minor edits
- **Issues**:
  1. BG-004 Success Criteria: "p99 matching latency < 10 µs" should add "for Profile A deployments"
  2. BG-005: "without code changes" should use canonical configurability wording
  3. §2.4 CTO row: "sub-10µs matching latency" should be profile-qualified
  4. §2.4 Product Team row: "New market models configurable without code changes" overclaimed

### File 03 — Target Markets
- **Status**: Acceptable as-is
- **Notes**: Well-structured venue type analysis. No contradictions found.

### File 04 — Stakeholders
- **Status**: Needs minor edit
- **Issue**: §4.3 Market Maker latency "< 1 µs gateway" is FPGA-only territory
- **Fix**: Change to "Ultra-low (see deployment profiles in §15.1)"

### File 05 — Business Context
- **Status**: Needs minor edit
- **Issues**:
  1. §5.2 bullet 1: "Sub-10µs matching latency" should be profile-qualified
  2. §5.4: "cuts technology cost by 50%+" is marketing language

### File 06 — Scope and Assumptions
- **Status**: Needs significant edits
- **Issues**:
  1. TC-01: "single-threaded per order book" contradicts canonical wording
  2. TC-05: "Journal write must be durable before execution acknowledgment (fsync)" — correct for default mode but must acknowledge the optional reduced-latency mode
  3. CC-01: "Phase 1 (MVR)" terminology

### File 07 — Microstructure Principles
- **Status**: Needs minor edits
- **Issues**:
  1. MS-01 Implications: "Single-threaded execution per order book" — use canonical partition wording
  2. MS-07: "through configuration, not code changes" overclaimed for market models
  3. §7.3.3: "globally unique, monotonically increasing" Order ID — correct (composite global uniqueness via partition prefix) but wording could confuse with global sequencing

### File 08 — Asset Classes
- **Status**: Acceptable as-is
- **Notes**: Thorough and accurate per-asset-class analysis.

### File 09 — Trading Models
- **Status**: Acceptable as-is
- **Notes**: Well-structured. State machine and session model are correct.

### File 10 — Functional Scope
- **Status**: Acceptable with one minor fix
- **Issue**: Layer 1 description could reference canonical partition model explicitly

### File 11 — Core Matching Engine
- **Status**: Needs significant edits (most critical file)
- **Issues**:
  1. §11.1: Architecture is correctly "single-threaded-per-partition" — good, but must ensure this is the term used everywhere
  2. REQ-SEQ-001: "globally unique, monotonically increasing sequence number" — contradicts partition-local model. The sequence is partition-local; global uniqueness is achieved by composite ID
  3. REQ-SEQ-004: "< 1 µs added latency for sequencing + journaling" — extremely aggressive; must be profile-qualified
  4. REQ-JNL-003: "durable (fsync or equivalent) before acknowledgment" — correct for default mode; add reference to dual durability modes
  5. REQ-JNL-007: "< 500 ns" journal write latency — not achievable with synchronous fsync on any hardware. This is an asynchronous/batched metric. Contradicts REQ-JNL-003.
  6. REQ-OT-001: Market order residual "convert to limit at last fill price" — unsafe default
  7. §11.7.1: "Has its own sequencer (or a shared sequencer with partition routing)" — good, but must be reconciled with REQ-SEQ-001 global wording

### File 12 — Platform Components
- **Status**: Acceptable as-is
- **Notes**: Comprehensive component specifications. Well-structured.

### File 13 — External Interfaces
- **Status**: Acceptable as-is
- **Notes**: SBE message layouts and FIX tag definitions are implementable.

### File 14 — Functional Requirements
- **Status**: Acceptable with minor fix
- **Issue**: FR-OMS-018 and FR-MTH-008 should reference canonical market-order residual wording

### File 15 — Non-Functional Requirements
- **Status**: Needs significant edits
- **Issues**:
  1. §15.1: All latency targets presented as universal "Mandatory" without deployment-profile context
  2. NFR-LAT-001: "p50 < 1 µs" matching — achievable only in C++/Rust on bare metal with no risk checks in path; must be Profile A
  3. NFR-JIT-001: "< 5 µs" matching jitter — aggressive; must be profile-qualified
  4. NFR-FAR-001: "< 1 µs fairness delta" — requires hardware sequencer; must be profile-qualified
  5. NFR-MNT-006: "New order types, market models configurable without code change" — overclaimed
  6. §15.13.1: Latency figures in language comparison table ("< 1 µs") are engine-only, not wire-to-wire — acceptable but should note measurement scope

### File 16 — Regulatory Compliance
- **Status**: Acceptable as-is
- **Notes**: Compliance-by-configuration framework is well-designed.

### File 17 — Data, Audit, Reporting
- **Status**: Needs minor edit
- **Issue**: §17.2.3 "Events sequenced per partition" is correct and consistent with canonical wording. Good.

### File 18 — Security
- **Status**: Acceptable as-is

### File 19 — Resilience
- **Status**: Acceptable as-is
- **Notes**: Resilience tiers and failure scenarios are realistic and well-specified.

### File 20 — Infrastructure
- **Status**: Acceptable as-is
- **Notes**: Hardware specifications and Linux tuning are realistic.

### File 21 — Performance Engineering
- **Status**: Needs significant edits
- **Issues**:
  1. §21.1.1: Latency budget presented for "Binary Gateway, Colocation" — good that it is scoped, but should explicitly label this as "Profile A" budget
  2. §21.2.2: "journal write latency < 500 ns (p99)" contradicts synchronous durability requirement
  3. §21.2.2: "GC pauses < 1 µs" is not achievable on any JVM
  4. §21.2.2: "gateway fairness delta < 1 µs" — profile-dependent
  5. §21.3.3: "Write batching" discussed as optimization without reconciling with mandatory sync durability

### File 22 — Observability
- **Status**: Acceptable as-is

### File 23 — Testing and Certification
- **Status**: Acceptable as-is
- **Notes**: Participant certification framework is practical and complete.

### File 24 — Migration / Rollout
- **Status**: Acceptable as-is

### File 25 — Target Operating Model
- **Status**: Acceptable as-is

### File 26 — Implementation Roadmap
- **Status**: Needs minor edits
- **Issues**:
  1. "MVR" → "Foundation Release" throughout
  2. §26.2.2: "p99 < 10 µs matching; 1M msg/s" should be profile-qualified
  3. Phase 1 auctions should be flagged as stretch goal

### File 27 — Risks, Tradeoffs
- **Status**: Needs minor edits
- **Issues**:
  1. §27.2.1: Journaling tradeoff correctly recommends sync — good. Should reference canonical dual-mode wording.
  2. §27.2.3: Language latency table "< 1 µs" is engine-only; note scope.
  3. "MVP discipline" in R-006 → "scope discipline"

### File 28 — Component Inventory
- **Status**: Needs minor edit
- **Issue**: "Mandatory" priority for some latency-critical items should reference profile

### File 29 — MVR vs Enterprise
- **Status**: Needs significant edits
- **Issues**:
  1. Title and all references: "MVR" → "Foundation Release"
  2. §29.1.1: "p99 < 10µs matching; 1M msg/s per partition" in Performance row — profile-qualify
  3. Top 20 Decisions table is good and should be preserved intact

### File 30 — Appendix / Glossary
- **Status**: Needs minor edits
- **Issues**:
  1. Glossary should add "Partition" definition aligned to canonical wording
  2. §30.2.1 sample workflow timestamps imply sequential µs processing which is consistent with Profile A — should note this
  3. Glossary entry for "Sequencer" should clarify partition-local scope

---

## 5. Revised Wording for Affected Sections

Below is the corrected text for all substantive changes. Only the affected passages are shown.

---

### File 01, §1.2 — Bullet 1 (REPLACE)

**Original**:
> A deterministic core matching engine — an ultra-low-latency, single-threaded-per-book, event-sourced order matching kernel capable of processing millions of messages per second with sub-microsecond internal latency and provable determinism.

**Revised**:
> A deterministic core matching engine — a single-threaded-per-partition, event-sourced order matching kernel. Each partition manages one or more order books (instruments) on a dedicated CPU core. The engine is designed for high throughput (target: ≥ 1M messages/second per partition on bare-metal hardware) with low, predictable latency (targets vary by deployment profile; see §15) and provable determinism.

---

### File 01, §1.6 — Phase table row 1 (REPLACE)

**Original**: `Phase 1 — MVR`
**Revised**: `Phase 1 — Foundation Release (FR-1)`

---

### File 01, §1.7 — Key Success Metrics table (REPLACE)

**Original table**:

**Revised**:

| Metric | Target | Qualification |
|--------|--------|---------------|
| Core matching latency (p99) | 5–100 µs per order action | Depends on deployment profile (A/B/C); see §15 |
| Sustained throughput | ≥ 1,000,000 messages/second per partition | On bare-metal hardware with CPU pinning |
| System availability | ≥ 99.99% during trading hours | Requires active-passive redundancy (Phase 2+) |
| Recovery time objective | < 60 seconds warm restart; < 5 minutes full DR failover | DR requires Phase 2 infrastructure |
| Audit trail completeness | 100% — every state mutation journaled | With nanosecond timestamps (PTP) |
| Time to onboard new asset class | < 4 weeks for configuration of a supported instrument type | New matching algorithms require a software release |
| Regulatory certification readiness | Platform designed to pass exchange licensing technical review | Jurisdiction-specific compliance overlays required |

---

### File 01, §1.7 — Last row on "new asset class" (REPLACE)

**Original**: `< 4 weeks (configuration, not code)`
**Revised**: `< 4 weeks for configuration of a supported instrument type | New matching algorithms require a software release`

---

### File 06, TC-01 (REPLACE)

**Original**: `TC-01: Matching engine hot path must be single-threaded per order book to guarantee determinism`
**Revised**: `TC-01: Matching engine hot path must be single-threaded per partition to guarantee determinism. Each partition processes one or more order books on a single dedicated thread.`

---

### File 06, TC-05 (REPLACE)

**Original**: `TC-05: Journal write must be durable before execution acknowledgment (fsync or equivalent)`
**Revised**: `TC-05: In the default Regulatory-Grade durability mode, the journal write must be durable (fsync or equivalent) before the matching engine processes the message and before any acknowledgment is sent. An optional Reduced-Latency mode with batched writes is available for venue types where the operator accepts the associated data-loss window and where the regulatory framework permits it (see §11.3, §27.2.1).`

---

### File 07, MS-01 Implications — bullet 1 (REPLACE)

**Original**: `Single-threaded execution per order book (or fully deterministic multi-threaded with provable equivalence)`
**Revised**: `Single-threaded execution per partition (each partition manages one or more order books). A fully deterministic multi-threaded alternative is theoretically possible but not recommended due to verification complexity.`

---

### File 07, MS-07 Statement (REPLACE)

**Original**: `The platform supports multiple market models through configuration, not code changes.`
**Revised**: `The platform supports multiple market models. Existing, already-implemented models and order types can be enabled, disabled, and parameterized per instrument or market segment through configuration. Materially new matching algorithms or order semantics require a controlled software release.`

---

### File 11, REQ-SEQ-001 (REPLACE)

**Original**: `Assign globally unique, monotonically increasing sequence number to every inbound message`
**Revised**: `Assign a partition-local monotonically increasing sequence number to every inbound message for that partition. Global event identity is formed as the composite [partition_id]:[local_sequence], which is unique across the platform without requiring a global sequencer.`

---

### File 11, REQ-SEQ-004 (REPLACE)

**Original**: `Achieve < 1 µs added latency for sequencing + journaling | Mandatory`
**Revised**: `Achieve low added latency for sequencing + journaling. Target: < 1 µs for Profile A (kernel bypass, NVMe); < 5 µs for Profile B; < 50 µs for Profile C. | Target (profile-dependent)`

---

### File 11, REQ-JNL-003 (REPLACE)

**Original**: `Journal write durable (fsync or equivalent) before acknowledgment | Mandatory`
**Revised**: `In Regulatory-Grade mode (default): journal write durable (fsync or equivalent) before message processing and before acknowledgment. In Reduced-Latency mode (opt-in): journal writes batched within a configurable window. See §3 of the Review Report for mode definitions. | Mandatory (mode selection is deployment-configured)`

---

### File 11, REQ-JNL-007 (REPLACE)

**Original**: `Journal write latency < 500 ns (with kernel bypass / io_uring) | Highly Recommended`
**Revised**: `Journal write-to-stable-storage latency targets: < 2 µs for Regulatory-Grade synchronous mode on NVMe (Profile A); < 500 ns per entry for Reduced-Latency batched mode (amortized across batch). These targets assume NVMe SSD with direct I/O and are validated through performance testing. | Highly Recommended`

---

### File 11, REQ-OT-001 — Market Order Residual (REPLACE)

**Original**: `Residual: If book exhausted, configurable: reject residual OR convert to limit at last fill price`
**Revised**: `Residual handling when the book is exhausted (configurable per venue rulebook):
(a) **Cancel residual** (default) — unfilled portion is immediately cancelled.
(b) **Market-to-limit with protection collar** — residual converts to a limit order at the last fill price, subject to a configurable protection collar (e.g., ± N ticks or ± X% from reference price). If the collar would be breached, the residual is cancelled.
(c) **Venue-rulebook-defined policy** — custom handling via rule-parameter framework.
Note: Resting a market order residual at an unprotected price is explicitly not supported.`

---

### File 15, §15.1 — Latency Table Header (ADD)

**Add this preamble before the table**:

> The following latency targets are stated for **Deployment Profile A** (bare-metal colocation, kernel bypass, CPU pinning, PTP, NIC timestamping). For Profile B and Profile C targets, apply the multipliers defined in the deployment profile table (§3.4 of the Review Report). All targets are engineering goals validated through performance testing under sustained load, not contractual guarantees.

---

### File 15, NFR-FAR-001 (REPLACE)

**Original**: `Gateway fairness | Max latency difference between gateways < 1 µs at sequencer | Mandatory`
**Revised**: `Gateway fairness | Max latency difference between gateways at sequencer: < 1 µs (Profile A with hardware sequencer), < 5 µs (Profile A/B with software time-based sequencer), < 50 µs (Profile C). Actual fairness statistics published for regulatory transparency. | Mandatory (threshold is profile-dependent)`

---

### File 15, NFR-MNT-006 (REPLACE)

**Original**: `Configuration-driven behavior | New order types, market models configurable without code change | Mandatory`
**Revised**: `Configuration-driven behavior | Existing order types and market models are enabled/disabled/parameterized through configuration without code change. Materially new order semantics or matching algorithms require a controlled software release. | Mandatory`

---

### File 21, §21.1.1 — Title (REPLACE)

**Original**: `End-to-End Latency Budget (Binary Gateway, Colocation)`
**Revised**: `End-to-End Latency Budget — Deployment Profile A (Binary Gateway, Bare-Metal Colocation, Kernel Bypass)`

---

### File 21, §21.2.2 — Journal write latency row (REPLACE)

**Original**: `Journal write latency | Under load | < 500 ns (p99) | I/O optimization`
**Revised**: `Journal write-to-storage latency | Under load, Regulatory-Grade sync mode | < 2 µs (p99) | I/O optimization; < 500 ns achievable only in Reduced-Latency batched mode`

---

### File 21, §21.2.2 — GC pauses row (REPLACE)

**Original**: `GC pauses (if JVM) | Under load | < 1 µs (if applicable) | Off-heap migration`
**Revised**: `GC pauses (if JVM) | Under load | No GC on hot path (off-heap allocation); if GC is present in non-hot-path JVM components, target p99 < 500 µs | Off-heap allocation or GC-free language for hot path`

---

### File 21, §21.2.2 — Gateway fairness delta row (REPLACE)

**Original**: `Gateway fairness delta | Multi-gateway test | < 1 µs between gateways | Sequencing review`
**Revised**: `Gateway fairness delta | Multi-gateway test | Per deployment profile: < 1 µs (hardware sequencer), < 5 µs (software time-based), < 50 µs (Profile C) | Sequencing configuration review`

---

### File 21, §21.3.3 — Write batching bullet (REPLACE)

**Original**: `Write batching: Batch journal entries within configurable window (trade latency vs. durability)`
**Revised**: `Write batching: Available only in Reduced-Latency durability mode. Batch journal entries within a configurable window (e.g., 10–100 µs). Not available in Regulatory-Grade mode where each entry must be durably written before processing. See §11.3 and §27.2.1 for mode definitions and venue-type applicability.`

---

### File 26, §26.2 — Title (REPLACE)

**Original**: `Phase 1: Minimal Viable Release (Months 1–9)`
**Revised**: `Phase 1: Foundation Release — FR-1 (Months 1–9)`

---

### File 26, §26.2.2 — Performance deliverable (REPLACE)

**Original**: `Performance targets met (p99 < 10 µs matching; 1M msg/s sustained)`
**Revised**: `Performance targets met for the selected deployment profile (Profile A target: engine p99 < 5 µs; Profile B: engine p99 < 20 µs); sustained throughput ≥ 1M msg/s per partition on target hardware`

---

### File 29 — Title (REPLACE)

**Original**: `Final Recommended MVR vs Enterprise Release`
**Revised**: `Final Recommended Foundation Release vs Enterprise Target State`

All internal references to "MVR" → "Foundation Release (FR-1)"

---

### File 30 — Glossary additions

Add to glossary table:

| **Foundation Release (FR-1)** | The first production-capable release of the platform, supporting a single asset class with continuous order book, basic risk controls, and connectivity gateways. Previously referred to as MVP or MVR. |
| **Deployment Profile** | A named combination of infrastructure, networking, and configuration choices that determines achievable performance characteristics. See §15 for Profile A / B / C definitions. |

Update existing entry:

| **Partition** | A unit of work assignment in the matching engine. Each partition manages one or more order books (instruments) on a single dedicated thread. Instruments are sharded across partitions for horizontal scaling. There is no cross-partition matching. |

| **Sequencer** | Component that assigns a partition-local monotonic sequence number and nanosecond timestamp to all inbound messages for a partition. Global event identity uses the composite `[partition_id]:[local_sequence]`. |

---

## 6. Cross-File Consistency Check

| Concept | Canonical Wording / Value | Files That Must Agree |
|---------|--------------------------|----------------------|
| Threading model | "Single-threaded per partition" | 01, 06, 07, 10, 11, 15, 27, 30 |
| Sequencing scope | "Partition-local monotonic; global identity = partition_id:local_sequence" | 07, 11, 15, 17, 30 |
| Journal durability default | "Regulatory-Grade synchronous (fsync before processing)" | 06, 11, 15, 19, 21, 27 |
| Reduced-latency journal mode | "Optional batched mode; venue-type restricted" | 11, 21, 27 |
| Market order residual | "Cancel residual (default); market-to-limit with collar; venue-rulebook policy. No unprotected last-fill-price resting." | 07, 11, 14 |
| Fairness delta | "Profile-dependent: < 1µs (HW), < 5µs (SW time), < 50µs (Profile C)" | 15, 21, 23 |
| Performance targets | "Deployment-profile-dependent; Profile A / B / C bands" | 01, 02, 05, 15, 21, 26, 29 |
| Configurability vs. code | "Configuration = parameters/flags; new algorithms = software release" | 01, 02, 07, 15, 29 |
| Release naming | "Foundation Release (FR-1)" for Phase 1 | 00, 01, 06, 26, 27, 28, 29 |
| Instrument states | PRE_OPEN, AUCTION, CONTINUOUS, HALT, SUSPEND, CLOSING_AUCTION, CLOSED, POST_CLOSE, INACTIVE | 07, 09, 11, 14, 17 |
| Order lifecycle states | NEW, PARTIALLY_FILLED, FILLED, CANCELLED, REJECTED, EXPIRED, SUSPENDED | 14, 17, 30 |
| Recovery modes | Warm restart, hot standby, cold restart, DR failover, point-in-time | 11, 15, 19 |
| Availability target | 99.99% during trading hours | 01, 02, 15, 19, 25 |

---

## 7. Final Change Log

| File | Section | Original Problem | Change Applied | Reason |
|------|---------|-----------------|----------------|--------|
| 00 | Title row 29 | "MVR" | → "Foundation Release" | Terminology normalization |
| 01 | §1.2 bullet 1 | "single-threaded-per-book", "sub-microsecond" overclaim | Rewritten with partition model and profile-qualified language | Architecture + performance consistency |
| 01 | §1.6 | "MVR" | → "Foundation Release (FR-1)" | Terminology |
| 01 | §1.7 | Universal perf targets | Profile-qualified targets with caveats | Performance realism |
| 02 | BG-004, BG-005, §2.4 | Unqualified latency; overclaimed configurability | Profile qualifiers added; configurability bounded | Consistency |
| 04 | §4.3 | "< 1 µs gateway" for MM | → "Ultra-low (deployment-profile-dependent)" | FPGA contradiction |
| 05 | §5.2, §5.4 | Unqualified latency; marketing cost claim | Profile-qualified; softened cost language | Realism |
| 06 | TC-01 | "per order book" | → "per partition" | Architecture canonical |
| 06 | TC-05 | Only sync mode stated | Added dual-mode reference | Durability consistency |
| 06 | CC-01 | "MVR" | → "Foundation Release" | Terminology |
| 07 | MS-01 | "per order book" | → "per partition" | Architecture canonical |
| 07 | MS-07 | "configuration, not code" overclaim | Bounded configurability wording | Configurability realism |
| 11 | REQ-SEQ-001 | "globally unique, monotonically increasing" implied global sequencer | Partition-local with composite global identity | Sequencing canonical |
| 11 | REQ-SEQ-004 | "< 1 µs" as mandatory | Profile-dependent targets | Performance realism |
| 11 | REQ-JNL-003 | Single sync mode only | Dual-mode reference | Durability consistency |
| 11 | REQ-JNL-007 | "< 500 ns" contradicts sync fsync | Corrected to per-mode targets | Durability/latency reconciliation |
| 11 | REQ-OT-001 | "convert to limit at last fill price" | Replaced with safe residual handling options | Market order safety |
| 15 | §15.1 | Universal mandatory targets | Added Profile A preamble | Performance realism |
| 15 | NFR-FAR-001 | "< 1 µs" universal | Profile-dependent thresholds | Fairness realism |
| 15 | NFR-MNT-006 | "New order types configurable without code" | Bounded to existing types | Configurability realism |
| 21 | §21.1.1 title | Unscoped | Explicit Profile A label | Performance clarity |
| 21 | §21.2.2 | Journal < 500ns, GC < 1µs, fairness < 1µs | Corrected per mode/profile | Performance realism |
| 21 | §21.3.3 | Write batching without mode context | Added mode restriction | Durability consistency |
| 26 | Throughout | "MVR" | → "Foundation Release" | Terminology |
| 26 | §26.2.2 | Unqualified perf targets | Profile-qualified | Performance realism |
| 27 | R-006 | "MVP discipline" | → "scope discipline" | Terminology |
| 29 | Title + throughout | "MVR" | → "Foundation Release" | Terminology |
| 29 | §29.1.1 Performance row | Unqualified targets | Profile-qualified | Performance realism |
| 30 | Glossary | Missing/imprecise entries | Added FR-1, Deployment Profile; updated Partition, Sequencer | Terminology consistency |

---

## 8. Final Editorial Guidance

For future updates to this specification package, apply these rules:

1. **Threading model**: Always say "single-threaded per partition." Never say "per book" or "per symbol."
2. **Sequencing**: Always say "partition-local monotonic." Global uniqueness via composite ID.
3. **Performance numbers**: Always state the deployment profile. Never present a number as a universal platform guarantee.
4. **Durability**: Always name the mode (Regulatory-Grade or Reduced-Latency). Never discuss journal batching without naming it as Reduced-Latency mode.
5. **Configurability**: Distinguish "configuration" (parameters, flags) from "controlled software release" (new algorithms, new semantics). Never say "without code changes" for materially new behavior.
6. **Release names**: Use "Foundation Release (FR-1)" for Phase 1. Do not use MVP or MVR in formal specification text.
7. **Market order residuals**: Always reference the three safe options. Never allow unprotected last-fill-price resting as a named option.
8. **Fairness targets**: Always state the sequencing mechanism assumed (hardware vs. software time-based).
9. **Latency components**: When stating wire-to-wire latency, itemize the components (gateway, risk, sequencer, engine, journal, output). Do not conflate engine-only with wire-to-wire.
10. **Voice**: Write as a specification author, not a product marketer. Use "is designed to," "targets," "subject to deployment profile" rather than "delivers," "guarantees," "blazing fast."

---

## 9. Top 10 Mandatory Corrections

1. **Normalize threading model** to "single-threaded per partition" in all files
2. **Normalize sequencing** to "partition-local monotonic" with composite global ID
3. **Introduce deployment profiles** (A/B/C) and qualify all latency/throughput targets
4. **Define dual durability modes** (Regulatory-Grade sync + Reduced-Latency batch) and reconcile all references
5. **Fix market order residual handling** — remove "last fill price" default; replace with safe options
6. **Bound configurability claims** — distinguish configuration from controlled software release
7. **Fix journal write latency** — reconcile < 500ns claim with synchronous fsync requirement
8. **Fix GC pause target** — change from "< 1 µs" (impossible) to "no GC on hot path"
9. **Standardize release naming** — "Foundation Release (FR-1)" everywhere
10. **Fix fairness NFR** — make gateway delta profile-dependent, not universal < 1 µs

## 10. Top 10 Optional Improvements

1. Add a "Deployment Profile" appendix section with full hardware/software specifications per profile
2. Add a formal "Decision Log" template for the 20 open design decisions
3. Add a wire-protocol versioning and deprecation policy section
4. Add a "Participant Experience" section covering onboarding journey end-to-end
5. Add capacity planning worked examples for small (500 instruments), medium (5K), and large (50K) deployments
6. Add a formal RACI matrix for operational responsibilities
7. Add a "Day in the Life" operational scenario for market operations staff
8. Flag opening/closing auctions as FR-1 stretch goal with explicit go/no-go decision point
9. Add a "Technology Radar" section for emerging technologies under evaluation (FPGA, eBPF, CXL memory)
10. Add a regulatory engagement timeline aligned with the implementation roadmap

---

## 11. Final Conclusion

### Package Readiness Assessment

| Purpose | Ready? | Conditions |
|---------|--------|-----------|
| **Stakeholder workshops** | **Yes** | After applying the 10 mandatory corrections above |
| **Architecture baseline** | **Yes** | After corrections; architecture is sound and well-layered |
| **Regulator pre-review** | **No** | Requires jurisdiction-specific compliance overlays, formal rule-book draft, and controls evidence documentation |
| **Implementation kickoff** | **No** | Requires resolution of the 20 open design decisions (§29.3), hot-path language selection, and detailed interface specifications |
| **Vendor evaluation** | **Yes** | After corrections; strong enough for technology assessment |

**Overall verdict**: This is a professional, substantive specification package that is materially above the quality bar for most exchange technology RFPs and internal planning documents. The corrections identified in this report are necessary but bounded — they normalize terminology, correct overstatements, and resolve a small number of genuine contradictions. The underlying architecture, microstructure design, and operational model are sound. After applying the mandatory corrections, the package is suitable as the authoritative next-draft baseline for stakeholder workshops and architecture design.
