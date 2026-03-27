# 29. Final Recommended Foundation Release vs Enterprise Target State

## 29.1 Foundation Release (FR-1) — Phase 1

### 29.1.1 Foundation Release Feature Matrix

| Feature | FR-1 Scope | Enterprise Scope | Deferred Risk |
|---------|-----------|-----------------|---------------|
| **Matching Engine** | Single asset class; price-time FIFO; basic order types (market, limit, IOC, FOK, GTC, day) | Multi-asset; configurable priority (pro-rata, parity); all order types | Limited market appeal without advanced order types |
| **Auctions** | Opening + closing auctions | + Volatility, intraday, periodic auctions | No circuit breaker protection in FR-1 |
| **Order Types** | Market, Limit, IOC, FOK, GTC, GTD, Day | + Stop, Stop-Limit, Iceberg, Pegged, Post-Only, Reduce-Only, Complex, Mass Quote | Market makers need mass quote; institutions need iceberg |
| **Self-Trade Prevention** | Cancel-newest mode | All modes (newest, oldest, both, decrement) | Limited STP flexibility |
| **Cancel-on-Disconnect** | Basic (cancel all on disconnect) | Configurable (grace period, scope, session-specific) | Market makers may resist without configurable CoD |
| **Connectivity — FIX** | FIX 4.4 (1 active-passive pair) | FIX 4.2/4.4/5.0SP2 (multiple pairs, high capacity) | Single FIX version limits participant compatibility |
| **Connectivity — Binary** | Binary gateway (1 pair); transport abstraction layer (ISocket/IServerSocket, TLS-ready) | Multiple pairs, DPDK optimized, full TLS/mTLS | Capacity limited; TLS not yet active |
| **Connectivity — REST/WS** | Not included | WebSocket + REST gateway | No web-based access for lower-tier participants |
| **Market Maker Gateway** | Not included (use FIX) | Dedicated mass quote gateway with CoD | Market makers use FIX for quotes in FR-1 |
| **Pre-Trade Risk** | Fat finger (price, qty, notional); message rate throttle; basic kill switch | + Credit limits, position limits, exposure limits, short-sell, per-participant config | No credit/position controls in FR-1 |
| **Market Data** | Multicast depth + trades + stats; TCP snapshot | + WAN feed, auction imbalance, conflation, vendor feeds | No WAN distribution; colocation only |
| **Drop Copy** | FIX drop copy | Enhanced with filtering, replay | Basic functionality |
| **Reference Data** | Instruments, participants, sessions, tick/lot sizes | + Corporate actions, fee tiers, holiday calendar, multi-currency | Manual reference data management |
| **Post-Trade** | Trade capture, basic fee calc, trade confirmation, EOD files | + CCP clearing, allocations, give-up, settlement instructions | No clearing integration; manual post-trade |
| **Surveillance** | Basic event export (order/trade lifecycle to file) | Full real-time stream; spoofing/layering/wash hooks; MM obligation monitoring | Limited surveillance capability |
| **Operations Console** | Session start/stop, halt/resume, basic parameter view | Full console with four-eyes, replay, reconciliation, runbooks | Manual operations; no four-eyes |
| **Audit Trail** | Journal-based (segmented journal with rotation, CRC32 checksums); basic query tool | Full audit service with search, export, integrity verification, hash chain | Limited audit query capability |
| **DR / Resilience** | Warm restart from snapshot + journal (StartupController implemented); cold start controller operational | + Hot standby, active-passive DR, automated failover | No DR site; extended downtime on hardware failure |
| **Monitoring** | Basic metrics + dashboards + alerting | Full observability stack with synthetic probes, SLA dashboards | Limited operational visibility |
| **Compliance** | Audit trail; basic clock sync; single jurisdiction | Multi-jurisdiction profiles; regulatory reporting hooks; compliance dashboard | Compliance limited to one jurisdiction |
| **Certification Env** | Basic cert environment | Full self-service certification portal with test case management | Manual certification process |
| **Performance** | Profile A: engine p99 < 5µs; Profile B: p99 < 20µs; 1M msg/s per partition | + DPDK gateway optimization, FPGA evaluation, sub-µs gateway | Software-only; targets are deployment-profile-dependent |

### 29.1.2 FR-1 Timeline and Cost Estimate

| Dimension | Estimate |
|-----------|---------|
| Duration | 9 months |
| Team size | 12–15 people (8 engineering + 2 QA + 1 product + 1 ops + 1 infra) |
| Hardware budget | $200K–$500K (servers, NICs, switches, PTP) |
| Software/licensing | $50K–$100K (FIX engine, monitoring tools, HSM) |
| Data center | $10K–$30K/month (colocation) |

### 29.1.3 FR-1 Revenue Capability
- Can operate as a single-asset venue with basic participant set
- Supports maker-taker fee model
- Generates market data for redistribution
- Sufficient for regulatory license application (with compliance roadmap)

## 29.2 Enterprise Target State — Phase 2+

### 29.2.1 Enterprise Architecture Summary

