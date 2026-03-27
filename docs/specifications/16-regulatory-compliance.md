# 16. Regulatory and Compliance Requirement Framework

## 16.1 Compliance-by-Configuration Framework

The platform implements regulatory requirements through a **configurable compliance layer** rather than jurisdiction-specific code. This enables the same core platform to operate under different regulatory regimes by adjusting policy parameters.

### 16.1.1 Compliance Configuration Architecture

```
┌─────────────────────────────────────────────────────┐
│           COMPLIANCE CONFIGURATION LAYER             │
│                                                       │
│  ┌──────────────┐  ┌──────────────┐  ┌────────────┐ │
│  │  Market       │  │  Participant │  │  Reporting │ │
│  │  Integrity    │  │  Controls    │  │  Controls  │ │
│  │  Controls     │  │              │  │            │ │
│  └──────┬───────┘  └──────┬───────┘  └─────┬──────┘ │
│         │                  │                 │        │
│  ┌──────┴───────┐  ┌──────┴───────┐  ┌─────┴──────┐ │
│  │ Surveillance │  │  Access &    │  │  Retention │ │
│  │ Hooks        │  │  Governance  │  │  & Audit   │ │
│  └──────────────┘  └──────────────┘  └────────────┘ │
│                                                       │
│  ┌──────────────────────────────────────────────────┐ │
│  │        JURISDICTION POLICY PROFILES               │ │
│  │  EU_MiFID2 │ US_RegNMS │ SG_MAS │ Digital_MiCA  │ │
│  └──────────────────────────────────────────────────┘ │
└─────────────────────────────────────────────────────┘
```

### 16.1.2 Policy Profile Structure

```yaml
jurisdiction_profile: "EU_MiFID2_MTF"
version: "2024.1"
effective_date: "2024-01-01"

market_integrity:
  circuit_breakers:
    enabled: true
    static_threshold_pct: 10
    dynamic_threshold_pct: 5
    auction_duration_seconds: 300
  clock_sync:
    required_accuracy_us: 100  # RTS 25: 100µs for trading venues
    sync_protocol: PTP
    evidence_retention_days: 2555  # 7 years
  tick_size_regime:
    source: "ESMA_RTS_11"
    table_id: "MIFID2_TICK_SIZES"
  pre_trade_transparency:
    enabled: true
    waivers: ["LIS", "REFERENCE_PRICE", "NEGOTIATED_TRADE", "ORDER_MANAGEMENT"]
  post_trade_transparency:
    enabled: true
    deferred_publication:
      enabled: true
      lis_thresholds: "ESMA_RTS_1_ANNEX_II"
      max_deferral_minutes: 60

participant_controls:
  pre_trade_risk:
    mandatory: true
    fat_finger_price: true
    fat_finger_quantity: true
    message_rate_limit: true
    kill_switch: true
  algo_trading_flagging:
    enabled: true
    flag_field: "AlgoIndicator"
  dea_controls:
    enabled: true
    sponsor_responsible: true

surveillance_hooks:
  spoofing_detection: true
  layering_detection: true
  wash_trade_detection: true
  market_maker_obligation_monitoring: true
  cross_venue_data_sharing: true

reporting:
  transaction_reporting:
    enabled: true
    destination: "ARM"  # Approved Reporting Mechanism
    format: "MiFIR_RTS_22"
    deadline: "T+1_before_close"
  trade_publication:
    enabled: true
    destination: "APA"  # Approved Publication Arrangement
    format: "MiFIR_RTS_1"

retention:
  order_data: 2555  # 7 years in days
  trade_data: 2555
  communication_records: 2555
  clock_sync_evidence: 2555

governance:
  four_eyes_rule: true
  change_management_required: true
  incident_notification:
    regulator_notification_hours: 24
    participant_notification_hours: 4
```

## 16.2 Global Compliance Control Categories

### 16.2.1 Market Integrity Controls

| Control ID | Control | Description | Configuration |
|-----------|---------|-------------|---------------|
| CC-MI-001 | Circuit breakers | Automated trading halt on excessive price movement | Thresholds, duration, extension rules |
| CC-MI-002 | Price bands | Rejection of orders outside acceptable range | Static/dynamic bands, percentages |
| CC-MI-003 | Tick size regime | Minimum price increment enforcement | Tick table per instrument/group |
| CC-MI-004 | Lot size enforcement | Minimum quantity increment | Lot size per instrument |
| CC-MI-005 | Self-trade prevention | Prevention of wash trading | STP modes per participant |
| CC-MI-006 | Market maker obligations | Quoting requirements for designated market makers | Spread, size, time-in-market requirements |
| CC-MI-007 | Order-to-trade ratio | Monitoring excessive non-productive messaging | Threshold, measurement period, action |
| CC-MI-008 | Auction mechanisms | Fair price discovery at open/close | Auction type, duration, algorithm |

### 16.2.2 Pre-Trade Risk Controls

| Control ID | Control | Applicability |
|-----------|---------|--------------|
| CC-PR-001 | Fat finger price limits | All venues |
| CC-PR-002 | Fat finger quantity limits | All venues |
| CC-PR-003 | Maximum notional per order | All venues |
| CC-PR-004 | Message rate throttling | All venues |
| CC-PR-005 | Credit/exposure limits | Venues with credit risk |
| CC-PR-006 | Position limits | Derivatives, commodities |
| CC-PR-007 | Short-sell controls | Equities (jurisdiction-dependent) |
| CC-PR-008 | Kill switch | All venues (regulatory mandate in most jurisdictions) |
| CC-PR-009 | Cancel-on-disconnect | All venues |
| CC-PR-010 | Algo trading controls | Venues accepting algo orders |

