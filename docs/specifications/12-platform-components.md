# 12. Surrounding Platform Components

## 12.1 Component Architecture Overview

```
┌──────────────────────────────────────────────────────────────────────────┐
│                        PARTICIPANT CONNECTIVITY                          │
│  ┌──────────┐ ┌──────────┐ ┌──────────┐ ┌──────────┐ ┌──────────────┐  │
│  │ FIX GW   │ │Binary GW │ │ MM Quote │ │ WS/REST  │ │ Drop Copy GW │  │
│  │ (4.2/5.0)│ │(low-lat) │ │ Gateway  │ │ Gateway  │ │              │  │
│  └────┬─────┘ └────┬─────┘ └────┬─────┘ └────┬─────┘ └──────┬───────┘  │
│       │             │            │             │              │          │
│       └─────────────┴────────────┴─────────────┘              │          │
│                          │                                    │          │
│                          ▼                                    │          │
│  ┌───────────────────────────────────┐                       │          │
│  │       PRE-TRADE RISK SERVICE      │                       │          │
│  │  (fat finger, credit, limits)     │                       │          │
│  └──────────────┬────────────────────┘                       │          │
│                 │                                             │          │
│                 ▼                                             │          │
│  ┌──────────────────────────────┐    ┌────────────────────┐  │          │
│  │    SEQUENCER + JOURNAL       │    │  REFERENCE DATA    │  │          │
│  └──────────────┬───────────────┘    │  SERVICE           │  │          │
│                 │                     └────────────────────┘  │          │
│                 ▼                                             │          │
│  ┌──────────────────────────────┐                            │          │
│  │     MATCHING ENGINE          │                            │          │
│  │     (partitions)             │────── Output Bus ──────────┤          │
│  └──────────────────────────────┘                   │        │          │
│                                                     │        │          │
│  ┌────────────────┐  ┌────────────────┐  ┌─────────┴──────┐ │          │
│  │ MARKET DATA    │  │ POST-TRADE     │  │ SURVEILLANCE   │ │          │
│  │ PUBLISHER      │  │ SERVICE        │  │ EXPORT         │ │          │
│  └────────────────┘  └────────────────┘  └────────────────┘ │          │
│                                                              │          │
│  ┌────────────────┐  ┌────────────────┐  ┌────────────────┐ │          │
│  │ KILL SWITCH    │  │ MARKET OPS     │  │ PARTICIPANT    │ │          │
│  │ SERVICE        │  │ CONSOLE        │  │ ADMIN          │ │          │
│  └────────────────┘  └────────────────┘  └────────────────┘ │          │
│                                                              │          │
│  ┌───────────────────────────────────────────────────────────┘          │
│  │            OBSERVABILITY STACK (Metrics, Logs, Traces)               │
│  └──────────────────────────────────────────────────────────────────────┘
```

## 12.2 Client Connectivity Gateways

### 12.2.1 FIX Order Entry Gateway

**Purpose**: Provide industry-standard FIX protocol connectivity for order entry and execution reporting.

| Attribute | Specification |
|-----------|--------------|
| **Protocol** | FIX 4.2, FIX 4.4, FIX 5.0SP2 (configurable per session) |
| **Transport** | TCP with optional TLS 1.3 |
| **Sessions** | Multiple concurrent sessions per participant; session-level authentication |
| **Throughput** | ≥ 10,000 messages/second per session; ≥ 100,000 aggregate per gateway instance |
| **Latency** | < 50 µs gateway processing (decode → validate → forward) |
| **High Availability** | Active-passive pair; session failover with sequence gap recovery |
| **Supported Messages** | NewOrderSingle (D), OrderCancelRequest (F), OrderCancelReplaceRequest (G), OrderMassCancelRequest (q), ExecutionReport (8), OrderCancelReject (9), Heartbeat, Logon, Logout, Reject, SequenceReset, ResendRequest |
| **Compliance** | Configurable FIX tag mapping; custom tags for venue-specific fields (STP group, cancel-on-disconnect flag) |
| **Security** | Mutual TLS; CompID/SubID validation; IP whitelist; message rate throttling |

**Functional Responsibilities**:
- FIX session management (logon, logout, heartbeat, sequence management)
- Message parsing and validation (syntax, required fields, data types)
- FIX-to-internal message translation
- Internal-to-FIX response translation
- Sequence gap detection and recovery (resend request handling)
- Session-level throttling
- Session audit logging
- Cancel-on-disconnect trigger on session loss

