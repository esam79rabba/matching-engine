# 27. Risks, Tradeoffs, and Open Questions

## 27.1 Risk Register

| Risk ID | Risk | Probability | Impact | Mitigation | Owner |
|---------|------|:-----------:|:------:|-----------|-------|
| R-001 | **Regulatory license delayed or denied** | Medium | Critical | Early regulatory engagement; design for compliance from day one; hire experienced regulatory advisor | Compliance |
| R-002 | **Performance targets not achievable** | Low | High | Dedicated performance engineering; early profiling; hardware selection validated before build | Engineering |
| R-003 | **Insufficient participant adoption** | Medium | Critical | Market maker incentive programs; competitive fee structure; minimal certification burden; strategic launch partners | Business Dev |
| R-004 | **Key talent departure** | Medium | High | Documentation-driven development; no single points of knowledge; competitive compensation | CTO |
| R-005 | **Hardware supply chain delays** | Medium | Medium | Order early; maintain vendor alternatives; cloud fallback for non-critical components | Infrastructure |
| R-006 | **Scope creep in Phase 1** | High | Medium | Strict scope discipline; product owner empowered to say no; phase gates | Product |
| R-007 | **CCP integration complexity** | Medium | High | Engage CCP early; dedicated integration team; plan for CCP-specific quirks | Engineering |
| R-008 | **Security breach** | Low | Critical | Security by design; penetration testing; bug bounty; incident response plan | Security |
| R-009 | **Data loss due to journal failure** | Very Low | Critical | RAID-1 journals; synchronous DR replication; checksum verification; regular backup validation | SRE |
| R-010 | **Clock synchronization failure** | Low | High | Redundant PTP grandmasters; GPS + atomic backup; continuous monitoring; alerting | SRE |
| R-011 | **Determinism bug in matching engine** | Low | Critical | Comprehensive simulation tests; replay verification on every release; formal verification consideration | Engineering |
| R-012 | **Vendor lock-in for critical components** | Medium | Medium | Open standards preference; abstraction layers; multi-vendor evaluation for hardware | Architecture |
| R-013 | **Operational error (wrong parameter)** | Medium | High | Four-eyes approval; parameter validation; UAT testing before production; rollback capability | Operations |
| R-014 | **DDoS attack on gateways** | Medium | High | DDoS mitigation appliance; rate limiting; IP whitelisting; gateway isolation | Security |
| R-015 | **Legacy system migration complexity** | Medium (if migrating) | High | Parallel running period; comprehensive data validation; rollback plan | Engineering |

## 27.2 Key Design Tradeoffs

### 27.2.1 Latency vs. Durability

| Choice | Approach | Trade-off |
|--------|----------|-----------|
| **Option A: Sync journal before ack** | Write journal entry + fsync before sending execution report | Adds ~200ns–1µs latency; guarantees no data loss |
| **Option B: Async journal** | Ack immediately; journal writes asynchronously | Lowest latency; risk of losing last few events on crash |
| **Recommendation** | **Option A** — Regulatory and audit requirements demand zero data loss. The latency cost is acceptable. |

### 27.2.2 Single-Threaded vs. Multi-Threaded Matching

| Choice | Approach | Trade-off |
|--------|----------|-----------|
| **Option A: Single-threaded per partition** | One thread processes all orders for assigned instruments | Simple, deterministic, no locks; limited by single-core throughput |
| **Option B: Multi-threaded with deterministic scheduling** | Multiple threads with provable deterministic ordering | Higher throughput per partition; much higher complexity; harder to verify determinism |
| **Recommendation** | **Option A** — Simplicity and provable determinism outweigh theoretical throughput gains. Scale horizontally via more partitions. |

### 27.2.3 C++ vs. Rust vs. Java for Hot Path

| Factor | C++ | Rust | Java (tuned) |
|--------|-----|------|-------------|
| Latency | < 1 µs | < 1 µs | 1–5 µs |
| Determinism | Excellent (manual memory) | Excellent (ownership model) | Good (requires GC avoidance) |
| Safety | Low (manual memory) | High (compiler-enforced) | High (managed runtime) |
| Talent pool (exchange domain) | Large | Small (growing) | Large |
| Development velocity | Medium | Medium | High |
| **Recommendation** | **Primary choice** | **Strong alternative** | **Acceptable with significant tuning effort** |

