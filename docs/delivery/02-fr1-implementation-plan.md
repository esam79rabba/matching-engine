# FR-1 Implementation Plan

**Version**: 1.0 | **Date**: 2026-03-24 | **Classification**: Internal — Program Planning
**Status**: Planning document — dates are targets pending decision closure

---

## 1. Implementation Overview

**Objective**: Deliver the Foundation Release (FR-1) of the multi-asset matching engine platform — a production-capable release supporting the initial market scope with core order-book trading, basic risk controls, and primary connectivity gateways.

**Planning timeline**: 9 months from decision closure to go-live
**Team size**: 12–15 people (planning assumption)
**Phases**: Decide (M0–1) → Build Core (M1–4) → Integrate (M4–6) → Certify (M6–8) → Launch (M8–9)

---

## 2. Workstream Charters

### WS-01: Market Microstructure and Product Rules

| Field | Detail |
|-------|--------|
| **Objective** | Define and configure market model, order types, session schedule, and venue rules for FR-1 |
| **Scope** | Instrument configuration, session profiles, tick/lot tables, price bands, STP mode, residual policy, auction config |
| **Out of scope** | Pro-rata allocation, RFQ, dark/midpoint, complex orders (deferred to Phase 2+) |
| **Deliverables** | FR-1 market rule-book draft; instrument configs; session schedule; tick tables |
| **Dependencies** | D-02 (asset class), D-03 (jurisdiction) |
| **Roles** | Product Manager, Microstructure Specialist, Compliance Officer |
| **Entry criteria** | Asset class and jurisdiction decided |
| **Exit criteria** | Rule-book approved by compliance; configs loaded and validated |
| **Top risks** | Late regulatory feedback changes rules; market-maker requirements conflict |
| **First actions** | Draft rule-book outline once D-02/D-03 decided; engage market-maker advisory |

### WS-02: Core Matching Engine

| Field | Detail |
|-------|--------|
| **Objective** | Build deterministic, single-threaded-per-partition matching engine kernel |
| **Scope** | Order book structures, price-time FIFO matching, STP, instrument state machine, market-order residual handling, sequencer integration |
| **Out of scope** | Pro-rata matching, implied matching, complex orders |
| **Deliverables** | Engine binary; deterministic replay test suite; benchmark results |
| **Dependencies** | D-01 (language), D-04 (sequencer), D-06 (journal), WS-07 |
| **Roles** | 3–4 Senior Engine Engineers, 1 Performance Engineer |
| **Entry criteria** | Language, sequencer, and journal decisions made |
| **Exit criteria** | p99 latency within profile target; replay determinism proven; 1M msg/s sustained |
| **Top risks** | Performance targets not met; determinism bugs; journal integration |
| **First actions** | Skeleton project + build pipeline on Day 1 after D-01; order book data structures first |

### WS-03: Gateways and Connectivity

| Field | Detail |
|-------|--------|
| **Objective** | Build FIX and binary order-entry gateways, drop copy gateway |
| **Scope** | FIX 4.4 session mgmt, SBE binary protocol, cancel-on-disconnect, drop copy, cert harness |
| **Out of scope** | WS/REST gateway (Phase 2), MM Quote gateway (Phase 2) |
| **Deliverables** | FIX GW pair, Binary GW pair, Drop Copy GW, cert test harness |
| **Dependencies** | D-01, FIX engine library selection, WS-02 (internal message format) |
| **Roles** | 2 Gateway Engineers, 1 Connectivity Specialist |
| **Entry criteria** | Binary protocol spec finalized; FIX engine library selected |
| **Exit criteria** | 3+ pilot participants certified; gateway failover tested |
| **Top risks** | FIX edge cases; participant-specific quirks; cert delays |
| **First actions** | Select FIX engine library; draft binary protocol SBE schema |

### WS-04: Pre-Trade Risk and Controls

