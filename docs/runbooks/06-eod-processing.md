# Runbook: End-of-Day Processing

**Audience**: Operator / Engineer
**Last updated**: 2026-03-25
**Applies to**: FR-1 prototype pilot

---

## When to Run

- After trading session closes (instruments transition to CLOSED)
- Before system shutdown at end of trading day
- On demand for reporting

## Prerequisites

- All instruments in CLOSED state
- No pending order activity
- EOD output directory configured: `deploy/data/eod/`

## EOD Processing

### What EOD Produces

| Output File | Contents |
|------------|----------|
| `trade_summary_YYYYMMDD.csv` | Per-instrument trade aggregation: OHLC, volume, trade count |
| `fee_summary_YYYYMMDD.csv` | Per-instrument fee totals: maker fees, taker fees |
| `daily_report_YYYYMMDD.txt` | Human-readable summary with all instruments |

### EOD Flow

1. **Session close**: Instruments transition to CLOSED state
   - Session scheduler triggers this automatically if configured
   - Manual: `curl -X POST http://localhost:8080/ops/halt -d '{"instrument_id": 100}'`

2. **Day-order cleanup**: All Day orders are automatically cancelled on session close

3. **EOD aggregation**: EodProcessor computes:
   - OHLC (Open, High, Low, Close) prices per instrument
   - Total volume and trade count
   - Fee calculations (maker/taker based on configured fee schedule)

4. **File output**: EodFileWriter generates CSV and text files

### Verify EOD Output

```bash
ls deploy/data/eod/
# Should show:
# trade_summary_20260325.csv
# fee_summary_20260325.csv
# daily_report_20260325.txt

# Check trade summary
head deploy/data/eod/trade_summary_20260325.csv
# instrument_id,symbol,open,high,low,close,volume,trade_count
```

## GTC/GTD Order Handling

| Order Type | Behavior at Session Close |
|-----------|:-------------------------:|
| Day | Cancelled automatically |
| GTC | Persists through session close |
| GTD | Persists if expiry not reached; cancelled if expired |
| IOC/FOK | Already filled or cancelled (never rest past fill) |

## Post-EOD Actions

1. Verify EOD files are complete and non-empty
2. Capture a snapshot for the next day's warm restart
3. Archive journal segments if needed
4. Back up EOD files to external storage

## Failure Recovery

| Symptom | Cause | Action |
|---------|-------|--------|
| Empty trade summary | No trades during session | Expected if no trading activity |
| Missing EOD files | EOD not triggered | Check session scheduler or trigger manually |
| Incorrect OHLC | Trade feed issue | Compare with journal replay |
| Fee calculation wrong | Fee schedule config | Check instrument config fee rates |
