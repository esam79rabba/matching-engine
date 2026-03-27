# Pilot Execution Pack

**Version**: 1.0 | **Date**: 2026-03-25
**Purpose**: Single consolidated reference for pilot-day execution
**Audience**: Venue operator, engineering lead, pilot coordinator

---

## Quick Reference

| Document | Location | Purpose |
|----------|----------|---------|
| Onboarding guide | `docs/pilot/PARTICIPANT-ONBOARDING.md` | Participant integration reference |
| Credential provisioning | `docs/pilot/CERTIFICATE-AND-CREDENTIAL-PROVISIONING.md` | Cert + CompID + API key setup |
| Pre-pilot checklist | `docs/pilot/PRE-PILOT-CHECKLIST.md` | Go/no-go gate |
| Dry-run guide | `docs/pilot/PILOT-DAY-DRY-RUN.md` | Operator rehearsal |
| Issue log | `docs/pilot/PILOT-ISSUE-LOG-TEMPLATE.md` | Live incident tracking |
| Evidence collection | `docs/pilot/PILOT-EVIDENCE-COLLECTION.md` | What to capture and where |
| Connectivity validation | `deploy/scripts/validate-connectivity.sh` | Automated connectivity check |
| Environment config | `deploy/pilot-env/` | Editable config bundle |
| Runbooks | `docs/runbooks/01-startup.md` through `07-troubleshooting.md` | Operational procedures |

---

## Pilot-Day Sequence

### T-7 Days: Preparation

| Step | Action | Owner | Done |
|:----:|--------|-------|:----:|
| 1 | Complete credential provisioning guide | Security | |
| 2 | Deploy pilot-env config pack to host | Operations | |
| 3 | Distribute onboarding guide to participant | Pilot coordinator | |
| 4 | Confirm participant has FIX client ready | Pilot coordinator | |
| 5 | Generate and distribute API key to ops team | Security | |

### T-2 Days: Dry-Run

| Step | Action | Owner | Done |
|:----:|--------|-------|:----:|
| 6 | Execute full dry-run (PILOT-DAY-DRY-RUN.md) | Operations + Engineering | |
| 7 | Run connectivity validation script | Engineering | |
| 8 | Run certification harness (all 7 scenarios) | Engineering | |
| 9 | Document any issues found during dry-run | Engineering | |
| 10 | Resolve blockers or update deferred items list | Engineering | |

### T-1 Day: Final Gate

| Step | Action | Owner | Done |
|:----:|--------|-------|:----:|
| 11 | Complete pre-pilot checklist (all mandatory items) | All | |
| 12 | Confirm participant connectivity from their network | Participant | |
| 13 | Confirm ops team has runbooks and issue log ready | Operations | |
| 14 | **Go / No-Go decision** (see below) | All stakeholders | |

### T-0: Pilot Day

| Time | Action | Owner | Notes |
|:----:|--------|-------|-------|
| T-60m | Start platform, verify health | Operations | Runbook 01 |
| T-30m | Participant connects, logon verified | Operations + Participant | Monitor logs |
| T-15m | Final connectivity validation | Engineering | Script or manual |
| T-0 | **Market open** — instruments to CONTINUOUS | Operations | Auto or manual |
| During | Monitor metrics, respond to issues | Operations | Issue log open |
| During | Execute planned test scenarios | Pilot coordinator | Per dry-run |
| T+end | Session close — instruments to CLOSED | Operations | Auto or manual |
| T+end | Collect evidence (PILOT-EVIDENCE-COLLECTION.md) | Engineering | |
| T+1d | Complete post-pilot report | All | Template provided |

---

## Go / No-Go Decision Point

**When**: T-1 day (or morning of pilot day)
**Who**: Engineering lead, operations lead, security lead, pilot coordinator

### Mandatory Go Criteria

All must be YES to proceed:

| # | Criterion | Status |
|:-:|-----------|:------:|
| 1 | All mandatory pre-pilot checklist items pass | |
| 2 | Dry-run completed without blocking issues | |
| 3 | Participant confirms FIX client ready and tested | |
| 4 | Connectivity validation script passes from participant network | |
| 5 | Ops team confirms runbooks reviewed and understood | |
| 6 | Issue log and evidence collection tools ready | |
| 7 | Rollback plan confirmed (stop daemon, warm restart) | |

### No-Go Triggers

Any of these trigger No-Go:
- Mandatory checklist item fails and cannot be resolved
- Participant unable to connect during dry-run
- Unresolved blocking issue from dry-run
- Missing credential or certificate
- Ops team not confident in runbook execution

### Decision

| Decision | Rationale | Signed By | Date |
|:--------:|-----------|-----------|------|
| GO / NO-GO | | | |

---

## Contacts

| Role | Name | Contact | Backup |
|------|------|---------|--------|
| Engineering lead | | | |
| Operations lead | | | |
| Security lead | | | |
| Pilot coordinator | | | |
| Participant contact | | | |

---

## Rollback Plan

If critical issues arise during pilot:

1. **Halt all instruments** — `curl -H "Authorization: Bearer $KEY" -X POST http://localhost:8080/api/v1/instruments/{id}/halt`
2. **Notify participant** — communicate trading halt
3. **Assess** — review issue log, determine if resolvable
4. **If resolvable**: fix, resume instruments, continue pilot
5. **If not resolvable**: collect evidence, stop daemon, document in issue log, schedule follow-up

---

## Prototype Limitations Acknowledgment

Before pilot, all stakeholders must acknowledge:

- This is a prototype pilot, not a production launch
- Performance is on development hardware, not production-certified
- FIX gap recovery is bounded (stored messages only)
- No DR/failover — single-site warm restart only
- No auction mechanism — continuous trading only
- Security is prototype-grade (API key auth, server TLS, no full RBAC/mTLS)
- Market data is TCP/JSON, not multicast/SBE

Participant acknowledgment: _________________________ Date: _________
