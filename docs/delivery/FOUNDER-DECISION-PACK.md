# Founder Decision Pack — Minimum Choices Needed This Week

**Date**: 2026-03-24 | **Status**: Requires founder input | **Response needed by**: End of this week

---

## Minimum Founder Choices (1 Page)

You need to make **three choices** this week to keep the 9-month timeline intact. Everything else can follow.

### Choice 1: What do we trade first?

| Option | Why consider it | Risk |
|--------|----------------|------|
| **Cash equities** | Largest participant pool; best-understood model | Highly competitive; CCP needed for Phase 2 |
| **Carbon / environmental** | Low competition; growing demand; simpler scope | Smaller participant pool; market liquidity uncertain |
| **Digital assets** | Fastest-growing segment | Custody needed; regulatory uncertainty |
| **Listed derivatives** | High per-contract revenue | More complex (pro-rata, position limits) |

**Provisional default**: Cash equities (broadest market) OR carbon (lowest competition). Your call.

### Choice 2: Where do we get licensed?

| Option | Timeline | Clock sync | Notes |
|--------|----------|-----------|-------|
| **EU (MiFID II MTF)** | 12–18 months | 100 µs (PTP required) | Largest addressable market |
| **US (ATS)** | 6–12 months | 50 ms (NTP OK) | Fastest to revenue |
| **Singapore (MAS)** | 6–12 months | Per MAS | Pro-innovation; APAC hub |
| **UAE (VARA/ADGM)** | 3–6 months | Per framework | Fastest licensing; digital-asset-friendly |

**Provisional default**: Depends on Choice 1. Equities → EU or US. Digital → Singapore or UAE. Carbon → EU.

### Choice 3: What language do we build the engine in?

| Option | Latency | Hiring ease | Safety |
|--------|---------|-------------|--------|
| **C++** | Best | Good (exchange domain) | Manual memory |
| **Rust** | Best | Hard (small pool) | Compiler-enforced |
| **Java (tuned)** | Acceptable | Easy | Runtime-managed |

**Provisional default**: C++ — largest exchange-domain talent pool; proven at every major exchange.

---

### What these choices unlock

| Once you decide... | We can immediately... |
|--------------------|----------------------|
| Asset class + jurisdiction | Begin regulatory advisor engagement; draft market rules; recruit pilot participants |
| Language | Post engine engineer job ads; begin engine skeleton; select journal library |
| (All three) | Set the FR-1 kickoff date and begin the 9-month build clock |

### What happens if we wait

Delay on these choices is likely to pressure the current FR-1 planning target due to hardware lead times (6–8 weeks) and hiring timelines (4–8 weeks), both of which start from the decision date.

---

## Detailed Decision Briefs

### D-02: FR-1 Asset Class

**Statement**: Select the initial asset class for the Foundation Release.
**Why it matters**: Determines instrument model, market model, participant targets, and regulatory scope.
**Options**: Cash equities | Listed derivatives | Digital assets | Carbon/environmental
**Default recommendation (provisional)**: Cash equities — broadest ecosystem. Carbon is the lower-risk alternative.
**What can proceed before closure**: Pitch template drafting, general MM outreach, gateway engineering, security architecture
**What is blocked**: Market rule-book, instrument configuration, participant recruitment, compliance config
**Latest responsible date**: This week
**Consequence of delay**: Cannot scope FR-1; regulatory engagement cannot start

### D-03: Primary Jurisdiction

**Statement**: Select the primary jurisdiction for licensing and compliance.
**Why it matters**: Determines compliance profile, clock sync tolerance, reporting hooks, licensing timeline.
**Options**: EU (MiFID II) | US (Reg ATS) | Singapore (MAS) | UAE (VARA/ADGM)
**Default recommendation (provisional)**: Co-dependent with D-02. Equities → EU or US. Digital → SG or UAE.
**What can proceed before closure**: Security architecture, test framework, monitoring stack
**What is blocked**: Compliance configuration, regulatory advisor selection, clock sync requirements
**Latest responsible date**: This week (same session as D-02)
**Consequence of delay**: Cannot file license application; cannot configure compliance layer

### D-01: Hot-Path Language

**Statement**: Select C++, Rust, or Java for the matching engine hot path.
**Why it matters**: Determines engine development approach, hiring profile, and achievable performance.
**Options**: C++ (17/20) | Rust | Java (low-latency tuned)
**Default recommendation (provisional)**: C++. Largest exchange-domain talent pool; sub-µs latency without GC.
**What can proceed before closure**: All decision-independent work; interface specs; gateway design; security; QA
**What is blocked**: Engine skeleton; journal integration; engine engineer hiring (language-specific)
**Latest responsible date**: Within 1 week of D-02/D-03 closure
**Consequence of delay**: Delays engine development start and hiring pipeline, pressuring the FR-1 planning target

### D-05: Colocation Site

**Statement**: Select physical deployment site for matching engine infrastructure.
**Options**: Candidate examples — Equinix LD4 (London), NY5 (NJ), SG1 (Singapore), or cloud bare-metal initial
**Default recommendation (provisional)**: Site aligned with D-03 jurisdiction. Cloud bare-metal as parallel dev environment.
**What can proceed before closure**: Cloud dev environment; all software development
**What is blocked**: Hardware procurement; production networking; participant cross-connect
**Latest responsible date**: Month 1
**Consequence of delay**: Hardware delivery slips; production environment not ready for Month 4 milestone

### D-04: Sequencing Strategy

**Statement**: Hardware FPGA sequencer or software PTP-based sequencer.
**Default recommendation (provisional)**: Software PTP for FR-1. Hardware sequencer is a Phase 4 option.
**What can proceed before closure**: Engine core; gateways; most testing
**What is blocked**: Sequencer module design; fairness testing calibration
**Latest responsible date**: Month 2
**Consequence of delay**: Sequencer development blocked; fairness testing delayed

### D-06: Journal Implementation

**Statement**: Custom NVMe journal, Aeron Archive, or Chronicle Queue.
**Default recommendation (provisional)**: Custom (NVMe + Direct I/O) if C++/Rust; Aeron Archive if Java.
**What can proceed before closure**: Engine order book structures; gateway development
**What is blocked**: Journal module; snapshot/recovery; durability testing
**Latest responsible date**: Month 2
**Consequence of delay**: Recovery testing delayed; blocks warm-restart milestone

---

## Recommended Decision Order

```
Week 1:  D-02 (asset class) + D-03 (jurisdiction)  ← decide together
Week 1-2: D-01 (language)                           ← informed by D-02/D-03
Week 2-3: D-05 (colocation)                         ← informed by D-03
Month 2:  D-04 (sequencer) + D-06 (journal)         ← informed by D-01
```

---

## Short Version (Email/Message Ready)

> **Subject: Three decisions needed this week to start the matching engine build**
>
> We have an accepted architecture baseline and full implementation plan for the matching engine platform. To start the 9-month build clock, I need three decisions from you:
>
> 1. **What asset class for FR-1?** Equities (broadest market) or carbon (lowest competition) are the provisional defaults.
> 2. **What jurisdiction?** EU, US, Singapore, or UAE — depends on asset class choice.
> 3. **What language for the engine?** C++ is the provisional default (proven exchange technology; best latency).
>
> Everything else (sequencing strategy, colocation site, journal library) can follow within weeks. But these three choices must close this week or the go-live date slips.
>
> I've attached the full decision pack with options, pros/cons, and defaults for your review. Happy to walk through it in 30 minutes.
>
> The provisional timeline, budget, and staffing are planning assumptions — not commitments. They become firm after these decisions close and detailed planning completes.
