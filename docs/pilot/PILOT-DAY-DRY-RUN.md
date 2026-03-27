# Pilot-Day Operator Dry-Run Guide

**Version**: 1.0 | **Date**: 2026-03-25
**Purpose**: Rehearsal script for pilot-day operations
**Audience**: Venue operator performing or supervising the pilot

---

## Prerequisites

- [ ] Platform installed per systemd installer or manual setup
- [ ] Config files reviewed and edited for pilot environment
- [ ] Credentials provisioned per CERTIFICATE-AND-CREDENTIAL-PROVISIONING.md
- [ ] Pre-pilot checklist completed and signed
- [ ] Participant has received onboarding guide and connection details

---

## Phase 1: Pre-Market Startup (T-60 minutes)

### Step 1.1 — Start Platform

```bash
# Option A: systemd
sudo systemctl start matching-engine
sudo systemctl status matching-engine

# Option B: direct
cd /opt/matching-engine
./bin/matching-engine-daemon ./config/platform.conf &
```

**Expected**: Startup banner with port assignments. No errors in log.

**If fails**: Check `journalctl -u matching-engine` or stdout. Common issues: port conflict, missing config, journal directory permissions.

### Step 1.2 — Health Check

```bash
curl http://localhost:8080/health
```

**Expected**: `{"status":"ok"}`

### Step 1.3 — Verify Instruments

```bash
curl http://localhost:8080/api/v1/instruments
```

**Expected**: JSON listing configured instruments (BTC-USD, ETH-USD or your pilot set).

### Step 1.4 — Verify Metrics

```bash
curl http://localhost:8080/api/v1/metrics
```

**Expected**: Prometheus-format metrics with zero counters (no activity yet).

### Step 1.5 — Run Connectivity Validation

```bash
cd deploy/scripts
./validate-connectivity.sh localhost
```

**Expected**: All checks PASS. Report file generated.

---

## Phase 2: Participant Connectivity (T-30 minutes)

### Step 2.1 — Participant Logon Verification

Ask pilot participant to connect their FIX client. Monitor:

```bash
# Watch for logon events
journalctl -u matching-engine -f | grep "logon"
```

**Expected**: `[FIX] Session X logon (participant=Y)` message.

**If participant cannot connect**:
1. Check port reachability: `netstat -tlnp | grep 9876`
2. Check TLS: if enabled, verify cert is valid
3. Check CompID: verify sender_comp_id matches sessions.json
4. Check firewall rules

### Step 2.2 — Verify Session Count

```bash
curl http://localhost:8080/api/v1/metrics | grep fix_sessions
```

**Expected**: `fix_sessions_logon` counter incremented.

---

## Phase 3: Market Open (T-0)

### Step 3.1 — Session Transition to CONTINUOUS

If session scheduler is configured, this happens automatically at the scheduled time.

Manual override if needed:
```bash
# Instruments should transition automatically. If not, resume manually:
curl -H "Authorization: Bearer ${API_KEY}" \
     -X POST http://localhost:8080/api/v1/instruments/100/resume
```

**Expected**: Instruments in CONTINUOUS state. Orders accepted.

### Step 3.2 — Participant Order Test

Ask participant to submit a small test order.

```bash
# Monitor metrics
curl http://localhost:8080/api/v1/metrics | grep fix_orders
```

**Expected**: `fix_orders_accepted` counter incremented. Or `fix_orders_rejected` if order is intentionally invalid.

---

## Phase 4: Operational Scenarios (During Trading)

### Step 4.1 — Halt/Resume Test

```bash
# Halt an instrument
curl -H "Authorization: Bearer ${API_KEY}" \
     -X POST http://localhost:8080/api/v1/instruments/100/halt

# Verify: participant orders should be rejected
# Ask participant to submit an order — expect reject

# Resume
curl -H "Authorization: Bearer ${API_KEY}" \
     -X POST http://localhost:8080/api/v1/instruments/100/resume

# Verify: orders accepted again
```

**Expected**: Clean halt → reject → resume → accept cycle.

### Step 4.2 — Kill Switch Test

```bash
# Activate kill switch for test participant (use a test account, not the live pilot participant)
curl -H "Authorization: Bearer ${API_KEY}" \
     -X POST http://localhost:8080/api/v1/kill-switch \
     -d '{"participant_id": 99, "action": "activate"}'

# Deactivate
curl -H "Authorization: Bearer ${API_KEY}" \
     -X POST http://localhost:8080/api/v1/kill-switch \
     -d '{"participant_id": 99, "action": "deactivate"}'
```

**Expected**: Kill switch activates and deactivates cleanly. Live participant unaffected.

**WARNING**: Do NOT test kill switch on the live pilot participant during active trading unless intentional.

### Step 4.3 — Monitor Metrics

```bash
# Check key metrics periodically
curl -s http://localhost:8080/api/v1/metrics | grep -E "orders|sessions|messages"
```

---

## Phase 5: Warm Restart Test (Optional)

### Step 5.1 — Graceful Stop

```bash
sudo systemctl stop matching-engine
# or: kill -TERM <pid>
```

**Expected**: Clean shutdown log. Journal flushed. FIX sessions persisted.

### Step 5.2 — Restart

```bash
sudo systemctl start matching-engine
```

**Expected**: Platform starts, loads from journal/snapshot. Sequence numbers resume from persisted state.

### Step 5.3 — Verify Recovery

```bash
curl http://localhost:8080/health
curl http://localhost:8080/api/v1/instruments
curl http://localhost:8080/api/v1/metrics
```

**Expected**: All healthy. Instrument state preserved. Metrics counters reset (in-memory).

### Step 5.4 — Participant Reconnect

Ask participant to reconnect. FIX session should resume with persisted sequence numbers.

---

## Phase 6: Day Close / EOD (T+end)

### Step 6.1 — Session Close

Session scheduler transitions instruments to CLOSED automatically. Or manual:

```bash
curl -H "Authorization: Bearer ${API_KEY}" \
     -X POST http://localhost:8080/api/v1/instruments/100/halt
curl -H "Authorization: Bearer ${API_KEY}" \
     -X POST http://localhost:8080/api/v1/instruments/101/halt
```

### Step 6.2 — Verify EOD Output

```bash
ls -la /opt/matching-engine/data/eod/
# Expected: trade_summary_YYYYMMDD.csv, fee_summary_YYYYMMDD.csv, daily_report_YYYYMMDD.txt
```

### Step 6.3 — Collect Pilot Evidence

```bash
# Metrics snapshot
curl http://localhost:8080/api/v1/metrics > pilot_metrics_$(date +%Y%m%d).txt

# Connectivity report (if generated)
cp connectivity-report.txt pilot_evidence/

# EOD files
cp /opt/matching-engine/data/eod/* pilot_evidence/

# Certification report (if run)
cp /tmp/cert_report_*.json pilot_evidence/
```

---

## Incident Response During Pilot

| Symptom | Immediate Action | Escalation |
|---------|-----------------|------------|
| Daemon crashes | `systemctl restart matching-engine` | Check logs, contact engineering |
| Participant disconnects | Check `netstat`, verify FIX port | Contact participant, check network |
| Orders rejected unexpectedly | Check instrument state, risk limits | Review risk config, check kill switch |
| High latency | Check `top`, reduce order rate | Contact engineering |
| Journal error | Stop, check disk space, warm restart | Contact engineering |

---

## Post-Pilot

1. Collect all evidence files
2. Note any issues encountered
3. Record participant feedback
4. Update the pre-pilot checklist with findings
5. Plan remediation for any blockers found
