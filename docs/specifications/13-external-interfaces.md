# 13. External Interfaces and Integration Boundaries

## 13.1 Interface Classification

| Interface | Direction | Latency Class | Transport | Protocol | Security |
|-----------|-----------|--------------|-----------|----------|----------|
| FIX Order Entry | Bidirectional | Low (< 50 µs) | TCP + TLS | FIX 4.2/4.4/5.0SP2 | mTLS, CompID auth |
| Binary Order Entry | Bidirectional | Ultra-low (< 5 µs) | TCP / DPDK | SBE binary | mTLS or IP whitelist |
| Mass Quote | Bidirectional | Ultra-low (< 5 µs) | TCP / DPDK | SBE binary | mTLS or IP whitelist |
| WebSocket Order Entry | Bidirectional | Moderate (< 10 ms) | WSS | JSON over WS | API key + HMAC |
| REST Order Entry | Request/Reply | Moderate (< 10 ms) | HTTPS | REST JSON | API key + HMAC / OAuth |
| Drop Copy | Outbound (push) | Low (< 100 µs) | TCP + TLS | FIX | mTLS |
| Market Data (colo) | Outbound (push) | Ultra-low (Profile A: < 10 µs) | UDP Multicast | SBE binary | Network isolation |
| Market Data (WAN) | Outbound (push) | Moderate (< 10 ms) | TCP + TLS / WSS | SBE or JSON | mTLS / API key |
| Market Data Snapshot | Request/Reply | Moderate (< 100 ms) | TCP | SBE binary | mTLS |
| Clearing Adapter | Outbound (push) | Near-real-time (< 1 s) | TCP + TLS | FIX / proprietary | mTLS |
| Settlement Adapter | Outbound (push) | Batch (minutes) | HTTPS / SFTP | ISO 20022 / CSV | mTLS / SSH |
| Surveillance Export | Outbound (stream) | Near-real-time (< 1 s) | TCP / Kafka | Binary / Avro | mTLS |
| Regulatory Reporting | Outbound (batch) | Batch (T+1) | HTTPS / SFTP | XML / CSV (per regime) | mTLS / encryption |
| Admin / Ops API | Bidirectional | Low priority (< 1 s) | HTTPS | REST JSON | OAuth 2.0 + RBAC |
| Reference Data API | Request/Reply | Low priority (< 1 s) | HTTPS | REST JSON | OAuth 2.0 + RBAC |
| Risk Control API | Bidirectional | Low (< 100 ms) | HTTPS / gRPC | REST / gRPC | mTLS |
| Observability Export | Outbound (push) | Background | TCP | OpenTelemetry / Prometheus | mTLS |

## 13.2 FIX Order Entry Interface

### 13.2.1 Supported Message Types

**Inbound (Participant → Venue)**:

| Message | FIX MsgType | Description |
|---------|-------------|-------------|
| NewOrderSingle | D | Submit new order |
| OrderCancelRequest | F | Request cancel of resting order |
| OrderCancelReplaceRequest | G | Modify price/quantity of resting order |
| OrderMassCancelRequest | q | Cancel multiple orders by criteria |
| MassQuote | i | Submit/update mass quotes |
| QuoteCancel | Z | Cancel quotes |
| SecurityListRequest | x | Request instrument list |
| MarketDataRequest | V | Subscribe to market data (if FIX MD supported) |

**Outbound (Venue → Participant)**:

| Message | FIX MsgType | Description |
|---------|-------------|-------------|
| ExecutionReport | 8 | Order acknowledgment, fill, cancel confirm, reject |
| OrderCancelReject | 9 | Rejection of cancel/modify request |
| MassQuoteAcknowledgement | b | Acknowledgment of mass quote |
| SecurityList | y | Instrument list response |
| BusinessMessageReject | j | Business-level rejection |

### 13.2.2 Custom Tags

