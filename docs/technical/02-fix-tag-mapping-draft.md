# FIX Tag Mapping — Draft Outline

**Status**: Starter artifact — decision-independent
**Source**: §13 External Interfaces (frozen baseline)
**FIX Version**: FIX 4.4 (FR-1); FIX 5.0SP2 (Phase 2+)
**Note**: This is a starter mapping outline. Final custom tags, session rules, and message scope remain subject to FR-1 scope, participant certification, and venue-profile refinement.

---

## Supported FIX Messages (FR-1)

### Inbound (Participant → Venue)

| MsgType | Name | Key Tags |
|:-------:|------|----------|
| D | NewOrderSingle | ClOrdID(11), Symbol(55), Side(54), OrdType(40), Price(44), OrderQty(38), TimeInForce(59), Account(1) |
| F | OrderCancelRequest | OrigClOrdID(41), ClOrdID(11), Symbol(55), Side(54) |
| G | OrderCancelReplaceRequest | OrigClOrdID(41), ClOrdID(11), Symbol(55), Side(54), Price(44), OrderQty(38) |
| q | OrderMassCancelRequest | MassCancelRequestType(530), Symbol(55), Side(54) |
| A | Logon | EncryptMethod(98), HeartBtInt(108), Username(553), Password(554) |
| 5 | Logout | Text(58) |
| 0 | Heartbeat | TestReqID(112) |
| 1 | TestRequest | TestReqID(112) |
| 2 | ResendRequest | BeginSeqNo(7), EndSeqNo(16) |
| 4 | SequenceReset | GapFillFlag(123), NewSeqNo(36) |

### Outbound (Venue → Participant)

| MsgType | Name | Key Tags |
|:-------:|------|----------|
| 8 | ExecutionReport | OrderID(37), ClOrdID(11), ExecID(17), ExecType(150), OrdStatus(39), Symbol(55), Side(54), Price(44), LastPx(31), LastQty(32), CumQty(14), LeavesQty(151), TransactTime(60) |
| 9 | OrderCancelReject | OrderID(37), ClOrdID(11), OrigClOrdID(41), CxlRejReason(102) |
| 3 | Reject | RefSeqNum(45), RefMsgType(372), SessionRejectReason(373), Text(58) |
| j | BusinessMessageReject | RefMsgType(372), BusinessRejectReason(380), Text(58) |

## Custom Tags (Venue-Specific)

| Tag | Name | Type | Description |
|:---:|------|------|-------------|
| 20001 | STPGroupID | String | Self-trade prevention group |
| 20002 | CancelOnDisconnect | Boolean | Y/N — enable CoD for this session |
| 20003 | DisplayQty | Qty | Iceberg display quantity |
| 20004 | PostOnlyFlag | Boolean | Y/N — post-only order |
| 20005 | PartitionID | Int | Execution partition (informational, outbound only) |
| 20006 | LiquidityIndicator | Char | M=Maker, T=Taker (in ExecutionReport) |

## Tag-to-Internal Mapping

| FIX Tag | Internal Field | Transformation |
|:-------:|----------------|----------------|
| ClOrdID (11) | ClientOrderId | String → uint64 parse |
| Symbol (55) | InstrumentId | Symbol lookup → uint32 ID |
| Side (54) | Side | '1'→Buy(1), '2'→Sell(2) |
| OrdType (40) | OrderType | '1'→Market, '2'→Limit, '3'→Stop, '4'→StopLimit |
| Price (44) | Price | Decimal → integer (multiply by instrument price_multiplier) |
| OrderQty (38) | Quantity | Decimal → integer (multiply by instrument qty_multiplier) |
| TimeInForce (59) | TimeInForce | '0'→Day, '1'→GTC, '3'→IOC, '4'→FOK, '6'→GTD |
| Account (1) | AccountId | Account string → uint32 lookup |
| TransactTime (60) | Timestamp | UTC string → nanosecond epoch |

## Sequence Management

- Session sequence numbers maintained per session (CompID pair)
- Gap detected via sequence mismatch → ResendRequest
- Daily sequence reset on Logon (configurable: reset or continue)
- Duplicate ClOrdID within session → Reject (BusinessMessageReject)

## Rejection Codes

| Code | Meaning |
|:----:|---------|
| 1 | Unknown instrument |
| 2 | Invalid order type for instrument state |
| 3 | Price outside bands |
| 4 | Quantity exceeds max |
| 5 | Invalid price tick |
| 6 | Invalid quantity lot |
| 7 | Duplicate ClOrdID |
| 8 | Rate limit exceeded |
| 9 | Risk check failed |
| 10 | Account not found |
| 11 | Instrument halted/suspended |
| 99 | Internal error |
