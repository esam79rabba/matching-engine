# Docker-Based Pilot Dry-Run Plan

**Purpose**: Verify containerized deployment before real participant connects

---

## Dry-Run Steps

### Step 1: Build and Start (5 min)

```bash
cd /path/to/MatchingEngine
docker build -t matching-engine .
docker-compose up -d
docker-compose ps
```

**Expected**: Container status "Up", health "healthy".

### Step 2: Health Verification (2 min)

```bash
curl http://HOST:8080/api/v1/health
curl http://HOST:8080/api/v1/instruments
curl http://HOST:8080/api/v1/metrics
```

**Expected**: Health OK, 2 instruments, metrics counters at zero.

### Step 3: TLS Verification (2 min)

```bash
echo | openssl s_client -connect HOST:9876 2>/dev/null | head -5
```

**Expected**: `CONNECTED`, certificate chain displayed.

### Step 4: FIX Logon (3 min)

Send FIX Logon with assigned CompID via test tool or cert harness.

**Expected**: 35=A ack received.

### Step 5: Order Lifecycle (5 min)

Submit: buy → verify resting → sell (crossing) → verify trade → cancel flow.

**Expected**: 150=0, 150=F (with fills), 150=4.

### Step 6: Restart Recovery (5 min)

```bash
docker-compose restart matching-engine
sleep 5
curl http://HOST:8080/api/v1/health
```

Reconnect FIX client with continued MsgSeqNum.

**Expected**: Health OK after restart. FIX logon succeeds with persisted sequence.

### Step 7: RBAC Verification (3 min)

Login as viewer → attempt halt → expect 403.
Login as operator → halt → expect 200.

**Expected**: Role enforcement working.

### Step 8: Metrics and Logs (2 min)

```bash
curl http://HOST:8080/api/v1/metrics
docker-compose logs --tail=50 matching-engine
```

**Expected**: Counters reflect activity. Structured logs visible.

## Dry-Run Pass Criteria

| # | Check | Must Pass |
|:-:|-------|:---------:|
| 1 | Container starts and stays healthy | Yes |
| 2 | FIX TLS connection works | Yes |
| 3 | Order lifecycle works (trade + cancel) | Yes |
| 4 | Restart preserves state | Yes |
| 5 | Reconnection with persisted seq works | Yes |
| 6 | RBAC enforced | Yes |
| 7 | Metrics and logs accessible | Yes |

**All 7 must pass before inviting real participant.**

## If Dry-Run Fails

| Failure | Action |
|---------|--------|
| Container won't start | Check `docker-compose logs`, verify config paths |
| TLS fails | Verify cert files mounted, check cert expiry |
| FIX rejects | Check CompID in sessions.json, check instrument state |
| Restart loses state | Verify Docker volumes exist (`docker volume ls`) |
| RBAC not enforced | Verify ops.api_key set and auth service configured |
