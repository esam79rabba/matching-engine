# 8. Supported Asset Classes and Their Specific Needs

## 8.1 Design Approach

The matching engine core is **asset-class-agnostic**. It operates on a generic instrument model with configurable parameters. Asset-class-specific behavior (tick sizes, lot sizes, settlement conventions, price formats, expiry handling) is injected through **instrument configuration** and **market model parameters** at the reference data layer.

This section defines the per-asset-class configuration requirements that the platform must support.

## 8.2 Cash Equities

### 8.2.1 Characteristics
| Parameter | Typical Configuration |
|-----------|----------------------|
| Price format | Decimal (2–4 decimal places) |
| Tick size | Variable by price range (see §7.8) |
| Lot size | Round lot (100 shares typical) or odd lot allowed |
| Trading hours | Session-based (pre-open, continuous, close) |
| Settlement | T+1 or T+2 via CSD |
| Clearing | CCP or bilateral |
| Market model | Continuous LOB with opening/closing auctions |
| Priority | Price-time (FIFO) |

### 8.2.2 Specific Requirements
- **Corporate actions**: Price adjustments for dividends, splits, rights issues (reference data update before session open)
- **Short selling**: Locate/borrow integration hooks; short-sell flagging on orders
- **Circuit breakers**: Static and dynamic thresholds with volatility auctions
- **Market maker obligations**: Continuous quoting requirements with maximum spread and minimum size
- **Ex-date handling**: Automatic reference price adjustment on ex-date
- **Closing price**: Official closing price from closing auction
- **Odd lots**: Configurable treatment (separate book, integrated, or rejected)

### 8.2.3 Order Types Required
Market, Limit, Stop, Stop-Limit, Pegged (primary, midpoint, market), Iceberg, IOC, FOK, GTC, GTD, Day, Post-Only, Auction-Only

---

## 8.3 Exchange-Traded Funds (ETFs)

### 8.3.1 Characteristics
| Parameter | Typical Configuration |
|-----------|----------------------|
| Price format | Same as equities |
| Tick size | Same regime as equities (may be tighter) |
| Lot size | Same as equities |
| Settlement | T+1 or T+2 |
| Market model | Continuous LOB with auctions |
| Priority | Price-time |

### 8.3.2 Specific Requirements
- **NAV proximity monitoring**: Track deviation of trading price from indicative NAV
- **Creation/redemption**: Not matching engine function, but reference data must track creation unit size
- **Authorized participants**: Special entitlements for AP firms
- **iNAV dissemination**: Real-time indicative NAV publication (feed from external source)
- **Tracking error alerts**: Operational monitoring when price deviates significantly from NAV

---

## 8.4 Fixed Income / Bonds

### 8.4.1 Characteristics
| Parameter | Typical Configuration |
|-----------|----------------------|
| Price format | Percentage of par (e.g., 99.875) or yield |
| Tick size | Varies (1/32nds for US Treasuries, basis points for corporate bonds) |
| Lot size | Minimum denomination (e.g., $1,000 face value increments) |
| Settlement | T+1 (US Treasuries) to T+2 (corporate) |
| Market model | RFQ, periodic auction, or continuous LOB |
| Priority | Price-time or configurable |

### 8.4.2 Specific Requirements
- **Price representation**: Support for fractional pricing (32nds, 64ths) and decimal
- **Accrued interest**: Not calculated by matching engine, but trade record must include trade date for accrued interest calculation
- **Yield calculation**: May be displayed alongside price (calculated externally, published via market data)
- **RFQ workflow**: Request-for-quote with dealer response, auto-execution, and timeout
- **Streaming quotes**: Dealers stream continuous prices; taker selects and executes
- **Multi-dealer competition**: RFQ sent to multiple dealers; best response wins
- **Minimum quantity**: Common for institutional orders
- **Benchmark bonds**: Special treatment for on-the-run government bonds

### 8.4.3 Order Types Required
Limit, RFQ Request, RFQ Response, Streaming Quote, IOC, FOK, Day, Auction-Only

---

## 8.5 Listed Derivatives — Futures

### 8.5.1 Characteristics
| Parameter | Typical Configuration |
|-----------|----------------------|
| Price format | Varies by contract (decimal, fractions, basis points) |
| Tick size | Fixed per contract specification |
| Lot size | 1 contract (standardized) |
| Settlement | Daily mark-to-market; final cash or physical delivery |
| Clearing | CCP mandatory |
| Market model | Continuous LOB with auctions |
| Priority | Price-time OR pro-rata (configurable) |

### 8.5.2 Specific Requirements
- **Expiry management**: Contract month/quarter rollover; instrument lifecycle from listing through last trading day to expiry
- **Settlement price calculation**: End-of-day settlement price algorithm (VWAP, auction, or manual)
- **Position limits**: Per-participant, per-instrument, per-expiry, and aggregate
- **Spread/strategy trading**: Calendar spreads, inter-commodity spreads
- **Implied matching**: Implied-in and implied-out pricing from outright and spread order books
- **Daily settlement**: Mark-to-market variation margin calculation support (data feed to clearing)
- **Final settlement**: Cash settlement or physical delivery workflow triggers
- **Delivery period**: Special trading rules during delivery period
- **Price limits**: Daily price limit (lock limit) with limit-up/limit-down behavior
- **Block trade minimum**: Large minimum size for off-book block trades

