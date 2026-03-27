# 9. Trading Models Supported

## 9.1 Overview

The platform supports multiple trading models, selectable per instrument, market segment, or trading session. A single instrument may transition between models during a trading day (e.g., auction → continuous → auction → close).

## 9.2 Trading Model Catalog

### 9.2.1 Continuous Limit Order Book (CLOB)

**REQ-TM-001**: Continuous Limit Order Book Matching

| Attribute | Detail |
|-----------|--------|
| **Purpose** | Continuous price discovery through immediate matching of aggressive orders against resting liquidity |
| **Business Rationale** | The most common and efficient model for liquid instruments; provides real-time price discovery |
| **Matching Behavior** | Incoming order immediately checked against opposite side; matches at resting order's price. Limit order residual rests in book. Market order residual handled per venue policy (cancel or market-to-limit with collar; see §11.4.3). |
| **Priority** | Configurable: price-time, pro-rata, hybrid (see §7.4) |
| **Applicable Asset Classes** | All |
| **Configuration Parameters** | `matching_model=CONTINUOUS`, `priority_model`, tick size table, lot size, price bands |
| **Acceptance Criteria** | Aggressive order matches at best available price; limit order partial fills create resting residual; market order residual per venue policy (see §11.4.3); all fills journaled with correct timestamps |
| **Operational Implications** | Requires real-time market data dissemination; pre-trade risk checks on every order |
| **Priority** | **Mandatory** |

**Sequence — Limit Order Match in Continuous Trading**:
```
Participant A                    Gateway              Matching Engine            Market Data
     │                             │                        │                       │
     │── New Limit Buy @100.02 ──→│                        │                       │
     │                             │── Validate + Seq ────→│                       │
     │                             │                        │── Match vs Ask @100.02│
     │                             │                        │── Generate Exec ─────→│── Trade Msg
     │                             │                        │── Update Book ───────→│── Depth Update
     │                             │←── Exec Report ────────│                       │
     │←── Fill Confirmation ───────│                        │                       │
```

---

### 9.2.2 Call Auction / Periodic Auction

**REQ-TM-002**: Call Auction Matching

| Attribute | Detail |
|-----------|--------|
| **Purpose** | Aggregate orders over a period and execute at a single equilibrium price |
| **Business Rationale** | Concentrates fragmented liquidity; reduces market impact for large orders; establishes fair reference prices (opening/closing) |
| **Matching Behavior** | All orders collected during auction period; equilibrium price calculated; all matchable volume executes at single price |
| **Priority** | Price only (within the equilibrium price, all fills at same price; allocation by time or pro-rata for partial fills at marginal price) |
| **Applicable Asset Classes** | Equities, ETFs, derivatives, commodities, carbon |
| **Configuration Parameters** | `matching_model=AUCTION`, `auction_type`, duration, random end range, extension rules, equilibrium algorithm |
| **Acceptance Criteria** | Equilibrium price maximizes executable volume; all fills at single price; surplus orders rest or cancel per TIF |
| **Operational Implications** | Indicative data publication during collection; random end prevents gaming |
| **Priority** | **Mandatory** |

**Equilibrium Price Algorithm**:
```
For each candidate price P (at each tick from lowest sell to highest buy):
  cumulative_buy_volume(P) = sum of all buy orders at price >= P
  cumulative_sell_volume(P) = sum of all sell orders at price <= P
  executable_volume(P) = min(cumulative_buy_volume(P), cumulative_sell_volume(P))
  imbalance(P) = |cumulative_buy_volume(P) - cumulative_sell_volume(P)|

Select P that:
  1. Maximizes executable_volume
  2. If tied: minimizes imbalance
  3. If tied: closest to reference_price
  4. If tied: higher price (configurable: higher/lower/last)
```

---

### 9.2.3 Request-for-Quote (RFQ)

**REQ-TM-003**: Request-for-Quote Workflow

