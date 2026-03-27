# Decision Workshop Pack — FR-1 Build-Blocking Decisions

**Version**: 1.0 | **Date**: 2026-03-24 | **Classification**: Internal — Workshop Material
**Status**: Planning assumptions — not approved commitments until stakeholder sign-off

---

## Workshop Agenda

| Time | Item | Owner | Duration |
|------|------|-------|----------|
| 09:00 | Opening — purpose, ground rules, decision criteria | Program Lead | 10 min |
| 09:10 | D-02 + D-03: FR-1 Asset Class + Jurisdiction (co-dependent) | Product + Compliance | 45 min |
| 09:55 | Break | | 10 min |
| 10:05 | D-01: Hot-Path Programming Language | CTO | 30 min |
| 10:35 | D-05: Colocation Site Approach | Infrastructure + CTO | 20 min |
| 10:55 | D-04: Sequencing Strategy | Architecture | 20 min |
| 11:15 | D-06: Journal Implementation | Engineering Lead | 20 min |
| 11:35 | Decision Summary and Next Steps | Program Lead | 15 min |
| 11:50 | Close | | 10 min |

**Total duration**: 3 hours

## Recommended Attendees

| Role | Required / Optional |
|------|:-------------------:|
| CTO | Required |
| Head of Product | Required |
| Head of Compliance / Legal | Required |
| Head of Engineering | Required |
| Infrastructure Lead | Required |
| Head of Business Development | Required |
| CFO or Finance representative | Optional |
| Head of Market Operations (designate) | Optional |
| Program Manager | Required (facilitator) |

---

## Decision D-01: Hot-Path Programming Language

### Decision Statement
Select the programming language for the matching engine hot path (sequencer, matching kernel, journal writer, output bus).

### Why It Matters
This decision determines the entire engine development approach, team hiring profile, achievable performance characteristics, and long-term maintainability. It is the single most consequential technical decision and cannot be deferred.

### Options

| Option | Description |
|--------|-------------|
| **A: C++ (17/20)** | Industry-standard for exchange engines; manual memory management; no GC |
| **B: Rust** | Modern systems language; compiler-enforced memory safety; no GC |
| **C: Java (low-latency tuned)** | Managed runtime with aggressive GC tuning (Azul C4, off-heap allocation) |

### Pros and Cons

| Factor | C++ | Rust | Java (tuned) |
|--------|-----|------|-------------|
| Latency | Sub-µs achievable | Sub-µs achievable | 2–5 µs with tuning |
| Memory safety | Manual (higher bug risk) | Compiler-enforced | Runtime-managed |
| Exchange domain talent | Large, proven pool | Small, growing | Large, general |
| Development velocity | Medium | Medium | High |
| Ecosystem maturity | Very mature (exchange-specific) | Growing | Very mature (general) |
| GC risk | None | None | Requires aggressive mitigation |

### Decision Criteria
1. Can we hire 3–4 senior engineers in this language within 60 days?
2. Does the language achieve the target deployment profile's latency requirements?
3. What is the long-term maintenance burden?
4. Is there a proven track record in exchange-grade hot-path systems?

### Dependencies
- Affects D-06 (journal library compatibility)
- Affects D-04 (sequencer integration approach)
- Affects hiring timeline and cost

### Recommended Owner: CTO
### Recommended Due Date: Phase 1, Week 2 (planning assumption)

### Pre-Read Questions
- What is the current team's language expertise?
- What is the realistic hiring timeline for each language?
- Is Profile A latency (< 5 µs p99) a hard requirement or a competitive aspiration?

### Workshop Discussion Questions
1. Do we have or can we hire a C++ team within 60 days?
2. If Rust, are we willing to accept a 3–6 month hiring ramp?
3. If Java, do we accept the latency ceiling and GC management complexity?
4. Should the control plane use a different language from the hot path?

### Provisional Default
**C++** — largest exchange-domain talent pool; proven at every major exchange; achieves Profile A latency. This is a provisional recommendation; the CTO has final authority.

---

## Decision D-02: FR-1 Asset-Class Scope

### Decision Statement
Select the initial asset class(es) for the Foundation Release (FR-1).

### Why It Matters
The asset class determines the instrument model, market model configuration, participant target set, regulatory requirements, and commercial viability of the first production release. D-02 and D-03 (jurisdiction) are co-dependent and should be decided together.

### Options

