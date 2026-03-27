# 28. Final Recommended Component Inventory

## 28.1 Component Inventory Table

| Component | Mandatory / Optional | In Hot Path? | Asset-Class Dependency | Latency Critical? | Compliance Critical? | Phase | Notes |
|-----------|:-------------------:|:------------:|:---------------------:|:-----------------:|:-------------------:|:-----:|-------|
| Matching Engine Kernel | Mandatory | ✅ | None (generic) | ✅ | ✅ | 1 | Core — single-threaded per partition |
| Sequencer | Mandatory | ✅ | None | ✅ | ✅ | 1 | Assigns sequence + timestamp |
| Journal / WAL | Mandatory | ✅ | None | ✅ | ✅ | 1 | Append-only, NVMe |
| Snapshot Manager | Mandatory | ◐ | None | ✗ | ✅ | 1 | Periodic checkpoints |
| FIX Order Entry Gateway | Mandatory | ✗ | None | ◐ | ✅ | 1 | FIX 4.2/4.4/5.0SP2 |
| Binary Order Entry Gateway | Mandatory | ✗ | None | ✅ | ✅ | 1 | SBE protocol, DPDK |
| Market Maker Quote Gateway | Mandatory | ✗ | None | ✅ | ✅ | 2 | Mass quote, CoD |
| WebSocket/REST Gateway | Highly Recommended | ✗ | None | ✗ | ✗ | 2 | Lower-tier participants |
| Drop Copy Gateway | Mandatory | ✗ | None | ◐ | ✅ | 1 | FIX drop copy |
| Pre-Trade Risk Service | Mandatory | ✗ | None | ✅ | ✅ | 1 | Fat finger, credit, limits |
| Kill Switch Service | Mandatory | ✗ | None | ◐ | ✅ | 1 | Emergency controls |
| Reference Data Service | Mandatory | ✗ | All | ✗ | ✅ | 1 | Instruments, participants, sessions |
| Market Data Publisher | Mandatory | ✗ | None | ✅ | ✅ | 1 | Multicast depth + trades |
| Market Data Snapshot Service | Mandatory | ✗ | None | ◐ | ✗ | 1 | TCP snapshot recovery |
| Post-Trade Service | Mandatory | ✗ | None | ✗ | ✅ | 1 | Trade capture, fees |
| Clearing Adapter | Mandatory | ✗ | None | ✗ | ✅ | 2 | CCP integration |
| Settlement Adapter | Mandatory | ✗ | Asset-class specific | ✗ | ✅ | 2 | CSD/custodian |
| Surveillance Export | Mandatory | ✗ | None | ✗ | ✅ | 1 (basic), 2 (full) | Event stream to surveillance |
| Participant Administration | Mandatory | ✗ | None | ✗ | ✅ | 1 | Onboarding, entitlements |
| Operations Console | Mandatory | ✗ | None | ✗ | ✅ | 1 (basic), 2 (full) | Web-based ops UI |
| Replay Tooling | Mandatory | ✗ | None | ✗ | ✅ | 2 | Historical reconstruction |
| Reconciliation Tools | Mandatory | ✗ | None | ✗ | ✅ | 2 | Internal/external reconciliation |
| Audit Trail Service | Mandatory | ✗ | None | ✗ | ✅ | 1 | Query, export, integrity |
| Monitoring / Observability | Mandatory | ✗ | None | ✗ | ◐ | 1 | Metrics, logs, dashboards |
| Alerting Service | Mandatory | ✗ | None | ✗ | ◐ | 1 | Automated alerting |
| PTP Clock Infrastructure | Mandatory | ✅ | None | ✅ | ✅ | 1 | GPS grandmaster + PTP |
| Certification Environment | Mandatory | ✗ | None | ✗ | ✅ | 1 | Participant testing |
| RFQ Engine | Highly Recommended | ✗ | Bonds, OTF, FX | ◐ | ✅ | 3 | Request-for-quote workflow |
| Dark/Midpoint Book | Optional | ✅ | Equities | ✅ | ✅ | 3 | Requires regulatory waiver |
| Block Trade Workflow | Highly Recommended | ✗ | All | ✗ | ✅ | 2 | Negotiated trades |
| Complex Order Engine | Optional | ✅ | Derivatives | ✅ | ✗ | 3 | Multi-leg strategies |
| Implied Matching Engine | Optional | ✅ | Derivatives | ✅ | ✗ | 4 | Implied-in/out pricing |
| Corporate Actions Adapter | Highly Recommended | ✗ | Equities, Bonds | ✗ | ◐ | 2 | Reference data updates |
| Custody/Wallet Adapter | Optional | ✗ | Digital Assets | ✗ | ✅ | 5 | Blockchain settlement |
| Analytics Platform | Optional | ✗ | None | ✗ | ✗ | 5 | Historical analytics |
| API Gateway / Service Mesh | Optional | ✗ | None | ✗ | ✗ | 3 | Non-hot-path services |

