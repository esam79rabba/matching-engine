# Final Review, Consistency Confirmation, and Diagram Plan

**Version**: 2.0
**Date**: 2026-03-24
**Status**: Post-normalization final pass

---

## 1. Final Executive Assessment

| Criterion | Status | Notes |
|-----------|--------|-------|
| **Workshop-ready** | **Yes** | All terminology, architecture, and performance language is now internally consistent |
| **Architecture-baseline-ready** | **Yes** | Core architecture is stable; partition model, sequencing, durability, and deployment profiles are canonical |
| **Diagram-ready** | **Yes** — for the 7 high-priority diagrams | Architecture is sufficiently stable for system context, logical architecture, hot-path, connectivity, FR-1 vs Enterprise, deployment profiles, and component interaction diagrams |
| **Regulator-pre-read-ready** | **Not yet** | Requires jurisdiction-specific compliance overlays and formal rule-book draft |
| **Implementation-kickoff-ready** | **Not yet** | Requires resolution of the 20 open design decisions (§29.3) — particularly hot-path language, target asset class, jurisdiction, and sequencing strategy |

---

## 2. Remaining Issue Register (Post-This-Pass)

After applying all corrections in this session, the following table reflects the **resolved** state:

| Issue ID | Severity | Description | Status |
|----------|----------|-------------|--------|
| FIN-001 | High | BG-004 (§02) had unqualified "p99 < 10 µs" | **Fixed** — now profile-qualified |
| FIN-002 | High | BG-005 (§02) said "without code changes" broadly | **Fixed** — now bounded to existing capabilities |
| FIN-003 | High | §02 §2.4 CTO row had "sub-10µs" unqualified | **Fixed** — now deployment-profile-referenced |
| FIN-004 | High | §02 §2.4 Product row overclaimed configurability | **Fixed** — now bounded |
| FIN-005 | High | §05 §5.2 bullet 1 had "Sub-10µs" absolute | **Fixed** — now profile-qualified |
| FIN-006 | High | §05 §5.2 bullet 2 said "configuration, not custom builds" | **Fixed** — now bounded |
| FIN-007 | Medium | §05 §5.3 cost claim "40–60%" | **Fixed** — softened to qualitative |
| FIN-008 | Medium | §25 Technology KPI had "< 10 µs" unqualified | **Fixed** — now profile-qualified |
| FIN-009 | Medium | §21 perf acceptance had "< 10 µs" and "< 25 µs" | **Fixed** — now profile-referenced |
| FIN-010 | Low | §12 market data latency lacked profile tag | **Fixed** — now "Profile A:" prefixed |
| FIN-011 | Low | §13 interface table lacked profile tag | **Fixed** — now "Profile A:" prefixed |
| FIN-012 | Low | Phase 2–5 naming inconsistent with FR-1 convention | **Fixed** — now EGR, MAR, AOR, ECO |

**No remaining open Critical or High issues.** Package is internally consistent.

---

## 3. Final Consistency Checklist

| Concept | Canonical Wording Present? | Consistent Across Files? |
|---------|:--------------------------:|:------------------------:|
| Partition model ("single-threaded per partition") | Yes | Yes — verified in 01, 06, 07, 11, 27, 28, 30 |
| Sequencing ("partition-local monotonic; composite global ID") | Yes | Yes — verified in 11, 15, 17, 30 |
| Deployment profiles (A/B/C with multipliers) | Yes | Yes — 01, 02, 05, 12, 13, 15, 21, 25, 26, 29 |
| Durability modes (Regulatory-Grade / Reduced-Latency) | Yes | Yes — 06, 11, 21, 27 |
| Release naming (FR-1, EGR, MAR, AOR, ECO) | Yes | Yes — 00, 01, 05, 06, 10, 15, 26, 27, 29, 30 |
| Configurability vs. software change | Yes | Yes — 02, 07, 15 |
| Fairness (profile-dependent at sequencing boundary) | Yes | Yes — 15, 21 |
| Market order residual (cancel / collar / rulebook; no unprotected last-fill) | Yes | Yes — 11, 14 (FR-MTH-008 references "per configuration") |
| Hot path vs. non-hot-path boundary | Yes | Yes — 10, 11, 12 |
| Instrument state machine | Yes | Yes — 07, 09, 14, 17 |
| Availability target (99.99% during trading hours) | Yes | Yes — 01, 02, 15, 19, 25 |