| Field | Detail |
|-------|--------|
| **Objective** | Build pre-trade risk service with fat-finger, throttle, and kill-switch controls |
| **Scope** | Price/qty/notional checks, message rate limits, kill switch |
| **Out of scope** | Credit limits, position limits, short-sell (Phase 2) |
| **Deliverables** | Risk service, kill-switch integration, limit config API |
| **Dependencies** | WS-02 (engine ingress), WS-06 (ref data for limits) |
| **Roles** | 1–2 Engineers |
| **Entry criteria** | Engine ingress interface defined |
| **Exit criteria** | Risk checks within profile latency target; kill switch < 1 second |
| **Top risks** | Latency impact; false rejection rates |
| **First actions** | Define risk-check interface with WS-02 lead |

### WS-05: Market Data Platform

| Field | Detail |
|-------|--------|
| **Objective** | Build market data dissemination (depth, trades, stats, snapshot recovery) |
| **Scope** | Multicast publisher, SBE encoding, snapshot service, gap detection |
| **Out of scope** | WAN feed, vendor feed, conflation (Phase 2) |
| **Deliverables** | MD publisher, snapshot service, channel config |
| **Dependencies** | WS-02 (output bus interface) |
| **Roles** | 1–2 Engineers |
| **Entry criteria** | Output bus interface defined |
| **Exit criteria** | Complete, sequenced data; snapshot recovery < 10 sec |
| **Top risks** | Multicast config complexity; gap recovery reliability |
| **First actions** | Define output event schema with WS-02 lead |

### WS-06: Reference Data and Static Data

| Field | Detail |
|-------|--------|
| **Objective** | Build reference data service for instruments, participants, sessions, tick/lot tables |
| **Scope** | CRUD APIs, bulk load, versioning, audit, session scheduling |
| **Out of scope** | Corporate actions, multi-currency (Phase 2) |
| **Deliverables** | Ref data service, admin API, bulk import tools |
| **Dependencies** | WS-01 (rule-book defines data model) |
| **Roles** | 1 Engineer, 1 Business Analyst |
| **Entry criteria** | Market rule-book draft available |
| **Exit criteria** | FR-1 instruments loaded; sessions configured; tick tables validated |
| **Top risks** | Missing fields discovered late |
| **First actions** | Draft data model from rule-book output |

### WS-07: Journaling / Recovery / Durability

| Field | Detail |
|-------|--------|
| **Objective** | Implement journal, snapshot manager, warm-restart recovery |
| **Scope** | Append-only journal (Regulatory-Grade default), segment rotation, checksums, snapshot capture, replay |
| **Out of scope** | DR replication (Phase 2), Reduced-Latency mode (deferred unless needed) |
| **Deliverables** | Journal library, snapshot manager, replay controller, recovery test suite |
| **Dependencies** | D-06 (journal implementation), WS-02 (engine integration) |
| **Roles** | 1–2 Engineers (may overlap with WS-02) |
| **Entry criteria** | Journal implementation decision made |
| **Exit criteria** | Warm restart < 60 sec for 100K orders; replay determinism; zero data loss |
| **Top risks** | Corruption edge cases; sync fsync latency |
| **First actions** | Prototype journal write path on target hardware |

### WS-08: Post-Trade / Trade Capture

| Field | Detail |
|-------|--------|
| **Objective** | Build trade capture, fee calculation, confirmation, EOD files |
| **Scope** | Trade capture from output bus, fee engine, trade confirmation, EOD recon files |
| **Out of scope** | CCP clearing adapter, allocations, give-up (Phase 2) |
| **Deliverables** | Post-trade service, fee engine, EOD file generator |
| **Dependencies** | WS-02 (output bus), WS-06 (fee schedules), WS-01 (fee model) |
| **Roles** | 1–2 Engineers |
| **Entry criteria** | Fee model decided; output event schema finalized |
| **Exit criteria** | All trades captured; fees correct; EOD files balance |
| **Top risks** | Fee edge cases; EOD timing pressure |
| **First actions** | Define fee model with Product (WS-01) |

### WS-09: Surveillance Hooks and Compliance Evidence

