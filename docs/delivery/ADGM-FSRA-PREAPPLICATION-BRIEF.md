# ADGM / FSRA Pre-Application Meeting Brief

**Classification**: Confidential — Regulator Engagement Use
**Date**: 2026-03-25
**Prepared by**: [Company Name — placeholder]
**Disclaimer**: This is a working engagement brief prepared by the platform team. It is not legal advice and does not represent a formal application. All regulatory interpretations require confirmation by qualified ADGM/FSRA legal counsel.

---

## 1. Project Summary

We are developing an electronic trading venue platform for institutional-grade digital-asset trading. We are seeking to operate a **Multilateral Trading Facility (MTF)** for virtual assets within the Abu Dhabi Global Market (ADGM), regulated by the Financial Services Regulatory Authority (FSRA).

We are requesting a pre-application meeting to:
- Introduce the project and team
- Confirm the regulatory perimeter and permissions required
- Understand the application process, timeline, and key requirements
- Discuss the FSRA's expectations for virtual-asset MTF operators

## 2. Proposed FR-1 Scope

| Aspect | Description |
|--------|-------------|
| **Venue type** | Multilateral Trading Facility (MTF) for virtual assets |
| **Asset class** | Spot digital-asset trading (not derivatives, not margin, not lending) |
| **Trading pairs** | Major crypto-to-fiat and crypto-to-crypto pairs (BTC, ETH, SOL vs USD, USDT, AED) |
| **Market model** | Continuous limit order book; price-time (FIFO) priority |
| **Target participants** | Institutional investors, broker-dealers, market makers, qualified investors |
| **Settlement model** | Internal ledger-based settlement for FR-1; on-chain DvP settlement planned for later phases |
| **Custody** | Internal participant account model for FR-1. We seek FSRA guidance on whether this requires a separate custody permission or can operate under the MTF permission with appropriate controls. |
| **Trading hours** | Session-based (extended hours within ADGM business context); 24/7 trading planned for later phases |

## 3. Technology Overview

The platform is purpose-built for institutional-grade digital-asset trading:

| Capability | Detail |
|-----------|--------|
| **Matching engine** | Deterministic, event-sourced matching with provable replay |
| **Connectivity** | Industry-standard FIX protocol + low-latency binary protocol |
| **Pre-trade risk** | Fat-finger protection, message rate controls, kill switch |
| **Audit trail** | 100% event capture with nanosecond timestamps; tamper-evident; 5+ year retention |
| **Clock synchronization** | PTP (IEEE 1588) providing < 1 microsecond accuracy to UTC |
| **Market data** | Real-time order-book depth and trade publication |
| **Surveillance** | Order and trade data export for market surveillance and abuse detection |
| **Business continuity** | Documented BCP; warm restart recovery < 60 seconds; DR site planned for subsequent phase |
| **Cybersecurity** | mTLS, RBAC, HSM-backed key management, encryption at rest and in transit |

The technology architecture has been through formal specification and review. A Technology Risk Assessment can be prepared from existing documentation.

## 4. Controls Summary

| Control Area | Platform Capability | Notes |
|-------------|-------------------|-------|
| **AML/KYC** | Participant onboarding with KYC integration hooks | FR-1: manual verification with integration planned. We seek FSRA guidance on acceptable KYC providers and CDD standards. |
| **Transaction monitoring** | Order and trade event export for suspicious activity review | FR-1: file-based export for manual review. Automated monitoring planned for subsequent phase. |
| **Market conduct** | Self-trade prevention; price-band protection; kill switch; surveillance hooks | Active controls from day one. |
| **Client asset protection** | Internal ledger with participant-level segregation | We seek FSRA guidance on segregation requirements and whether third-party custody is required. |
| **Technology resilience** | Documented recovery procedures; tested warm restart; resilience testing program | DR site and automated failover planned for subsequent phase. |
| **Cybersecurity** | Mutual TLS on all connections; role-based access control; HSM for cryptographic keys; annual penetration testing | Aligned with international cybersecurity standards. |
| **Audit and record keeping** | Complete, timestamped, tamper-evident audit trail; 5+ year retention | Exceeds typical requirements for granularity and integrity. |

## 5. Questions for FSRA

### Regulatory Perimeter

1. Is a Financial Services Permission (FSP) to operate an MTF the correct permission for our proposed scope, or are additional/alternative permissions required?
2. Does the internal ledger settlement model for FR-1 require a separate custody permission under FSRA rules? If so, can this be applied for concurrently with the MTF permission?
3. Are there specific requirements or restrictions on offering AED-denominated trading pairs?

### Accepted Virtual Assets

4. We plan to offer trading in BTC, ETH, SOL, and USDT-denominated pairs. Are these currently on the FSRA Accepted Virtual Assets list? What is the process for confirming or requesting acceptance of specific assets?
5. Is USDT (Tether) accepted as a virtual asset for trading on an ADGM-regulated MTF? Are there specific stablecoin-related requirements?

### Application Process

6. What are the key stages and expected timeline for an MTF FSP application?
7. Is concurrent development and application supported (i.e., can we build the platform while the application is in progress)?
8. What documentation is required for the initial application? Is a Technology Risk Assessment required at application stage or at a later stage?

### Operational Requirements

9. Are there specific ADGM requirements for data residency or data hosting location?
10. What are the FSRA's expectations for minimum capital adequacy for an MTF operator dealing in virtual assets?
11. Are there specific requirements for the composition or qualifications of the board and senior management?

### Ongoing Obligations

12. What periodic reporting will be required once operational?
13. What are the incident notification requirements and timelines?

## 6. Team Summary

| Role | Description |
|------|-------------|
| **Founder / CEO** | [Name — placeholder] |
| **CTO** | [Name — placeholder]; responsible for platform architecture and technology |
| **Head of Compliance** | [Name — placeholder]; responsible for regulatory affairs and AML |
| **Legal Counsel** | [Firm — placeholder]; ADGM-qualified legal advisor |

*Full CV/F&P documentation will be prepared as part of the formal application.*

## 7. Requested Next Steps

1. Pre-application meeting with FSRA to discuss the points above
2. Written guidance on custody permission requirement
3. Confirmation of accepted virtual asset status for proposed trading pairs
4. Guidance on expected application timeline

---

*This brief is prepared for engagement purposes only and does not constitute a formal application or binding commitment. All information is subject to change as the project develops and regulatory guidance is received.*
