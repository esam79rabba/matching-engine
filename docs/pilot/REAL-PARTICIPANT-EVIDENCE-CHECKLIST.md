# Real-Participant Pilot — Evidence Checklist

**Purpose**: Define exactly what to capture for post-pilot analysis and Phase 2C-M2 decision.

---

## Before Pilot

| # | Evidence | How | Captured |
|:-:|----------|-----|:--------:|
| 1 | Container image SHA | `docker images matching-engine --format '{{.ID}}'` | |
| 2 | Config snapshot | Copy deploy/docker-config/ to evidence dir | |
| 3 | Pre-pilot metrics | `curl HOST:8080/api/v1/metrics` | |
| 4 | Test suite result | `ctest` output (423+ tests) | |

## During Pilot

| # | Evidence | How | Captured |
|:-:|----------|-----|:--------:|
| 5 | Participant logon observed | Daemon structured logs (`event=logon`) | |
| 6 | Orders processed | Metrics: fix.orders_accepted | |
| 7 | Trades executed | Metrics: fix.orders_accepted with fills | |
| 8 | Cancels processed | Metrics: fix.cancels_processed | |
| 9 | Halt/resume performed | Metrics: ops.halt, ops.resume | |
| 10 | Market data events | Participant confirms receipt | |
| 11 | Issues encountered | Issue log (PILOT-ISSUE-LOG-TEMPLATE.md) | |
| 12 | Participant observations | Notes during pilot | |

## After Pilot

| # | Evidence | How | Captured |
|:-:|----------|-----|:--------:|
| 13 | Final metrics snapshot | `curl HOST:8080/api/v1/metrics` | |
| 14 | Audit log | `curl HOST:8080/api/v1/audit` | |
| 15 | EOD output | Copy from eod-data volume | |
| 16 | Container logs | `docker-compose logs > pilot_logs.txt` | |
| 17 | FIX session state | Copy from fix-sessions volume | |
| 18 | Participant written feedback | Email or form | |

## Restart/Reconnection Evidence (If Tested)

| # | Evidence | How | Captured |
|:-:|----------|-----|:--------:|
| 19 | Pre-restart metrics | Snapshot before restart | |
| 20 | Restart command and timing | `docker-compose restart` + timestamp | |
| 21 | Post-restart health | `curl HOST:8080/api/v1/health` | |
| 22 | Participant reconnection observed | Logs: `event=logon` with persisted seq | |

## Questions to Answer Post-Pilot

| Question | Answer | Informs |
|----------|--------|---------|
| Did the participant connect successfully? | | Basic readiness |
| Did orders execute correctly? | | Engine correctness |
| Did the participant experience any latency issues? | | Performance assessment |
| Did reconnection (if tested) work smoothly? | | DR readiness |
| Did the participant understand the onboarding guide? | | Documentation quality |
| What features did the participant request? | | Phase 2C-M2 / Phase 3 backlog |
| Would the participant do an extended pilot? | | Commercial readiness |
