# Pilot Issue / Incident Log

**Pilot Date**: _______________
**Environment**: _______________
**Operator**: _______________

---

## Instructions

- Log every issue as it occurs — do not wait until post-pilot
- Use severity levels: **Critical** (pilot-stopping), **High** (degraded), **Medium** (workaround available), **Low** (cosmetic/minor)
- Update status as issues are resolved or deferred
- Attach evidence references (log lines, screenshots, metric snapshots)

---

## Issue Log

| # | Timestamp (UTC) | Reporter | Participant / Session | Component | Severity | Summary | Reproduction Steps | Temp Mitigation | Status | Owner | Follow-Up |
|:-:|:---------------:|----------|----------------------|-----------|:--------:|---------|-------------------|-----------------|:------:|-------|-----------|
| 1 | | | | | | | | | Open | | |
| 2 | | | | | | | | | Open | | |
| 3 | | | | | | | | | Open | | |
| 4 | | | | | | | | | Open | | |
| 5 | | | | | | | | | Open | | |
| 6 | | | | | | | | | Open | | |
| 7 | | | | | | | | | Open | | |
| 8 | | | | | | | | | Open | | |
| 9 | | | | | | | | | Open | | |
| 10 | | | | | | | | | Open | | |

---

## Component Reference

| Component Code | Description |
|---------------|-------------|
| FIX | FIX gateway / session layer |
| MATCH | Matching engine / order book |
| RISK | Risk service (fat-finger, rate limit) |
| OPS | Ops console / HTTP server |
| MD | Market data publisher |
| JOURNAL | Journal / persistence |
| SCHED | Session scheduler |
| CONFIG | Configuration / ref data |
| TLS | TLS / transport |
| NET | Network / connectivity |
| OTHER | Other |

---

## Severity Guide

| Severity | Impact | Response |
|:--------:|--------|----------|
| Critical | Pilot must stop, data integrity at risk | Immediate: halt trading, collect evidence, escalate |
| High | Major function degraded, workaround not clean | Fix within pilot window if possible, document |
| Medium | Function impaired but workaround available | Apply workaround, log for post-pilot |
| Low | Cosmetic, minor, or expected limitation | Log for post-pilot review |

---

## Post-Pilot Summary

| Metric | Value |
|--------|:-----:|
| Total issues logged | |
| Critical | |
| High | |
| Medium | |
| Low | |
| Resolved during pilot | |
| Deferred to post-pilot | |
| New findings (not previously known) | |