**Data Owned**: FIX session state, sequence numbers, session configuration

**Boundaries**: Does NOT perform business validation (price bands, risk checks) — forwards to risk service and sequencer.

---

### 12.2.2 Binary Low-Latency Order Entry Gateway

**Purpose**: Provide minimum-overhead binary protocol connectivity for latency-sensitive participants (market makers, HFT firms).

| Attribute | Specification |
|-----------|--------------|
| **Protocol** | Custom binary protocol (SBE — Simple Binary Encoding recommended) |
| **Transport** | TCP or kernel-bypass (DPDK/RDMA) with optional TLS offload |
| **Latency** | < 5 µs gateway processing (wire to sequencer) |
| **Throughput** | ≥ 500,000 messages/second per session |
| **Features** | Zero-copy message forwarding where possible; pre-allocated buffers; lock-free queues |
| **Session Management** | Lightweight binary logon/heartbeat; application-level sequencing |

**Design Notes**:
- SBE (Simple Binary Encoding) chosen for its zero-copy decode, fixed-size headers, and FIX-semantic compatibility
- The binary protocol is a wire-format optimization; it maps 1:1 to the same internal message model as FIX
- Participants who need lowest latency use binary; others use FIX — both are first-class citizens

---

### 12.2.3 Market Maker Quote Gateway

**Purpose**: Specialized gateway optimized for mass quote operations.

| Attribute | Specification |
|-----------|--------------|
| **Protocol** | Binary (SBE) or FIX mass quote messages |
| **Mass Quote** | Single message updates bid+ask for up to 100 instruments simultaneously |
| **Quote Replace** | New mass quote implicitly replaces previous quotes for the same instruments from the same session |
| **Cancel-on-Disconnect** | Mandatory for market maker sessions; configurable grace period |
| **Throughput** | ≥ 100,000 quote updates/second (representing up to 10M instrument-side updates/second) |
| **Latency** | < 5 µs gateway processing |

---

### 12.2.4 WebSocket / REST Gateway

**Purpose**: Provide modern web-standard connectivity for participants who do not require ultra-low latency.

| Attribute | Specification |
|-----------|--------------|
| **Protocol** | HTTPS REST (order submission) + WebSocket (streaming updates) |
| **Transport** | TLS 1.3 |
| **Authentication** | API key + HMAC signature; or OAuth 2.0 bearer token |
| **Latency** | < 10 ms round-trip (acceptable for this tier) |
| **Throughput** | ≥ 1,000 orders/second per client |
| **Features** | JSON message format; WebSocket streams for executions and market data |
| **Rate Limiting** | Per-API-key rate limits with configurable tiers |
| **Use Cases** | Retail intermediaries, low-frequency algo traders, admin/support tools |

---

### 12.2.5 Drop Copy Gateway

**Purpose**: Provide a real-time, read-only copy of all orders and executions for a participant.

| Attribute | Specification |
|-----------|--------------|
| **Protocol** | FIX drop copy session (separate from order-entry session) |
| **Content** | All execution reports, order status, cancellations, and fills for the participant |
| **Latency** | < 100 µs from matching engine event to drop copy delivery |
| **Sequencing** | Messages sequenced; gap recovery supported |
| **Use Cases** | Middle-office reconciliation, compliance monitoring, backup order management |
| **Independence** | Drop copy sessions do not affect order-entry sessions |

## 12.3 Pre-Trade Risk Service

### 12.3.1 Purpose
Enforce configurable risk controls on every order before it reaches the matching engine. This is the primary defense against erroneous or malicious order flow.

### 12.3.2 Risk Checks

| Check | Description | Scope | Priority |
|-------|-------------|-------|----------|
| **Fat finger — price** | Order price deviates > X% from reference price | Per instrument | Mandatory |
| **Fat finger — quantity** | Order quantity exceeds maximum allowed | Per instrument, per participant | Mandatory |
| **Fat finger — notional** | Order notional value exceeds maximum | Per participant | Mandatory |
| **Message rate** | Participant exceeds message rate limit | Per session, per participant | Mandatory |
| **Max open orders** | Participant exceeds maximum resting orders | Per participant, per instrument | Mandatory |
| **Credit limit** | Order would exceed participant's credit allocation | Per participant, per clearing account | Highly Recommended |
| **Position limit** | Order would exceed position limit (derivatives) | Per participant, per instrument | Highly Recommended |
| **Exposure limit** | Aggregate exposure across instruments exceeds limit | Per participant | Highly Recommended |
| **Short-sell check** | Sell order flagged short; locate required (equities) | Per instrument (jurisdiction-dependent) | Highly Recommended |
| **Duplicate order** | Detect duplicate client order IDs within a session | Per session | Mandatory |

