# Digital Asset FR-1 Scope — ADGM/FSRA

**Status**: Active planning artifact — based on closed founder decisions
**Decisions**: D-02 (digital assets), D-03 (ADGM/FSRA) — CLOSED

---

## FR-1 Market Scope

### Asset Subtype
- **Spot digital-asset order-book trading** (not derivatives, not margin, not lending)
- Trading pairs: crypto-to-fiat and crypto-to-crypto
- Examples: BTC/USD, ETH/USD, BTC/USDT, ETH/BTC

### Instrument Model (Digital-Asset Specific)

| Field | Typical Value | Notes |
|-------|--------------|-------|
| InstrumentType | DIGITAL | Enum from baseline |
| BaseCurrency | "BTC", "ETH", etc. | Base asset of the pair |
| QuoteCurrency | "USD", "USDT", etc. | Quote asset |
| PricePrecision | 2–8 decimal places | BTC/USD: 2; altcoin/BTC: 8 |
| QuantityPrecision | 0–8 decimal places | BTC: 8 (satoshi); whole tokens for some |
| PriceMultiplier | 100–100000000 | Internal integer = price × multiplier |
| QuantityMultiplier | 1–100000000 | Internal integer = qty × multiplier |
| TickSize | Varies (e.g., 0.01 for BTC/USD) | Per trading pair |
| LotSize | Varies (e.g., 0.00000001 BTC) | Fractional quantities standard |
| MaxOrderQty | Per pair | Higher for stablecoins |
| StaticPriceBandPct | 10–30% | Wider than equities due to volatility |
| DynamicPriceBandPct | 5–15% | Wider than equities |

### Session Model
- **FR-1 assumption: session-based trading** (not 24/7)
- Trading hours aligned with ADGM business hours or extended hours (e.g., 08:00–22:00 GST)
- 24/7 trading is a Phase 2+ consideration (requires operational model change)
- Maintenance window: daily (e.g., 22:00–08:00) or as defined by ADGM/FSRA

**Rationale**: 24/7 trading fundamentally changes the operational model (staffing, monitoring, incident response). FR-1 uses session-based trading to manage operational risk. This is consistent with ADR-005 (FR-1 scope boundaries).

### Key Digital-Asset Differences from Equities

| Aspect | Equities | Digital Assets (FR-1) |
|--------|----------|----------------------|
| Price precision | 2–4 decimals | 2–8 decimals |
| Quantity precision | Whole shares (typically) | Fractional (up to 8 decimals) |
| Tick size | Price-dependent table | Per trading pair |
| Lot size | Round lot (e.g., 100) | Fractional (e.g., 0.00000001) |
| Price bands | 5–10% | 10–30% (wider due to volatility) |
| Settlement | T+1/T+2 via CSD | T+0 possible (on-chain); or internal ledger |
| Trading hours | Session-based | FR-1: session-based; Enterprise: 24/7 |
| Short selling | Regulated | Not applicable for spot FR-1 |
| Corporate actions | Dividends, splits, etc. | Hard forks, airdrops (Phase 2+) |
| Custody | CSD / custodian | Wallet integration (Phase 2+) |

### What FR-1 Does NOT Include for Digital Assets

| Feature | Why Deferred |
|---------|-------------|
| 24/7 trading | Operational model change; FR-1 uses sessions |
| Wallet/custody integration | Complex; separate domain; use internal ledger for FR-1 |
| On-chain settlement | Requires chain-specific adapters; Phase 2+ |
| Margin trading | Different risk model; not spot |
| Derivatives (perps, futures) | Different instrument type; Phase 3+ |
| Staking/lending | Not trading venue functionality |
| Chain analytics (AML) | Phase 2 integration with Chainalysis/Elliptic |
| Hard fork handling | Phase 2+ instrument lifecycle |

