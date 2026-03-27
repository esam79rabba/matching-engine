# 10. Product Functional Scope

## 10.1 Functional Scope Overview

The platform functionality is organized into five layers, from innermost (hot path) to outermost (ecosystem):

```
┌─────────────────────────────────────────────────────────────────────┐
│  Layer 5: ECOSYSTEM INTEGRATIONS                                     │
│  (Clearing, Settlement, Surveillance, Regulatory Reporting, Vendors) │
│ ┌─────────────────────────────────────────────────────────────────┐ │
│ │  Layer 4: OPERATIONS & GOVERNANCE                                │ │
│ │  (Market Ops Console, Admin, Monitoring, Support Tooling)        │ │
│ │ ┌─────────────────────────────────────────────────────────────┐ │ │
│ │ │  Layer 3: PLATFORM SERVICES                                  │ │ │
│ │ │  (Gateways, Risk, Reference Data, Market Data, Post-Trade)   │ │ │
│ │ │ ┌───────────────────────────────────────────────────────┐   │ │ │
│ │ │ │  Layer 2: MATCHING ENGINE CONTROL PLANE                │   │ │ │
│ │ │ │  (Config, State Management, Recovery, Feature Flags)   │   │ │ │
│ │ │ │ ┌───────────────────────────────────────────────────┐ │   │ │ │
│ │ │ │ │  Layer 1: MATCHING ENGINE HOT PATH                 │ │   │ │ │
│ │ │ │ │  (Order Book, Matching, Journaling, Sequencing)    │ │   │ │ │
│ │ │ │ └───────────────────────────────────────────────────┘ │   │ │ │
│ │ │ └───────────────────────────────────────────────────────┘   │ │ │
│ │ └─────────────────────────────────────────────────────────────┘ │ │
│ └─────────────────────────────────────────────────────────────────┘ │
└─────────────────────────────────────────────────────────────────────┘
```

## 10.2 Layer 1: Matching Engine Hot Path

**What belongs here**: Only the absolute minimum required for deterministic order matching.

| Function | Description | Priority |
|----------|-------------|----------|
| Order book management | Insert, modify, cancel orders in the book | Mandatory |
| Order matching | Match aggressive orders against resting liquidity | Mandatory |
| Auction matching | Equilibrium price calculation and auction execution | Mandatory |
| Fill generation | Create execution records for matched orders | Mandatory |
| Journal writing | Append-only write of every input and output event | Mandatory |
| Sequencing | Assign monotonic sequence numbers to all events | Mandatory |
| Timestamping | Nanosecond timestamps on all events | Mandatory |
| Self-trade prevention | STP check on every potential match | Mandatory |
| Price band enforcement | Reject/hold orders outside configured bands | Mandatory |
| Instrument state enforcement | Permit/reject actions based on current state | Mandatory |

**What does NOT belong in the hot path**:
- Pre-trade risk checks (credit, position limits) — done at gateway/risk layer before sequencer
- Market data fan-out — done by dedicated market data publisher reading from journal/output bus
- Participant authentication — done at gateway layer
- Regulatory reporting — done asynchronously from journal
- Analytics or aggregations — done by downstream consumers
- Logging beyond journal — journal IS the log
- External service calls — no network I/O except journal write

## 10.3 Layer 2: Matching Engine Control Plane

| Function | Description | Priority |
|----------|-------------|----------|
| Configuration management | Load/update instrument parameters, session definitions, market rules | Mandatory |
| State machine management | Instrument state transitions (pre-open → auction → continuous → close) | Mandatory |
| Session scheduling | Automated and manual session phase transitions | Mandatory |
| Snapshot management | Periodic state checkpoints for recovery | Mandatory |
| Warm restart | Recover engine state from snapshot + journal replay | Mandatory |
| Feature flags | Enable/disable order types, matching modes, rules | Mandatory |
| Parameter updates | Hot-reload of tick sizes, price bands, lot sizes | Highly Recommended |
| Partition management | Assign/reassign instruments to partitions | Highly Recommended |

## 10.4 Layer 3: Platform Services