| Option | Description |
|--------|-------------|
| **A: Cash equities** | Continuous LOB + auctions; largest participant ecosystem |
| **B: Listed derivatives (futures)** | High per-contract revenue; may need pro-rata allocation |
| **C: Digital assets** | Fastest-growing segment; fewer regulatory barriers in some jurisdictions |
| **D: Carbon / environmental** | Underserved market; growing demand; simpler initial scope |

### Pros and Cons

| Factor | Equities | Derivatives | Digital Assets | Carbon |
|--------|----------|-------------|---------------|--------|
| Market size | Very large | Large | Large (growing) | Small (growing) |
| Competition | Very high | High | Medium | Low |
| Regulatory complexity | High | High | Medium (varies) | Medium |
| FR-1 implementation complexity | Medium | High (pro-rata, position limits) | Medium (24/7, custody) | Low |
| Revenue per trade | Low | High | Medium | Medium |
| Participant availability | Many | Moderate | Growing | Limited |

### Decision Criteria
1. Which asset class aligns with our target jurisdiction (D-03)?
2. Where can we secure committed launch participants (especially market makers)?
3. Which minimizes FR-1 implementation complexity while generating revenue?
4. Which has the clearest regulatory licensing path?

### Dependencies
- Co-dependent with D-03 (jurisdiction)
- Informs WS-01 (market rules), WS-06 (reference data), WS-08 (post-trade)

### Recommended Owner: Product / Business with CTO input
### Recommended Due Date: Phase 1, Week 1 (planning assumption)

### Pre-Read Questions
- Do we have committed market-maker interest for any specific asset class?
- What is the regulatory licensing timeline for each asset class in candidate jurisdictions?
- What is the minimum viable instrument universe for a credible launch?

### Workshop Discussion Questions
1. Which asset class gives us the fastest path to revenue?
2. Can we secure at least one market maker and three brokers for this asset class?
3. Does the chosen asset class require features beyond FR-1 scope (e.g., pro-rata, position limits, custody)?

### Provisional Default
**Cash equities** — best-understood model; largest participant pool; most reference implementations. If the competitive landscape is prohibitive, **carbon/environmental** is an attractive alternative with lower competition and simpler scope. Provisional recommendation pending business input.

---

## Decision D-03: Primary Target Jurisdiction

### Decision Statement
Select the primary jurisdiction for FR-1 regulatory licensing and compliance configuration.

### Why It Matters
Determines the compliance configuration profile, clock synchronization tolerance, regulatory reporting requirements, and licensing timeline. Must be decided alongside D-02 (asset class).

### Options

| Option | Regulatory Framework | Clock Sync | Licensing Timeline (est.) |
|--------|---------------------|-----------|--------------------------|
| **A: EU (MiFID II MTF)** | MiFID II / MiFIR | 100 µs (PTP required) | 12–18 months |
| **B: US (ATS)** | Reg ATS / FINRA | 50 ms (NTP acceptable) | 6–12 months |
| **C: Singapore (MAS RMO)** | SFA / MAS guidelines | Per MAS guidance | 6–12 months |
| **D: UAE (VARA / ADGM)** | Digital-asset-specific | Per framework | 3–6 months |

### Decision Criteria
1. Which jurisdiction aligns with D-02 (asset class)?
2. What is the licensing timeline vs. our target go-live?
3. Where are our target participants located?
4. What is the regulatory relationship cost and complexity?

### Dependencies: Co-dependent with D-02; informs WS-09, WS-10
### Recommended Owner: Compliance / Legal with Business input
### Recommended Due Date: Phase 1, Week 1 (planning assumption)

### Workshop Discussion Questions
1. Can we file a license application concurrently with build, or must we wait?
2. Do we have regulatory advisory relationships in the candidate jurisdictions?
3. Is a "lighter" jurisdiction (UAE/SG) acceptable as a first market with EU/US as Phase 2?

### Provisional Default
Depends on D-02. If equities → **EU (MTF)** or **US (ATS)**. If digital assets → **Singapore** or **UAE**. If carbon → **EU** (ETS alignment). Provisional only.

---

## Decision D-04: Sequencing Strategy

### Decision Statement
Select hardware-based or software-based sequencing for multi-gateway message ordering.

### Options

| Option | Fairness Delta | Cost | Complexity |
|--------|:-------------:|:----:|:----------:|
| **A: Software time-based (PTP)** | < 5 µs | Low ($0) | Low |
| **B: Hardware sequencer (FPGA)** | < 1 µs | High ($200K+) | High |

