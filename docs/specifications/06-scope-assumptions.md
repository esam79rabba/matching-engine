# 6. Scope, Out of Scope, Assumptions, Constraints

## 6.1 In Scope

### 6.1.1 Core Matching Engine
- Deterministic, event-sourced order matching kernel
- Configurable order book model (price-time, pro-rata, hybrid)
- Full order type support (market, limit, stop, iceberg, pegged, IOC, FOK, GTC, GTD, etc.)
- Auction mechanisms (opening, closing, volatility interruption, intraday, periodic)
- Self-trade prevention
- Cancel-on-disconnect
- Instrument state machine (pre-open, auction, continuous, halt, suspend, close)
- Journal / write-ahead log for replay and recovery
- State checkpointing and warm restart
- Price band and circuit breaker enforcement
- Tick size and lot size regimes
- Fill allocation logic (partial fills, pro-rata distribution)
- Trade generation and execution reporting
- Deterministic replay from journal

### 6.1.2 Exchange Platform Services
- FIX 4.2/4.4/5.0SP2 order entry gateway
- Binary low-latency order entry gateway
- Market maker quote gateway (mass quote support)
- WebSocket / REST gateway for lower-tier participants
- Drop copy gateway
- Market data dissemination (depth, top-of-book, trades, auctions)
- Pre-trade risk management service
- Reference data and instrument management service
- Participant administration and onboarding service
- Post-trade capture and clearing adapter framework
- Operations console and market control tooling
- Surveillance hooks and data export
- Observability stack (logging, metrics, tracing)

### 6.1.3 Compliance Framework
- Configurable compliance policy engine
- Audit trail with nanosecond precision
- Clock synchronization infrastructure
- Regulatory reporting hooks (not jurisdiction-specific report generation)
- Surveillance data model and export interfaces
- Market abuse detection hooks (spoofing, layering, wash trade patterns)

### 6.1.4 Infrastructure and Operations
- Deployment architecture for colocation and cloud environments
- DR and business continuity design
- Performance engineering and capacity planning
- Monitoring, alerting, and incident management framework
- Security architecture (IAM, encryption, network segmentation)
- CI/CD pipeline design
- Certification and release management framework

### 6.1.5 Documentation and Specification
- This specification package (all 30 sections)
- API specifications for all interfaces
- Operations runbooks (structure and content requirements)
- Certification test case framework for participants

## 6.2 Out of Scope

| Item | Rationale | Future Consideration |
|------|-----------|---------------------|
| **Retail trading application / mobile app** | The platform is infrastructure, not a consumer product | Expose APIs for third-party retail apps |
| **Clearing house / CCP functionality** | Separate regulated entity; platform integrates with CCPs | Clearing adapter is in scope |
| **Central securities depository (CSD)** | Separate regulated entity | Settlement adapter is in scope |
| **Market surveillance application** | Specialized vendor domain (e.g., Nasdaq SMARTS, NICE Actimize) | Surveillance hooks and data export are in scope |
| **Jurisdiction-specific regulatory reports** | Report formats are jurisdiction-specific and change frequently | Report generation hooks and data extracts are in scope |
| **Smart contract execution engine** | Non-deterministic arbitrary code is excluded from hot path | Tokenized instrument support via configuration is in scope |
| **Wallet / custody management** | Specialized domain; integrate with external custodians | Custody adapter framework is in scope |
| **Market data redistribution licensing** | Business/legal activity, not technology | Market data feed infrastructure is in scope |
| **Payment / fiat on-ramp** | Banking/payment system integration | Reference data for currency support is in scope |
| **Index calculation engine** | Typically separate system or vendor | Real-time trade data feed for index inputs is in scope |
| **News dissemination** | Separate content system | Instrument halt/resume events triggered by news are in scope |

## 6.3 Assumptions

### Technical Assumptions

