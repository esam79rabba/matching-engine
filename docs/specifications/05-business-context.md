# 5. Business Context and Problem Statement

## 5.1 Industry Context

The global electronic trading venue landscape is characterized by:

### 5.1.1 Technology Obsolescence
Most incumbent exchanges operate matching engines built 10–20 years ago. These platforms exhibit:
- Monolithic architectures resistant to change
- Language and runtime environments with declining talent pools (e.g., legacy C, proprietary frameworks)
- Tight coupling between matching logic and infrastructure
- Limited configurability requiring code changes for new market models
- High vendor license fees with limited customization freedom
- Difficulty meeting modern resilience and observability standards

### 5.1.2 Market Fragmentation
Regulatory changes (MiFID II in EU, Reg NMS in US, competitive licensing in APAC) have created a proliferation of trading venues:
- 50+ equity venues in Europe alone
- 40+ ATSs registered in the US
- Multiple competing exchanges in most APAC markets
- Growing number of digital-asset venues seeking institutional credibility

This fragmentation creates demand for cost-effective, rapidly deployable venue technology.

### 5.1.3 Multi-Asset Convergence
Market participants increasingly demand:
- Unified access across asset classes through common connectivity
- Cross-asset risk management and margining
- Portfolio-level execution and reporting
- Single technology platform reducing operational cost and complexity

### 5.1.4 Digital-Asset Institutionalization
The tokenization of traditional financial instruments and the maturation of digital-asset markets requires:
- Institutional-grade matching and risk infrastructure
- Regulatory compliance (EU MiCA, jurisdictional licensing)
- Interoperability with traditional settlement and custody
- 24/7 operational capability

### 5.1.5 Regulatory Expectations
Post-2010 regulatory frameworks globally require venues to demonstrate:
- System resilience and business continuity (MiFID II RTS 7, SEC Reg SCI)
- Market surveillance and abuse prevention capabilities
- Complete audit trails with synchronized clocks
- Pre-trade risk controls and circuit breakers
- Regular stress testing and DR drills
- Algorithmic trading supervision

## 5.2 Problem Statement

**There is no readily available, configurable, multi-asset matching engine platform that simultaneously satisfies all of the following requirements:**

1. **Exchange-grade performance**: Low-latency matching competitive with tier-1 venues (deployment-profile-dependent; see §15), ≥ 1M msg/s throughput per partition, deterministic execution
2. **Multi-asset configurability**: Supports equities, derivatives, FX, fixed income, commodities, and digital assets through a unified instrument model with configurable market rules. New instruments using existing types are configuration-driven; materially new matching logic requires a controlled software release.
3. **Regulatory readiness**: Built-in compliance framework adaptable to multiple jurisdictions without re-architecture
4. **Modern architecture**: Event-sourced, deterministically replayable, cleanly separated hot path and control plane
5. **Operational maturity**: Comprehensive monitoring, automated operations, lean team operation
6. **Commercial flexibility**: No vendor lock-in, flexible deployment models, controllable cost structure

Existing options and their limitations:

| Option | Limitation |
|--------|-----------|
| **Major vendor platforms** (Nasdaq, LSEG, etc.) | High license cost, limited customization, vendor dependency, slow evolution |
| **Open-source engines** (exchange-core, liquibook) | Incomplete for production use; lack surrounding platform, compliance, operations tooling |
| **In-house legacy systems** | Technical debt, limited talent, high maintenance cost, difficult to extend |
| **Crypto exchange platforms** | Not designed for regulated markets; lack compliance framework, determinism guarantees |
| **Cloud-native startups** | Often sacrifice latency for deployment convenience; unproven at exchange scale |

## 5.3 Opportunity

Building a modern matching engine platform addresses:

| Opportunity | Description |
|-------------|-------------|
| **New venue launches** | Emerging markets, new asset classes (carbon, digital), and new venue types create demand |
| **Technology refresh** | Incumbent exchanges seeking to replace legacy platforms represent large contracts |
| **Cost reduction** | Multi-asset convergence is expected to materially reduce per-market infrastructure cost compared to separate single-asset platforms |
| **Speed to market** | Configurable platform enables new products in weeks vs. months |
| **Regulatory arbitrage** | Compliance-by-configuration enables rapid entry into new jurisdictions |
| **Revenue diversification** | Platform can be deployed as licensed software, managed service, or operated venue |

## 5.4 Value Proposition

**For exchange operators**: A production-ready, regulatorily compliant matching engine platform designed to reduce time-to-market from years to months, offer competitive cost compared to tier-1 vendor licensing, and eliminate vendor dependency.

**For regulators**: A platform with built-in compliance controls, complete audit trails, deterministic replay, and configurable policy enforcement — simplifying technical examination and ongoing supervision.

**For market participants**: A fast, fair, reliable, and well-documented trading venue with modern connectivity options, comprehensive order types, and transparent market microstructure.

## 5.5 Key Business Constraints

| Constraint | Impact | Mitigation |
|-----------|--------|-----------|
| Regulatory approval timelines | 6–18 months for exchange licensing | Begin regulatory engagement early; design for compliance from day one |
| Participant onboarding inertia | Brokers slow to connect to new venues | Offer FIX compatibility; provide sandbox and certification support; offer incentive programs |
| Liquidity chicken-and-egg | No participants without liquidity; no liquidity without participants | Market maker incentive programs; strategic launch partners; targeted asset classes with unmet demand |
| Talent scarcity | Low-latency exchange engineering talent is rare | Design for maintainability; comprehensive documentation; use well-known languages (C++/Rust/Java) |
| Capital requirements | Significant investment before revenue | Phased delivery with early commercial milestones; Foundation Release scoped to generate early revenue |
