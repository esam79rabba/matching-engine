# 25. Target Operating Model

## 25.1 Organizational Structure

```
                          ┌──────────────┐
                          │  Exchange CEO │
                          └──────┬───────┘
              ┌──────────────────┼──────────────────┐
              │                  │                   │
     ┌────────▼────────┐ ┌──────▼───────┐  ┌───────▼────────┐
     │  CTO / Head of  │ │  Head of     │  │ Head of        │
     │  Technology     │ │  Markets     │  │ Compliance     │
     └────────┬────────┘ └──────┬───────┘  └───────┬────────┘
              │                  │                   │
    ┌─────────┴─────────┐  ┌────┴─────┐    ┌───────┴────────┐
    │ Engineering (8-12) │  │ Market   │    │ Surveillance   │
    │ SRE/Infra (3-5)   │  │ Ops (3-5)│    │ & Compliance   │
    │ QA/Perf (2-3)     │  │ Product  │    │ (2-4)          │
    │ Security (1-2)    │  │ (2-3)    │    │                │
    └───────────────────┘  └──────────┘    └────────────────┘
```

## 25.2 Team Roles and Sizes

### 25.2.1 Steady-State Team (Post-Launch)

| Role | Count | Responsibilities |
|------|-------|-----------------|
| **Market Operations** | 3–5 | Daily session management, incident response, participant support |
| **Core Engine Engineering** | 4–6 | Matching engine development, performance optimization |
| **Platform Engineering** | 4–6 | Gateways, risk, market data, post-trade, reference data |
| **SRE / Infrastructure** | 3–5 | Infrastructure, monitoring, capacity, DR |
| **QA / Performance** | 2–3 | Testing, certification, performance validation |
| **Product Management** | 2–3 | Market model design, participant requirements, roadmap |
| **Compliance & Surveillance** | 2–4 | Regulatory compliance, surveillance review, audit support |
| **Security** | 1–2 | Security operations, vulnerability management, access control |
| **Connectivity / Participant Onboarding** | 2–3 | Participant certification, connectivity support |
| **Total** | **23–37** | |

### 25.2.2 Build Phase Team (Additional)

| Role | Count | Duration |
|------|-------|----------|
| Solution Architect | 1–2 | Phase 1–2 |
| Additional Engineers | 4–8 | Phase 1–2 |
| Additional QA | 2–3 | Phase 1–2 |
| Project Manager | 1 | Phase 1–3 |
| Business Analyst | 1–2 | Phase 1–2 |

## 25.3 Operational Processes

### 25.3.1 Daily Operations

| Time | Activity | Owner |
|------|----------|-------|
| T-60min | Pre-market health checks | SRE |
| T-30min | Reference data verification | Market Ops |
| T-15min | Session configuration review | Market Ops |
| T-0 | Trading session start | Market Ops |
| During trading | Continuous monitoring; incident response | Market Ops + SRE |
| Close | Trading session end; closing auction | Market Ops |
| Close+15min | End-of-day processing; trade reconciliation | Market Ops |
| Close+30min | Daily reports generation | Market Ops |
| Close+60min | Post-session review | Market Ops + Engineering (if incidents) |

### 25.3.2 Incident Management

| Severity | Detection | Response | Resolution | Communication |
|----------|-----------|----------|-----------|---------------|
| P1 | Automated alert | < 5 min | < 30 min | Participants + regulator immediately |
| P2 | Automated alert | < 15 min | < 2 hours | Participants within 1 hour |
| P3 | Alert or observation | < 1 hour | < 24 hours | Next daily report |
| P4 | Observation | < 4 hours | Next sprint | Internal tracking |

### 25.3.3 Change Management

| Change Type | Approval | Lead Time | Testing | Rollback |
|-------------|----------|-----------|---------|----------|
| Emergency fix (P1) | CTO + Operations Lead | Immediate | Minimum: unit + smoke | < 5 min |
| Hot fix (P2) | Engineering Lead | Same day | Unit + integration + regression | < 5 min |
| Standard release | Product + Engineering + Ops | 2 weeks | Full test suite | < 15 min |
| Major release | All stakeholders | 4 weeks | Full + participant cert | Scheduled |
| Parameter change | Operations Lead + four-eyes | Same day | Validation in UAT | Immediate |
| Infrastructure change | SRE Lead + four-eyes | 1 week | Staging verification | < 30 min |

## 25.4 Key Performance Indicators

### 25.4.1 Business KPIs

| KPI | Target | Measurement |
|-----|--------|-------------|
| Daily traded volume | Growth quarter-over-quarter | Shares/contracts/notional per day |
| Active participants | ≥ 50 within 12 months | Monthly active participant count |
| Market maker presence | ≥ 3 market makers per instrument | Quoting time coverage |
| Execution quality | Competitive with peer venues | Spread, depth, fill rate analysis |
| Revenue per trade | Positive unit economics | Fee revenue / trade count |

### 25.4.2 Technology KPIs

| KPI | Target | Measurement |
|-----|--------|-------------|
| System availability | 99.99% during trading hours | Monthly uptime calculation |
| Matching latency (p99) | Within deployment-profile target (Profile A: < 5 µs; Profile B: < 20 µs) | Continuous measurement |
| Incidents (P1/P2) | < 2 per quarter | Incident count |
| Mean time to recovery | < 30 min (P1); < 2 hours (P2) | Incident resolution time |
| Release frequency | Monthly patches; quarterly features | Release calendar |
| Performance regression | Zero | Automated performance tests |
| Security vulnerabilities | Zero critical/high unresolved > 7 days | Vulnerability tracking |

### 25.4.3 Operational KPIs

| KPI | Target | Measurement |
|-----|--------|-------------|
| Session start on time | 100% | Daily tracking |
| Participant certification time | < 5 business days | Certification tracking |
| Support ticket response | < 15 min (trading hours) | Ticket system metrics |
| Regulatory report accuracy | 100% | Reconciliation checks |
| DR drill success | 100% (quarterly) | Drill reports |
