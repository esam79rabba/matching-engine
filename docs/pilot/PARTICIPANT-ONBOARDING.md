# Pilot Participant Onboarding Guide

**Version**: 1.0 | **Date**: 2026-03-25
**Applies to**: FR-1 prototype pilot
**Audience**: External pilot participant integration team

---

## Overview

This guide covers connecting to and trading on the FR-1 matching engine prototype as a pilot participant. The platform supports FIX 4.4 order entry with optional TLS encryption.

**This is a prototype pilot.** See the [Limitations](#known-limitations) section before beginning integration.

---

## 1. Prerequisites

| Item | Requirement |
|------|-------------|
| Network | TCP connectivity to venue host on ports 9876 (FIX), 9877 (market data), 8080 (ops/status) |
| FIX client | FIX 4.4 compliant client (QuickFIX, custom, or vendor OMS) |
| Credentials | SenderCompID and API key provided by venue operations |
| TLS (if enabled) | TLS 1.2+ client capability; venue CA certificate if mTLS required |

## 2. Connection Details

| Service | Port | Protocol | Description |
|---------|:----:|----------|-------------|
| FIX Gateway | 9876 | TCP or TLS (FIX 4.4) | Order entry, cancel, modify |
| Market Data | 9877 | TCP (JSON, newline-delimited) | Top-of-book, trades, instrument status |
| Operations | 8080 | HTTP | Health check, instrument status, metrics |

## 3. FIX Session Setup

### 3.1 Logon (35=A)

Send a standard FIX 4.4 Logon message:

```
8=FIX.4.4|35=A|49=<your_sender_comp_id>|56=VENUE|34=1|108=30|98=0|10=xxx|
```

| Tag | Value | Notes |
|:---:|-------|-------|
| 49 | Your SenderCompID | Provided during onboarding |
| 56 | VENUE | Target (venue side) |
| 34 | MsgSeqNum | Start at 1 for new session, or persisted value for reconnect |
| 108 | HeartBtInt | Heartbeat interval in seconds (30 recommended) |
| 98 | EncryptMethod | 0 = None (TLS handled at transport layer) |

**Expected response**: Logon ack (35=A) from the venue with the venue's outbound sequence number.

### 3.2 Session Sequence Numbers

- Sequence numbers persist across restarts
- If you reconnect with a sequence number higher than the venue expects, the venue sends a ResendRequest (35=2)
- If the venue detects a gap, it sends ResendRequest for the missing range
- Respond to ResendRequest with SequenceReset-GapFill (35=4, 123=Y) or replay stored messages

### 3.3 Heartbeat / TestRequest

- Send Heartbeat (35=0) every `HeartBtInt` seconds
- Respond to TestRequest (35=1) with Heartbeat

### 3.4 TLS Configuration

If TLS is enabled on the venue:
- Connect using TLS 1.2 or higher
- The venue will present its server certificate during handshake
- If mTLS is enabled, prepare your client certificate signed by the venue CA

## 4. Supported Order Types

### 4.1 NewOrderSingle (35=D)

```
8=FIX.4.4|35=D|49=<sender>|56=VENUE|34=<seq>|
11=<client_order_id>|55=<symbol>|54=<side>|40=<ord_type>|
44=<price>|38=<quantity>|59=<time_in_force>|10=xxx|
```

| Tag | Field | Values |
|:---:|-------|--------|
| 11 | ClOrdID | Unique per session (your reference) |
| 55 | Symbol | Instrument symbol (e.g., "BTC-USD", "ETH-USD") |
| 54 | Side | 1=Buy, 2=Sell |
| 40 | OrdType | 1=Market, 2=Limit |
| 44 | Price | Limit price (required for Limit orders) |
| 38 | OrderQty | Quantity |
| 59 | TimeInForce | 0=Day, 1=GTC, 3=IOC, 4=FOK, 6=GTD |

### 4.2 OrderCancelRequest (35=F)

```
8=FIX.4.4|35=F|49=<sender>|56=VENUE|34=<seq>|
11=<cancel_cl_ord_id>|41=<venue_order_id>|55=<symbol>|54=<side>|10=xxx|
```

| Tag | Field | Notes |
|:---:|-------|-------|
| 11 | ClOrdID | New unique ID for the cancel request |
| 41 | OrigClOrdID | The venue OrderID (tag 37) from the original order ack |

### 4.3 Execution Reports (35=8)

The venue sends ExecutionReports for all order lifecycle events:

| ExecType (150) | Meaning |
|:--------------:|---------|
| 0 | New — order accepted and resting |
| 4 | Canceled — order canceled |
| 8 | Rejected — order rejected (see RejectCode) |
| F | Trade — fill executed |

Key tags in ExecutionReport:
- **37** (OrderID): Venue-assigned order identifier
- **39** (OrdStatus): Current order status
- **31** (LastPx): Fill price
- **32** (LastQty): Fill quantity
- **14** (CumQty): Cumulative filled quantity
- **151** (LeavesQty): Remaining quantity

## 5. Available Instruments

Instruments are loaded from the venue's reference data configuration. For the pilot:

| Symbol | InstrumentId | Description |
|--------|:------------:|-------------|
| BTC-USD | 100 | Bitcoin / US Dollar Spot |
| ETH-USD | 101 | Ethereum / US Dollar Spot |

Query the current instrument list via HTTP:
```bash
curl http://<venue_host>:8080/api/v1/instruments
```

## 6. Market Data Feed

Connect to port 9877 to receive JSON-encoded market data:

```json
{"type":"trade","instrument_id":100,"price":50000,"qty":10,"side":"buy","seq":1}
{"type":"top_of_book","instrument_id":100,"bid":49999,"ask":50001,"seq":2}
{"type":"status","instrument_id":100,"state":"CONTINUOUS","seq":3}
```

Each event is a newline-delimited JSON object. No authentication required for the market data feed in the prototype.

## 7. Drop Copy

Drop copy events are delivered to connected participants showing all orders and trades for their session.

## 8. Operations API

| Endpoint | Method | Description | Auth Required |
|----------|:------:|-------------|:-------------:|
| `/health` | GET | Health check | No |
| `/api/v1/instruments` | GET | List instruments | No |
| `/api/v1/metrics` | GET | Prometheus metrics | No |
| `/api/v1/instruments/{id}/halt` | POST | Halt instrument | Yes (API key) |
| `/api/v1/instruments/{id}/resume` | POST | Resume instrument | Yes (API key) |

Authenticated requests require: `Authorization: Bearer <api_key>` or `X-API-Key: <api_key>`

## 9. Common Reject Reasons

| Scenario | Expected Response |
|----------|-------------------|
| Invalid instrument symbol | 35=8, 150=8 (Rejected) |
| Instrument halted | 35=8, 150=8 with RejectCode |
| Price outside band | 35=8, 150=8 (fat-finger reject from risk) |
| Quantity exceeds max | 35=8, 150=8 (risk reject) |
| Rate limit exceeded | 35=8, 150=8 (throttle reject) |
| Kill switch active | 35=8, 150=8 (participant blocked) |
| Unknown OrderID on cancel | 35=8, 150=8 |

## 10. Order Lifecycle Example

```
1. Participant sends:  35=D (NewOrderSingle) Buy 10 BTC-USD @ 50000
2. Venue responds:     35=8, 150=0 (New) — order accepted, resting

3. Counterparty sends: 35=D (NewOrderSingle) Sell 10 BTC-USD @ 50000
4. Venue sends to both:
   - To buyer:  35=8, 150=F (Trade) — fill at 50000, qty=10
   - To seller: 35=8, 150=F (Trade) — fill at 50000, qty=10

5. Market data published: trade at 50000, qty=10
```

## 11. Known Limitations

| Area | Limitation |
|------|-----------|
| **FIX gap recovery** | Bounded: venue replays stored outbound messages; older messages are gap-filled. No full journal-to-FIX replay. |
| **Order types** | Market, Limit only. No iceberg, pegged, or stop orders. |
| **Auctions** | Not implemented. Continuous trading only. |
| **Modify** | OrderCancelReplaceRequest (35=G) support is basic. |
| **Market data** | TCP/JSON only. No multicast UDP or SBE encoding. |
| **DR/HA** | Single-site warm restart. No DR failover. |
| **Auth** | FIX session auth via CompID only. No cryptographic session auth. |
| **TLS** | Server TLS available. mTLS not enforced in pilot. |
| **STP** | Cancel-newest mode only. |
| **Performance** | Prototype on dev hardware. Not benchmarked for production load. |

## 12. Support

- **Ops console**: `http://<venue_host>:8080/health`
- **Metrics**: `http://<venue_host>:8080/api/v1/metrics`
- **Contact**: Venue operations team
- **Runbooks**: Available in `docs/runbooks/` directory