| Field | Detail |
|-------|--------|
| **Objective** | Implement surveillance data export and audit trail for FR-1 |
| **Scope** | File-based event export, audit trail with nanosecond timestamps, clock sync evidence |
| **Out of scope** | Real-time streaming, pattern detection, MM obligation monitoring (Phase 2) |
| **Deliverables** | Event export pipeline, audit trail query tool, clock sync logs |
| **Dependencies** | WS-02 (event taxonomy), D-03 (jurisdiction evidence requirements) |
| **Roles** | 1 Engineer, Compliance Officer |
| **Entry criteria** | Jurisdiction decided; event taxonomy finalized |
| **Exit criteria** | 100% event capture; audit trail queryable; clock sync auditable |
| **Top risks** | Completeness gaps; clock sync insufficient |
| **First actions** | Define event taxonomy with WS-02 lead |

### WS-10: Infrastructure / Colocation / Platform Engineering

| Field | Detail |
|-------|--------|
| **Objective** | Procure hardware, set up colocation, PTP, networking, CI/CD |
| **Scope** | Servers, NICs, PTP grandmaster, network switches, OS tuning, CI/CD, cert environment |
| **Out of scope** | DR site (Phase 2) |
| **Deliverables** | Production hardware, PTP operational, CI/CD, cert environment |
| **Dependencies** | D-05 (colo site), D-01 (hardware spec) |
| **Roles** | 2–3 SRE / Infrastructure Engineers |
| **Entry criteria** | Colo site decided; hardware spec finalized |
| **Exit criteria** | Hardware operational; PTP < 1 µs; cert env available |
| **Top risks** | Hardware supply chain; PTP config complexity |
| **First actions** | Request colo quotes; begin hardware spec once D-01 shortlisted |

### WS-11: Security / IAM / Certificates

| Field | Detail |
|-------|--------|
| **Objective** | Implement mTLS, RBAC, HSM, secrets management, network segmentation |
| **Deliverables** | PKI infrastructure, RBAC config, HSM integration |
| **Dependencies** | WS-10 (network infra), WS-03 (gateway TLS) |
| **Roles** | 1–2 Security Engineers |
| **Exit criteria** | mTLS on all connections; RBAC enforced; pen test passed |

### WS-12: Observability / Monitoring / SRE

| Field | Detail |
|-------|--------|
| **Objective** | Deploy monitoring stack — metrics, logging, alerting, dashboards |
| **Deliverables** | Prometheus/Grafana stack, alert rules, latency dashboards, on-call runbooks |
| **Dependencies** | WS-10 (infrastructure) |
| **Roles** | 1–2 SRE Engineers |
| **Exit criteria** | All critical alerts defined; latency dashboards operational |

### WS-13: Testing / Certification / Performance Engineering

| Field | Detail |
|-------|--------|
| **Objective** | Build test suites, certification framework, performance validation |
| **Deliverables** | Unit tests (95% engine), simulation tests, 25 cert test cases, perf benchmarks, fairness reports |
| **Dependencies** | All other workstreams |
| **Roles** | 2–3 QA Engineers, 1 Performance Engineer |
| **Exit criteria** | 95% engine coverage; cert tests passing; perf within profile; fairness validated |

### WS-14: Program / Release / Rollout Management

| Field | Detail |
|-------|--------|
| **Objective** | Coordinate cross-workstream delivery, drive decisions, execute rollout |
| **Deliverables** | Program plan, decision log, risk register, rollout checklist, go-live report |
| **Dependencies** | All workstreams |
| **Roles** | Program Manager, Release Manager |
| **Exit criteria** | FR-1 go-live executed; 30-day stabilization completed |

---

## 3. Milestone Plan

*All dates are planning targets assuming Month 0 = decision closure. Milestones marked [BLOCKED] require unresolved decisions.*