### Decision Criteria
1. Does our target venue compete on latency with tier-1 exchanges?
2. Do we have FPGA engineering capability?
3. Is the regulator likely to scrutinize gateway fairness at sub-µs level?

### Dependencies: D-01 (integration), D-05 (facility FPGA support)
### Recommended Owner: CTO / Architecture
### Recommended Due Date: Phase 1, Month 2

### Provisional Default
**Software time-based (PTP)** for FR-1. Evaluate hardware sequencer for Phase 4 if competitive pressure demands it. Most regulated venues operate successfully with software sequencing.

---

## Decision D-05: Colocation Site Approach

### Decision Statement
Select the physical deployment site for the matching engine and gateway infrastructure.

### Options (examples — not pre-approved)

| Option | Geography | Proximity | Notes |
|--------|-----------|-----------|-------|
| **Equinix LD4/LD5** | London | EU/UK markets | Example candidate for EU jurisdiction |
| **Equinix NY5** | New Jersey | US markets | Example candidate for US jurisdiction |
| **Equinix SG1** | Singapore | APAC markets | Example candidate for SG jurisdiction |
| **Cloud bare-metal (initial)** | Variable | Variable | Fastest setup; acceptable for Profile B/C |

### Decision Criteria
1. Aligned with D-03 (jurisdiction)?
2. Are target participants already present at this facility?
3. Cross-connect and multicast support available?
4. DR pairing options from this site?
5. Contract lead time and cost?

### Dependencies: D-03 (jurisdiction determines geography)
### Recommended Owner: Infrastructure / CTO
### Recommended Due Date: Phase 1, Month 1

### Provisional Default
Facility aligned with jurisdiction decision. Cloud bare-metal acceptable as a parallel development environment while procurement proceeds.

---

## Decision D-06: Journal Implementation

### Decision Statement
Select the journal (write-ahead log) implementation for the matching engine.

### Options

| Option | Language Compat. | Replication | Maturity |
|--------|:----------------:|:-----------:|:--------:|
| **A: Custom (NVMe + Direct I/O)** | Any | Build | N/A |
| **B: Aeron Archive** | Java / C++ (via Aeron) | Built-in | High |
| **C: Chronicle Queue** | Java | Built-in | High |

### Decision Criteria
1. Compatible with D-01 (hot-path language)?
2. Does it support Regulatory-Grade synchronous durability?
3. What is the replication model for DR?
4. Development effort vs. off-the-shelf benefit?

### Dependencies: D-01 (language compatibility is primary constraint)
### Recommended Owner: Engineering Lead
### Recommended Due Date: Phase 1, Month 2

### Provisional Default
If C++/Rust → **Custom (NVMe + Direct I/O)** (Aeron/Chronicle are Java-native). If Java → **Aeron Archive** (proven in financial systems). Provisional only.

---

## Decision Log Template

| Decision ID | Decision | Options Considered | Decision Made | Rationale | Decided By | Date | Status |
|-------------|----------|-------------------|---------------|-----------|------------|------|--------|
| D-01 | Hot-path language | C++ / Rust / Java | | | | | Open |
| D-02 | FR-1 asset class | Equities / Derivatives / Digital / Carbon | | | | | Open |
| D-03 | Jurisdiction | EU / US / SG / UAE | | | | | Open |
| D-04 | Sequencing strategy | Software PTP / Hardware FPGA | | | | | Open |
| D-05 | Colocation site | (per jurisdiction) | | | | | Open |
| D-06 | Journal implementation | Custom / Aeron / Chronicle | | | | | Open |

---

## Decision Summary Table

| # | Decision | Owner | Target Date | Blocks FR-1? | Escalation |
|---|----------|-------|-------------|:------------:|------------|
| D-01 | Hot-path language | CTO | Week 2 | **Yes** | Board/CTO |
| D-02 | FR-1 asset class | Product + CTO | Week 1 | **Yes** | CEO/Board |
| D-03 | Jurisdiction | Compliance + Legal | Week 1 | **Yes** | CEO/Board |
| D-04 | Sequencing strategy | Architecture | Month 2 | **Yes** | CTO |
| D-05 | Colocation site | Infrastructure | Month 1 | **Yes** | CTO |
| D-06 | Journal implementation | Eng Lead | Month 2 | **Yes** | CTO |

*All dates are planning assumptions. Actual dates confirmed after workshop.*