### FR-1 Settlement Model (Digital Assets)
- **Internal ledger-based settlement** (not on-chain for FR-1)
- Participants deposit assets into venue-held accounts before trading
- Trades settle internally (balance transfer between participant accounts)
- Withdrawals processed batch or on-request (operational, not automated on-chain for FR-1)
- On-chain DvP settlement is a Phase 2+ capability

---

## ADGM / FSRA Compliance Overlay — Preliminary

### Regulatory Framework
- **Abu Dhabi Global Market (ADGM)** is a financial free zone in Abu Dhabi
- **Financial Services Regulatory Authority (FSRA)** is the ADGM financial regulator
- FSRA has a specific framework for **Virtual Asset Service Providers (VASPs)** and **Multilateral Trading Facilities (MTFs)** dealing in virtual assets

### License Type
- **Financial Services Permission (FSP)** to operate an MTF for virtual assets
- May also require: custody permission (if venue holds assets) — defer to Phase 2 if possible

### Key FSRA Requirements (Preliminary — Verify with Regulatory Advisor)

| Requirement | Platform Implication | FR-1 Status |
|-------------|---------------------|:-----------:|
| AML/KYC | Participant onboarding with identity verification | Hooks ready; integration Phase 2 |
| Transaction monitoring | Surveillance hooks for suspicious activity | File-based export (FR-1) |
| Technology risk assessment | Documented controls, resilience testing | Covered by baseline §19, §23 |
| Audit trail | Complete, timestamped, tamper-evident | Covered by baseline §17 |
| Clock synchronization | Per FSRA guidance (likely ≤ 1 ms acceptable) | PTP provides ≤ 1 µs — exceeds requirement |
| Business continuity | Documented BCP; tested recovery | Warm restart (FR-1); DR (Phase 2) |
| Capital requirements | Minimum capital for FSP holder | Business/legal — not platform scope |
| Fit and proper | Management and key personnel assessment | Business/legal |
| Cybersecurity | Controls, pen testing, incident response | Covered by baseline §18 |
| Market conduct | Fair and orderly trading; manipulation prevention | Surveillance hooks + STP + price bands |

### FSRA Engagement Timeline (Planning Assumption)

| Phase | Activity | Timeline |
|-------|----------|----------|
| Pre-application | Engage FSRA innovation desk; discuss scope | Month 1–2 |
| Application prep | Assemble documentation; technology risk assessment | Month 2–4 |
| Application submission | Submit FSP application with supporting docs | Month 4–5 |
| Review period | FSRA review; Q&A; possible conditions | Month 5–8 |
| Conditional approval | Receive in-principle approval with conditions | Month 8–9 |
| Final approval | Meet conditions; receive FSP | Month 9–12 |

**Note**: Timeline is a planning assumption. Actual timeline depends on FSRA engagement quality and application completeness. Regulatory advisor engagement should begin immediately.

---

## Digital-Asset-Specific Instrument Configuration Example

```yaml
instrument:
  instrument_id: 1001
  symbol: "BTC-USD"
  type: DIGITAL
  base_currency: "BTC"
  quote_currency: "USD"
  price_precision: 2
  price_multiplier: 100          # 50000.01 stored as 5000001
  quantity_precision: 8
  quantity_multiplier: 100000000  # 0.00000001 BTC stored as 1
  tick_size: 0.01                 # $0.01 minimum price increment
  lot_size: 0.00000001            # 1 satoshi minimum quantity
  max_order_qty: 100              # 100 BTC max per order
  static_price_band_pct: 20.0     # wider than equities
  dynamic_price_band_pct: 10.0
  circuit_breaker_enabled: false   # FR-1 (stretch goal to enable)
  priority_model: PRICE_TIME
  partition_id: 1
  market_segment_id: 1            # SPOT
  session_profile_id: 1           # ADGM_EXTENDED_HOURS

  # Digital-asset-specific
  settlement_model: INTERNAL_LEDGER
  min_notional_usd: 10.0          # minimum order value $10
  maker_fee_rate: -0.001          # -0.1% (rebate) — provisional
  taker_fee_rate: 0.002           # 0.2% — provisional
```