---

## 4. Canonical Wording Set (Final)

### Partition Model
> The matching engine core uses a single-threaded-per-partition processing model. Each partition is assigned one or more instruments (order books). A single dedicated thread processes all messages for its assigned instruments in strict sequence order. High-activity instruments may be isolated to a dedicated partition; lower-activity instruments may share a partition. There is no cross-partition matching.

### Sequencing
> Sequencing is partition-local. Each partition's sequencer assigns a monotonically increasing sequence number to every inbound message for that partition. Global event identity is formed as a composite: `[partition_id]:[local_sequence]`. There is no global total ordering across partitions.

### Deployment Profiles
> | Profile | Infrastructure | Latency Class |
> |---------|---------------|---------------|
> | **A — Ultra-Low-Latency Colo** | Bare metal, DPDK, CPU pinning, PTP, HW timestamping | Engine p99 < 5 µs; wire-to-wire p99 < 25 µs |
> | **B — Standard Exchange-Grade** | Bare metal / dedicated, standard kernel, PTP | Engine p99 < 20 µs; wire-to-wire p99 < 100 µs |
> | **C — Hybrid / Cloud-Compatible** | Cloud bare-metal or VMs, NTP or PTP | Engine p99 < 100 µs; wire-to-wire p99 < 1 ms |
>
> All targets are engineering goals validated under sustained load, not contractual guarantees.

### Durability Modes
> - **Regulatory-Grade (default)**: Journal entry durably written (fsync) before processing and acknowledgment. Zero data loss. Required for regulated venues.
> - **Reduced-Latency (opt-in)**: Journal entries batched within a configurable window. Data-loss window equals batch interval. Suitable only where the regulatory framework permits and the operator accepts the risk.

### Release Naming
> Phase 1: Foundation Release (FR-1). Phase 2: Exchange-Grade Release (EGR). Phase 3: Multi-Asset Release (MAR). Phase 4: Advanced Operations Release (AOR). Phase 5: Ecosystem Release (ECO).

### Configurability vs. Software Change
> **Configuration** (no code change): instrument setup, sessions, tick/lot regimes, enabling existing features, risk limits, fee parameters, venue rules.
> **Controlled software release** (code change): materially new matching algorithms, new order semantics, new protocol messages, structural schema changes.

### Fairness
> Fairness guarantees apply at the venue-controlled sequencing boundary. Targets: < 1 µs (Profile A with hardware sequencer), < 5 µs (Profile A/B with software time-based sequencer), < 50 µs (Profile C). Network path variability outside the venue boundary is not within the venue's fairness guarantee.

### Market Order Residual
> Options: (1) Cancel residual (default). (2) Market-to-limit with protection collar at last fill price ± configurable collar width. (3) Venue-rulebook-defined policy. Resting at an unprotected last-fill price is not supported.

---

## 5. Diagram Readiness Recommendation

| # | Diagram Type | Ready Now? | Dependencies | Notes |
|---|-------------|:----------:|-------------|-------|
| 1 | System Context | **Yes** | None | All external actors and system boundary defined in §04, §12, §13 |
| 2 | High-Level Logical Architecture | **Yes** | None | Layered model stable in §10, §11, §12 |
| 3 | Component Interaction | **Yes** | None | All components and interfaces defined in §12, §13 |
| 4 | Hot Path vs Control Plane | **Yes** | None | Clear separation defined in §10, §11 |
| 5 | Participant Connectivity | **Yes** | None | Gateway types and persona mapping in §04, §12, §13 |
| 6 | FR-1 vs Enterprise Target State | **Yes** | None | Defined in §29 |
| 7 | Deployment Profile Comparison | **Yes** | None | Defined in §15, §21, Review Report §3.4 |
| 8 | Detailed Order Lifecycle Sequence | **Wait** | Final order-type list and residual handling frozen | Sequence timings depend on deployment profile |
| 9 | HA/DR Failover Sequence | **Wait** | Final DR architecture decision (active-passive vs. active-active) | §19 defines active-passive; active-active is Phase 4 |
| 10 | Journaling / Replay Recovery Flow | **Wait** | Durability mode design frozen | Both modes must be diagrammed |
| 11 | Fairness / Sequencing Boundary | **Wait** | Sequencer hardware vs. software decision | Open question #4 in §29.3 |
| 12 | Rollout / Migration Swimlane | **Wait** | Go-live strategy confirmed | Depends on venue type and migration context |

