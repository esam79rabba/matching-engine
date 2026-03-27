# Architecture Review Deck — Slide Content

**Purpose**: Establish technical confidence; align engineering team; validate architecture; identify build risks
**Audience**: CTO, solution architects, engineering leads, SRE/platform leads
**Slides**: 28 | **Duration**: 60 min presentation + 30 min discussion
**Tone**: Technical, precise, architecture-review-ready. Uses frozen baseline terminology.

---

## Slide 1: Title
**Title**: Matching Engine Platform — Architecture Review
**Key points**: Project name, date, version 1.0.0-FINAL, confidential
**Speaker notes**: Set context: this deck reviews the frozen architecture baseline. Decisions marked as open require workshop resolution before build starts.

---

## Slide 2: Architecture Principles
**Title**: Core Design Principles
**Key points**:
- Determinism: identical input → identical output, always
- Separation of concerns: hot path isolated from control plane
- Configurability: market models parameterized; new logic requires controlled release
- Resilience by design: journal, snapshots, warm restart, defined RTO/RPO
- Regulatory readiness: audit trails, clock sync, surveillance hooks from inception
**Speaker notes**: These principles are non-negotiable. Every design decision must be tested against them.
**Source**: §01.4
**Diagram**: None (text slide)

---

## Slide 3: System Context
**Title**: System Context — External Actors
**Key points**:
- Platform boundary and all external actors
- Participants: market makers, brokers, buy-side, retail intermediaries
- Post-trade: CCP, custodian
- Regulatory: surveillance system, regulator
- Operations: market ops staff, SRE team
**Speaker notes**: This is the "outside-in" view. Everything inside the blue box is our platform. Everything outside is an external actor we integrate with.
**Diagram**: Diagram 01 (System Context)
**Source**: §04, §12, §13

---

## Slide 4: 5-Layer Logical Architecture
**Title**: Logical Architecture — 5-Layer Model
**Key points**:
- Layer 1 (Hot Path): Sequencer → Journal → Engine → Output Bus
- Layer 2 (Control Plane): Config, state machine, snapshots, feature flags
- Layer 3 (Platform Services): Gateways, risk, market data, post-trade, ref data
- Layer 4 (Operations): Ops console, admin, replay, reconciliation, monitoring
- Layer 5 (Ecosystem): Clearing, settlement, surveillance, regulatory reporting
**Speaker notes**: The critical insight is the separation between Layers 1–2 (engine-owned, latency-critical, deterministic) and Layers 3–5 (standard services, containerizable, independently deployable).
**Diagram**: Diagram 02 (Logical Architecture)
**Source**: §10

---

## Slide 5: Hot Path vs Control Plane
**Title**: Hot Path Boundary
**Key points**:
- Hot path: sequencer through output bus. Single-threaded per partition. No GC, no allocation, no external I/O except journal.
- Control plane: everything else. Standard networking, containers, databases, logging acceptable.
- Risk checks execute BEFORE the sequencer — latency-sensitive but not in the deterministic engine hot path.
- Boundary rule: the hot path begins at the sequencer and ends at the output bus.
**Speaker notes**: This is the most important boundary in the system. Anything that violates hot-path discipline (allocation, logging, external calls) directly degrades latency and jitter.
**Diagram**: Diagram 04 (Hot Path Separation)
**Source**: §10, §11

---

## Slide 6: Partition Model
**Title**: Single-Threaded Per Partition
**Key points**:
- Each partition: 1 dedicated CPU core, 1 thread, 1+ order books (instruments)
- Deterministic: no locks, no shared mutable state, no context switches
- Horizontal scaling: add partitions (more instruments → more cores)
- No cross-partition matching: each instrument belongs to exactly one partition
- High-activity instruments may be isolated to a dedicated partition
**Speaker notes**: This is the foundational architecture choice. Simplicity and provable determinism outweigh theoretical throughput gains from multi-threading. Scale horizontally via more partitions.
**Diagram**: Engine partition detail from §11.7
**Source**: §11.1, §11.7, §27.2.2

---

## Slide 7: Sequencing and Fairness
**Title**: Sequencing Boundary and Fairness Model
**Key points**:
- Sequencing is partition-local monotonic. Global identity = partition_id:local_sequence.
- Fairness guarantees apply at the venue-controlled sequencing boundary.
- Network variability outside the venue boundary is not guaranteed.
- Profile A (HW sequencer): < 1 µs fairness delta. Profile A/B (SW): < 5 µs. Profile C: < 50 µs.
- Venue publishes per-gateway fairness statistics for regulatory transparency.
**Speaker notes**: This is a critical regulatory and competitive point. The fairness boundary is the sequencer — that's where we guarantee equal treatment. Anything before the sequencer (participant networks, cross-connects) varies by access tier. D-04 closed: software time-based sequencing with PTP for FR-1.
**Diagram**: Diagram 09 (Fairness/Sequencing Boundary)
**Source**: §11.2, §15.8

