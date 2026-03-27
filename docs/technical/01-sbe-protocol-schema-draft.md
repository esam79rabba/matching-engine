# SBE Binary Protocol Schema — Draft Skeleton

**Status**: Starter artifact — language-agnostic, decision-independent
**Source**: §13 External Interfaces (frozen baseline)
**Note**: This is a starter protocol skeleton for planning and interface design. The final message catalog, field set, and phase scope remain subject to FR-1 product scope and participant certification refinement.

---

## Message Header (all messages)

| Field | Type | Size | Description |
|-------|------|:----:|-------------|
| BlockLength | uint16 | 2 | Message body length |
| TemplateId | uint16 | 2 | Message type (see catalog below) |
| SchemaId | uint16 | 2 | Schema identifier (1 for v1) |
| Version | uint16 | 2 | Schema version |
| SequenceNumber | uint64 | 8 | Session sequence number |
| SendingTime | uint64 | 8 | Nanosecond UTC timestamp |
| **Total header** | | **24** | |

## Message Catalog

| TemplateId | Name | Direction | Description |
|:----------:|------|:---------:|-------------|
| 1 | NewOrder | Inbound | Submit new order |
| 2 | CancelOrder | Inbound | Cancel resting order |
| 3 | ModifyOrder | Inbound | Modify price/qty |
| 4 | MassCancelRequest | Inbound | Cancel by criteria |
| 10 | ExecutionReport | Outbound | Ack, fill, cancel confirm, reject |
| 11 | CancelReject | Outbound | Cancel/modify rejection |
| 20 | Logon | Bidirectional | Session establishment |
| 21 | Logout | Bidirectional | Session termination |
| 22 | Heartbeat | Bidirectional | Liveness check |
| 30 | MassQuote | Inbound | Mass quote (Phase 2) |
| 31 | MassQuoteAck | Outbound | Mass quote ack (Phase 2) |

## NewOrder (TemplateId = 1)

| Field | Type | Size | Description |
|-------|------|:----:|-------------|
| ClientOrderId | uint64 | 8 | Client-assigned ID |
| InstrumentId | uint32 | 4 | Venue instrument ID |
| Side | uint8 | 1 | 1=Buy, 2=Sell |
| OrderType | uint8 | 1 | 1=Market, 2=Limit, 3=Stop, 4=StopLimit |
| TimeInForce | uint8 | 1 | 0=Day, 1=GTC, 2=GTD, 3=IOC, 4=FOK |
| Price | int64 | 8 | Price x multiplier (integer) |
| Quantity | uint64 | 8 | Order quantity |
| DisplayQty | uint64 | 8 | Iceberg display (0=full) |
| StopPrice | int64 | 8 | Stop trigger (0=N/A) |
| STPGroupId | uint32 | 4 | Self-trade prevention group |
| AccountId | uint32 | 4 | Trading account |
| Flags | uint16 | 2 | Bit flags: PostOnly(0x01), ReduceOnly(0x02), CoD(0x04) |
| **Total body** | | **57** | + 24 header = **81 bytes** |

## CancelOrder (TemplateId = 2)

| Field | Type | Size | Description |
|-------|------|:----:|-------------|
| OrigOrderId | uint64 | 8 | Venue-assigned order ID to cancel |
| ClientOrderId | uint64 | 8 | Client's cancel request ID |
| InstrumentId | uint32 | 4 | Instrument (for routing) |
| **Total body** | | **20** | + 24 header = **44 bytes** |

## ModifyOrder (TemplateId = 3)

| Field | Type | Size | Description |
|-------|------|:----:|-------------|
| OrigOrderId | uint64 | 8 | Venue order ID to modify |
| ClientOrderId | uint64 | 8 | Client's modify request ID |
| InstrumentId | uint32 | 4 | Instrument |
| NewPrice | int64 | 8 | New price (0=unchanged) |
| NewQuantity | uint64 | 8 | New quantity (0=unchanged) |
| **Total body** | | **36** | + 24 header = **60 bytes** |

## ExecutionReport (TemplateId = 10)

| Field | Type | Size | Description |
|-------|------|:----:|-------------|
| OrderId | uint64 | 8 | Venue-assigned order ID |
| ClientOrderId | uint64 | 8 | Client-assigned ID |
| ExecId | uint64 | 8 | Execution ID (unique per fill) |
| TradeId | uint64 | 8 | Trade ID (links buyer+seller) |
| InstrumentId | uint32 | 4 | Instrument |
| Side | uint8 | 1 | 1=Buy, 2=Sell |
| OrdStatus | uint8 | 1 | 0=New, 1=PartFill, 2=Fill, 3=Cancel, 4=Reject, 5=Expired |
| ExecType | uint8 | 1 | 0=New, 1=Trade, 2=Cancel, 3=Modify, 4=Reject |
| Price | int64 | 8 | Execution price (for fills) |
| LastQty | uint64 | 8 | Last fill quantity |
| CumQty | uint64 | 8 | Cumulative filled qty |
| LeavesQty | uint64 | 8 | Remaining qty |
| LiquidityInd | uint8 | 1 | 0=Maker, 1=Taker |
| RejectReason | uint16 | 2 | Reject code (0=none) |
| TransactTime | uint64 | 8 | Exchange timestamp (nanos) |
| **Total body** | | **82** | + 24 header = **106 bytes** |

## Enum Definitions

### Side
| Value | Name |
|:-----:|------|
| 1 | Buy |
| 2 | Sell |

### OrderType
| Value | Name |
|:-----:|------|
| 1 | Market |
| 2 | Limit |
| 3 | Stop |
| 4 | StopLimit |
| 5 | Pegged (Phase 2) |

### TimeInForce
| Value | Name |
|:-----:|------|
| 0 | Day |
| 1 | GTC |
| 2 | GTD |
| 3 | IOC |
| 4 | FOK |
| 5 | AuctionOnly |

### OrdStatus
| Value | Name |
|:-----:|------|
| 0 | New |
| 1 | PartiallyFilled |
| 2 | Filled |
| 3 | Cancelled |
| 4 | Rejected |
| 5 | Expired |
| 6 | Suspended |

### Flags (bitfield)
| Bit | Name |
|:---:|------|
| 0x01 | PostOnly |
| 0x02 | ReduceOnly |
| 0x04 | CancelOnDisconnect |

## Versioning Strategy

- Schema version in header; old versions supported for 6 months after deprecation
- New fields added at end of message body (SBE extension mechanism)
- Breaking changes require new TemplateId
