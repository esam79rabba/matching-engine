# 17. Data, Audit, and Reporting Requirements

## 17.1 Core Business Entities

### 17.1.1 Participant

| Field | Type | Description |
|-------|------|-------------|
| ParticipantId | uint32 | Venue-assigned unique ID |
| LegalName | string | Registered legal entity name |
| LEI | string(20) | Legal Entity Identifier (ISO 17442) |
| MembershipType | enum | TRADING_MEMBER, CLEARING_MEMBER, MARKET_MAKER, SPONSOR |
| Status | enum | PENDING, ACTIVE, SUSPENDED, TERMINATED |
| JurisdictionCode | string(2) | ISO 3166-1 country code |
| OnboardingDate | date | Membership start date |
| KYCStatus | enum | PENDING, APPROVED, EXPIRED |
| RiskTier | string | Risk classification for limit assignment |
| ClearingMemberId | uint32 | FK to clearing member (if give-up) |
| FeeTierId | uint32 | FK to fee schedule |
| **Lifecycle**: PENDING → ACTIVE → SUSPENDED ↔ ACTIVE → TERMINATED |

### 17.1.2 Account

| Field | Type | Description |
|-------|------|-------------|
| AccountId | uint32 | Venue-assigned unique ID |
| ParticipantId | uint32 | FK to owning participant |
| AccountType | enum | HOUSE, CLIENT, MARKET_MAKING |
| ClearingAccountRef | string | CCP clearing account reference |
| SettlementAccountRef | string | CSD settlement account reference |
| CurrencyCode | string(3) | Primary currency (ISO 4217) |
| Status | enum | ACTIVE, SUSPENDED, CLOSED |

### 17.1.3 Instrument

| Field | Type | Description |
|-------|------|-------------|
| InstrumentId | uint32 | Venue-assigned unique ID |
| Symbol | string(32) | Trading symbol |
| ISIN | string(12) | International Securities Identification Number |
| InstrumentType | enum | EQUITY, ETF, BOND, FUTURE, OPTION, FX, COMMODITY, CARBON, DIGITAL |
| UnderlyingId | uint32 | FK to underlying (derivatives) |
| CurrencyCode | string(3) | Quotation currency |
| TickSizeTableId | uint32 | FK to tick size table |
| LotSize | decimal | Minimum quantity increment |
| PricePrecision | uint8 | Decimal places for price |
| QuantityPrecision | uint8 | Decimal places for quantity |
| PartitionId | uint16 | Assigned matching partition |
| MarketSegmentId | uint32 | FK to market segment |
| SessionProfileId | uint32 | FK to trading session profile |
| StaticPriceBandPct | decimal | Static price band percentage |
| DynamicPriceBandPct | decimal | Dynamic price band percentage |
| CircuitBreakerEnabled | boolean | Volatility auction enabled |
| PriorityModel | enum | PRICE_TIME, PRO_RATA, PRO_RATA_TOP, PARITY |
| Status | enum | PRE_LISTED, LISTED, SUSPENDED, DELISTED |
| ListingDate | date | First trading date |
| ExpiryDate | date | Last trading date (derivatives) |
| **Lifecycle**: PRE_LISTED → LISTED → SUSPENDED ↔ LISTED → DELISTED |

### 17.1.4 Order