### 8.5.3 Order Types Required
Market, Limit, Stop, Stop-Limit, IOC, FOK, GTC, GTD, Day, Spread/Strategy, Mass Quote, Iceberg

---

## 8.6 Listed Derivatives — Options

### 8.6.1 Characteristics
| Parameter | Typical Configuration |
|-----------|----------------------|
| Price format | Decimal (premium per unit) |
| Tick size | Variable by premium level |
| Lot size | 1 contract (standardized multiplier) |
| Settlement | Premium payment; exercise/assignment |
| Clearing | CCP mandatory |
| Market model | Continuous LOB, often with RFQ for complex strategies |
| Priority | Price-time, pro-rata, or parity (varies by exchange) |

### 8.6.2 Specific Requirements
- **Series generation**: Automatic strike price generation based on underlying price movement
- **Expiry management**: Daily, weekly, monthly, quarterly expiries
- **Exercise/assignment**: American, European, Bermudan style — workflow triggers (not matching engine function, but event generation)
- **Complex orders / strategies**: Multi-leg orders (verticals, straddles, strangles, butterflies, condors)
- **RFQ for complex**: Request-for-quote workflow for multi-leg strategies
- **Delta hedging**: Market maker functionality (not matching engine, but connectivity for rapid hedge execution)
- **Implied volatility**: Display alongside price (calculated externally)
- **Position limits**: Per series, per class, per underlying
- **Auto-exercise**: End-of-day automatic exercise of in-the-money options (event trigger)

### 8.6.3 Order Types Required
Market, Limit, IOC, FOK, GTC, Day, Complex/Strategy, Mass Quote, RFQ Request/Response, Parity allocation support

---

## 8.7 FX Spot / Forwards

### 8.7.1 Characteristics
| Parameter | Typical Configuration |
|-----------|----------------------|
| Price format | 4–5 decimal places (pips) |
| Tick size | 0.00001 (half-pip) or 0.0001 (full pip) |
| Lot size | Standard lot (100,000 base currency units) or configurable |
| Settlement | T+2 spot, T+1 (CAD, USD pairs), custom for forwards |
| Market model | Streaming quotes, RFQ, or continuous LOB |
| Priority | Price-time |

### 8.7.2 Specific Requirements
- **Currency pair configuration**: Base/quote currency, pip definition, settlement conventions
- **Streaming price model**: Liquidity providers stream prices; takers hit/lift
- **Last look**: Optional configurable window for liquidity providers to accept/reject (controversial — must be transparent)
- **Reject/timeout behavior**: What happens when a streaming quote acceptance is rejected
- **Swap points**: Forward pricing as swap points from spot (display convention)
- **Fixing rates**: Integration with WM/Reuters or other fixing sources for benchmark orders
- **Settlement date calculation**: Business day conventions (following, modified following, preceding)
- **NDFs**: Non-deliverable forwards with fixing and cash settlement
- **24/5.5 trading**: Near-continuous trading from Sunday evening to Friday evening

### 8.7.3 Order Types Required
Limit, Market, IOC, Streaming Quote, RFQ Request/Response, Pegged, Time-Weighted

---

## 8.8 Commodities / Energy Contracts

### 8.8.1 Characteristics
| Parameter | Typical Configuration |
|-----------|----------------------|
| Price format | Varies ($/MWh, $/barrel, $/tonne, cents/bushel) |
| Tick size | Contract-specific |
| Lot size | Contract-specific (e.g., 1,000 barrels, 1 MW) |
| Settlement | Physical or financial |
| Market model | Continuous LOB, auction, RFQ, negotiated block |
| Priority | Price-time or pro-rata |

### 8.8.2 Specific Requirements
- **Physical delivery**: Delivery location, delivery period, quality specifications (reference data, not matching)
- **Strip trading**: Trading multiple consecutive delivery periods as a single order
- **Spread trading**: Inter-commodity, time spreads, location spreads
- **Position limits**: Spot month, single month, all months aggregate
- **Position reporting**: Large trader reporting thresholds
- **REMIT / CFTC reporting**: Regulatory reporting hooks
- **Seasonal patterns**: Trading activity varies by delivery period
- **Negative prices**: Support for negative prices (e.g., power markets, natural gas)
- **Block trade thresholds**: Minimum size for off-exchange block trades
- **EFP/EFS**: Exchange for physical / exchange for swap workflows

### 8.8.3 Order Types Required
Market, Limit, Stop, Stop-Limit, IOC, FOK, GTC, Day, Strip, Spread/Strategy, Block, Mass Quote

---

## 8.9 Carbon Credits / Environmental Instruments