| Attribute | Detail |
|-----------|--------|
| **Purpose** | Taker requests quotes from dealers/market makers; selects best response |
| **Business Rationale** | Appropriate for illiquid instruments (bonds, large blocks, complex derivatives) where continuous order book lacks depth |
| **Matching Behavior** | Taker sends RFQ → dealers respond with quotes → taker selects (manual or auto-best) → trade executes |
| **Priority** | Best price; if tied, first response |
| **Applicable Asset Classes** | Bonds, OTF instruments, FX, complex derivatives, commodities |
| **Configuration Parameters** | `matching_model=RFQ`, response timeout, min dealers, auto-execution threshold, anonymous/disclosed mode |
| **Acceptance Criteria** | RFQ distributed to eligible dealers; responses within timeout; taker can select; trade confirmed bilaterally |
| **Operational Implications** | Requires dealer entitlement management; timeout monitoring |
| **Priority** | **Highly Recommended** |

**RFQ Workflow**:
```
Taker                    Platform                  Dealer A    Dealer B    Dealer C
  │                         │                         │           │           │
  │── RFQ (buy 1M, Bond X)→│                         │           │           │
  │                         │── RFQ Notification ────→│           │           │
  │                         │── RFQ Notification ──────────────→│           │
  │                         │── RFQ Notification ────────────────────────→│
  │                         │                         │           │           │
  │                         │←── Quote: 99.875 ───────│           │           │
  │                         │←── Quote: 99.850 ────────────────│           │
  │                         │←── Quote: 99.900 ──────────────────────────│
  │                         │                         │           │           │
  │←── Quotes Summary ──────│                         │           │           │
  │                         │                         │           │           │
  │── Select Dealer C ─────→│                         │           │           │
  │                         │── Trade Confirm ────────────────────────────→│
  │←── Trade Confirm ───────│                         │           │           │
  │                         │── Quote Expired ────────│           │           │
  │                         │── Quote Expired ─────────────────→│           │
```

**Configuration Options**:
- `rfq_mode = ANONYMOUS | DISCLOSED` — whether taker identity is revealed to dealers
- `rfq_auto_execute = true | false` — auto-execute at best price after all responses or timeout
- `rfq_min_responses = N` — minimum dealer responses before execution allowed
- `rfq_timeout_seconds = N` — time window for dealer responses
- `rfq_eligible_dealers` — dealer list per instrument or category
- `rfq_competition_mode = ALL_DEALERS | SELECTED_DEALERS | TIERED`

---

### 9.2.4 Request-for-Stream (RFS) / Streaming Quotes

**REQ-TM-004**: Streaming Quote Model

| Attribute | Detail |
|-----------|--------|
| **Purpose** | Liquidity providers stream continuous executable prices; takers hit/lift to trade |
| **Business Rationale** | Common in FX and fixed income; provides continuous liquidity without a central order book |
| **Matching Behavior** | Provider streams bid/offer; taker sends trade request referencing streamed price; provider confirms (optionally with last-look) |
| **Priority** | Taker selects provider; no priority algorithm |
| **Applicable Asset Classes** | FX, bonds, OTC derivatives |
| **Configuration Parameters** | `matching_model=STREAMING`, last_look_window, price_validity_ms, staleness_timeout |
| **Acceptance Criteria** | Streamed prices reach taker with < configurable latency; taker execution confirmed or rejected within SLA |
| **Priority** | **Optional** (Phase 3) |

---

### 9.2.5 Dark / Midpoint Matching

**REQ-TM-005**: Dark Pool / Midpoint Matching