| ID | Assumption | Impact if Wrong |
|----|-----------|----------------|
| A-T01 | Bare-metal or dedicated hardware available for matching engine hot path | Cloud deployment may increase latency by 10–100x |
| A-T02 | Linux operating system (kernel 5.15+ or 6.x) for production deployment | Windows/other OS would require significant porting |
| A-T03 | PTP (IEEE 1588) or GPS-disciplined clocks available for nanosecond timestamping | Degraded timestamp precision; may not meet regulatory requirements |
| A-T04 | NIC hardware timestamping supported (e.g., Mellanox/NVIDIA, Solarflare/Xilinx) | Software timestamps add microseconds of jitter |
| A-T05 | Multicast networking available for market data dissemination in colocation | Unicast fallback increases bandwidth and latency |
| A-T06 | NUMA-aware hardware (dual-socket or single-socket with multiple NUMA domains) | CPU pinning strategy may need adjustment |
| A-T07 | Sufficient CPU cores for dedicated pinning (≥ 16 cores for engine, ≥ 32 total) | Resource contention on shared cores increases jitter |
| A-T08 | SSD/NVMe storage available for journal and snapshots | Spinning disk limits journal write throughput |
| A-T09 | 10GbE or faster networking between all platform components | Slower networks create bottlenecks in market data and gateway communication |

### Business Assumptions

| ID | Assumption | Impact if Wrong |
|----|-----------|----------------|
| A-B01 | Regulatory license application will be filed concurrently with development | Regulatory feedback may require architecture changes late in development |
| A-B02 | At least one market maker committed before launch | Low liquidity at launch risks venue credibility |
| A-B03 | FIX protocol compatibility is essential for participant adoption | Non-FIX-only venues face higher adoption barriers |
| A-B04 | Initial launch targets a single asset class, expanding later | Multi-asset from day one would increase Phase 1 scope significantly |
| A-B05 | Clearing integration with at least one CCP is required for Phase 2 | Self-clearing model would require additional functionality |
| A-B06 | 24/7 trading is NOT required for Phase 1 (session-based model) | 24/7 operations significantly increases Phase 1 complexity |

### Organizational Assumptions

| ID | Assumption | Impact if Wrong |
|----|-----------|----------------|
| A-O01 | Engineering team of ≥ 8 senior engineers available for Phase 1 | Smaller team extends timeline or reduces scope |
| A-O02 | Access to exchange domain expertise (product, operations, compliance) | Engineering without domain expertise produces technically correct but operationally unusable systems |
| A-O03 | Dedicated QA/performance engineering resource available | Testing quality and performance validation suffer |
| A-O04 | SRE / operations team involved from Phase 1 design | Operability designed in, not bolted on |

## 6.4 Constraints

### Regulatory Constraints
- RC-01: Platform must pass regulatory technical examination before receiving trading license
- RC-02: All order and trade events must be journaled with timestamps traceable to UTC within regulatory tolerance (100µs for MiFID II, 50ms for SEC)
- RC-03: Audit trail must be immutable and retained for regulatory minimum period (5–7 years typical)
- RC-04: Circuit breakers and volatility interruption mechanisms must be operational before trading begins
- RC-05: Pre-trade risk controls must be active and configurable per participant

### Technical Constraints
- TC-01: Matching engine hot path must be single-threaded per partition to guarantee determinism. Each partition processes one or more order books on a single dedicated thread.
- TC-02: No external I/O (network calls, disk reads) in the matching hot path except journal writes
- TC-03: No garbage collection pauses in the matching hot path (relevant for JVM-based implementations)
- TC-04: Clock resolution must be nanosecond or better for internal timestamps
- TC-05: In the default Regulatory-Grade durability mode, the journal write must be durable (fsync or equivalent) before the matching engine processes the message and before any acknowledgment is sent. An optional Reduced-Latency mode with batched writes is available for venue types where the operator accepts the associated data-loss window and where the regulatory framework permits it (see §11.3, §27.2.1).

### Commercial Constraints
- CC-01: Phase 1 (Foundation Release) must be deliverable within 9 months to meet business timeline
- CC-02: Total platform cost must be competitive with vendor licensing (target: < 50% of tier-1 vendor annual cost)
- CC-03: Ongoing operational team size must be sustainable (target: < 15 FTE for core exchange operations)

## 6.5 Dependencies

| Dependency | Type | Owner | Risk Level |
|-----------|------|-------|-----------|
| Regulatory license approval | External | Compliance / Legal | High |
| CCP clearing agreement | External | Business Development | High |
| Hardware procurement (bare-metal servers, NICs) | External | Infrastructure | Medium |
| PTP/GPS clock infrastructure | External | Infrastructure | Medium |
| Co-location facility agreement | External | Infrastructure | Medium |
| Market maker partnership agreement | External | Business Development | High |
| FIX protocol certification with participants | External | Connectivity team | Medium |
| Market data vendor agreements | External | Business Development | Low |
| Surveillance vendor selection and integration | External / Build | Compliance / Engineering | Medium |
