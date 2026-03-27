# Digital-Asset Trading Pair Configuration — FR-1 Candidate List

**Status**: Candidate FR-1 planning scope — NOT final launch set
**Decisions**: D-02 (digital assets) CLOSED; D-03 (ADGM/FSRA) CLOSED
**Note**: All pairs subject to FSRA Accepted Virtual Assets list confirmation. Pairs not on the FSRA accepted list cannot be offered on a regulated venue without specific approval.

---

## Selection Methodology

Pairs selected for FR-1 candidate list based on:

1. **Regulatory eligibility**: High likelihood of being on FSRA Accepted Virtual Assets list (major, established assets)
2. **Liquidity availability**: Assets where market makers can realistically provide two-sided quotes
3. **Operational simplicity**: Avoid exotic assets with complex tokenomics, rebasing, or non-standard settlement
4. **Commercial plausibility**: Pairs that institutional participants actually want to trade
5. **Diversity**: Mix of crypto-to-fiat and crypto-to-crypto; BTC and ETH as anchors
6. **Manageable scope**: 5–10 pairs keeps FR-1 operationally simple while demonstrating multi-pair capability

---

## Candidate Trading Pairs

| # | Pair | Base | Quote | Rationale | Tick Size | Qty Precision | Price Band (Static) | Liquidity |
|---|------|------|-------|-----------|:---------:|:-------------:|:-------------------:|:---------:|
| 1 | **BTC-USD** | BTC | USD | Primary crypto benchmark; highest liquidity | 0.01 | 8 (satoshi) | 20% | High |
| 2 | **ETH-USD** | ETH | USD | Second-largest asset; institutional demand | 0.01 | 8 | 20% | High |
| 3 | **BTC-USDT** | BTC | USDT | Most traded pair globally; stablecoin quote | 0.01 | 8 | 20% | Very high |
| 4 | **ETH-USDT** | ETH | USDT | High volume; stablecoin quote | 0.01 | 8 | 20% | Very high |
| 5 | **SOL-USD** | SOL | USD | Top-10 asset; institutional interest growing | 0.001 | 6 | 25% | Medium |
| 6 | **ETH-BTC** | ETH | BTC | Primary crypto-to-crypto cross | 0.000001 | 6 | 15% | High |
| 7 | **USDT-USD** | USDT | USD | Stablecoin peg; tight spread; operational anchor | 0.0001 | 2 | 2% | Very high |
| 8 | **BTC-AED** | BTC | AED | Local fiat pair for ADGM participants | 0.01 | 8 | 20% | Low (build) |

---

## Detailed Configuration per Pair

### BTC-USD

```yaml
instrument_id: 1001
symbol: "BTC-USD"
type: DIGITAL
base_currency: "BTC"
quote_currency: "USD"
price_precision: 2
price_multiplier: 100              # $50000.01 → 5000001
quantity_precision: 8
quantity_multiplier: 100000000     # 0.00000001 BTC → 1
tick_size: 0.01
min_quantity: 0.00001              # ~$0.50 at $50K
max_quantity: 100                  # $5M at $50K
min_notional_usd: 10.0
static_price_band_pct: 20.0
dynamic_price_band_pct: 10.0
priority_model: PRICE_TIME
session_profile: ADGM_EXTENDED     # 08:00-22:00 GST (working assumption)
partition_id: 1
market_segment: SPOT
```

### ETH-USD

```yaml
instrument_id: 1002
symbol: "ETH-USD"
price_precision: 2
price_multiplier: 100
quantity_precision: 8
tick_size: 0.01
min_quantity: 0.0001
max_quantity: 5000
min_notional_usd: 10.0
static_price_band_pct: 20.0
dynamic_price_band_pct: 10.0
```

### ETH-BTC (crypto-to-crypto)

```yaml
instrument_id: 1006
symbol: "ETH-BTC"
base_currency: "ETH"
quote_currency: "BTC"
price_precision: 6
price_multiplier: 1000000          # 0.035000 → 35000
quantity_precision: 6
tick_size: 0.000001
min_quantity: 0.001
max_quantity: 50000
min_notional_btc: 0.0001
static_price_band_pct: 15.0
dynamic_price_band_pct: 8.0
```

### USDT-USD (stablecoin peg)

```yaml
instrument_id: 1007
symbol: "USDT-USD"
price_precision: 4
price_multiplier: 10000            # 1.0001 → 10001
quantity_precision: 2
tick_size: 0.0001
min_quantity: 10.0
max_quantity: 10000000
static_price_band_pct: 2.0         # very tight — stablecoin
dynamic_price_band_pct: 1.0
```

---

## Session Model (Working Assumption)

| Parameter | Value | Notes |
|-----------|-------|-------|
| Trading hours | 08:00–22:00 GST (Sun–Thu) | ADGM business week; extended hours |
| Pre-open | 07:30–08:00 | Orders accepted, no matching |
| Continuous | 08:00–22:00 | Normal trading |
| Maintenance | 22:00–07:30 | System maintenance; no orders |
| Weekend | Friday 22:00 – Sunday 07:30 | Closed |

**Note**: 24/7 trading is a Phase 2+ consideration. FR-1 uses session-based trading per ADR-005.

---

## Market-Maker Liquidity Assumptions

| Pair | MM Required? | Expected Spread | Depth Target | Notes |
|------|:----------:|:---------------:|:------------:|-------|
| BTC-USD | Yes | $1–5 | $100K+ each side | Must have at least 1 MM committed |
| ETH-USD | Yes | $0.50–2 | $50K+ each side | |
| BTC-USDT | Yes | $1–5 | $100K+ | Highest volume globally |
| ETH-USDT | Yes | $0.50–2 | $50K+ | |
| SOL-USD | Desirable | $0.01–0.05 | $20K+ | Optional for launch |
| ETH-BTC | Desirable | 0.00001–0.00005 | 10 BTC+ | Cross-pair |
| USDT-USD | Yes | 0.0001–0.0005 | $500K+ | Stablecoin anchor |
| BTC-AED | Desirable | AED 50–200 | AED 100K+ | Local fiat; may build slowly |

**Minimum for launch**: At least 1 market maker committed for BTC-USD and ETH-USD. Other pairs can launch with wider spreads initially.

---

## FSRA Accepted Virtual Assets Checkpoint

Before finalizing the pair list, confirm each base/quote asset against:
- FSRA Accepted Virtual Assets list (current published list)
- FSRA guidance on stablecoins (USDT status under ADGM)
- FSRA guidance on local fiat pairs (AED)

| Asset | Expected FSRA Status | Confirmation Needed? |
|-------|:-------------------:|:--------------------:|
| BTC | Likely accepted | Confirm |
| ETH | Likely accepted | Confirm |
| SOL | Likely accepted | Confirm |
| USDT | Requires confirmation (stablecoin) | **Yes — priority** |
| USD | Fiat — always accepted | No |
| AED | Fiat — always accepted | No |

*This list is a candidate planning scope. Final launch pairs confirmed after FSRA accepted-asset verification and market-maker commitment.*
