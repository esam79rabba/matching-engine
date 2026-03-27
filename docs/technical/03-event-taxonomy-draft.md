# Internal Event Taxonomy — Draft

**Status**: Starter artifact — decision-independent
**Source**: §17 Data, Audit, and Reporting (frozen baseline)

---

## Event Envelope (all events)

| Field | Type | Size | Description |
|-------|------|:----:|-------------|
| EventId | uint64 | 8 | Logical globally unique identifier derived from PartitionId + SequenceNumber. Representation is implementation-dependent (e.g., bit-packed as [partition_id:16][sequence:48], or computed as a hash). The combination of PartitionId + SequenceNumber in the envelope is the authoritative unique key. |
| PartitionId | uint16 | 2 | Source partition |
| SequenceNumber | uint64 | 8 | Partition-local monotonic |
| Timestamp | uint64 | 8 | Nanosecond UTC (PTP) |
| EventType | uint16 | 2 | Type code from catalog below |
| Version | uint8 | 1 | Event schema version |
| PayloadLength | uint32 | 4 | Payload bytes |
| Checksum | uint32 | 4 | CRC32 of envelope + payload |

## Event Type Catalog

### Order Events (100–199)

| Code | Name | Trigger | Key Payload Fields |
|:----:|------|---------|-------------------|
| 100 | OrderNew | New order accepted and inserted into book | OrderId, ClientOrderId, ParticipantId, InstrumentId, Side, Type, Price, Qty |
| 101 | OrderModify | Order price or quantity modified | OrderId, OldPrice, NewPrice, OldQty, NewQty |
| 102 | OrderCancel | Order cancelled (participant request) | OrderId, Reason=PARTICIPANT_REQUEST |
| 103 | OrderCancelCoD | Order cancelled (cancel-on-disconnect) | OrderId, SessionId, Reason=CANCEL_ON_DISCONNECT |
| 104 | OrderExpire | Order expired (GTD/GTC max lifetime) | OrderId, Reason=EXPIRED |
| 105 | OrderReject | Order rejected (validation/risk) | ClientOrderId, RejectCode, RejectText |
| 106 | OrderTriggered | Stop order triggered | OrderId, TriggerPrice, ConvertedType |
| 107 | OrderSTPCancel | Order cancelled by self-trade prevention | OrderId, CounterOrderId, STPMode |

### Execution Events (200–299)

| Code | Name | Trigger | Key Payload Fields |
|:----:|------|---------|-------------------|
| 200 | ExecutionNew | Fill generated | ExecId, TradeId, BuyOrderId, SellOrderId, InstrumentId, Price, Qty, AggressorSide |
| 201 | TradeBust | Trade cancelled by operator | TradeId, OrigExecId, OperatorId, Reason |
| 202 | TradeCorrection | Trade price corrected | TradeId, OrigPrice, CorrectedPrice, OperatorId |

### Market State Events (300–399)

| Code | Name | Trigger | Key Payload Fields |
|:----:|------|---------|-------------------|
| 300 | InstrumentStateChange | State transition | InstrumentId, OldState, NewState, Reason |
| 301 | AuctionStart | Auction phase begins | InstrumentId, AuctionType |
| 302 | AuctionEnd | Auction completes | InstrumentId, EquilibriumPrice, ExecutedVolume, Imbalance |
| 303 | CircuitBreakerTrigger | Volatility threshold breached | InstrumentId, TriggerType(STATIC/DYNAMIC), TriggerPrice, ThresholdPct |
| 304 | SessionPhaseChange | Trading session phase transition | SessionId, OldPhase, NewPhase |

### Risk Events (400–499)

| Code | Name | Trigger | Key Payload Fields |
|:----:|------|---------|-------------------|
| 400 | RiskLimitBreach | Pre-trade limit exceeded | ParticipantId, LimitType, LimitValue, OrderValue |
| 401 | KillSwitchActivation | Kill switch triggered | Scope(PARTICIPANT/INSTRUMENT/MARKET), TargetId, OperatorId |
| 402 | ThrottleReject | Message rate exceeded | SessionId, Rate, Limit |

### Connectivity Events (500–599)

| Code | Name | Trigger | Key Payload Fields |
|:----:|------|---------|-------------------|
| 500 | SessionLogon | Participant session established | SessionId, ParticipantId, GatewayId, Protocol |
| 501 | SessionLogout | Participant session terminated (clean) | SessionId, Reason |
| 502 | SessionDisconnect | Participant session lost | SessionId, HeartbeatsMissed, CoDTriggered |

### Configuration Events (600–699)

| Code | Name | Trigger | Key Payload Fields |
|:----:|------|---------|-------------------|
| 600 | ParameterChange | Venue parameter updated | ParameterName, OldValue, NewValue, OperatorId, ApproverOperatorId |
| 601 | InstrumentUpdate | Instrument reference data changed | InstrumentId, FieldChanged, OldValue, NewValue |

## Event Consumers

| Consumer | Events Consumed | Delivery |
|----------|----------------|----------|
| Market Data Publisher | 100, 101, 102, 200, 300–304 | Real-time (output bus) |
| Drop Copy Gateway | 100–107, 200 | Real-time (output bus) |
| Post-Trade Service | 200, 201, 202 | Real-time (output bus) |
| Surveillance Export | All (100–601) | Real-time stream (Kafka) |
| Audit Trail Service | All (100–601) | Journal (primary source) |
| Risk State Updates | 200, 400–402 | Real-time (output bus) |
| Operations Console | 300–304, 400–402, 600–601 | Near-real-time |

## Versioning

- All events carry a Version field (uint8)
- Backward-compatible: add optional fields at end (minor version)
- Breaking changes: new EventType code (major version, old code retained for transition)