### 16.2.3 Surveillance Hooks

| Hook ID | Hook | Data Provided | Integration |
|---------|------|---------------|-------------|
| CC-SH-001 | Order lifecycle events | Full order entry, modify, cancel, fill events | Real-time stream to surveillance |
| CC-SH-002 | Spoofing/layering patterns | Rapid order/cancel sequences flagged | Pattern detection parameters configurable |
| CC-SH-003 | Wash trade indicators | Self-trades that bypass STP | Flagged with counterparty analysis |
| CC-SH-004 | Market maker compliance | Quoting time, spread, size vs. obligations | Obligation monitoring dashboard |
| CC-SH-005 | Unusual activity | Volume, rate, or price movement anomalies | Configurable anomaly thresholds |
| CC-SH-006 | Cross-market data | Data export for cross-venue surveillance | Standard export format (e.g., FIX/Avro) |

### 16.2.4 Audit and Retention Controls

| Control ID | Control | Requirement |
|-----------|---------|-------------|
| CC-AR-001 | Immutable audit trail | Every state mutation recorded with nanosecond timestamp |
| CC-AR-002 | Tamper evidence | Cryptographic checksums on audit records |
| CC-AR-003 | Retention period | Configurable per jurisdiction (typically 5–7 years) |
| CC-AR-004 | Clock sync evidence | PTP sync status logged continuously |
| CC-AR-005 | Operator action logging | All administrative actions recorded with identity |
| CC-AR-006 | Configuration change logging | All parameter changes recorded with before/after values |
| CC-AR-007 | Data retrieval | Ability to retrieve any historical record within SLA |

## 16.3 Compliance Mapping Matrix

| Control Category | EU MiFID II | US Reg NMS/ATS | SG MAS | AU ASIC | Digital (MiCA) | Commodity (CFTC) |
|-----------------|:-----------:|:--------------:|:------:|:-------:|:--------------:|:----------------:|
| Circuit breakers | ✅ RTS 7 | ✅ LULD | ✅ | ✅ | ✅ | ✅ |
| Clock sync (100µs) | ✅ RTS 25 | ◐ (50ms) | ✅ | ✅ | ✅ | ◐ |
| Pre-trade transparency | ✅ MiFIR | ◐ (ATS-N) | ✅ | ✅ | ✅ | ✅ |
| Post-trade transparency | ✅ MiFIR | ✅ (SIP) | ✅ | ✅ | ✅ | ✅ |
| Pre-trade risk controls | ✅ RTS 7 | ✅ Reg SCI | ✅ | ✅ | ✅ | ✅ |
| Kill switch | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| Algo trading controls | ✅ RTS 6 | ◐ | ✅ | ✅ | ✅ | ✅ |
| Transaction reporting | ✅ MiFIR Art.26 | ✅ CAT | ✅ | ✅ | ✅ MiCA | ✅ |
| Order record keeping | ✅ RTS 24 | ✅ Rule 17a-4 | ✅ | ✅ | ✅ | ✅ |
| Surveillance | ✅ MAR | ✅ FINRA | ✅ | ✅ | ✅ | ✅ |
| Position limits | ◐ MiFID II | ✅ CFTC | ✅ | ◐ | ◐ | ✅ |
| Short-sell reporting | ✅ SSR | ✅ Reg SHO | ✅ | ✅ | N/A | N/A |
| Resilience testing | ✅ RTS 7 | ✅ Reg SCI | ✅ | ✅ | ✅ | ✅ |
| Incident notification | ✅ (24h) | ✅ Reg SCI (24h) | ✅ | ✅ | ✅ | ✅ |
| Data retention | 5–7 years | 6 years | 5 years | 7 years | 5 years | 5 years |
| DR/BCP | ✅ RTS 7 | ✅ Reg SCI | ✅ | ✅ | ✅ | ✅ |

Legend: ✅ = Explicitly required, ◐ = Partially required or implied

## 16.4 Algorithmic Trading Supervision

### 16.4.1 Requirements for Venues Accepting Algo Orders

| Requirement | Description | Configuration |
|-------------|-------------|---------------|
| Algo identification | Orders from algorithmic strategies must be flagged | Order flag field; participant self-classification |
| Throttling controls | Per-participant message rate limits | Rate per second, per minute; configurable |
| Kill switch for algos | Ability to disable algo participant immediately | Kill switch with < 1 second effect |
| Order-to-trade ratio | Monitor and limit excessive non-productive messaging | Ratio threshold; measurement window; warning/block action |
| Testing requirements | Algo strategies must be tested before production | Certification environment; mandatory test scenarios |
| Market making algo obligations | Algo market makers subject to quoting obligations | Time-in-market, spread, size requirements |

## 16.5 Environment Segregation Controls

| Environment | Purpose | Data | Access |
|-------------|---------|------|--------|
| Production | Live trading | Real participant orders | Authorized operators only |
| DR/Standby | Disaster recovery | Replicated from production | DR team only |
| UAT | Pre-production testing | Synthetic/anonymized data | Engineering + QA |
| Certification | Participant testing | Synthetic data | Participants + connectivity team |
| Development | Feature development | Synthetic data | Engineering |
| Sandbox | Experimentation | Isolated | Engineering |

**Segregation Requirements**:
- No production data in non-production environments (unless anonymized)
- Network isolation between environments
- Separate credentials per environment
- Environment identification in all logs and audit trails
- No direct access from non-production to production systems