### 12.3.3 Design Requirements
- Risk checks execute in < 5 µs per order
- Checks are configurable per participant, per instrument group, per account
- Limits are updateable in real-time without restart
- Risk state (exposure, position) updated in real-time from execution events
- Kill switch can disable a participant's order flow immediately
- All risk rejections logged with full reason detail

### 12.3.4 Kill Switch

| Feature | Description |
|---------|-------------|
| **Cancel all by participant** | Cancel all resting orders for a participant across all instruments |
| **Cancel all by instrument** | Cancel all resting orders for an instrument (market-wide halt) |
| **Cancel all by market segment** | Cancel all orders in a market segment |
| **Disable participant** | Prevent new orders from a participant; cancel all resting |
| **Emergency market halt** | Halt all trading across all instruments |
| **Authorization** | Four-eyes approval; authorized operator roles only |
| **Execution time** | Kill switch effect < 1 second |
| **Audit** | All kill switch actions fully journaled |

## 12.4 Reference Data Service

### 12.4.1 Purpose
Single source of truth for all static and semi-static data required by the platform.

### 12.4.2 Data Domains

| Domain | Examples | Update Frequency |
|--------|----------|-----------------|
| **Instruments** | Symbol, ISIN, tick size, lot size, price bands, trading session, clearing info | Daily / on-demand |
| **Participants** | Firm ID, accounts, entitlements, risk limits, fee tier | On-demand |
| **Users / Traders** | User ID, roles, session permissions, STP groups | On-demand |
| **Sessions** | Trading calendar, session schedule, phase timings | Daily / seasonal |
| **Fee tables** | Maker/taker fees, volume tiers, special programs | Monthly / on-demand |
| **Tick size tables** | Price-dependent tick sizes per instrument group | Regulatory / on-demand |
| **Holiday calendar** | Trading holidays per market / jurisdiction | Annual |
| **Market segments** | Segment definitions, board assignments | On-demand |

### 12.4.3 Requirements
- All reference data changes versioned and audited
- Changes effective at configured time (next session, immediately, scheduled)
- Bulk load capability for instrument universe (10,000+ instruments)
- API for querying current and historical reference data
- Integration with external sources (upstream instrument master, corporate actions feed)

## 12.5 Market Data Publisher

### 12.5.1 Purpose
Disseminate real-time market data to participants and market data vendors.

### 12.5.2 Feed Types

| Feed | Content | Transport | Latency Target |
|------|---------|-----------|---------------|
| **Full depth** | All price levels, order count per level | Multicast (UDP) | Profile A: < 10 µs from engine event |
| **Top of book** | Best bid/offer only | Multicast (UDP) | Profile A: < 10 µs |
| **Trade tape** | Executed trades (price, size, time, aggressor) | Multicast (UDP) | Profile A: < 10 µs |
| **Auction data** | Indicative price, volume, imbalance | Multicast (UDP) | < 100 ms |
| **Statistics** | OHLC, VWAP, volume, trade count | Multicast or TCP | < 1 second |
| **Reference data** | Instrument state changes, halts, sessions | Multicast or TCP | < 1 second |
| **Snapshot** | Full book snapshot on request | TCP (request/reply) | < 100 ms |

### 12.5.3 Protocol Design
- **Incremental + Snapshot model**: Clients receive a snapshot, then apply incremental updates
- **Sequence numbers**: Every message carries a monotonic sequence number per channel
- **Gap detection**: Clients detect gaps and request snapshot recovery
- **Heartbeats**: Regular heartbeats on each channel for liveness detection
- **Channel mapping**: Instruments mapped to multicast channels; participant subscribes to relevant channels
- **Binary encoding**: SBE or similar for minimum overhead

### 12.5.4 Conflation Policy
- **No conflation** for colocated participants (every event published individually)
- **Configurable conflation** for WAN distribution (e.g., combine updates within 1ms window)
- **Snapshot throttling**: Snapshot requests rate-limited to prevent abuse

## 12.6 Post-Trade Service

### 12.6.1 Purpose
Capture executed trades and prepare them for clearing, settlement, and reporting.

### 12.6.2 Functions