---

## 6. Draw.io Diagram Generation Plan

### Diagram Creation Order

**CREATE NOW** (7 diagrams):

---

### Diagram 1: System Context Diagram

**Purpose**: Show the matching engine platform as a single system and all external actors that interact with it.
**Page name**: `01-System-Context`
**Layout**: Central system box, actors arranged around it.

**Components**:
- Central container: "Matching Engine Platform" (large rounded rectangle, dark blue border)
- External actors (rounded rectangles, gray):
  - Broker-Dealer
  - Market Maker
  - Buy-Side Institution (DMA)
  - Retail Intermediary
  - Clearing House (CCP)
  - Custodian / Settlement Agent
  - Regulator / Supervisory Authority
  - Market Data Vendor
  - Surveillance System (external)
  - Market Operations Staff
  - SRE / Infrastructure Team

**Arrows** (labeled with protocol/interface):
- Broker-Dealer → Platform: "FIX / Binary (Orders)"
- Market Maker → Platform: "Binary / Mass Quote"
- Buy-Side → Platform: "FIX (DMA Orders)"
- Retail Intermediary → Platform: "REST / WebSocket"
- Platform → all participants: "Execution Reports, Drop Copy"
- Platform → CCP: "Trade Feed (FIX)"
- Platform → Custodian: "Settlement Instructions (ISO 20022)"
- Platform → Regulator: "Audit Trail, Regulatory Reports"
- Platform → Market Data Vendor: "Market Data Feed (SBE/Multicast)"
- Platform → Surveillance: "Event Stream (Kafka/Avro)"
- Platform ↔ Market Ops: "Operations Console (Web UI)"
- Platform ↔ SRE: "Monitoring (Prometheus/Grafana)"

**Exclusions**: Do not show internal components. This is a context-level diagram.
**Annotations**: "Deployment profile determines latency characteristics" in bottom note.

---

### Diagram 2: High-Level Logical Architecture

**Purpose**: Show the 5-layer internal architecture of the platform.
**Page name**: `02-Logical-Architecture`
**Layout**: Nested rectangles, outermost at top/bottom, innermost in center. Left-to-right flow for data.

**Layers** (nested containers):
1. **Layer 1 — Matching Engine Hot Path** (innermost, red border):
   - Order Book(s)
   - Matching Logic
   - Journal / WAL
   - Sequencer
2. **Layer 2 — Engine Control Plane** (orange border):
   - Configuration Manager
   - State Machine Manager
   - Snapshot Manager
   - Feature Flags
3. **Layer 3 — Platform Services** (blue border):
   - FIX Gateway
   - Binary Gateway
   - MM Quote Gateway
   - WS/REST Gateway
   - Drop Copy Gateway
   - Pre-Trade Risk Service
   - Reference Data Service
   - Market Data Publisher
   - Post-Trade Service
   - Kill Switch
4. **Layer 4 — Operations & Governance** (green border):
   - Operations Console
   - Participant Admin
   - Replay Tooling
   - Reconciliation
   - Audit Trail Service
5. **Layer 5 — Ecosystem Integrations** (gray border):
   - Clearing Adapter
   - Settlement Adapter
   - Surveillance Export
   - Regulatory Reporting
   - Market Data Vendor Feed

**Data flow arrows**:
- Participant → Layer 3 (Gateways) → Layer 1 (Engine) → Layer 3 (Market Data, Drop Copy, Post-Trade) → Layer 5 (External)
- Layer 4 ↔ Layer 2 (Operations controls engine via control plane)
- Layer 1 → Journal (separate callout)

**Color scheme**: Hot path = red/orange tint. Non-hot-path = blue/green.
**Annotations**: "Single-threaded per partition" inside Layer 1. "No external I/O in hot path except journal writes" as note.

---

### Diagram 3: Component Interaction Diagram

**Purpose**: Show message flow between all major platform components.
**Page name**: `03-Component-Interaction`
**Layout**: Left-to-right flow. Participants on far left, external systems on far right.