| Attribute | Detail |
|-----------|--------|
| **Purpose** | Match orders at midpoint of the NBBO or primary market BBO without pre-trade transparency |
| **Business Rationale** | Reduces market impact for institutional orders; compliant dark pool operation under applicable waivers |
| **Matching Behavior** | Orders resting in dark book; incoming order matched at midpoint reference price; no pre-trade price publication |
| **Priority** | Price-time at midpoint; minimum quantity filters |
| **Applicable Asset Classes** | Equities, ETFs |
| **Configuration Parameters** | `matching_model=DARK_MIDPOINT`, reference_price_source, min_execution_size, eligible_order_types |
| **Acceptance Criteria** | Midpoint price correctly calculated from reference; fills at midpoint; no pre-trade information leakage |
| **Regulatory Dependency** | Requires reference price waiver (EU) or ATS exemption (US) |
| **Priority** | **Optional** (Phase 3) |

**Reference Price Sources**:
- Primary market BBO (bid/offer midpoint)
- NBBO midpoint (consolidated best bid/offer)
- Last auction price
- Configurable per instrument / market segment

---

### 9.2.6 Negotiated / Block Trade

**REQ-TM-006**: Negotiated Trade Workflow

| Attribute | Detail |
|-----------|--------|
| **Purpose** | Enable bilateral negotiation and reporting of large block trades through the venue |
| **Business Rationale** | Large orders cannot be executed in the order book without severe market impact; negotiated workflow provides a controlled channel |
| **Matching Behavior** | Two parties agree terms off-platform or via messaging → submit matched trade report → venue validates and publishes |
| **Priority** | Not applicable (bilaterally agreed) |
| **Applicable Asset Classes** | All (with minimum size thresholds) |
| **Configuration Parameters** | `trade_type=NEGOTIATED`, min_block_size, price_tolerance_from_market, reporting_delay, eligible_participants |
| **Acceptance Criteria** | Both counterparties confirm; price within tolerance; trade published after regulatory delay (if applicable) |
| **Priority** | **Highly Recommended** |

**Validation Rules**:
- Minimum block size met (configurable per instrument)
- Price within configurable tolerance of current market (e.g., ± 5% of last trade or BBO midpoint)
- Both counterparties entitled and authenticated
- Trade subject to post-trade transparency (with deferred publication if LIS waiver applies)

---

### 9.2.7 Hybrid Models

**REQ-TM-007**: Configurable Hybrid Trading Model

| Attribute | Detail |
|-----------|--------|
| **Purpose** | Combine multiple matching mechanisms for a single instrument (e.g., RFQ + order book, dark + lit) |
| **Business Rationale** | Some markets require hybrid liquidity models; e.g., bond markets with central order book plus RFQ |
| **Matching Behavior** | Configurable pipeline: order evaluated against model 1, then model 2, etc. |
| **Configuration Parameters** | `matching_models=[CONTINUOUS, RFQ]`, model_priority, routing_rules |
| **Acceptance Criteria** | Order correctly evaluated against each model in configured priority |
| **Priority** | **Optional** (Phase 3) |

## 9.3 Instrument State Machine

Every instrument follows a state machine that governs which trading activities are permitted:

```
                    ┌──────────────┐
                    │   INACTIVE   │  (not scheduled for trading)
                    └──────┬───────┘
                           │ Schedule / Enable
                           ▼
                    ┌──────────────┐
                    │   PRE_OPEN   │  (orders accepted, no matching)
                    └──────┬───────┘
                           │ Auction Start
                           ▼
                    ┌──────────────┐
        ┌──────────│   AUCTION    │◄─────────────────────┐
        │          └──────┬───────┘                      │
        │                 │ Auction End                   │
        │                 ▼                               │
        │          ┌──────────────┐                      │
        │    ┌────→│  CONTINUOUS  │──── Volatility ──────┘
        │    │     └──────┬───────┘    Trigger
        │    │            │
        │    │            │ Close Trigger
        │    │            ▼
        │    │     ┌──────────────┐
        │    │     │CLOSING_AUCTION│
        │    │     └──────┬───────┘
        │    │            │ Auction End
        │    │            ▼
        │    │     ┌──────────────┐
        │    │     │   CLOSED     │
        │    │     └──────┬───────┘
        │    │            │
        │    │            ▼
        │    │     ┌──────────────┐
        │    │     │  POST_CLOSE  │  (trade-at-close orders)
        │    │     └──────────────┘
        │    │
        │    │      ┌──────────────┐
        │    └──────│   RESUME     │  (from halt/suspend)
        │           └──────────────┘
        │
        │          ┌──────────────┐
        └─────────→│    HALT      │  (temporary, resumable)
                   └──────┬───────┘
                          │ (may escalate)
                          ▼
                   ┌──────────────┐
                   │   SUSPEND    │  (extended, requires manual resume)
                   └──────────────┘
```