| Tag | Name | Type | Description |
|-----|------|------|-------------|
| 20001 | STPGroupID | String | Self-trade prevention group identifier |
| 20002 | CancelOnDisconnect | Boolean | Enable CoD for this session |
| 20003 | DisplayQty | Qty | Iceberg display quantity |
| 20004 | PegType | Int | Peg type (1=Primary, 2=Midpoint, 3=Market) |
| 20005 | PegOffset | Price | Offset from peg reference |
| 20006 | PostOnlyFlag | Boolean | Post-only order flag |
| 20007 | ReduceOnlyFlag | Boolean | Reduce-only order flag |
| 20008 | PartitionID | Int | Execution partition (informational) |

### 13.2.3 Sequence Management
- FIX sequence numbers maintained per session
- Gap detected via sequence number mismatch
- ResendRequest (MsgType 2) supported for gap recovery
- SequenceReset-GapFill for administrative messages
- Sequence reset on daily logon (configurable: reset or continue)

### 13.2.4 Error Handling
- Syntax errors: Session-level Reject (MsgType 3) with reason
- Business errors: BusinessMessageReject (MsgType j) with error code
- Order errors: ExecutionReport with OrdStatus=Rejected and text description
- All rejections include machine-readable error code and human-readable text

## 13.3 Binary Order Entry Interface

### 13.3.1 Message Schema (SBE)

**Header** (common to all messages):
```
Field           | Type    | Size | Description
─────────────────────────────────────────────────
BlockLength     | uint16  | 2    | Message body length
TemplateId      | uint16  | 2    | Message type identifier
SchemaId        | uint16  | 2    | Schema identifier
Version         | uint16  | 2    | Schema version
SequenceNumber  | uint64  | 8    | Session sequence number
SendingTime     | uint64  | 8    | Nanosecond timestamp
```

**NewOrder message** (example):
```
Field           | Type    | Size | Description
─────────────────────────────────────────────────
ClientOrderId   | uint64  | 8    | Client-assigned order ID
InstrumentId    | uint32  | 4    | Venue instrument ID
Side            | uint8   | 1    | 1=Buy, 2=Sell
OrderType       | uint8   | 1    | 1=Market, 2=Limit, 3=Stop, ...
TimeInForce     | uint8   | 1    | 0=Day, 1=GTC, 3=IOC, 4=FOK, ...
Price           | int64   | 8    | Price × multiplier (integer)
Quantity        | uint64  | 8    | Order quantity
DisplayQty      | uint64  | 8    | Iceberg display quantity (0=full)
StopPrice       | int64   | 8    | Stop trigger price (0=N/A)
STPGroupId      | uint32  | 4    | Self-trade prevention group
AccountId       | uint32  | 4    | Trading account
Flags           | uint16  | 2    | Bit flags (post-only, reduce-only, CoD)
─────────────────────────────────────────────────
Total: 65 bytes + 24 header = 89 bytes
```

### 13.3.2 Versioning Strategy
- SBE schema versioned; old versions supported for backward compatibility period (minimum 6 months)
- New fields added at end of message (SBE extension mechanism)
- Breaking changes require new TemplateId and migration period

## 13.4 Market Data Interface

### 13.4.1 Multicast Channel Layout

| Channel | Content | Update Rate | Instruments |
|---------|---------|-------------|-------------|
| Channel 1–N | Full depth (incremental) | Every book change | Partitioned by instrument group |
| Channel N+1–M | Trades | Every execution | Partitioned by instrument group |
| Channel M+1 | Instrument status | State changes | All instruments |
| Channel M+2 | Auction data | Periodic (1/sec) | Active auctions only |
| Channel M+3 | Statistics | Periodic (1/sec) | All instruments |

### 13.4.2 Incremental Message Types

| Message | Description | Key Fields |
|---------|-------------|------------|
| OrderBookUpdate | Price level add/change/delete | InstrumentId, Side, Price, Qty, NumOrders, Action(Add/Change/Delete) |
| TradeMessage | Execution occurred | InstrumentId, Price, Qty, AggressorSide, TradeId, Timestamp |
| InstrumentStatus | State change | InstrumentId, NewState, Reason, Timestamp |
| AuctionInfo | Auction indicative data | InstrumentId, IndicativePrice, IndicativeVolume, ImbalanceSide, ImbalanceQty |
| Statistics | Periodic statistics | InstrumentId, High, Low, Last, VWAP, Volume, TradeCount, Turnover |
| Heartbeat | Channel liveness | ChannelId, SequenceNumber, Timestamp |