| Function | Description | Priority |
|----------|-------------|----------|
| Trade capture | Record all executed trades with full detail | Mandatory |
| Trade enrichment | Add clearing member, account, fee calculation | Mandatory |
| Clearing submission | Format and submit trades to CCP | Mandatory (Phase 2) |
| Allocation | Split block trade into sub-allocations | Highly Recommended |
| Give-up / Take-up | Transfer trade from executing broker to clearing broker | Highly Recommended |
| Trade confirmation | Confirm trade details to both counterparties | Mandatory |
| Settlement instruction | Generate settlement instructions for CSD/custodian | Mandatory (Phase 2) |
| Fee calculation | Apply fee schedule, compute fees per trade | Mandatory |
| Position update | Update real-time position per participant/account | Highly Recommended |
| Trade reporting | Generate regulatory trade reports | Mandatory |

## 12.7 Surveillance and Compliance Services

### 12.7.1 Purpose
Provide hooks and data feeds for market surveillance, abuse detection, and regulatory compliance.

### 12.7.2 Data Provided to Surveillance

| Data Type | Content | Delivery |
|-----------|---------|----------|
| Order events | All new, modify, cancel events with timestamps | Real-time stream |
| Execution events | All fills with price, size, counterparties | Real-time stream |
| Quote events | Mass quote updates, quote lifecycle | Real-time stream |
| Session events | Logon, logout, disconnect, reconnect | Real-time stream |
| Instrument events | State changes, halts, circuit breakers | Real-time stream |
| Risk events | Limit breaches, kill switch activations | Real-time stream |
| Order book snapshots | Periodic full book state for reconstruction | Periodic (configurable) |

### 12.7.3 Surveillance Hooks (Built-In)
- **Spoofing/layering pattern detection**: Flag rapid order placement and cancellation patterns
- **Wash trade detection**: Flag potential self-trades that bypassed STP
- **Message rate anomaly**: Flag unusual spikes in order/cancel rate per participant
- **Price manipulation**: Flag order patterns that move price without genuine trade intent
- **Market maker obligation monitoring**: Track quoting compliance against obligations

**Note**: The platform provides data and hooks, not a full surveillance application. Integration with specialized surveillance vendors (Nasdaq SMARTS, NICE Actimize, Scila) is via the data export interface.

## 12.8 Participant Administration

### 12.8.1 Functions

| Function | Description | Priority |
|----------|-------------|----------|
| Member onboarding | Workflow for new participant registration and approval | Mandatory |
| KYC/KYB integration | Hooks for know-your-customer/business verification | Mandatory |
| Entitlement management | Assign instruments, markets, order types, gateways per participant | Mandatory |
| Session provisioning | Create and configure connectivity sessions (FIX, binary, WS) | Mandatory |
| Certification management | Track participant certification status; manage test environments | Mandatory |
| Fee tier assignment | Assign fee schedules and volume tiers per participant | Mandatory |
| API key lifecycle | Generate, rotate, revoke API keys and certificates | Mandatory |
| Participant directory | Search and query participant information | Mandatory |
| Access audit | Log all access and entitlement changes | Mandatory |

## 12.9 Operations Console

### 12.9.1 Purpose
Web-based console for market operations staff to manage day-to-day trading operations.

### 12.9.2 Key Capabilities

| Capability | Description |
|-----------|-------------|
| **Session control** | Start, stop, extend trading sessions; trigger state transitions |
| **Instrument management** | Halt, suspend, resume individual instruments; change instrument state |
| **Parameter management** | Update tick sizes, price bands, session times with four-eyes approval |
| **Kill switch** | Emergency controls: cancel all by participant, instrument, or market-wide |
| **Trade management** | Trade bust, price correction (four-eyes) |
| **Monitoring dashboards** | Real-time order rates, trade rates, latency, participant activity |
| **Alert management** | View and acknowledge operational alerts |
| **Replay viewer** | Reconstruct order book at any historical point |
| **Reconciliation** | Compare internal state with clearing/settlement records |
| **Runbook execution** | Step-through guided procedures for operational scenarios |
| **Audit log** | View all operator actions with timestamps and approver |

### 12.9.3 Access Control
- Role-based access: Market Operator, Senior Operator, Market Surveillance, Admin, Read-Only
- Four-eyes approval for destructive actions (trade bust, parameter change, kill switch)
- All actions authenticated and audit-logged
- Session timeout and re-authentication for sensitive operations