| # | Milestone | Target | Owner | Blocked By |
|---|-----------|:------:|-------|:----------:|
| M1 | Asset class + jurisdiction decided | M0 W1 | Product/Compliance | — |
| M2 | Hot-path language decided | M0 W2 | CTO | — |
| M3 | Colocation site contracted | M1 | Infrastructure | D-03 |
| M4 | Hardware ordered | M1 | Infrastructure | D-01, D-05 |
| M5 | Sequencer + journal decisions made | M2 | Architecture | D-01 |
| M6 | Engine: single-instrument matching functional | M3 | Engine team | D-01 |
| M7 | Journal + snapshot + warm restart functional | M3 | Engine team | D-06 |
| M8 | FIX gateway session management functional | M3 | Gateway team | — |
| M9 | Binary gateway functional | M3 | Gateway team | D-01 |
| M10 | Production hardware deployed + PTP operational | M4 | SRE | D-05 |
| M11 | End-to-end order lifecycle (submit → match → ack) | M4 | Integration | M6, M8 |
| M12 | Market data publisher operational | M5 | MD team | M6 |
| M13 | Pre-trade risk operational | M5 | Risk team | M11 |
| M14 | Drop copy operational | M5 | Gateway team | M11 |
| M15 | Certification environment available | M6 | SRE | M10 |
| M16 | First pilot participant certified | M7 | Connectivity | M15 |
| M17 | Performance targets validated under load | M7 | Perf eng | M10, M11 |
| M18 | 3 pilot participants certified | M8 | Connectivity | M16 |
| M19 | Dress rehearsal (full simulated trading day) | M8 | Operations | M18 |
| M20 | **FR-1 GO-LIVE** | **M9** | **Program** | M19 |

---

## 4. Dependency Register

| ID | Dependency | From → To | Type | Status |
|----|-----------|-----------|------|--------|
| DEP-01 | D-02/D-03 → WS-01 | Decision → Work | Hard | Open |
| DEP-02 | D-01 → WS-02, WS-03, WS-07 | Decision → Work | Hard | Open |
| DEP-03 | D-05 → WS-10 | Decision → Work | Hard | Open |
| DEP-04 | D-04/D-06 → WS-02, WS-07 | Decision → Work | Hard | Open |
| DEP-05 | WS-02 → WS-03, WS-04, WS-05 | Workstream → Workstream | Interface | Not started |
| DEP-06 | WS-01 → WS-06 | Workstream → Workstream | Data | Not started |
| DEP-07 | WS-10 → WS-11, WS-12 | Workstream → Workstream | Infra | Not started |
| DEP-08 | WS-06 → WS-08 | Workstream → Workstream | Data | Not started |
| DEP-09 | Hardware procurement → M10 | External → Milestone | Vendor | Not started |
| DEP-10 | Pilot participant commitment → M16 | External → Milestone | Business | Not started |

---

## 5. RAID Register

### Risks

| ID | Risk | Prob. | Impact | Mitigation | Owner |
|----|------|:-----:|:------:|------------|-------|
| R-01 | Decision closure delays | High | Critical | Decision pack with deadlines; escalation to CTO/Board | Program |
| R-02 | Performance targets not met | Medium | High | Early profiling M3; hardware validation | Eng Lead |
| R-03 | Hardware supply chain delays | Medium | High | Order M1; cloud fallback for dev | Infra |
| R-04 | Insufficient pilot participants | Medium | Critical | Incentive programs; strategic partnerships | BizDev |
| R-05 | Determinism bug in engine | Low | Critical | Replay tests on every commit | Eng Lead |
| R-06 | Key talent departure | Medium | High | Documentation-driven; pair programming | CTO |
| R-07 | Regulatory feedback requires changes | Low | Critical | Early regulator engagement | Compliance |
| R-08 | FIX certification complexity | Medium | Medium | Sandbox early; connectivity support | Connectivity |
| R-09 | Scope creep | High | Medium | Product owner empowered to defer | Product |
| R-10 | PTP/clock sync difficulties | Medium | Medium | Vendor support early | SRE |

### Assumptions

| ID | Assumption | Impact if Wrong |
|----|-----------|----------------|
| A-01 | Engineering team of 8+ available within 60 days | Timeline extends |
| A-02 | Hardware delivery within 8 weeks of order | M10 delayed |
| A-03 | At least 1 market maker committed before M7 | Launch viability risk |
| A-04 | 24/7 trading NOT required for FR-1 | Major scope change if wrong |
| A-05 | Single-site deployment sufficient for FR-1 (no DR) | Warm restart only |

