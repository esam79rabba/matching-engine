# 3. Target Market / Venue Types

## 3.1 Primary Venue Deployment Models

The platform is designed to be deployed across a range of venue types. Each deployment model implies different regulatory requirements, participant profiles, market models, and operational characteristics.

### 3.1.1 Regulated Exchange (National / Regional)

**Description**: A fully licensed exchange operating under a national securities regulator (e.g., SEC, ESMA NCA, MAS, ASIC). Operates as a self-regulatory organization (SRO) or recognized market operator (RMO).

**Characteristics**:
- Full regulatory license required (exchange license / RMO / RRM)
- Mandatory pre-trade and post-trade transparency
- Market surveillance obligations
- Participant membership framework
- Listing and admission of instruments
- CCP clearing integration typically mandatory
- Market maker programs with quoting obligations
- Formal rule book filed with regulator

**Asset Classes**: Cash equities, ETFs, listed derivatives, bonds (if exchange-traded), commodities

**Market Models**: Continuous order book with opening/closing auctions, volatility auctions, market maker schemes

**Key Platform Requirements**:
- Full audit trail with nanosecond timestamps
- Circuit breakers and volatility interruption mechanisms
- Market maker obligation monitoring
- Best execution data publication
- Regulatory reporting hooks (e.g., MiFID II RTS 25 clock sync, transaction reporting)

---

### 3.1.2 Multilateral Trading Facility (MTF) / Organized Trading Facility (OTF)

**Description**: An EU-regulated venue under MiFID II that matches third-party buying and selling interests. MTFs operate on non-discretionary rules; OTFs allow operator discretion for non-equity instruments.

**Characteristics**:
- MiFID II authorization required
- Non-discretionary matching (MTF) or discretionary execution (OTF)
- Pre-trade and post-trade transparency regime (subject to waivers for certain instruments/sizes)
- Transaction reporting to ARM
- Systematic internaliser boundary management

**Asset Classes**:
- MTF: Equities, equity-like instruments, bonds, derivatives
- OTF: Non-equity instruments only (bonds, derivatives, emission allowances)

**Market Models**: Continuous order book, periodic auctions, RFQ, hybrid voice-electronic

**Key Platform Requirements**:
- Waiver management (LIS, reference price, negotiated trade, order management facility)
- Dark pool functionality (if using reference price waiver)
- Periodic auction mechanism
- Pre-trade transparency calculations and publication
- RFQ workflow support (OTF)

---

### 3.1.3 Alternative Trading System (ATS)

**Description**: A US-regulated trading venue registered with the SEC and FINRA. Operates under Regulation ATS.

**Characteristics**:
- SEC Form ATS / ATS-N registration
- Fair access requirements (if above volume thresholds)
- FINRA membership and examination
- No self-listing capability (instruments listed elsewhere)
- SIP reporting obligations
- Reg NMS compliance (Order Protection Rule, Sub-Penny Rule)

**Asset Classes**: US equities, ETFs, fixed income (if ATS-N)

**Market Models**: Continuous matching, midpoint crossing, conditional orders, dark pools

**Key Platform Requirements**:
- NBBO reference price integration
- Reg NMS trade-through prevention
- Reg SHO short-sale compliance hooks
- OATS / CAT reporting support
- Midpoint and pegged order types
- Minimum quantity and conditional order support

---

### 3.1.4 Broker Internal Crossing Network

**Description**: An internal matching system operated by a broker-dealer to cross client orders before routing to external venues.

**Characteristics**:
- May or may not require ATS registration (jurisdiction-dependent)
- Typically operates as agent crossing
- Best execution obligations on the broker
- Limited transparency requirements (internal)
- Client consent and disclosure requirements

**Asset Classes**: Equities, fixed income, FX (depending on broker)

**Market Models**: Midpoint crossing, schedule-based matching, conditional matching

**Key Platform Requirements**:
- Best execution audit trail
- Client priority and fairness rules
- External venue reference price integration
- Conflict-of-interest controls
- Information barrier enforcement

---

### 3.1.5 Digital-Asset Exchange

**Description**: A platform for trading cryptocurrencies, security tokens, and other digital assets. Regulatory status varies significantly by jurisdiction.

**Characteristics**:
- Rapidly evolving regulatory landscape (EU MiCA, SG PSA, UAE VARA, HK SFC Type 7)
- Custody integration often required (hot/cold wallet management)
- 24/7 trading (no session model unless mandated)
- Atomic settlement possible (DvP on-chain)
- Higher volatility → wider price bands needed
- Retail and institutional participant mix

**Asset Classes**: Cryptocurrencies, stablecoins, security tokens, utility tokens, NFTs (if fungible)