### 13.4.3 Snapshot Recovery Protocol
1. Client detects gap (sequence number skip)
2. Client sends SnapshotRequest (TCP channel) with InstrumentId
3. Server responds with full book snapshot including current sequence number
4. Client applies snapshot, then resumes incremental updates from snapshot sequence
5. Rate limit: max 10 snapshot requests per second per client

## 13.5 Clearing Adapter Interface

### 13.5.1 Purpose
Submit executed trades to the central counterparty (CCP) for clearing.

### 13.5.2 Adapter Design
- **Protocol**: FIX 4.4/5.0SP2 or CCP-proprietary protocol (adapter pattern)
- **Content**: Trade details (instrument, price, quantity, counterparties, clearing members, accounts)
- **Timing**: Real-time (within 1 second of execution) or batch (end-of-session)
- **Acknowledgment**: CCP confirms acceptance of each trade; platform tracks clearing status
- **Rejection handling**: Rejected trades flagged for manual resolution
- **Netting**: CCP performs netting; platform receives net position updates

### 13.5.3 Give-Up / Take-Up
- Executing broker submits trade
- Trade allocated to clearing broker (give-up)
- Clearing broker accepts or rejects allocation
- Accepted trades cleared under clearing broker's account
- Workflow managed by post-trade service; adapter communicates with CCP

## 13.6 Surveillance Export Interface

### 13.6.1 Data Format
- **Schema**: Apache Avro or Protocol Buffers (schema-evolving, compact)
- **Transport**: Apache Kafka topic or dedicated TCP stream
- **Content**: Normalized order lifecycle events, execution events, session events
- **Sequencing**: Events ordered by exchange sequence number
- **Enrichment**: Events enriched with participant name, instrument details, account info

### 13.6.2 Event Types Exported

| Event | Key Fields |
|-------|------------|
| OrderEntry | OrderId, ClientOrderId, ParticipantId, InstrumentId, Side, Type, Price, Qty, Timestamp |
| OrderModify | OrderId, OldPrice, NewPrice, OldQty, NewQty, Timestamp |
| OrderCancel | OrderId, Reason, Timestamp |
| OrderReject | ClientOrderId, RejectReason, Timestamp |
| Execution | ExecId, TradeId, BuyOrderId, SellOrderId, Price, Qty, Timestamp |
| QuoteUpdate | ParticipantId, InstrumentId, BidPrice, BidQty, AskPrice, AskQty, Timestamp |
| SessionEvent | SessionId, EventType(Logon/Logout/Disconnect), Timestamp |
| InstrumentEvent | InstrumentId, EventType(Halt/Resume/StateChange), Timestamp |
| RiskEvent | ParticipantId, EventType(LimitBreach/KillSwitch), Details, Timestamp |

## 13.7 Admin / Operations API

### 13.7.1 API Style
- **Protocol**: REST over HTTPS (JSON)
- **Authentication**: OAuth 2.0 with JWT tokens
- **Authorization**: RBAC with fine-grained permissions per endpoint
- **Versioning**: URL path versioning (e.g., /api/v1/)
- **Rate Limiting**: Per-user rate limits; admin APIs are not latency-critical

### 13.7.2 Endpoint Groups

| Group | Endpoints | Description |
|-------|-----------|-------------|
| /instruments | CRUD + state management | Manage instruments, change states, update parameters |
| /sessions | Start, stop, schedule | Manage trading sessions |
| /participants | CRUD + entitlements | Manage participant records |
| /risk | Limits CRUD + kill switch | Manage risk parameters and emergency controls |
| /trades | Query + bust + correct | Trade management operations |
| /orders | Query + cancel | Order inquiry and mass cancel |
| /config | Parameters + rules | Venue configuration management |
| /audit | Query | Audit trail query and export |
| /monitoring | Health + metrics | System health and performance data |

### 13.7.3 Change Management
- All mutating operations require authentication + authorization
- Sensitive operations (trade bust, kill switch, parameter changes) require four-eyes approval
- All changes recorded in audit trail with operator identity and timestamp
- Changes can be scheduled for future effective time