| Function | Description | Priority |
|----------|-------------|----------|
| FIX order entry gateway | FIX 4.2/4.4/5.0SP2 protocol support | Mandatory |
| Binary order entry gateway | Low-latency binary protocol | Mandatory |
| Market maker gateway | Mass quote, cancel-on-disconnect | Mandatory |
| WebSocket/REST gateway | Lower-tier participant access | Highly Recommended |
| Drop copy gateway | Real-time trade/order copy for participants | Mandatory |
| Pre-trade risk service | Fat finger, credit, position limit checks | Mandatory |
| Reference data service | Instruments, participants, sessions, fee tables | Mandatory |
| Market data publisher | Depth, top-of-book, trades, auctions | Mandatory |
| Post-trade service | Trade capture, allocations, clearing submission | Mandatory |
| Participant management | Onboarding, entitlements, certification | Mandatory |
| Kill switch service | Emergency cancel-all per participant/instrument/market | Mandatory |

## 10.5 Layer 4: Operations & Governance

| Function | Description | Priority |
|----------|-------------|----------|
| Market operations console | Start/stop sessions, halt/resume instruments, parameter changes | Mandatory |
| Participant admin portal | Member management, access provisioning, fee management | Mandatory |
| Incident management | Alert triage, escalation, resolution tracking | Mandatory |
| Replay tooling | Reconstruct order book state at any point in time | Mandatory |
| Reconciliation tools | Match internal state with clearing/settlement | Mandatory |
| Audit trail viewer | Query and export audit records | Mandatory |
| Change management | Four-eyes approval for parameter/rule changes | Mandatory |
| Runbook system | Documented procedures for operational scenarios | Mandatory |

## 10.6 Layer 5: Ecosystem Integrations

| Function | Description | Priority |
|----------|-------------|----------|
| CCP clearing adapter | Submit trades for clearing, receive margin calls | Mandatory (Phase 2) |
| Settlement instruction generation | Generate settlement instructions for CSD/custodian | Mandatory (Phase 2) |
| Surveillance data export | Feed order/trade data to surveillance system | Mandatory |
| Regulatory reporting hooks | Data extracts for transaction reporting (MiFIR, CAT, etc.) | Mandatory |
| Market data vendor feed | Normalized feed for redistribution | Highly Recommended |
| Index data feed | Trade data for index calculation | Highly Recommended |
| Corporate actions integration | Receive corporate action events, trigger reference data updates | Highly Recommended |
| Custody/wallet adapter | Digital asset custody integration | Optional (Phase 3) |

## 10.7 Functional Scope by Release Phase

| Function | Phase 1 (FR-1) | Phase 2 (Exchange-Grade) | Phase 3 (Multi-Asset) | Phase 4 (Advanced) | Phase 5 (Ecosystem) |
|----------|:---:|:---:|:---:|:---:|:---:|
| Continuous LOB | ✅ | ✅ | ✅ | ✅ | ✅ |
| Opening/Closing Auction | ✅ | ✅ | ✅ | ✅ | ✅ |
| Volatility Auction | ✗ | ✅ | ✅ | ✅ | ✅ |
| RFQ | ✗ | ✗ | ✅ | ✅ | ✅ |
| Dark Midpoint | ✗ | ✗ | ✅ | ✅ | ✅ |
| Block Trade | ✗ | ✅ | ✅ | ✅ | ✅ |
| FIX Gateway | ✅ | ✅ | ✅ | ✅ | ✅ |
| Binary Gateway | ✅ | ✅ | ✅ | ✅ | ✅ |
| WebSocket/REST | ✗ | ✅ | ✅ | ✅ | ✅ |
| Pre-trade Risk | Basic | Full | Full | Full | Full |
| Market Data | ✅ | ✅ | ✅ | ✅ | ✅ |
| Drop Copy | ✅ | ✅ | ✅ | ✅ | ✅ |
| Clearing Adapter | ✗ | ✅ | ✅ | ✅ | ✅ |
| Surveillance Hooks | Basic | Full | Full | Advanced | Advanced |
| Ops Console | Basic | Full | Full | Full | Full |
| Multi-Asset Config | Single | 2–3 | All | All | All |
| DR / Failover | Warm restart | Active-passive | Active-passive | Active-active | Active-active |
| Implied Matching | ✗ | ✗ | ✗ | ✅ | ✅ |
| Complex Orders | ✗ | ✗ | ✅ | ✅ | ✅ |