### Issues
*None currently open. Decision closure will surface issues.*

---

## 6. Staffing View

| Workstream | Engineers | QA | Product | Ops/SRE | Compliance | Total |
|-----------|:---------:|:--:|:-------:|:-------:|:----------:|:-----:|
| WS-01 Rules | — | — | 1 | — | 0.5 | 1.5 |
| WS-02 Engine | 3–4 | — | — | — | — | 3–4 |
| WS-03 Gateways | 2 | — | — | — | — | 2 |
| WS-04 Risk | 1 | — | — | — | — | 1 |
| WS-05 Market Data | 1 | — | — | — | — | 1 |
| WS-06 Ref Data | 1 | — | 0.5 | — | — | 1.5 |
| WS-07 Journal | 1 | — | — | — | — | 1 |
| WS-08 Post-Trade | 1 | — | — | — | — | 1 |
| WS-09 Surveillance | 1 | — | — | — | 0.5 | 1.5 |
| WS-10 Infra | — | — | — | 2 | — | 2 |
| WS-11 Security | 1 | — | — | — | — | 1 |
| WS-12 Monitoring | — | — | — | 1 | — | 1 |
| WS-13 Testing | — | 2 | — | — | — | 2 |
| WS-14 Program | — | — | — | — | — | 1 (PM) |
| **Role demand total** | **~12** | **2** | **1.5** | **3** | **1** | **~20** |

> **This is a role-demand view, not a net headcount plan.** Several roles overlap across workstreams (e.g., WS-02 engine engineers also deliver WS-07 journal; SRE covers both WS-10 infra and WS-12 monitoring; Compliance is part-time across WS-01 and WS-09). With typical overlap, net FR-1 headcount is **12–15 FTE** (planning assumption). Final staffing confirmed after decision closure and workstream kickoff.

---

## 7. First 90-Day Plan

### Weeks 1–2: Decide

| Action | Owner |
|--------|-------|
| Decision workshop (D-01 through D-06) | Program |
| Close D-02 + D-03 (asset class + jurisdiction) | Product + Compliance |
| Close D-01 (language) | CTO |
| Begin colocation site evaluation (D-05) | Infrastructure |
| Begin hiring pipeline for engine engineers | CTO / HR |
| Engage regulatory advisor | Compliance |
| Stand up program infrastructure (Jira/Linear, Slack, CI repo) | Program |

### Weeks 3–4: Mobilize

| Action | Owner |
|--------|-------|
| Close D-05 (colo site); place hardware order | Infrastructure |
| Close D-04 (sequencer) and D-06 (journal) | Architecture / Eng Lead |
| Engine team begins: build pipeline, order book data structures | WS-02 Lead |
| Gateway team begins: FIX engine library evaluation | WS-03 Lead |
| Draft FR-1 market rule-book outline | WS-01 Lead |
| Set up CI/CD pipeline and dev environment | SRE |
| Begin security architecture design | WS-11 Lead |

### Month 2: Build Foundation

| Action | Owner |
|--------|-------|
| Engine: order book insert/cancel/match for single instrument | WS-02 |
| Journal: write path prototype on target hardware | WS-07 |
| Gateway: FIX logon/heartbeat/sequence management | WS-03 |
| Ref data: data model draft from rule-book | WS-06 |
| Infrastructure: cloud dev environment operational | WS-10 |
| Monitoring: Prometheus + basic metrics pipeline | WS-12 |
| Testing: simulation test framework scaffolded | WS-13 |

### Month 3: Core Integration

| Action | Owner |
|--------|-------|
| Engine: full matching with STP, price bands, state machine | WS-02 |
| Journal: snapshot + warm restart functional | WS-07 |
| Gateway: FIX order entry → engine → ack working end-to-end | WS-03 |
| Binary gateway: SBE encode/decode operational | WS-03 |
| Risk: fat-finger checks integrated with engine ingress | WS-04 |
| Milestone M6: single-instrument matching functional | Engine team |
| Milestone M11 target: end-to-end order lifecycle | Integration |