### 27.2.4 FPGA vs. Software Gateway

| Choice | Approach | Trade-off |
|--------|----------|-----------|
| **FPGA gateway** | Sub-microsecond processing; deterministic; very low jitter | High cost ($500K+); 6–12 month development; specialized talent; difficult to modify |
| **Software (DPDK)** | 2–5 µs processing; flexible; standard talent pool | Higher latency than FPGA; more jitter; but adequate for most markets |
| **Recommendation** | **Software (DPDK) for Phase 1–3**; evaluate FPGA for Phase 4+ only if competitive pressure demands it. |

### 27.2.5 Bare Metal vs. Cloud

| Component | Bare Metal | Cloud | Recommendation |
|-----------|-----------|-------|---------------|
| Matching engine | Required (latency) | Not suitable | **Bare metal** |
| Gateways (binary) | Required (latency) | Not suitable | **Bare metal** |
| Gateways (FIX) | Preferred | Acceptable | **Bare metal** |
| Market data (colo) | Required (latency) | Not suitable | **Bare metal** |
| Risk service | Preferred | Acceptable | **Bare metal** (colocated with engine) |
| Operations / admin | Acceptable | Preferred (flexibility) | **Cloud/containers** |
| Monitoring | Acceptable | Preferred | **Cloud/containers** |
| Cert environment | Acceptable | Preferred (cost) | **Cloud** |
| CI/CD | Acceptable | Preferred | **Cloud** |

## 27.3 Open Questions for Stakeholder Workshops

| # | Question | Why It Matters | Owner | Impact if Unanswered |
|---|---------|---------------|-------|---------------------|
| 1 | Which asset class for Phase 1 launch? | Determines instrument model, market model, and participant target | Product / Business | Cannot scope Phase 1 |
| 2 | Primary target jurisdiction(s)? | Determines compliance configuration, reporting requirements, clock sync tolerance | Compliance / Legal | Cannot design compliance layer |
| 3 | CCP partner for clearing? | Determines clearing adapter requirements, timeline, and technical interface | Business Dev | Blocks Phase 2 clearing integration |
| 4 | Co-location site selection? | Determines network topology, hardware procurement, participant proximity | Infrastructure / Business | Blocks infrastructure setup |
| 5 | Programming language for hot path? | Affects team hiring, development velocity, performance characteristics | CTO / Engineering | Blocks implementation start |
| 6 | Build vs. buy for surveillance? | Determines build scope and vendor evaluation timeline | Compliance / CTO | Blocks surveillance architecture |
| 7 | 24/7 trading requirement? | Fundamentally changes operational model, staffing, and architecture | Product / Business | Cannot design session model |
| 8 | Market maker partnership secured? | Without market makers, venue has no liquidity at launch | Business Dev | Launch viability at risk |
| 9 | Fee model (maker-taker, flat, tiered)? | Affects participant incentives, system design, revenue model | Product / Finance | Cannot configure fee engine |
| 10 | Dark pool / midpoint in scope? | Requires additional market model, regulatory waiver application | Product / Compliance | Scope ambiguity |
| 11 | FIX version(s) to support? | Affects gateway development and participant onboarding | Product / Connectivity | Gateway specification incomplete |
| 12 | DR site location? | Affects replication latency, regulatory requirements | Infrastructure | DR architecture incomplete |
| 13 | Budget and funding timeline? | Constrains team size, hardware, and delivery timeline | Finance / Board | All planning at risk |
| 14 | Target go-live date? | Drives phase planning, team scaling, and vendor engagement | Business / Board | Cannot plan delivery |
| 15 | Legacy system migration required? | Adds significant complexity and risk to rollout | CTO / Operations | Scope underestimate |
| 16 | Digital asset support in initial phases? | Requires custody integration, 24/7 ops, wider price bands | Product / Legal | Scope ambiguity |
| 17 | Multi-tenant or single-tenant deployment? | Fundamentally different isolation and scaling model | Architecture | Cannot finalize architecture |
| 18 | Licensing/white-label business model? | Affects IP strategy, deployment model, support model | Business | Product strategy unclear |
| 19 | Position limit source of truth? | Engine-internal vs. external risk system | Risk / Architecture | Risk architecture incomplete |
| 20 | Implied matching for derivatives? | Complex feature; significant engine work if required in early phases | Product / Architecture | Derivatives scope unclear |
