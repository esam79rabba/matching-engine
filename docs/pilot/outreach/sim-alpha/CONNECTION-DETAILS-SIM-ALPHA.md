# Connection Details — SIM_PARTICIPANT_ALPHA

## FIX Session

| Parameter | Value |
|-----------|-------|
| Protocol | FIX 4.4 |
| Transport | TLS v1.2+ |
| Host | 127.0.0.1 |
| Port | 9876 |
| SenderCompID | SIM_ALPHA |
| TargetCompID | VENUE |
| HeartBtInt | 30 seconds |

## Instruments

| Symbol | ID | Description |
|--------|:--:|-------------|
| BTC-USD | 100 | Bitcoin / US Dollar Spot |
| ETH-USD | 101 | Ethereum / US Dollar Spot |

## Session Schedule

| State | Time (GST) |
|-------|:----------:|
| PRE_OPEN | 08:00 |
| CONTINUOUS | 09:00 |
| CLOSED | 17:00 |

## Risk Limits

| Parameter | Value |
|-----------|:-----:|
| Max order rate | 500/sec |
| Fat-finger price deviation | 10% |
| Max notional | 5,000,000 |

## Support

| Channel | Contact |
|---------|---------|
| Primary | Internal simulation — same operator |
