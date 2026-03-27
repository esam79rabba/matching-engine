# Runbook: Basic Troubleshooting

**Audience**: Operator / Engineer
**Last updated**: 2026-03-25
**Applies to**: FR-1 prototype pilot

---

## Quick Diagnostics

### Health Check

```bash
# Ops console health
curl http://localhost:8080/health
# Expected: {"status": "ok"}

# Platform status
curl http://localhost:8080/ops/status
# Returns: instrument states, session info
```

### Test Suite

```bash
# Run full test suite (should always be first diagnostic step)
cd build && ctest --output-on-failure
# All 253+ tests must pass
```

---

## Common Issues

### 1. Build Fails

**Symptom**: `cmake` or `make` errors

| Check | Command |
|-------|---------|
| CMake version | `cmake --version` (need 3.20+) |
| C++ compiler | `c++ --version` (need C++20 support) |
| Clean rebuild | `cd build && rm -rf * && cmake .. && make -j$(nproc)` |

### 2. Tests Fail

**Symptom**: `ctest` reports failures

```bash
# Run specific failing test with verbose output
cd build && ./engine/core/engine-core-tests --gtest_filter="FailingTest*" --gtest_print_time=1
```

| Common Cause | Solution |
|-------------|----------|
| Port conflict | Ensure ports 9876/9877/8080 are free |
| Stale temp files | `rm -rf /tmp/matching_engine_*` |
| File descriptor limit | `ulimit -n 4096` |

### 3. FIX Connection Refused

**Symptom**: Cannot connect to port 9876

| Check | Solution |
|-------|----------|
| Platform running? | Verify test harness is active |
| Port in use? | `lsof -i :9876` |
| Firewall? | Check local firewall rules |

### 4. Orders Rejected Unexpectedly

**Symptom**: 35=8 with 150=8 (Rejected)

Check in order:
1. **Instrument state**: Must be CONTINUOUS (not HALTED/CLOSED/PRE_OPEN)
2. **Kill switch**: Check if participant is blocked
3. **Risk checks**: Fat-finger (price too far from reference), notional limit, rate limit
4. **Instrument config**: Valid instrument_id, valid price/qty within tick/lot rules
5. **Session state**: Participant must be logged in

### 5. Journal Corruption

**Symptom**: Warm restart fails or replay count doesn't match

```bash
# Check journal segment count and sizes
ls -la deploy/data/journal/

# The journal uses CRC32 checksums on every entry.
# Corruption is detected during replay.
# Last valid entry is the recovery point.
```

**Recovery**:
1. If snapshot exists: warm restart from snapshot (skips corrupted tail)
2. If no snapshot: truncate journal at last valid entry, cold start
3. Contact engineering for journal repair tools

### 6. Slow Performance

**Symptom**: High latency or low throughput

| Check | Tool |
|-------|------|
| CPU pinning | Not enabled in prototype (Phase 2+) |
| Journal flush mode | Verify `flush_mode=sync` vs `batch` in config |
| Order pool exhaustion | Check pool capacity vs active order count |
| System load | `top`, `iostat`, `vmstat` |

### 7. Market Data Not Received

**Symptom**: Market data subscribers see no updates

| Check | Solution |
|-------|----------|
| Port 9877 listening? | `lsof -i :9877` |
| Client connected? | Check MdTcpServer client count |
| Instrument active? | Must be in CONTINUOUS state for trade data |
| Any trades occurring? | Market data only publishes on events |

---

## Log Locations

| Log | Location | Contents |
|-----|----------|----------|
| Smoke test logs | `deploy/logs/smoke_*.log` | Individual test suite output |
| Journal data | `deploy/data/journal/` | Binary journal segments |
| Snapshots | `deploy/data/snapshots/` | Binary partition snapshots |
| EOD reports | `deploy/data/eod/` | CSV and text trade reports |

---

## Escalation

If the issue cannot be resolved with this runbook:

1. Capture full test output: `cd build && ctest --output-on-failure > /tmp/test_output.txt 2>&1`
2. Save journal and snapshot files
3. Note the exact error message and context
4. Contact engineering team with artifacts
