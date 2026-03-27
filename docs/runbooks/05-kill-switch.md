# Runbook: Kill Switch

**Audience**: Operator / Engineer
**Last updated**: 2026-03-25
**Applies to**: FR-1 prototype pilot

---

## When to Use

- Participant is behaving erratically (runaway algorithm)
- Regulatory request to suspend a participant
- Participant requests emergency stop
- Security incident involving a participant session

## Activation

```bash
# Activate kill switch for participant
curl -X POST http://localhost:8080/ops/kill-switch \
  -d '{"participant_id": 1, "action": "activate"}'
```

### What Happens

1. **Immediate block**: All new orders from the participant are rejected
2. **Order cancellation**: All resting orders from the participant are cancelled
   - Cancellations go through the normal partition path
   - Each cancellation generates a journal entry
   - Market data updates reflect the removals
3. **Session remains connected**: FIX session stays up (participant sees rejects)
4. **Drop copy**: Participant receives cancellation confirmations

### Verify Activation

```bash
curl http://localhost:8080/ops/status
# Check participant 1 shows "kill_switch: active"
```

## Deactivation

```bash
# Deactivate kill switch
curl -X POST http://localhost:8080/ops/kill-switch \
  -d '{"participant_id": 1, "action": "deactivate"}'
```

After deactivation:
- New orders from the participant are accepted again
- Previously cancelled orders are NOT restored (participant must resubmit)

## Scope

| Scope | FR-1 Status | Notes |
|-------|:-----------:|-------|
| Participant-level | Implemented | Blocks all instruments for one participant |
| Instrument-level | Use halt/resume | See [Runbook: Halt/Resume](04-halt-resume.md) |
| Market-wide | Halt all instruments | Iterate instrument halt for each |

## Expected Timeline

| Step | Expected Duration |
|------|:-----------------:|
| Activation request to block | < 1 ms |
| Resting order cancellation start | < 1 ms after activation |
| All orders cancelled (100 orders) | < 10 ms |
| All orders cancelled (10K orders) | < 100 ms |

These are prototype estimates. Production performance depends on order count and partition load.

## Failure Recovery

| Symptom | Cause | Action |
|---------|-------|--------|
| Kill switch not taking effect | Wrong participant ID | Verify participant_id in sessions.json |
| Orders still accepted | Kill switch state not propagated | Check risk service is wired correctly |
| Cannot deactivate | Internal state inconsistency | Restart platform (warm restart) |
