# Connection Details — VersiFi Pilot

**Classification**: Confidential — share only with VersiFi integration team
**Valid for**: Controlled pilot session only

---

## FIX Session

| Parameter | Value |
|-----------|-------|
| Protocol | FIX 4.4 |
| Transport | TLS v1.2+ (certificate attached) |
| Host | [VENUE HOST IP — fill before sending] |
| Port | 9876 |
| SenderCompID | VERSIFI |
| TargetCompID | VENUE |
| HeartBtInt | 30 seconds |
| EncryptMethod | 0 (TLS at transport layer) |

## Instruments

| Symbol | ID | Description | Tick Size | Lot Size |
|--------|:--:|-------------|:---------:|:--------:|
| BTC-USD | 100 | Bitcoin / US Dollar Spot | 1 | 1 |
| ETH-USD | 101 | Ethereum / US Dollar Spot | 1 | 1 |

## Session Schedule

| State | Time (GST) |
|-------|:----------:|
| PRE_OPEN | 08:00 |
| CONTINUOUS | 09:00 |
| CLOSED | 17:00 |

## Supported Messages (Inbound)

| MsgType | Name | Notes |
|:-------:|------|-------|
| A | Logon | Session initiation |
| D | NewOrderSingle | Limit and Market orders |
| F | OrderCancelRequest | Use tag 37 value from ack in tag 41 |
| 0 | Heartbeat | Session keepalive |

## Response Messages (Outbound)

| MsgType | Name | Key Tags |
|:-------:|------|----------|
| A | Logon | Session confirmation |
| 8 | ExecutionReport | 150 (ExecType), 37 (OrderID), 31 (LastPx), 32 (LastQty) |

## Additional Feeds

| Service | Port | Protocol | Auth |
|---------|:----:|----------|:----:|
| Market Data | 9877 | TCP (JSON, newline-delimited) | None |
| Ops Console | 8180 | HTTP (read-only endpoints) | None |

## Risk Limits (Configured for VersiFi)

| Parameter | Value |
|-----------|:-----:|
| Max order rate | 500/sec |
| Fat-finger price deviation | 10% from reference |
| Max notional | 5,000,000 |

## Known Limitations

1. Prototype on development hardware — not production-certified performance
2. No DR/failover — single-site warm restart only
3. FIX gap recovery is bounded (stored messages replayed, older gap-filled)
4. No auction — continuous trading only
5. Market data is TCP/JSON — top-of-book quantity may show 0
6. EOD reports triggered manually by venue operator
7. Cancel must use exact venue OrderID from tag 37 in tag 41
8. Self-signed TLS certificate (pilot only)

## Support During Pilot

| Channel | Contact |
|---------|---------|
| Primary | [OPERATOR CONTACT — fill before sending] |
| Escalation | [ENGINEERING CONTACT — fill before sending] |