```
┌────────────────────────────────────────────────────────────────────────┐
│                    ENTERPRISE MATCHING PLATFORM                         │
│                                                                         │
│  ┌──────────────────────────────────────────────────────────────────┐  │
│  │  CONNECTIVITY TIER                                                │  │
│  │  FIX GW (HA pairs) │ Binary GW (HA, DPDK) │ MM Quote GW         │  │
│  │  WebSocket/REST GW  │ Drop Copy GW          │ Admin API GW       │  │
│  └──────────────────────────────────────────────────────────────────┘  │
│                                    │                                    │
│  ┌──────────────────────────────────────────────────────────────────┐  │
│  │  RISK & CONTROLS TIER                                             │  │
│  │  Pre-Trade Risk │ Credit Engine │ Position Limits │ Kill Switch   │  │
│  │  Short-Sell     │ Algo Controls │ Rate Limiter    │ Circuit Break │  │
│  └──────────────────────────────────────────────────────────────────┘  │
│                                    │                                    │
│  ┌──────────────────────────────────────────────────────────────────┐  │
│  │  MATCHING TIER                                                    │  │
│  │  Sequencer (HA) → Engine Partitions (N) → Journal (replicated)   │  │
│  │  Continuous LOB │ Auction │ RFQ │ Dark/Midpoint │ Complex Orders  │  │
│  │  Hot Standby (per partition) │ Snapshot Manager                   │  │
│  └──────────────────────────────────────────────────────────────────┘  │
│                                    │                                    │
│  ┌──────────────────────────────────────────────────────────────────┐  │
│  │  DISSEMINATION TIER                                               │  │
│  │  Market Data (multicast + WAN) │ Trade Tape │ Statistics          │  │
│  │  Auction Imbalance │ Snapshot Service │ Vendor Feed               │  │
│  └──────────────────────────────────────────────────────────────────┘  │
│                                    │                                    │
│  ┌──────────────────────────────────────────────────────────────────┐  │
│  │  POST-TRADE TIER                                                  │  │
│  │  Trade Capture │ Fee Engine │ Clearing Adapters (multi-CCP)       │  │
│  │  Allocations │ Give-Up │ Settlement │ Position Management         │  │
│  └──────────────────────────────────────────────────────────────────┘  │
│                                    │                                    │
│  ┌──────────────────────────────────────────────────────────────────┐  │
│  │  COMPLIANCE & SURVEILLANCE TIER                                   │  │
│  │  Event Export │ Surveillance Hooks │ Regulatory Reporting          │  │
│  │  Audit Service │ Compliance Dashboard │ Clock Sync Evidence       │  │
│  └──────────────────────────────────────────────────────────────────┘  │
│                                    │                                    │
│  ┌──────────────────────────────────────────────────────────────────┐  │
│  │  OPERATIONS TIER                                                  │  │
│  │  Ops Console │ Replay Tools │ Reconciliation │ Cert Portal        │  │
│  │  Participant Admin │ Fee Management │ Runbook Engine              │  │
│  └──────────────────────────────────────────────────────────────────┘  │
│                                    │                                    │
│  ┌──────────────────────────────────────────────────────────────────┐  │
│  │  INFRASTRUCTURE TIER                                              │  │
│  │  DR Site (active-passive) │ PTP Infrastructure │ HSM              │  │
│  │  Monitoring │ Alerting │ Log Aggregation │ CI/CD                  │  │
│  └──────────────────────────────────────────────────────────────────┘  │
└────────────────────────────────────────────────────────────────────────┘
```

## 29.3 Top 20 Design Decisions Before Implementation

| # | Decision | Options | Impact | Deadline |
|---|----------|---------|--------|----------|
| 1 | **Hot path programming language** | C++ / Rust / Java | Entire engine architecture; team hiring | Before Phase 1 start |
| 2 | **Target asset class for Phase 1** | Equities / Derivatives / Digital Assets / Commodities | Scope, market model, participant target | Before Phase 1 start |
| 3 | **Primary jurisdiction** | EU / US / APAC / Digital-asset | Compliance layer, clock sync, reporting | Before Phase 1 start |
| 4 | **Sequencing strategy** | Time-based / Round-robin / Hardware sequencer | Fairness model, hardware requirements | Before Phase 1 month 2 |
| 5 | **Journal implementation** | Custom / Aeron Archive / Chronicle Queue | Durability, performance, operational model | Before Phase 1 month 2 |
| 6 | **FIX engine library** | QuickFIX / Chronicle FIX / Custom | Gateway development velocity | Before Phase 1 month 1 |
| 7 | **Binary protocol format** | SBE / FlatBuffers / Custom | Wire efficiency, tooling, participant adoption | Before Phase 1 month 2 |
| 8 | **Partition strategy** | Per-instrument / Per-group / Dynamic | Scaling model, cross-partition complexity | Before Phase 1 month 3 |
| 9 | **Kernel bypass networking** | DPDK / io_uring / Standard sockets | Gateway latency, operational complexity | Before Phase 1 month 3 |
| 10 | **Monitoring stack** | Prometheus+Grafana / Datadog / Custom | Operational tooling, cost | Before Phase 1 month 2 |
| 11 | **Colocation site** | Equinix / other data center | Network topology, participant proximity | Before Phase 1 month 1 |
| 12 | **DR site location** | Same city / different city / different region | Replication latency, regulatory requirement | Before Phase 2 start |
| 13 | **Clearing partner (CCP)** | LCH / Eurex / CME / other | Clearing adapter specification | Before Phase 2 month 3 |
| 14 | **Surveillance approach** | Build / Buy (SMARTS, Actimize, Scila) | Development scope, cost, time | Before Phase 2 start |
| 15 | **Inter-component messaging** | Aeron / Disruptor / Shared memory / Custom | Performance, complexity | Before Phase 1 month 2 |
| 16 | **Operations console technology** | React / Angular + REST API | Development velocity | Before Phase 1 month 4 |
| 17 | **Container orchestration (non-hot-path)** | Kubernetes / Docker Compose / None | Operational model for platform services | Before Phase 2 start |
| 18 | **CI/CD platform** | GitHub Actions / GitLab CI / Jenkins | Development workflow | Before Phase 1 month 1 |
| 19 | **Secrets management** | HashiCorp Vault / AWS Secrets Manager / Azure KV | Security architecture | Before Phase 1 month 2 |
| 20 | **Database for reference/audit data** | PostgreSQL / TimescaleDB / ClickHouse | Query patterns, retention, performance | Before Phase 1 month 3 |