Legend: ✅ = Yes, ◐ = Partially, ✗ = No

## 28.2 Requirement Priority Matrix

| Requirement Area | Priority | Reason | Suggested Phase |
|-----------------|----------|--------|----------------|
| Deterministic matching | P0 — Critical | Foundation of all auditability and regulatory proof | 1 |
| Journal and replay | P0 — Critical | Required for recovery, audit, and determinism verification | 1 |
| FIX connectivity | P0 — Critical | Industry standard; required for participant adoption | 1 |
| Pre-trade risk controls | P0 — Critical | Regulatory mandate in all jurisdictions | 1 |
| Market data dissemination | P0 — Critical | Participants need market data to trade | 1 |
| Audit trail | P0 — Critical | Regulatory requirement; foundation of compliance | 1 |
| Opening/closing auctions | P0 — Critical | Required for price discovery and official prices | 1 |
| Kill switch | P0 — Critical | Regulatory mandate; participant safety | 1 |
| Circuit breakers | P1 — High | Required for most regulated venues | 2 |
| Cancel-on-disconnect | P1 — High | Market maker safety; regulatory expectation | 2 |
| CCP clearing integration | P1 — High | Required for regulated exchange operation | 2 |
| Surveillance hooks | P1 — High | Regulatory requirement for market integrity | 1 (basic), 2 (full) |
| DR / business continuity | P1 — High | Regulatory requirement; operational necessity | 1 (warm restart), 2 (full DR) |
| Iceberg / reserve orders | P2 — Medium | Important for institutional participation | 2 |
| Pegged orders | P2 — Medium | Important for market makers and institutional flow | 2 |
| RFQ workflow | P2 — Medium | Required for fixed income and OTF operation | 3 |
| Dark / midpoint matching | P3 — Lower | Optional; requires regulatory approval | 3 |
| Complex/strategy orders | P3 — Lower | Required for derivatives; complex to implement | 3 |
| Implied matching | P3 — Lower | Derivatives optimization; very complex | 4 |
| 24/7 operations | P3 — Lower | Only for digital assets; major operational change | 5 |

## 28.3 Build vs. Buy Assessment

| Capability | Build | Buy | Hybrid | Recommendation | Reasoning |
|-----------|:-----:|:---:|:------:|:--------------:|-----------|
| Matching engine core | ✅ | ✗ | ✗ | **Build** | Core differentiator; no suitable off-the-shelf component |
| Sequencer | ✅ | ✗ | ✗ | **Build** | Tightly coupled to engine; performance critical |
| Journal / WAL | ✅ | ◐ | ✗ | **Build** | Custom requirements; could evaluate Aeron Archive |
| FIX gateway | ◐ | ✅ | ✅ | **Hybrid** | Use FIX engine library (QuickFIX/J, Chronicle FIX); build gateway logic |
| Binary gateway | ✅ | ✗ | ✗ | **Build** | Custom protocol; performance critical |
| Market data publisher | ✅ | ✗ | ✗ | **Build** | Custom protocol; tightly coupled to engine |
| Pre-trade risk | ✅ | ◐ | ✗ | **Build** | Needs to be on hot path; specific to engine |
| Reference data service | ✅ | ✗ | ✗ | **Build** | Specific to platform data model |
| Post-trade service | ✅ | ◐ | ✅ | **Hybrid** | Build core; evaluate vendors for clearing adapters |
| Surveillance system | ✗ | ✅ | ✅ | **Buy** | Specialized domain; mature vendors (Nasdaq SMARTS, NICE Actimize, Scila) |
| Operations console | ✅ | ✗ | ✗ | **Build** | Specific to platform; not available off-the-shelf |
| Monitoring / observability | ✗ | ✅ | ✅ | **Buy/OSS** | Use Prometheus, Grafana, ELK — mature, proven |
| PTP infrastructure | ✗ | ✅ | ✗ | **Buy** | Hardware: Meinberg, Spectracom; software: linuxptp |
| HSM / key management | ✗ | ✅ | ✗ | **Buy** | Thales, nCipher, or cloud HSM |
| CI/CD pipeline | ✗ | ✅ | ✅ | **Buy/OSS** | GitLab CI, GitHub Actions, Jenkins |
| Load testing tools | ✗ | ✅ | ✅ | **Hybrid** | Custom load generator for binary protocol; standard tools for REST |
| DDoS protection | ✗ | ✅ | ✗ | **Buy** | Akamai, Cloudflare, or hardware appliance |