**Components** (boxes):
- Left column: Participants (Broker, MM, Buy-Side, Retail)
- Gateway column: FIX GW, Binary GW, MM Quote GW, WS/REST GW
- Risk: Pre-Trade Risk Service
- Core: Sequencer → Matching Engine → Journal
- Output column: Market Data Publisher, Drop Copy GW, Post-Trade Service, Surveillance Export
- Right column: CCP, Custodian, Surveillance System, Regulator
- Separate: Reference Data Service (feeds into Gateway + Engine), Operations Console (feeds into Engine Control Plane), Kill Switch (feeds into Risk + Engine)

**Numbered flow** (sequential):
1. Participant → Gateway: Order message
2. Gateway → Risk Service: Validated order
3. Risk Service → Sequencer: Risk-cleared order
4. Sequencer → Journal: Persist input
5. Sequencer → Engine: Sequenced message
6. Engine → Journal: Persist output events
7. Engine → Output Bus: Fill/insert/reject events
8. Output Bus → Market Data Publisher → Participants
9. Output Bus → Drop Copy → Participant
10. Output Bus → Post-Trade → CCP
11. Output Bus → Surveillance Export → External Surveillance

---

### Diagram 4: Hot Path vs Control Plane Separation

**Purpose**: Clearly delineate what is in the hot path and what is not.
**Page name**: `04-Hot-Path-Separation`
**Layout**: Two lanes (swimlanes). Left lane = Hot Path (red tint). Right lane = Control Plane (blue tint).

**Hot Path lane**:
- Sequencer
- Journal Writer
- Matching Engine Partition (with Book(s) inside)
- Output Bus
- Market Data Publisher (output only)
- Binary Gateway (receive/send)

**Control Plane lane**:
- Configuration Manager
- State Machine Manager
- Snapshot Manager
- Reference Data Service
- Operations Console
- Participant Admin
- Pre-Trade Risk Service (note: risk on ingress boundary — executes before sequencer)
- Post-Trade Service
- Surveillance Export
- Monitoring / Observability
- Kill Switch

**Boundary annotations**:
- "No GC, no allocation, no external I/O, no locks" on hot path
- "May use standard networking, containers, databases" on control plane
- "Risk checks execute before sequencer — not in engine hot path but latency-sensitive" on risk service

---

### Diagram 5: Participant Connectivity Model

**Purpose**: Show which participant types connect through which gateways with which protocols.
**Page name**: `05-Participant-Connectivity`
**Layout**: Left column = participant types. Middle = gateways. Right = internal platform.

**Participant types** (color-coded):
- Market Maker (orange): → Binary GW (SBE) + MM Quote GW (Mass Quote)
- Broker-Dealer (blue): → FIX GW (FIX 4.4/5.0)
- Buy-Side DMA (blue): → FIX GW
- Retail Intermediary (green): → WS/REST GW (JSON)
- All participants: ← Drop Copy GW (FIX)
- All participants: ← Market Data (Multicast for colo; TCP/WS for WAN)

**Protocol labels on each arrow**:
- SBE binary, FIX 4.4, FIX 5.0SP2, JSON/WebSocket, UDP Multicast, TCP

**Latency annotations per gateway**:
- Binary GW: "Profile A: < 25 µs wire-to-wire"
- FIX GW: "Profile A: < 100 µs wire-to-wire"
- WS/REST GW: "< 10 ms"
- Drop Copy: "Profile A: < 100 µs"

---

### Diagram 6: Foundation Release (FR-1) vs Enterprise Target State

**Purpose**: Visual comparison of FR-1 scope and enterprise scope.
**Page name**: `06-FR1-vs-Enterprise`
**Layout**: Two columns side by side. Left = FR-1. Right = Enterprise. Shared rows = feature areas.

**Rows** (feature areas):
| Area | FR-1 (green = included, gray = not) | Enterprise (all green) |
- Matching: CLOB + Auctions | + RFQ, Dark, Complex, Implied
- Order Types: Basic (Market, Limit, IOC, FOK, GTC, Day) | + Stop, Iceberg, Pegged, Post-Only, Mass Quote
- Gateways: FIX + Binary | + MM Quote, WS/REST
- Risk: Fat finger + kill switch | + Credit, Position, Short-sell
- Post-Trade: Capture + fees | + CCP clearing, allocations, settlement
- DR: Warm restart | + Hot standby, active-passive DR
- Surveillance: File export | + Real-time stream, pattern detection
- Operations: Basic console | + Four-eyes, replay, reconciliation

