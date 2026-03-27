# 4. Stakeholders and User Personas

## 4.1 Internal Stakeholders

### 4.1.1 Exchange Board / Venue Operator
- **Role**: Strategic decision-making, investment approval, regulatory liaison
- **Concerns**: Business viability, regulatory license, reputational risk, time-to-market
- **Needs from platform**: Demonstrable compliance readiness, commercial flexibility, scalable cost model
- **Interaction**: Receives dashboards and KPI reports; approves rule changes and new market launches

### 4.1.2 Chief Technology Officer (CTO)
- **Role**: Technology strategy, architecture governance, build-vs-buy decisions
- **Concerns**: Technical debt, vendor dependency, scalability ceiling, operational complexity
- **Needs from platform**: Clean architecture, proven performance, extensibility, talent availability
- **Interaction**: Architecture reviews, technology roadmap, vendor evaluations

### 4.1.3 Chief Risk Officer (CRO) / Head of Risk
- **Role**: Enterprise risk, market risk, operational risk, credit risk oversight
- **Concerns**: Systemic failure, participant default, market disruption, data loss
- **Needs from platform**: Real-time risk visibility, kill switch capability, configurable limits, DR assurance
- **Interaction**: Risk dashboards, limit configuration, incident post-mortems

### 4.1.4 Head of Compliance / Regulatory Affairs
- **Role**: Regulatory relationship, rule compliance, reporting obligations
- **Concerns**: Regulatory sanctions, audit findings, reporting accuracy, change management
- **Needs from platform**: Complete audit trails, regulatory report generation, clock sync evidence, surveillance data
- **Interaction**: Compliance reports, regulator examination support, rule change management

### 4.1.5 Head of Market Operations
- **Role**: Day-to-day trading session management, incident response, parameter changes
- **Concerns**: Session reliability, halt/resume efficiency, parameter accuracy, incident resolution time
- **Needs from platform**: Operations console, runbooks, automated alerts, replay tools
- **Interaction**: Market operations console, daily start-of-day/end-of-day procedures, incident management

### 4.1.6 Head of Market Surveillance
- **Role**: Detection and investigation of market abuse, manipulation, and policy violations
- **Concerns**: Detection coverage, false positive rate, investigation tooling, regulatory cooperation
- **Needs from platform**: Real-time alert feeds, order/trade reconstruction, participant activity reports
- **Interaction**: Surveillance platform integration, alert triage, regulatory data requests

### 4.1.7 Product Manager — Trading Products
- **Role**: Defining market models, order types, participant features, fee structures
- **Concerns**: Participant adoption, competitive positioning, feature parity, time to market
- **Needs from platform**: Configurable market models, rapid feature enablement, A/B testing support
- **Interaction**: Product configuration, participant feedback analysis, market model design

### 4.1.8 Engineering Lead / Development Team
- **Role**: Platform implementation, testing, deployment, maintenance
- **Concerns**: Code quality, test coverage, deployment safety, debugging capability
- **Needs from platform**: Clear specifications, testable architecture, CI/CD pipeline, observability
- **Interaction**: Source code, test suites, deployment pipelines, monitoring dashboards

### 4.1.9 SRE / Infrastructure Team
- **Role**: Platform reliability, infrastructure management, capacity planning
- **Concerns**: Uptime, performance degradation, capacity limits, incident response
- **Needs from platform**: Health metrics, capacity dashboards, automated alerting, DR runbooks
- **Interaction**: Infrastructure management, monitoring, on-call response

### 4.1.10 Security Team
- **Role**: Cybersecurity, access control, vulnerability management, incident response
- **Concerns**: Unauthorized access, data breach, DDoS, insider threat
- **Needs from platform**: Zero-trust architecture, audit logs, HSM integration, penetration test results
- **Interaction**: Security reviews, access control management, incident forensics

## 4.2 External Stakeholders

### 4.2.1 Broker-Dealer / Execution Firm
- **Role**: Routes client orders to the venue for execution
- **Persona**: Technical connectivity team + trading desk
- **Concerns**: Latency, fill rates, execution quality, connectivity reliability, certification effort
- **Needs from platform**: Low-latency gateway, comprehensive order types, reliable drop copy, clear API documentation
- **Certification**: Full order-entry certification including negative test cases