### State Permissions Matrix

| State | New Orders | Modify | Cancel | Matching | Market Data |
|-------|:----------:|:------:|:------:|:--------:|:-----------:|
| INACTIVE | ✗ | ✗ | ✗ | ✗ | Reference only |
| PRE_OPEN | ✅ | ✅ | ✅ | ✗ | Indicative auction |
| AUCTION | ✅ | ✅ | ✅ | At end only | Indicative |
| CONTINUOUS | ✅ | ✅ | ✅ | ✅ | Full depth + trades |
| HALT | ✗ | ✗ | ✅ | ✗ | Last known state |
| SUSPEND | ✗ | ✗ | ✅ | ✗ | Last known state |
| CLOSING_AUCTION | ✅ | ✅ | ✅ | At end only | Indicative |
| CLOSED | ✗ | ✗ | ✗ | ✗ | Final statistics |
| POST_CLOSE | ◐ | ✗ | ✅ | ◐ | Closing price |

## 9.4 Trading Session Model

### 9.4.1 Session Definition
A trading session defines the schedule for an instrument or market segment:

```yaml
session: "EQUITY_MAIN"
timezone: "Europe/London"
phases:
  - name: "Pre-Open"
    start: "07:00:00"
    state: PRE_OPEN
  - name: "Opening Auction"
    start: "07:50:00"
    state: AUCTION
    end_mode: RANDOM  # random end between 08:00:00 and 08:00:30
  - name: "Continuous Trading"
    start: "08:00:30"  # latest possible after random end
    state: CONTINUOUS
  - name: "Closing Auction"
    start: "16:30:00"
    state: CLOSING_AUCTION
    duration: "00:05:00"
    end_mode: RANDOM  # random end ±30s
  - name: "Post Close"
    start: "16:35:30"
    state: POST_CLOSE
    duration: "00:10:00"
  - name: "End of Day"
    start: "16:45:30"
    state: CLOSED
holidays: "CALENDAR_UK"
half_days: "CALENDAR_UK_HALFDAY"
```

### 9.4.2 Multi-Session Support
- An instrument can belong to multiple sessions (e.g., pre-market session + main session + after-hours)
- Different sessions may have different rules (e.g., pre-market: no market orders, no auctions)
- 24/7 instruments have a single continuous session with no state transitions (or minimal maintenance windows)

## 9.5 Trading Model Decision Matrix

| Factor | CLOB | Auction | RFQ | Streaming | Dark Midpoint | Negotiated Block |
|--------|:----:|:-------:|:---:|:---------:|:-------------:|:----------------:|
| Liquid instruments | ✅ | ◐ | ✗ | ✗ | ◐ | ◐ |
| Illiquid instruments | ◐ | ✅ | ✅ | ✅ | ✗ | ✅ |
| Price discovery quality | High | High | Medium | Low | None | None |
| Market impact | High | Low | Low | Medium | Very Low | Very Low |
| Pre-trade transparency | Full | Indicative | None | Dealer only | None | None |
| Latency sensitivity | Very High | Low | Medium | High | High | Low |
| Complexity | Medium | Medium | High | High | Medium | Low |
| Regulatory scrutiny | Low | Low | Medium | Medium | High | Medium |