**Color coding**: Green = included. Gray dashed = deferred. Orange = stretch goal.

---

### Diagram 7: Deployment Profile Comparison

**Purpose**: Show the three deployment profiles with their infrastructure stacks.
**Page name**: `07-Deployment-Profiles`
**Layout**: Three columns, one per profile. Shared rows = infrastructure layers.

**Columns**: Profile A | Profile B | Profile C

**Rows**:
| Layer | Profile A | Profile B | Profile C |
|-------|-----------|-----------|-----------|
| Hardware | Bare metal, dedicated | Bare metal or dedicated | Cloud bare-metal / VMs |
| Networking | DPDK kernel bypass | Standard kernel | Standard kernel |
| NIC | HW timestamping (Mellanox) | HW timestamping | Standard NIC |
| Clock | PTP (IEEE 1588) + GPS | PTP | NTP or PTP |
| CPU | Pinned, isolated | Pinned | Shared |
| Journal | NVMe + sync fsync | NVMe + sync fsync | SSD + sync or batched |
| Sequencer | Hardware or software | Software time-based | Software time-based |
| Engine latency p99 | < 5 µs | < 20 µs | < 100 µs |
| Wire-to-wire p99 | < 25 µs | < 100 µs | < 1 ms |
| Fairness delta | < 1 µs | < 5 µs | < 50 µs |
| Venue types | Tier-1 exchange, latency-competitive | Most regulated venues | Broker crossing, startup, carbon |

**Style**: Clean table with color gradients — Profile A = deepest blue, Profile C = lightest.

---

### DEFERRED DIAGRAMS (Create after text freeze)

| # | Diagram | Why Deferred | Prerequisites |
|---|---------|-------------|---------------|
| 8 | Order Lifecycle Sequence | Timings are profile-dependent; residual handling has options | Freeze order-type list, residual policy per venue, profile selection |
| 9 | HA/DR Failover Sequence | Active-active is Phase 4 item; DR site location TBD | Resolve open question #12 (DR site); freeze failover procedure |
| 10 | Journaling/Replay Recovery Flow | Both durability modes need diagramming | Freeze durability mode selection per venue type |
| 11 | Fairness/Sequencing Boundary | Depends on hardware vs. software sequencer choice | Resolve open question #4 (sequencing strategy) |
| 12 | Rollout/Migration Swimlane | Depends on venue type and legacy migration context | Resolve open question #15 (migration required?) |

---

## 7. Diagram Creation — Draw.io Readiness

**Is it the right time to create architecture diagrams?**
**Yes** — for the 7 high-priority diagrams listed above. The core architecture is stable, the terminology is normalized, and the component model is well-defined.

**Which diagrams should be created immediately?**
Diagrams 1–7 (System Context, Logical Architecture, Component Interaction, Hot Path Separation, Participant Connectivity, FR-1 vs Enterprise, Deployment Profiles).

**Which diagrams should wait?**
Diagrams 8–12 (Order Lifecycle Sequence, HA/DR Failover, Journaling/Replay, Fairness/Sequencing, Rollout/Migration). These depend on design decisions that are still in the open-questions list (§29.3).

**What textual items must be frozen before the deferred diagrams?**
1. Hot-path language selection (C++/Rust/Java) — affects sequence timing annotations
2. Sequencing strategy (hardware vs. software) — affects fairness diagram
3. DR site location and failover mode — affects HA/DR diagram
4. Target venue type for FR-1 — affects order lifecycle and migration diagrams
5. Durability mode selection per venue type — affects journaling flow diagram

---

## 8. Final Conclusion

| Purpose | Ready? |
|---------|--------|
| **Workshop-ready** | **Yes** — all terminology, targets, and architecture language are internally consistent |
| **Architecture-baseline-ready** | **Yes** — sufficient for solution architecture work and draw.io diagram creation |
| **Regulator-pre-read-ready** | **No** — requires jurisdiction-specific compliance overlays |
| **Implementation-kickoff-ready** | **No** — requires resolution of 20 open design decisions (§29.3) |

The specification package is now a credible, internally consistent, professionally edited baseline suitable for stakeholder workshops, architecture design, technology evaluation, and vendor engagement. The 7 high-priority diagrams can be created immediately in draw.io format.