### 4.2.2 Market Maker / Liquidity Provider
- **Role**: Provides continuous two-sided quotes to maintain market liquidity
- **Persona**: Quantitative trading desk + technology team
- **Concerns**: Quoting latency, mass quote performance, self-trade prevention, cancel-on-disconnect, fairness
- **Needs from platform**: Ultra-low-latency binary gateway, mass quote support, priority programs, STP prevention modes
- **Certification**: Extended certification including mass quote, cancel-on-disconnect, auction participation

### 4.2.3 Buy-Side Institution (Asset Manager, Pension Fund, Hedge Fund)
- **Role**: Executes large orders, often through brokers or via DMA
- **Persona**: Portfolio manager + execution desk + compliance
- **Concerns**: Market impact, execution quality, information leakage, best execution evidence
- **Needs from platform**: Block trading, dark/midpoint matching, conditional orders, execution quality reports
- **Certification**: Via broker (indirect) or DMA certification (direct)

### 4.2.4 Retail Intermediary
- **Role**: Aggregates retail order flow and routes to venue
- **Persona**: Technology team + compliance
- **Concerns**: Simplicity, reliability, cost, best execution
- **Needs from platform**: REST/WebSocket gateway, standard order types, market data feed, clear fee schedule
- **Certification**: Simplified certification for basic order types

### 4.2.5 Issuer
- **Role**: Entity whose instruments are listed/traded on the venue
- **Persona**: Corporate treasury + investor relations + legal
- **Concerns**: Listing requirements, corporate action handling, market quality for their instruments
- **Needs from platform**: Listing workflow, corporate action notification, market quality reports
- **Certification**: Not applicable (no order entry)

### 4.2.6 Clearing House / CCP
- **Role**: Central counterparty clearing for trades executed on the venue
- **Persona**: Technology team + risk management
- **Concerns**: Trade capture completeness, real-time position updates, risk data feeds, netting accuracy
- **Needs from platform**: Real-time trade feed, position updates, give-up/take-up workflow, clearing member mapping
- **Integration**: Clearing adapter with defined SLAs

### 4.2.7 Custodian / Settlement Agent
- **Role**: Holds assets and processes settlement of trades
- **Persona**: Operations team + technology
- **Concerns**: Settlement instruction accuracy, corporate action handling, fail management
- **Needs from platform**: Settlement instruction generation, trade confirmation, corporate action data
- **Integration**: Post-trade adapter

### 4.2.8 Market Data Vendor
- **Role**: Redistributes market data to end consumers
- **Persona**: Technology team + product management
- **Concerns**: Data completeness, latency, gap recovery, licensing terms
- **Needs from platform**: Normalized market data feed, snapshot + incremental model, gap recovery mechanism
- **Integration**: Market data dissemination gateway

### 4.2.9 Regulator / Supervisory Authority
- **Role**: Oversees venue operations, market integrity, participant conduct
- **Persona**: Market supervision team + IT examination team
- **Concerns**: Market fairness, resilience, auditability, surveillance effectiveness, incident response
- **Needs from platform**: Audit trail access, surveillance data exports, incident reports, clock sync evidence, rule documentation
- **Integration**: Regulatory reporting feeds, examination access tools

### 4.2.10 Technology Vendor / Integrator
- **Role**: Implements, customizes, or extends the platform for a specific deployment
- **Persona**: Solution architects + developers
- **Concerns**: Documentation quality, API completeness, extensibility, support responsiveness
- **Needs from platform**: Comprehensive specification (this document), SDK, reference implementations, sandbox environment
- **Integration**: Development and certification environments

## 4.3 Persona-to-Interface Mapping

| Persona | Primary Interfaces | Latency Sensitivity |
|---------|-------------------|-------------------|
| Market Maker | Binary gateway, market data multicast | Ultra-low (deployment-profile-dependent; see §15) |
| Broker-Dealer | FIX gateway, drop copy, market data | High (< 100 µs gateway) |
| Buy-Side (DMA) | FIX gateway, conditional order API | High |
| Retail Intermediary | REST/WebSocket gateway | Moderate (< 10 ms) |
| Market Operations | Operations console (Web UI) | Low (< 1 s) |
| Surveillance | Surveillance export API, event stream | Moderate (< 1 s) |
| Clearing House | Trade feed adapter | Moderate (< 100 ms) |
| Regulator | Audit trail export, reporting API | Low (batch acceptable) |
| SRE / Infrastructure | Monitoring dashboards, metrics API | Low (< 5 s) |
