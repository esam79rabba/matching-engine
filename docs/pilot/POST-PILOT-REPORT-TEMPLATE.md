# Post-Pilot Report

**Pilot Date**: _______________
**Report Date**: _______________
**Author**: _______________
**Status**: Draft / Final

---

## 1. Pilot Scope

| Item | Detail |
|------|--------|
| Objective | |
| Duration | |
| Environment | Prototype FR-1 on [hardware description] |
| Instruments traded | |
| Participants | |

## 2. Participants

| Participant | CompID | Connection Type | Notes |
|-------------|--------|:---------------:|-------|
| | | TCP / TLS | |
| | | TCP / TLS | |

## 3. Environment

| Component | Version / Config | Notes |
|-----------|-----------------|-------|
| Platform | FR-1 build [date/commit] | |
| Config | instruments.json [hash] | |
| TLS | Enabled / Disabled | |
| Host | [OS, CPU, RAM, storage] | |

## 4. Scenarios Executed

| # | Scenario | Expected Outcome | Actual Outcome | Pass/Fail |
|:-:|----------|-----------------|----------------|:---------:|
| 1 | Participant logon | Logon ack received | | |
| 2 | Valid order accepted | ExecutionReport 150=0 | | |
| 3 | Invalid order rejected | ExecutionReport 150=8 | | |
| 4 | Order cancel | ExecutionReport 150=4 | | |
| 5 | Crossing trade | Both sides receive fill | | |
| 6 | Instrument halt/resume | Orders rejected during halt | | |
| 7 | Warm restart | State recovered, session resumed | | |
| 8 | EOD generation | Trade summary CSV produced | | |

## 5. Observed Outcomes

### 5.1 Functional

| Area | Observation |
|------|------------|
| Order entry | |
| Matching | |
| Risk controls | |
| Session management | |
| Market data | |
| EOD processing | |

### 5.2 Performance (Prototype — Not Production Benchmark)

| Metric | Observed | Notes |
|--------|:--------:|-------|
| Order acceptance latency (approx) | | Dev hardware caveat |
| Orders processed during pilot | | |
| Trades executed | | |
| Peak order rate | | |

### 5.3 Operational

| Area | Observation |
|------|------------|
| Startup/shutdown | |
| Monitoring/metrics | |
| Warm restart | |
| Ops console | |

## 6. Incidents / Issues

| # | Severity | Summary | Resolved? | Follow-Up |
|:-:|:--------:|---------|:---------:|-----------|
| | | | | |

Total issues: ___  Critical: ___  High: ___  Medium: ___  Low: ___

See `PILOT-ISSUE-LOG.md` for full details.

## 7. Participant Feedback

| Topic | Feedback |
|-------|----------|
| Connectivity experience | |
| Order lifecycle behavior | |
| Market data quality | |
| Documentation quality | |
| Overall impression | |
| Feature requests | |

## 8. Readiness Conclusion

| Question | Answer |
|----------|--------|
| Did the platform perform as expected? | |
| Were all planned scenarios completed? | |
| Were any blocking issues found? | |
| Is the platform ready for expanded pilot? | |
| Is the platform ready for Phase 2 development? | |

## 9. Recommended Next Steps

### Immediate (Before Next Pilot)

| # | Action | Owner | Priority |
|:-:|--------|-------|:--------:|
| | | | |

### FR-1 Hardening Backlog

| # | Item | Rationale | Priority |
|:-:|------|-----------|:--------:|
| | | | |

### Phase 2 Candidates

| # | Feature | Rationale | Priority |
|:-:|---------|-----------|:--------:|
| | | | |

## 10. Appendices

- [ ] Pre-pilot checklist (signed)
- [ ] Connectivity validation report
- [ ] Certification harness report
- [ ] Metrics snapshots
- [ ] EOD output files
- [ ] Issue log
- [ ] Participant feedback (raw)
