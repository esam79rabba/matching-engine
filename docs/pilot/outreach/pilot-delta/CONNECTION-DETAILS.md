# Connection Details — Pilot Delta Trading

**Classification**: Confidential — share only with Pilot Delta Trading integration team
**Valid for**: Controlled pilot session, 2026-03-31

---

## FIX Session

| Parameter | Value |
|-----------|-------|
| Protocol | FIX 4.4 |
| Transport | TLS v1.2+ (CA-signed certificate) |
| Host | pilot.yourdomain.com |
| Port | 19876 |
| SenderCompID | PARTICIPANT_6 |
| TargetCompID | VENUE |
| HeartBtInt | 30 seconds |
| EncryptMethod | 0 (TLS at transport layer) |

## Instruments

| Symbol | ID | Description |
|--------|:--:|-------------|
| BTC-USD | 100 | Bitcoin / US Dollar Spot |
| ETH-USD | 101 | Ethereum / US Dollar Spot |

## Session Schedule (GST)

| State | Time |
|-------|:----:|
| PRE_OPEN | 08:00 |
| CONTINUOUS | 09:00 |
| CLOSED | 17:00 |

## Additional Services

| Service | Port | Protocol | Auth |
|---------|:----:|----------|:----:|
| Market Data | 19877 | TCP (JSON, newline-delimited) | None |
| Ops Console | 18080 | HTTP (read-only endpoints) | None |

## Risk Limits

| Parameter | Value |
|-----------|:-----:|
| Max order rate | 500/sec |
| Fat-finger price deviation | 10% from reference |
| Max notional | 5,000,000 |

## Reconnection

After venue restart, reconnect with your current MsgSeqNum. The venue persists session state and will resume from the last known sequence.

## Support

| Channel | Contact |
|---------|---------|
| Primary | [YOUR CONTACT — fill before sending] |