**Market Models**: Continuous order book, OTC/RFQ desk, AMM hybrid (optional)

**Key Platform Requirements**:
- Wallet/custody integration layer
- Real-time proof-of-reserves hooks
- 24/7 operational model
- Wider dynamic price bands for volatile assets
- AML/KYC integration at onboarding and transaction level
- Chain-specific settlement adapters

---

### 3.1.6 Commodity / Energy / Carbon Venue

**Description**: A trading venue for physical and financial commodity contracts, energy products, and environmental instruments.

**Characteristics**:
- Commodity-specific regulation (CFTC, EU REMIT, national energy regulators)
- Position limit frameworks
- Physical delivery settlement workflows
- Market maker / liquidity provider programs
- Seasonality in trading patterns
- Large block trade workflows common

**Asset Classes**: Energy (power, gas, oil), metals, agriculture, carbon credits, renewable energy certificates

**Market Models**: Continuous order book, auction, RFQ, negotiated block, strip/calendar spread trading

**Key Platform Requirements**:
- Position limit monitoring and enforcement
- Physical delivery workflow support
- Spread and strip instrument support
- Large-in-scale block trade workflow
- REMIT / CFTC reporting hooks
- Environmental instrument registry integration

---

### 3.1.7 Regional Multi-Asset Market Operator

**Description**: An emerging-market exchange operator seeking to modernize technology across multiple asset classes, potentially replacing legacy systems.

**Characteristics**:
- Multiple asset classes on a single platform
- May operate in a less mature regulatory environment
- Technology modernization driver
- Potential for hosted/managed service delivery
- Local language and timezone requirements
- Phased migration from legacy systems

**Asset Classes**: All — equities, bonds, derivatives, FX, commodities

**Market Models**: All — varies by asset class and local market convention

**Key Platform Requirements**:
- Multi-currency support
- Localization framework
- Legacy system migration tools
- Hosted deployment option
- Training and knowledge transfer program

## 3.2 Venue Type Requirements Matrix

| Requirement | Regulated Exchange | MTF/OTF | ATS | Broker Crossing | Digital Asset | Commodity/Energy | Regional Multi-Asset |
|------------|:-:|:-:|:-:|:-:|:-:|:-:|:-:|
| Continuous order book | ✅ | ✅ | ✅ | ◐ | ✅ | ✅ | ✅ |
| Auction mechanisms | ✅ | ✅ | ◐ | ✗ | ◐ | ✅ | ✅ |
| RFQ workflow | ◐ | ✅ | ✗ | ✗ | ◐ | ✅ | ◐ |
| Dark/midpoint matching | ◐ | ✅ | ✅ | ✅ | ✗ | ✗ | ◐ |
| Block trade workflow | ✅ | ✅ | ◐ | ✗ | ◐ | ✅ | ✅ |
| Market maker program | ✅ | ✅ | ◐ | ✗ | ✅ | ✅ | ✅ |
| Pre-trade transparency | ✅ | ✅ | ◐ | ✗ | Varies | ✅ | ✅ |
| Post-trade transparency | ✅ | ✅ | ✅ | ◐ | Varies | ✅ | ✅ |
| Surveillance | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| CCP clearing integration | ✅ | ✅ | ◐ | ✗ | ◐ | ✅ | ✅ |
| Custody/wallet integration | ✗ | ✗ | ✗ | ✗ | ✅ | ✗ | ◐ |
| 24/7 operation | ✗ | ✗ | ✗ | ✗ | ✅ | ◐ | ✗ |
| Position limits | ◐ | ◐ | ✗ | ✗ | ✗ | ✅ | ◐ |
| Physical delivery | ✗ | ✗ | ✗ | ✗ | ✗ | ✅ | ◐ |

Legend: ✅ = Required, ◐ = Optional/Conditional, ✗ = Not Applicable

## 3.3 Deployment Topology Options

| Topology | Description | Best For |
|----------|-------------|----------|
| **Single-site colocation** | All components in one data center, participant co-location | Low-latency venues, regulated exchanges |
| **Dual-site active-passive** | Primary site + DR site with journal replay | Most regulated venues |
| **Multi-region active-active** | Multiple sites processing independently with reconciliation | 24/7 digital-asset venues, global FX |
| **Cloud-hosted** | Platform runs in cloud infrastructure (bare-metal cloud or VMs) | Startup venues, broker crossing, commodity/carbon |
| **Hybrid** | Matching engine on bare metal; surrounding services in cloud/containers | Balancing latency with operational flexibility |
| **Managed / SaaS** | Platform operated as a service for the venue operator | Regional operators, new market entrants |