### 8.9.1 Characteristics
| Parameter | Typical Configuration |
|-----------|----------------------|
| Price format | $/tonne CO2e (2 decimal places) |
| Tick size | 0.01 or 0.05 |
| Lot size | 1 tonne or 1,000 tonnes |
| Settlement | Registry transfer |
| Market model | Continuous LOB, auction, RFQ |
| Priority | Price-time |

### 8.9.2 Specific Requirements
- **Registry integration**: Settlement via carbon credit registry (e.g., Verra, Gold Standard, EU ETS registry)
- **Vintage tracking**: Credits have issuance vintage affecting value (reference data attribute)
- **Project type**: Different project types (renewable energy, forestry, methane capture) may be different instruments
- **Retirement workflow**: Participant retires credits for compliance or voluntary purposes (post-trade event)
- **Compliance vs. voluntary**: Different market segments with different rules
- **Auction-based allocation**: Government/regulator auctions for new allowances (sealed-bid)
- **Banking and borrowing**: Compliance credits may be banked or borrowed across periods
- **Verification status**: Only verified credits tradeable (reference data flag)

### 8.9.3 Order Types Required
Market, Limit, IOC, Day, GTC, Auction-Only, Block

---

## 8.10 Digital Assets / Tokenized Instruments

### 8.10.1 Characteristics
| Parameter | Typical Configuration |
|-----------|----------------------|
| Price format | High precision (8 decimal places for crypto) |
| Tick size | Very small (e.g., 0.01 for BTC/USD, 0.000001 for altcoins) |
| Lot size | Fractional (e.g., 0.00000001 BTC) |
| Settlement | On-chain atomic or batched; T+0 possible |
| Trading hours | 24/7 (unless jurisdictionally restricted) |
| Market model | Continuous LOB |
| Priority | Price-time |

### 8.10.2 Specific Requirements
- **High decimal precision**: Up to 18 decimal places for ERC-20 tokens
- **24/7 operation**: No session model (or configurable session windows if required by regulation)
- **Wallet integration**: Custody/wallet balance checks for pre-trade risk (external service)
- **Chain settlement**: Adapter for on-chain settlement (blockchain-specific)
- **Proof of reserves**: Real-time or periodic proof-of-reserves data feed
- **Wider price bands**: Higher volatility requires wider dynamic price bands
- **Market making incentives**: Aggressive programs needed for illiquid pairs
- **Stablecoin pairs**: Treatment of stablecoin/fiat pairs
- **Token listing/delisting**: Rapid instrument lifecycle (faster than traditional securities)
- **AML transaction monitoring**: Integration with chain analytics (Chainalysis, Elliptic)
- **Negative rebates / maker incentives**: Common fee model

### 8.10.3 Order Types Required
Market, Limit, Stop, Stop-Limit, Iceberg, IOC, FOK, GTC, Post-Only, Reduce-Only

## 8.11 Asset Class Configuration Matrix

| Parameter | Equities | ETFs | Bonds | Futures | Options | FX | Commodities | Carbon | Digital Assets |
|-----------|:--------:|:----:|:-----:|:-------:|:-------:|:--:|:-----------:|:------:|:--------------:|
| Price-time priority | ✅ | ✅ | ✅ | ◐ | ◐ | ✅ | ◐ | ✅ | ✅ |
| Pro-rata priority | ✗ | ✗ | ✗ | ◐ | ◐ | ✗ | ◐ | ✗ | ✗ |
| Continuous LOB | ✅ | ✅ | ◐ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| Auctions | ✅ | ✅ | ◐ | ✅ | ✅ | ✗ | ✅ | ✅ | ◐ |
| RFQ | ✗ | ✗ | ✅ | ✗ | ✅ | ✅ | ✅ | ✅ | ✗ |
| Streaming quotes | ✗ | ✗ | ✅ | ✗ | ✗ | ✅ | ✗ | ✗ | ✗ |
| Complex/strategy orders | ✗ | ✗ | ✗ | ✅ | ✅ | ✗ | ✅ | ✗ | ✗ |
| Implied matching | ✗ | ✗ | ✗ | ✅ | ✗ | ✗ | ✅ | ✗ | ✗ |
| Negative prices | ✗ | ✗ | ✗ | ✅ | ✗ | ✗ | ✅ | ✗ | ✗ |
| Fractional quantities | ✗ | ✗ | ✅ | ✗ | ✗ | ✅ | ✗ | ✗ | ✅ |
| 24/7 trading | ✗ | ✗ | ✗ | ✗ | ✗ | ◐ | ✗ | ✗ | ✅ |
| Position limits | ◐ | ✗ | ✗ | ✅ | ✅ | ✗ | ✅ | ✅ | ✗ |
| Physical delivery | ✗ | ✗ | ✗ | ◐ | ✗ | ✗ | ✅ | ✗ | ✗ |
| Short sell controls | ✅ | ✅ | ✗ | ✗ | ✗ | ✗ | ✗ | ✗ | ✗ |
| Circuit breakers | ✅ | ✅ | ◐ | ✅ | ✅ | ✗ | ✅ | ✅ | ✅ |

Legend: ✅ = Required, ◐ = Optional/Configurable, ✗ = Not Applicable