| Field | Type | Description |
|-------|------|-------------|
| OrderId | uint64 | Venue-assigned unique ID (partition-prefixed) |
| ClientOrderId | uint64 | Participant-assigned ID |
| ParticipantId | uint32 | Submitting participant |
| AccountId | uint32 | Trading account |
| InstrumentId | uint32 | Instrument |
| Side | enum | BUY, SELL |
| OrderType | enum | MARKET, LIMIT, STOP, STOP_LIMIT, PEGGED, ICEBERG |
| TimeInForce | enum | DAY, GTC, GTD, IOC, FOK, AUCTION_ONLY |
| Price | int64 | Limit price (integer representation) |
| StopPrice | int64 | Stop trigger price |
| Quantity | uint64 | Original order quantity |
| DisplayQuantity | uint64 | Iceberg display quantity |
| RemainingQuantity | uint64 | Unfilled quantity |
| CumulativeFilledQty | uint64 | Total filled so far |
| AverageFilledPrice | int64 | Volume-weighted average fill price |
| Status | enum | NEW, PARTIALLY_FILLED, FILLED, CANCELLED, REJECTED, EXPIRED, SUSPENDED |
| STPGroupId | uint32 | Self-trade prevention group |
| Flags | bitfield | POST_ONLY, REDUCE_ONLY, CANCEL_ON_DISCONNECT |
| EntryTimestamp | uint64 | Nanosecond timestamp of entry |
| LastUpdateTimestamp | uint64 | Nanosecond timestamp of last update |
| SequenceNumber | uint64 | Sequencer-assigned sequence number |
| ExpiryTime | uint64 | GTD expiry timestamp |
| **Lifecycle**: NEW → PARTIALLY_FILLED → FILLED / CANCELLED / EXPIRED |

### 17.1.5 Execution

| Field | Type | Description |
|-------|------|-------------|
| ExecutionId | uint64 | Venue-assigned unique ID |
| TradeId | uint64 | Links buyer and seller executions |
| OrderId | uint64 | FK to order |
| ParticipantId | uint32 | Participant on this side |
| InstrumentId | uint32 | Instrument |
| Side | enum | BUY, SELL |
| Price | int64 | Execution price |
| Quantity | uint64 | Execution quantity |
| LiquidityIndicator | enum | MAKER, TAKER |
| ExecutionTimestamp | uint64 | Nanosecond timestamp |
| SequenceNumber | uint64 | Journal sequence number |
| AggressorSide | enum | BUY, SELL |
| MatchingPhase | enum | CONTINUOUS, AUCTION, CLOSING_AUCTION |

### 17.1.6 Trade

| Field | Type | Description |
|-------|------|-------------|
| TradeId | uint64 | Unique trade identifier |
| BuyerExecId | uint64 | Buyer's execution ID |
| SellerExecId | uint64 | Seller's execution ID |
| InstrumentId | uint32 | Instrument |
| Price | int64 | Trade price |
| Quantity | uint64 | Trade quantity |
| BuyerParticipantId | uint32 | Buyer participant |
| SellerParticipantId | uint32 | Seller participant |
| TradeTimestamp | uint64 | Nanosecond timestamp |
| TradeType | enum | REGULAR, AUCTION, BLOCK, NEGOTIATED |
| ClearingStatus | enum | PENDING, SUBMITTED, CLEARED, REJECTED |
| BustStatus | enum | ACTIVE, BUSTED, CORRECTED |

## 17.2 Event Taxonomy

### 17.2.1 Canonical Event Types

| Category | Event Type | Source | Consumers |
|----------|-----------|--------|-----------|
| **Order** | OrderNew | Matching Engine | MD, DC, PT, SUR, Audit |
| | OrderModify | Matching Engine | MD, DC, PT, SUR, Audit |
| | OrderCancel | Matching Engine | MD, DC, PT, SUR, Audit |
| | OrderReject | Gateway / Risk / ME | DC, SUR, Audit |
| | OrderExpire | Matching Engine | DC, SUR, Audit |
| | OrderTriggered | Matching Engine (stop) | MD, DC, SUR, Audit |
| **Execution** | ExecutionNew | Matching Engine | MD, DC, PT, SUR, Audit |
| | TradeBust | Operations | MD, DC, PT, SUR, Audit |
| | TradeCorrection | Operations | MD, DC, PT, SUR, Audit |
| **Market State** | InstrumentStateChange | ME / Operations | MD, DC, SUR, Audit |
| | AuctionStart | Matching Engine | MD, Audit |
| | AuctionEnd | Matching Engine | MD, Audit |
| | CircuitBreakerTrigger | Matching Engine | MD, OPS, SUR, Audit |
| | SessionPhaseChange | Scheduler | MD, Audit |
| **Risk** | RiskLimitBreach | Risk Service | OPS, SUR, Audit |
| | KillSwitchActivation | Risk / Operations | DC, SUR, Audit |
| | CancelOnDisconnect | Gateway | DC, SUR, Audit |
| **Quote** | MassQuoteUpdate | Matching Engine | MD, SUR, Audit |
| | QuoteCancel | Matching Engine | MD, SUR, Audit |
| **Connectivity** | SessionLogon | Gateway | SUR, Audit |
| | SessionLogout | Gateway | SUR, Audit |
| | SessionDisconnect | Gateway | SUR, Audit |
| **Config** | ParameterChange | Operations | Audit |
| | InstrumentUpdate | Reference Data | MD, Audit |

