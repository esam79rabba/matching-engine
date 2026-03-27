# Pilot Retrospective and Phase 2 Decision Input

**Pilot Date**: _______________
**Retrospective Date**: _______________
**Facilitator**: _______________
**Attendees**: _______________

---

## Purpose

Turn pilot findings into actionable decisions across four categories:
1. **Immediate fixes** — before next pilot or expanded use
2. **FR-1 hardening backlog** — strengthen what exists
3. **Phase 2 candidates** — new capabilities justified by pilot evidence
4. **Defer / avoid** — items to explicitly NOT pursue unless justified

---

## Category 1: Immediate Fixes

Items that must be resolved before the next pilot or expanded use.

| # | Finding | Evidence (Issue # / Observation) | Fix Description | Owner | Target Date | Status |
|:-:|---------|--------------------------------|-----------------|-------|:-----------:|:------:|
| | | | | | | |
| | | | | | | |
| | | | | | | |

**Gate**: All immediate fixes must be completed and tested before next pilot engagement.

---

## Category 2: FR-1 Hardening Backlog

Strengthen existing capabilities based on pilot evidence.

| # | Area | Finding | Proposed Improvement | Effort (S/M/L) | Priority (P1/P2/P3) | Justified By |
|:-:|------|---------|---------------------|:--------------:|:--------------------:|-------------|
| | FIX | | | | | Issue #X |
| | Risk | | | | | Observation Y |
| | Ops | | | | | Participant feedback |
| | Perf | | | | | Metric Z |
| | Security | | | | | |
| | Config | | | | | |

**Decision rule**: P1 items before next pilot. P2 items before production. P3 items as capacity allows.

---

## Category 3: Phase 2 Feature Candidates

New capabilities not in FR-1 that are justified by pilot evidence.

| # | Feature | Pilot Evidence / Justification | Complexity (S/M/L/XL) | Revenue / Regulatory Impact | Recommend? |
|:-:|---------|-------------------------------|:---------------------:|:---------------------------:|:----------:|
| | Auction mechanism | | L | | |
| | Iceberg / pegged orders | | M | | |
| | Multicast market data | | L | | |
| | Full RBAC | | M | | |
| | DR / failover | | XL | | |
| | CCP clearing | | XL | | |
| | Pro-rata matching | | M | | |
| | FIX 5.0 / FIXT | | L | | |

**Decision rule**: Only move to Phase 2 if supported by pilot evidence, participant demand, or regulatory requirement. Do not build speculatively.

---

## Category 4: Defer / Avoid

Items explicitly decided NOT to pursue (with rationale).

| # | Item | Why Defer / Avoid | Revisit Trigger |
|:-:|------|-------------------|-----------------|
| | | | |
| | | | |

**Decision rule**: Only revisit if the trigger condition is met. Do not re-open without new evidence.

---

## Architecture Change Assessment

Did the pilot reveal any need for architecture changes?

| Question | Answer | Evidence |
|----------|--------|----------|
| Does the single-threaded-per-partition model hold? | | |
| Does the integer price encoding work for pilot assets? | | |
| Does the journal model need changes? | | |
| Does the transport abstraction work? | | |
| Does the config-driven startup work at pilot scale? | | |
| Any unexpected performance bottlenecks? | | |

**Rule**: Do NOT change architecture unless pilot evidence clearly justifies it. The bar for architecture change is high — prefer hardening over redesign.

---

## Key Metrics for Decision-Making

| Metric | Pre-Pilot Expectation | Actual | Assessment |
|--------|:---------------------:|:------:|:----------:|
| Orders processed | | | |
| Trades executed | | | |
| Rejects | | | |
| Incidents (Critical/High) | 0 / 0 | | |
| Warm restart recovery time | < 30s | | |
| Participant satisfaction | Positive | | |

---

## Decision Summary

| Decision | Choice | Rationale | Owner | Date |
|----------|--------|-----------|-------|------|
| Proceed to expanded pilot? | | | | |
| Begin Phase 2 development? | | | | |
| Architecture changes needed? | | | | |
| Additional participants? | | | | |
| Timeline for next milestone? | | | | |

---

## Sign-Off

| Role | Name | Date | Signature |
|------|------|------|-----------|
| Engineering Lead | | | |
| Product Lead | | | |
| Operations Lead | | | |
| Compliance | | | |
