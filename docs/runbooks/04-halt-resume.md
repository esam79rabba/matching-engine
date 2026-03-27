# Runbook: Halt and Resume Trading

**Audience**: Operator / Engineer
**Last updated**: 2026-03-25
**Applies to**: FR-1 prototype pilot

---

## When to Use

- Emergency market event requiring trading suspension
- Instrument-level circuit breaker activation
- Regulatory request to halt trading
- Scheduled maintenance window

## Halt Procedure

### Via Ops HTTP API

```bash
# Halt a specific instrument
curl -X POST http://localhost:8080/ops/halt \
  -d '{"instrument_id": 100}'

# Expected response:
# {"status": "ok", "instrument_id": 100, "state": "HALTED"}
```

### What Happens on Halt

1. Instrument state transitions from CONTINUOUS to HALTED
2. New orders for the instrument are **rejected** with RejectCode::InstrumentHalted
3. Existing resting orders **remain** in the book (not cancelled)
4. Market data publishes instrument status change
5. Cancel requests are still processed (participants can cancel during halt)
6. Journal records the halt event

### Verify Halt

```bash
# Check instrument state
curl http://localhost:8080/ops/status

# Expected: instrument 100 shows state "HALTED"
```

## Resume Procedure

```bash
# Resume trading on a halted instrument
curl -X POST http://localhost:8080/ops/resume \
  -d '{"instrument_id": 100}'

# Expected response:
# {"status": "ok", "instrument_id": 100, "state": "CONTINUOUS"}
```

### What Happens on Resume

1. Instrument state transitions from HALTED to CONTINUOUS
2. New orders accepted again
3. Any resting orders that remained during halt become matchable
4. Crossing orders that arrived during halt (if any were resting) will match
5. Market data publishes instrument status change

## Kill Switch

For participant-level emergency (not instrument halt):

```bash
# Activate kill switch for a participant
curl -X POST http://localhost:8080/ops/kill-switch \
  -d '{"participant_id": 1, "action": "activate"}'
```

This will:
1. Block all new orders from the participant
2. Cancel all resting orders from the participant
3. Record the event in the journal

See [Runbook: Kill Switch](05-kill-switch.md) for details.

## Expected Outcome

| Action | Orders Accepted | Existing Orders | Cancels |
|--------|:---------------:|:---------------:|:-------:|
| Halt | Rejected | Remain | Allowed |
| Resume | Accepted | Matchable | Allowed |
| Kill switch | Blocked for participant | Cancelled | N/A |

## Failure Recovery

| Symptom | Cause | Action |
|---------|-------|--------|
| Halt request fails | Instrument already halted | Check current state via /ops/status |
| Resume fails | Instrument not in HALTED state | Only HALTED instruments can be resumed |
| Orders still accepted after halt | State transition not propagated | Verify ops response; check matching engine logs |
