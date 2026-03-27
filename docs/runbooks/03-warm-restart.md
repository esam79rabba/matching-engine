# Runbook: Warm Restart

**Audience**: Operator / Engineer
**Last updated**: 2026-03-25
**Applies to**: FR-1 prototype pilot

---

## When to Use

- After an unplanned process crash
- After a planned restart (e.g., config change)
- To recover partition state without replaying entire journal

## Prerequisites

- Previous journal data exists in `deploy/data/journal/`
- At least one snapshot exists in `deploy/data/snapshots/` (optional but recommended)
- FIX session state file exists in `deploy/data/fix-sessions/sessions.dat` (preserves sequence numbers)
- Build is current and passing

## Warm Restart Procedure

### 1. Stop any running instance

```bash
cd deploy/scripts && ./stop.sh
```

### 2. Verify data integrity

```bash
# Count journal segments
ls deploy/data/journal/*.journal | wc -l

# Check snapshot availability
ls deploy/data/snapshots/
```

### 3. Execute warm restart

```bash
cd deploy/scripts && ./warm-restart.sh
```

### How It Works

The `StartupController::warm_restart()` performs:

1. **Snapshot restore**: Loads the latest snapshot into the partition
   - Restores all resting orders, book state, and sequence counter
   - Sets the replay-from point to snapshot's sequence number

2. **Journal replay**: Replays journal entries from snapshot point forward
   - Only replays `TYPE_INPUT_COMMAND` entries
   - Output events are regenerated deterministically (not replayed)
   - Each replayed message passes through the normal matching path

3. **State validation**: Checks restored partition for consistency
   - Verifies all instruments are accessible
   - Validates order books are non-null
   - Confirms sequence counter is consistent

4. **Resume**: Partition ready for new traffic

### Without Snapshot (Cold Start)

If no snapshot exists, use cold start:
- Replays entire journal from sequence 1
- Slower for large journals but always correct
- `StartupController::cold_start()` handles this path

## Expected Outcome

- Partition state restored to pre-crash state
- All resting orders recovered
- Sequence counter advanced to correct position
- Platform ready for new orders

## Timing Expectations

| Journal Size | Snapshot Available | Expected Recovery Time |
|-------------|:------------------:|:---------------------:|
| < 10K entries | Yes | < 5 seconds |
| < 10K entries | No | < 10 seconds |
| 100K entries | Yes | < 30 seconds |
| 100K entries | No | < 60 seconds |

These are prototype estimates on development hardware. Production performance depends on NVMe I/O and CPU.

## Failure Recovery

| Symptom | Cause | Action |
|---------|-------|--------|
| "Snapshot restore failed" | Corrupted snapshot file | Delete snapshot, use cold start |
| "Post-replay state validation failed" | Journal corruption or version mismatch | Check journal integrity, contact engineering |
| Sequence gap in journal | Incomplete write before crash | Journal CRC32 will detect; truncate at last valid entry |
| Slow replay | Large journal without snapshot | Take regular snapshots (every 60 seconds recommended) |
