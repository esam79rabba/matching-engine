# 24. Migration / Rollout Strategy

## 24.1 Go-Live Approach

### 24.1.1 Phased Rollout

| Phase | Activity | Duration | Risk |
|-------|----------|----------|------|
| **Phase A: Internal Testing** | Platform running with synthetic load; internal team only | 4 weeks | Low |
| **Phase B: Participant Certification** | Selected participants certify and test connectivity | 6 weeks | Low |
| **Phase C: Market Rehearsal** | Full dress rehearsal with all participants; simulated trading day | 2 weeks | Medium |
| **Phase D: Parallel Running** | Real orders processed on new platform alongside legacy (if migrating) | 4 weeks | Medium |
| **Phase E: Soft Launch** | Limited instruments, limited participants, real trading | 2 weeks | High |
| **Phase F: Full Launch** | All instruments, all participants, full production | Ongoing | High |

### 24.1.2 Rollback Strategy

| Stage | Rollback Approach | Time to Rollback |
|-------|-------------------|-----------------|
| Phase A–C | Revert to previous state; no external impact | Immediate |
| Phase D | Switch back to legacy system; reconcile any discrepancies | < 1 hour |
| Phase E | Halt new platform; redirect participants to legacy | < 30 minutes |
| Phase F | Emergency halt; manual reconciliation; participant notification | < 5 minutes (halt); full rollback depends on context |

## 24.2 Migration from Legacy System

### 24.2.1 Data Migration

| Data Type | Migration Approach | Validation |
|-----------|-------------------|-----------|
| Instruments | Bulk export from legacy → transform → import to new platform | Record count match; field-by-field comparison |
| Participants | Export → transform → import; re-provision credentials | Participant count match; entitlement verification |
| Open orders (GTC) | Export from legacy book → replay into new engine | Order count match; price/qty verification |
| Reference data | Export → transform → import | Full comparison |
| Historical data | Not migrated to new engine; retained in legacy for regulatory retention | Accessibility verified |

### 24.2.2 Connectivity Migration

| Step | Activity | Participant Impact |
|------|----------|-------------------|
| 1 | Provision certification environment on new platform | None |
| 2 | Participants certify on new platform (parallel with production on legacy) | Certification effort |
| 3 | Participants configure dual connectivity (legacy + new) | Configuration change |
| 4 | Cutover: new platform becomes primary; legacy becomes backup | Brief outage during cutover |
| 5 | Decommission legacy connectivity after stabilization period | Remove legacy configs |

## 24.3 Go-Live Checklist

### 24.3.1 Pre-Launch (T-2 weeks)

- [ ] All participant certifications complete
- [ ] Performance test results accepted
- [ ] DR failover drill completed successfully
- [ ] Regulatory notification submitted (if required)
- [ ] Participant communication sent (go-live date, procedures, contacts)
- [ ] Operations runbooks reviewed and updated
- [ ] On-call roster confirmed for go-live period
- [ ] Monitoring and alerting configured and tested
- [ ] Rollback procedure documented and rehearsed

### 24.3.2 Go-Live Day (T-0)

- [ ] Pre-market system health check: all components green
- [ ] Reference data loaded and verified
- [ ] Trading sessions configured for the day
- [ ] Market operations team in position
- [ ] Engineering support team on standby
- [ ] Participant helpdesk staffed
- [ ] Regulatory contacts on standby
- [ ] Session started per schedule
- [ ] First trades verified end-to-end
- [ ] Market data verified by participants
- [ ] Drop copy verified by participants
- [ ] Post-trade processing verified

### 24.3.3 Post-Launch (T+1 to T+30)

- [ ] Daily post-session review for first 30 days
- [ ] Latency monitoring with daily reports
- [ ] Participant feedback collection
- [ ] Incident tracking and resolution
- [ ] Performance trend analysis
- [ ] Regulatory debrief (if required)
- [ ] Stabilization sign-off from all stakeholders
