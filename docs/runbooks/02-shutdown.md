# Runbook: Platform Shutdown

**Audience**: Operator / Engineer
**Last updated**: 2026-03-25
**Applies to**: FR-1 prototype pilot

---

## Prerequisites

- Platform is currently running

## Shutdown Procedure

### 1. Initiate graceful shutdown

```bash
cd deploy/scripts && ./stop.sh
```

### 2. Verify shutdown

- No processes listed in `deploy/run/*.pid`
- FIX port 9876 no longer accepting connections
- HTTP port 8080 no longer responding

### 3. Post-shutdown checks

```bash
# Verify journal is intact
ls -la deploy/data/journal/
# Check for any crash artifacts
ls deploy/logs/
```

## Ordered Shutdown Sequence (Manual)

If performing a controlled end-of-day shutdown:

1. Transition instruments to CLOSED state via ops HTTP
2. Wait for all session draining (no pending orders)
3. Trigger EOD processing
4. Capture final snapshot
5. Stop accepting new FIX connections
6. Close all TCP sessions
7. Flush and close journal
8. Exit

## Current Limitation

The prototype does not have a signal-based daemon. Shutdown is:
- For test harness: test process exits naturally
- For any background process: `stop.sh` sends SIGTERM

## Expected Outcome

- All processes stopped
- Journal files intact on disk
- No data loss (journal flushed before close)
