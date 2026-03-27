# ADGM / FSRA Compliance Overlay — Working Draft

**Status**: Working compliance overlay — requires legal/regulatory confirmation before use in regulatory submissions
**Decision**: D-03 (ADGM/FSRA) CLOSED
**Disclaimer**: This document is a platform-team working artifact. It is NOT legal advice. All regulatory interpretations must be confirmed by qualified ADGM/FSRA legal counsel before reliance.

---

## Regulatory Framework Summary

| Element | Detail |
|---------|--------|
| **Jurisdiction** | Abu Dhabi Global Market (ADGM) |
| **Regulator** | Financial Services Regulatory Authority (FSRA) |
| **Applicable framework** | FSMR (Financial Services and Markets Regulations); FSRA Virtual Asset guidance |
| **License type** | Financial Services Permission (FSP) — to operate an MTF dealing in virtual assets |
| **Asset scope** | Accepted Virtual Assets as defined by FSRA (must be on FSRA's accepted list or approved) |
| **Additional permissions** | Custody permission may be required if venue holds participant assets — legal confirmation needed |

---

## Compliance Mapping: Baseline Capability vs ADGM Requirements

| # | Topic | Baseline Platform Support | ADGM/FSRA Implication | Gap / Action Needed | Owner |
|---|-------|--------------------------|----------------------|--------------------:|-------|
| 1 | **AML/KYC** | Participant onboarding hooks; KYC status field in participant model | FSRA requires CDD (Customer Due Diligence) for all participants; ongoing monitoring; sanctions screening | Integrate KYC provider (e.g., Onfido, Jumio); sanctions screening service. FR-1: manual KYC with hooks for automation. | Compliance |
| 2 | **Transaction monitoring** | Surveillance event export (file-based FR-1); event taxonomy covers all order/trade events | FSRA expects ongoing transaction monitoring for suspicious activity; STRs (Suspicious Transaction Reports) to FIU | FR-1 exports data for manual review. Phase 2: integrate automated monitoring tool. File STRs manually. | Compliance |
| 3 | **Technology risk** | Documented architecture baseline; 30-file spec; security architecture; resilience plan | FSRA requires Technology Risk Assessment; documented controls; regular testing | Assemble Technology Risk Assessment document from existing baseline artifacts. | Engineering + Compliance |
| 4 | **Audit trail** | 100% event capture; nanosecond timestamps; tamper-evident hash chain; 7-year retention | FSRA requires complete audit trail with sufficient retention | Baseline exceeds typical requirements. Confirm FSRA retention period. | Compliance |
| 5 | **Clock synchronization** | PTP (< 1 µs to UTC); NIC hardware timestamping | FSRA guidance likely ≤ 1 ms (less strict than MiFID II's 100 µs) | PTP provides ~1000× better than expected requirement. Document compliance. | SRE |
| 6 | **Business continuity** | Warm restart implemented and tested (StartupController with snapshot + journal replay, < 60 seconds); cold start controller operational; DR (Phase 2); documented BCP framework | FSRA requires documented BCP; tested recovery; notification procedures | Warm restart now implemented and tested. Draft BCP document from existing §19 baseline. Warm restart operational for FR-1 with documented roadmap to DR. | Operations + Compliance |
| 7 | **Cybersecurity** | TLS operational on FIX transport (OpenSSL); API key auth on ops console; transport abstraction supports mTLS config. Full mTLS enforcement, RBAC, HSM, encryption at rest pending. | FSRA has cybersecurity expectations aligned with international standards | FIX sessions can run over TLS. Ops console protected by API key auth. mTLS enforcement and full RBAC deferred to Phase 2+. Schedule pen test before launch. | Security |
| 8 | **Market conduct** | STP, price bands, kill switch, surveillance hooks | FSRA expects fair and orderly trading; manipulation prevention | Baseline controls are strong. Document market conduct rules in venue rulebook. | Compliance + Product |
| 9 | **Client asset protection** | Internal ledger model for FR-1; no on-chain custody | FSRA may require segregated client assets; third-party custody or self-custody with permission | Legal confirmation needed: does internal ledger require custody permission? If yes, engage custody provider or apply for custody permission. | Legal + Compliance |
| 10 | **Fit and proper** | Not platform scope | FSRA assesses directors, officers, controllers | Prepare F&P documentation for key personnel. Legal/HR responsibility. | Legal / HR |
| 11 | **Capital requirements** | Not platform scope | FSRA sets minimum capital for FSP holders (typically $2M+ for MTF) | Financial planning. Confirm capital requirement with FSRA. | Finance / Legal |
| 12 | **Accepted virtual assets** | Configurable instrument list | FSRA maintains an Accepted Virtual Assets list; only approved assets tradeable on regulated venue | Confirm FR-1 trading pairs against FSRA accepted list. Some pairs may require individual approval. | Product + Compliance |
| 13 | **Data protection** | Encryption at rest/transit; access controls; audit logs | ADGM has its own data protection regulations (based on international standards) | Review ADGM Data Protection Regulations 2021. Confirm data residency requirements. | Legal + Security |
| 14 | **Outsourcing** | Platform may use cloud infrastructure | FSRA has outsourcing notification requirements for material functions | Document cloud hosting arrangement as potential outsourcing. Notify FSRA if material. | Compliance |
| 15 | **Reporting** | Regulatory reporting hooks in baseline; event export | FSRA may require periodic reporting; transaction reports | Confirm specific reporting format/frequency with FSRA. Build report generator in Phase 2 if needed. | Compliance |
| 16 | **Complaints handling** | Not platform scope (ops process) | FSRA requires documented complaints procedure | Draft complaints procedure. Operational, not platform. | Operations |

---

## Pre-Application Engagement Artifacts

| # | Artifact | Purpose | Status |
|---|----------|---------|:------:|
| 1 | FSRA pre-application meeting request | Introduce project; understand requirements; gauge timeline | To prepare |
| 2 | Business plan summary | Venue concept, target market, technology approach, team | To prepare |
| 3 | Technology overview document | Extract from architecture baseline; non-confidential version | To prepare |
| 4 | Regulatory perimeter analysis | What permissions are needed (MTF, custody, etc.) | Needs legal input |
| 5 | Key personnel CVs | F&P documentation for directors/officers | HR / Legal |
| 6 | Capital adequacy evidence | Proof of required capital | Finance |
| 7 | AML/CFT framework outline | Policies and procedures for anti-money-laundering | Compliance |

---

## ADGM Compliance Configuration Profile (Platform)

Based on the §16 compliance-by-configuration framework:

```yaml
jurisdiction_profile: "ADGM_FSRA_DIGITAL_ASSET"
version: "2026.1"
effective_date: "TBD"

market_integrity:
  circuit_breakers:
    enabled: true                    # FR-1 stretch goal
    static_threshold_pct: 20         # wider for digital assets
    dynamic_threshold_pct: 10
    auction_duration_seconds: 300
  clock_sync:
    required_accuracy_ms: 1          # FSRA expected (confirm)
    sync_protocol: PTP               # platform provides < 0.001 ms
    evidence_retention_days: 1825    # 5 years (confirm)
  tick_size_regime:
    source: "VENUE_DEFINED"          # per trading pair
  pre_trade_transparency:
    enabled: true
    depth_levels: 10                 # configurable

participant_controls:
  pre_trade_risk:
    mandatory: true
    fat_finger_price: true
    fat_finger_quantity: true
    message_rate_limit: true
    kill_switch: true
  kyc_required: true
  sanctions_screening: true          # before onboarding

surveillance_hooks:
  spoofing_detection: true           # data export for manual review (FR-1)
  wash_trade_detection: true
  manipulation_monitoring: true

reporting:
  transaction_reporting:
    enabled: true                    # format TBD with FSRA
    destination: "FSRA"
    format: "TBD"
    deadline: "TBD"

retention:
  order_data: 1825                   # 5 years (confirm with FSRA)
  trade_data: 1825
  communication_records: 1825
  clock_sync_evidence: 1825

governance:
  four_eyes_rule: false              # FR-1; Phase 2 adds this
  change_management_required: true
  incident_notification:
    regulator_notification_hours: 24  # confirm with FSRA
```

---

## Immediate Actions

| # | Action | Owner | Timeline |
|---|--------|-------|----------|
| 1 | Engage ADGM/FSRA legal counsel | Compliance / Legal | This week |
| 2 | Request FSRA pre-application meeting | Compliance | This week |
| 3 | Confirm custody permission requirement | Legal | Week 2 |
| 4 | Review FSRA Accepted Virtual Assets list against proposed pairs | Product + Compliance | Week 2 |
| 5 | Prepare business plan summary for FSRA | Product + Compliance | Month 1 |
| 6 | Draft Technology Risk Assessment from baseline | Engineering + Compliance | Month 1 |
| 7 | Draft AML/CFT framework outline | Compliance | Month 1 |