---

## Slide 8: Order Lifecycle
**Title**: Order Lifecycle — End to End
**Key points**:
- Submit → Gateway validate → Risk check → Sequencer (assign seq# + timestamp) → Journal persist → Engine match → Output bus → Market data + Drop copy + Post-trade + Surveillance
- Rejection path: Risk REJECT → Gateway → Participant (no sequencer involvement)
- Market order residual policy: cancel (default) or market-to-limit with collar. No unprotected resting.
**Speaker notes**: Walk through each step. Emphasize that the journal write happens BEFORE the engine processes (Regulatory-Grade mode). This is the zero-data-loss guarantee.
**Diagram**: Diagram 08 (Order Lifecycle Sequence)
**Source**: §11.4

---

## Slide 9: Matching Algorithm
**Title**: Matching — Price-Time FIFO (FR-1)
**Key points**:
- FR-1: price-time (FIFO) priority only
- Enterprise: adds pro-rata, parity, hybrid — selectable from implemented priority models via venue configuration. Materially new matching algorithms require a controlled software release.
- Execution always at passive (resting) order's price — aggressive order gets price improvement
- STP: cancel-newest (FR-1 default); all modes in Enterprise
- Market order residual: cancel or collar — frozen policy
**Speaker notes**: Price-time is the simplest and most widely used. Pro-rata is needed for derivatives (Phase 2+). Already-implemented priority models can be selected through configuration. Materially new matching logic (e.g., a novel allocation algorithm) requires a code change and release governance — it is not purely a configuration parameter.
**Source**: §11.5, §07.4

---

## Slide 10: Journaling and Durability
**Title**: Journal, Snapshots, and Durability Modes
**Key points**:
- Journal = single source of truth. Append-only, checksummed, locally durable.
- **Regulatory-Grade** (default): fsync before processing. Zero data loss. Required for regulated venues.
- **Reduced-Latency** (opt-in): batched writes. Data-loss window = batch interval. Only where permitted.
- Snapshots: periodic full-state capture. Enables fast warm restart.
- FR-1 baseline: local journal + snapshot/replay + warm restart (single site).
- DR journal replication: deployment-dependent; Phase 2+ unless explicitly included in FR-1 scope.
- Storage tiers: NVMe (hot, 90 days) → SSD (warm, 1 year) → Object storage (cold, 7 years).
**Speaker notes**: FR-1 ships with local durable journal and warm restart. Journal replication to a DR site is a Phase 2 capability. The Regulatory-Grade/Reduced-Latency distinction is a deployment configuration, not a code branch. D-06 closed: custom C++ append-only journal implemented with segmented rotation and CRC32 checksums.
**Diagram**: Diagram 10 (Journaling/Replay Recovery)
**Source**: §11.3, §27.2.1

---

## Slide 11: Recovery Model
**Title**: Recovery — Warm Restart from Journal
**Key points**:
- Failure detected by watchdog (< 2 seconds)
- Load latest snapshot (all books + orders + seq# marker)
- Replay journal from snapshot point (deterministic — produces identical state)
- Validate recovered state (order count, checksums, seq# continuity)
- Resume processing (< 60 seconds for 100K resting orders)
**Speaker notes**: The determinism guarantee means recovery is not approximate — it produces the exact same state as before the failure. This is also the mechanism for audit reconstruction.
**Diagram**: Diagram 10 (right panel — Recovery flow)
**Source**: §11.6

---

## Slide 12: Gateway Architecture
**Title**: Participant Connectivity — Gateways
**Key points**:
- FIX 4.4 gateway (FR-1): industry standard; broadest participant compatibility
- Binary gateway (SBE protocol): lowest latency; for market makers and HFT
- Drop copy: real-time read-only trade/order feed for participants
- WS/REST gateway: Phase 2+ for lower-tier participants
- MM Quote gateway: Phase 2+ for mass-quote market makers
**Speaker notes**: FR-1 ships with FIX + binary + drop copy. This covers the critical participant base. WS/REST and dedicated MM Quote gateways are Phase 2 additions.
**Diagram**: Diagram 05 (Participant Connectivity)
**Source**: §12.2, §13

---

## Slide 13: Pre-Trade Risk
**Title**: Pre-Trade Risk Controls
**Key points**:
- Fat finger: price, quantity, notional limits
- Message rate throttling per session/participant
- Kill switch: cancel all by participant/instrument/market-wide (< 1 second effect)
- FR-1 scope: fat finger + throttle + kill switch
- Enterprise adds: credit limits, position limits, exposure limits, short-sell controls
**Speaker notes**: Risk checks execute BEFORE the sequencer — they are on the ingress path but not in the deterministic engine hot path. This is a deliberate boundary choice.
**Source**: §12.3, §14.5

---

## Slide 14: Market Data
**Title**: Market Data Dissemination
**Key points**:
- Full depth (all price levels) + trades + statistics
- Multicast (UDP) for colocated participants; TCP/WebSocket for WAN (Phase 2)
- Snapshot + incremental model with gap detection
- Sequenced — published in exact engine processing order
- Publication latency target: Profile A < 10 µs from engine event
**Speaker notes**: Market data is critical for participant adoption. Completeness and correctness matter more than pure speed for most venues.
**Source**: §12.5, §15.1

---

## Slide 15: Deployment Profiles
**Title**: Three Deployment Profiles
**Key points**:
- Profile A: bare metal, DPDK, CPU pinning, PTP — for tier-1 exchanges
- Profile B: bare metal, standard kernel, PTP — for most regulated venues
- Profile C: cloud bare-metal or VMs, NTP — for startups, commodity/carbon venues
- Same platform binary; different infrastructure determines performance
- All targets are engineering goals validated through performance testing, not guarantees
**Speaker notes**: This is a key architecture decision. We do NOT hard-code latency assumptions into the software. The deployment profile is an infrastructure choice.
**Diagram**: Diagram 07 (Deployment Profiles)
**Source**: §15, §21

---

## Slide 16: Performance Budget (Profile A)
**Title**: End-to-End Latency Budget — Profile A
**Key points**:
- Gateway receive: ~1 µs | Validation: ~2 µs | Risk: ~3 µs
- Sequencer + journal fsync: ~3 µs | Engine match: ~5 µs
- Output + fan-out: ~5 µs | Total wire-to-wire: ~20 µs (p99)
- These are engineering targets for bare-metal colocation, not universal guarantees
- Profile B: ~4× multiplier. Profile C: ~20× multiplier.
**Speaker notes**: Walk through each component. The largest contributor is the engine match (~5 µs). The journal fsync adds ~2 µs in Regulatory-Grade mode. These numbers are validated through performance testing.
**Source**: §21.1

---

## Slide 17: Technology Recommendations
**Title**: Technology Stack — Recommendations
**Key points**:
- Hot path: C++ (D-01 closed, implemented)
- Control plane: Java/Kotlin or Go
- Messaging: Aeron (engine ↔ services); Kafka (surveillance, analytics)
- Monitoring: Prometheus + Grafana
- Containers: Kubernetes for non-hot-path services only
- FPGA: not recommended for FR-1; evaluate Phase 4+
**Speaker notes**: D-01 closed: C++ selected and implemented for the hot path (~10,000 lines, 17 modules). Java/Kotlin remains the recommendation for control plane services.
**Source**: §15.13

---

## Slide 18: HA/DR Failover
**Title**: HA/DR — Active-Passive Reference Model
**Key points**:
- Primary site: active (all components processing)
- DR site: passive/hot standby (journal replicated, state rebuilt)
- Failover: detect (< 2s) → decide → promote (< 60s) → validate → activate → resume
- Total RTO target: < 5 minutes. RPO: near-zero (journal replication lag).
- DR is Phase 2 scope; FR-1 uses warm restart (same host) only
**Speaker notes**: The reference model is active-passive. Active-active is a Phase 4 consideration for 24/7 venues. Operator authorization for failover is configurable.
**Diagram**: Diagram 11 (HA/DR Failover)
**Source**: §19

---

## Slide 19: Compliance Framework
**Title**: Compliance-by-Configuration
**Key points**:
- Jurisdiction-agnostic core with configurable policy profiles
- Policy profile defines: clock sync, transparency, risk controls, reporting hooks, retention
- Example profiles available for EU MiFID II, US Reg ATS, SG MAS, UAE VARA
- Audit trail: 100% of state mutations; nanosecond timestamps; tamper-evident hash chain
- Surveillance: data export hooks; pattern detection in Phase 2 (buy vs. build open)
**Speaker notes**: The compliance layer is a configuration overlay, not a code branch per jurisdiction. One platform binary serves all jurisdictions through policy profiles. [OPEN DECISION: D-03 determines which profile is configured for FR-1]
**Source**: §16

---

## Slide 20: Component Interaction
**Title**: Full Component Interaction Flow
**Key points**:
- Numbered message flow from participant to external systems
- 8-step flow: submit → validate → risk → sequence → journal → match → output → deliver
- Supporting services: reference data, operations console, kill switch
- WS/REST gateway marked as Phase 2+
**Speaker notes**: This is the detailed "how it all fits together" view. Use for engineering team alignment.
**Diagram**: Diagram 03 (Component Interaction)
**Source**: §12, §13

---

## Slide 21: FR-1 Scope
**Title**: Foundation Release — What Ships
**Key points**:
- Matching: continuous LOB + price-time FIFO (253 tests, operational). Auctions deferred to Phase 2.
- Connectivity: FIX 4.4 + binary + drop copy
- Risk: fat finger + throttle + kill switch
- Market data: multicast depth + trades + stats + snapshot
- Post-trade: capture + fees + confirmation + EOD files
- Operations: basic console (session, halt/resume, params)
- Surveillance: file-based event export
- Recovery: warm restart (no DR site)
**Speaker notes**: This is a production-capable release, not a prototype. It can run a trading day and generate revenue. What it defers: CCP clearing, advanced order types, DR site, WS/REST gateway, full surveillance.
**Diagram**: Diagram 06 (FR-1 vs Enterprise)
**Source**: §29

---

## Slide 22: What FR-1 Defers
**Title**: Enterprise Features — Phased After FR-1
**Key points**:
- Phase 2 (EGR): Circuit breakers, iceberg/pegged orders, CCP clearing, mass quote GW, DR site
- Phase 3 (MAR): Multi-asset, pro-rata, RFQ, dark/midpoint, complex orders
- Phase 4 (AOR): Implied matching, automated failover, chaos testing, DPDK optimization
- Phase 5 (ECO): Analytics, white-label, API marketplace, digital-asset custody
**Speaker notes**: The architecture is designed so all these features are additive. No re-architecture needed. Each phase extends the platform's addressable market.
**Source**: §26

---

## Slide 23: Workstream Overview
**Title**: 14 Implementation Workstreams
**Key points**: WS-01 through WS-14 names and owners (one row per workstream)
**Speaker notes**: This is the delivery organization. Each workstream has a charter, dependencies, and exit criteria documented in the implementation plan.
**Source**: Implementation Plan

---

## Slide 24: Milestone Plan
**Title**: 20 Milestones to FR-1 Go-Live
**Key points**: Top 10 milestones with target months (M1 through M9)
**Speaker notes**: Highlight the critical path: D-01/D-02/D-03 → engine core → end-to-end lifecycle → certification → go-live.
**Source**: Implementation Plan

---

## Slide 25: Critical Dependencies
**Title**: Dependency Chain
**Key points**: Dependency flow diagram showing D-01→WS-02→WS-05, D-02/D-03→WS-01→WS-06, D-05→WS-10→WS-03
**Speaker notes**: The longest dependency chain runs through the engine core. Any delay in D-01 delays everything downstream.
**Source**: Implementation Plan dependency register

---

## Slide 26: Closed Design Decisions
**Title**: All Design Decisions Closed and Implemented
**Key points**: D-01 (C++ — implemented), D-02 (Digital assets — implemented), D-03 (ADGM/FSRA — compliance overlay prepared), D-04 (Software time-based sequencing with PTP — implemented), D-05 (Cloud dev/test active; ADGM-aligned production in parallel — operational), D-06 (Custom append-only journal with segmented rotation and CRC32 — implemented). All 6 decisions CLOSED.
**Speaker notes**: All founder decisions are resolved and implemented in the FR-1 prototype. M1–M10 complete with 253 tests across 17 modules. Focus shifts to pilot certification and production deployment.
**Source**: Decision Register, IMPLEMENTATION-STATUS-M10.md

---

## Slide 27: Top 10 Delivery Risks
**Title**: Risk Register
**Key points**: R-01 through R-10 with probability, impact, and lead mitigation
**Speaker notes**: Decision closure delay (R-01) is the highest-probability risk. Performance not met (R-02) is the highest-impact technical risk. Both have named mitigations.
**Source**: RAID Register

---

## Slide 28: Next Steps
**Title**: Next Steps — Post M10
**Key points**:
- M10 complete: segmented journal, warm restart, transport abstraction layer operational
- M11 in progress: client certification harness, pilot deployment packaging, operational runbooks
- Parallel: ADGM/FSRA pre-application engagement, participant recruitment
- Target: pilot deployment readiness upon M11 completion
- Phase 2+ deferred items: kernel-bypass Direct I/O, OpenSSL TLS/mTLS integration, auction mechanism
**Speaker notes**: All decisions are closed and the prototype is code-complete. The next concrete steps are pilot certification, deployment packaging, and regulatory engagement. Phase 2+ items are tracked but not blocking FR-1 pilot.
**Source**: Action Tracker v3.0, IMPLEMENTATION-STATUS-M10.md