Legend: MD=Market Data, DC=Drop Copy, PT=Post-Trade, SUR=Surveillance, OPS=Operations

### 17.2.2 Event Envelope

Every event shares a common envelope:

| Field | Type | Description |
|-------|------|-------------|
| EventId | uint64 | Globally unique event identifier |
| SequenceNumber | uint64 | Partition-scoped sequence number |
| Timestamp | uint64 | Nanosecond UTC timestamp |
| EventType | uint16 | Event type from taxonomy |
| PartitionId | uint16 | Source partition |
| Version | uint8 | Event schema version |
| Payload | bytes | Type-specific payload |
| Checksum | uint32 | CRC32 of envelope + payload |

### 17.2.3 Event Sequencing Model
- Events sequenced per partition (partition-local monotonic sequence)
- Global ordering across partitions is NOT guaranteed (no global sequencer)
- Consumers that need cross-partition ordering must use timestamps (with PTP accuracy)
- Within a partition, sequence number = causal order = audit order

### 17.2.4 Event Versioning
- All event schemas versioned using semantic versioning
- Backward-compatible changes: add optional fields (minor version)
- Breaking changes: new event type or major version (requires consumer migration)
- Old versions supported for minimum 6 months after deprecation

## 17.3 Audit Trail Requirements

| REQ ID | Requirement | Detail |
|--------|------------|--------|
| AU-001 | Completeness | Every state mutation captured with no gaps |
| AU-002 | Immutability | Audit records cannot be modified or deleted |
| AU-003 | Tamper evidence | Cryptographic hash chain on audit segments |
| AU-004 | Timestamp precision | Nanosecond UTC on every record |
| AU-005 | Identity tracking | Operator/system identity on every admin action |
| AU-006 | Before/after values | Configuration changes include before and after state |
| AU-007 | Correlation | Events correlatable by order ID, trade ID, session ID, participant ID |
| AU-008 | Queryability | Audit records searchable by time range, entity, event type |
| AU-009 | Export capability | Export to CSV, JSON, regulatory formats |
| AU-010 | Retention | Online: 90 days; warm: 1 year; cold: 7 years |
| AU-011 | Access control | Audit trail access restricted to authorized roles |
| AU-012 | Integrity verification | Periodic automated integrity checks on audit data |

## 17.4 Reporting Requirements

### 17.4.1 Operational Reports

| Report | Frequency | Content | Audience |
|--------|-----------|---------|----------|
| Daily trading summary | End of day | Volume, trades, turnover by instrument | Operations, Management |
| Participant activity | End of day | Orders, trades, fees per participant | Operations, Finance |
| Market quality | End of day | Spreads, depth, market maker compliance | Product, Compliance |
| Latency report | End of day | p50/p99/p99.9 per gateway, per component | Engineering, SRE |
| Incident report | Per incident | Timeline, impact, root cause, remediation | Operations, Management |
| Circuit breaker report | Per activation | Trigger details, auction results, recovery | Compliance, Surveillance |

### 17.4.2 Regulatory Reports (Framework)

| Report | Jurisdiction | Format | Deadline |
|--------|-------------|--------|----------|
| Transaction report | EU (MiFIR Art.26) | XML per RTS 22 | T+1 before market close |
| Trade publication | EU (MiFIR) | Per RTS 1 | Real-time or deferred |
| Order record keeping | EU (RTS 24) | Per RTS 24 | On-demand for regulator |
| CAT reporting | US | OATS/CAT format | T+1 per FINRA schedule |
| Position reporting | CFTC/MiFID II | Per regulation | Per schedule |
| Short-sell reporting | EU SSR / US Reg SHO | Per regulation | Per schedule |
