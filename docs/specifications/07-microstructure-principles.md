# 7. Market-Microstructure Design Principles

## 7.1 Overview

Market microstructure defines how the venue organizes the interaction between buyers and sellers. The design of the order book, priority rules, order types, auction mechanisms, and price protection controls determines the quality of price discovery, liquidity provision, execution fairness, and market stability.

This section establishes the foundational microstructure principles that the matching engine must support. These principles are asset-class-agnostic at the engine level; asset-class-specific behavior is achieved through configuration parameters.

## 7.2 Core Principles

### Principle MS-01: Deterministic Matching
**Statement**: Given an identical sequence of input messages, the matching engine must always produce an identical sequence of output events.

**Rationale**: Determinism is the foundation of auditability, replay, recovery, and regulatory proof. Without determinism, it is impossible to reconstruct the exact state of the order book at any point in time or to prove that a specific execution was correct.

**Implications**:
- Single-threaded execution per partition (each partition manages one or more order books). A fully deterministic multi-threaded alternative is theoretically possible but not recommended due to verification complexity.
- No external non-deterministic inputs in the matching path (e.g., wall-clock reads, random numbers, external service calls)
- All time references from a deterministic sequenced clock
- Journal replay must produce bit-identical output

### Principle MS-02: Price Priority
**Statement**: Orders at a better price always execute before orders at a worse price.

**Rationale**: Price priority is the universal fairness foundation. Without it, participants cannot trust that the best price wins, and price discovery is compromised.

**Implications**:
- Buy orders sorted best (highest) to worst (lowest)
- Sell orders sorted best (lowest) to worst (highest)
- No configuration should allow violation of price priority (secondary allocation within a price level is configurable)

### Principle MS-03: Configurable Secondary Priority
**Statement**: Within a price level, the allocation method is configurable per instrument, market segment, or venue rule.

**Rationale**: Different asset classes and market models use different allocation methods. Equities use time priority. Derivatives may use pro-rata. Some markets use hybrid models.

**Supported Allocation Methods**:

| Method | Description | Typical Use |
|--------|-------------|-------------|
| **Price-Time (FIFO)** | First order at a price level gets filled first | Cash equities, ETFs, most markets |
| **Pro-Rata** | Orders at a price level share fills proportionally to their size | Interest rate derivatives, some commodity futures |
| **Size-Time** | Largest order fills first, then time | Rarely used; some OTC platforms |
| **Pro-Rata with Top** | Top-of-queue gets priority fill, remainder pro-rata | Some derivatives markets (CME Eurodollar legacy) |
| **Parity** | Equal share to each order, regardless of size | Some options markets |
| **Configurable Hybrid** | Combination of the above with venue-defined weights | Complex derivatives markets |

### Principle MS-04: Fair Access
**Statement**: All participants with equivalent connectivity and permissions receive equivalent treatment by the matching engine.

**Rationale**: Regulatory requirement in most jurisdictions. Perception of fairness is essential for venue credibility and liquidity attraction.

**Implications**:
- No special hardware path for privileged participants within the matching engine
- Gateway-level fairness: multi-gateway ingestion must use a deterministic sequencing mechanism (e.g., sequencer appliance, time-based batching, round-robin multiplexing)
- Transparent priority rules published to all participants
- Measurable and reportable latency distributions per gateway

### Principle MS-05: Price Protection
**Statement**: The venue must protect against erroneous executions caused by fat-finger errors, stale quotes, or sudden volatility.

**Rationale**: Erroneous trades damage participant confidence, create settlement and risk issues, and may trigger regulatory scrutiny.

**Mechanisms**:
- **Static price bands**: Reject orders outside a fixed percentage from reference price
- **Dynamic price bands**: Reject orders outside a percentage from last traded price or best bid/offer
- **Circuit breakers / volatility auctions**: Transition to auction mode when price moves exceed thresholds
- **Anti-manipulation collars**: Prevent aggressive sweeping through multiple price levels in a single action

### Principle MS-06: Transparency
**Statement**: The venue publishes market data that enables participants to assess execution quality and make informed order routing decisions.

**Rationale**: Regulatory requirement (MiFID II, Reg NMS) and competitive necessity. Transparent venues attract more order flow.

**Data Published**:
- Pre-trade: Bid/offer prices and sizes at each level (configurable depth)
- Post-trade: Executed trades with price, size, and timestamp
- Auction: Indicative/equilibrium price, imbalance size, paired quantity
- Reference: Instrument state changes, circuit breaker activations, halts/resumes

### Principle MS-07: Configurable Market Models
**Statement**: The platform supports multiple market models. Existing, already-implemented models and order types can be enabled, disabled, and parameterized per instrument or market segment through configuration. Materially new matching algorithms or order semantics require a controlled software release.

**Rationale**: Different asset classes, different instruments within an asset class, and different times of day may require different matching behavior.

**Configurable Dimensions**:
- Matching algorithm (continuous, auction, RFQ, hybrid)
- Priority allocation (price-time, pro-rata, hybrid)
- Order type eligibility per session state
- Tick size schedule
- Lot size and precision
- Price band parameters
- Auction duration and extension rules
- Market maker quoting obligations
- Self-trade prevention mode
- Minimum order sizes

## 7.3 Order Book Model

### 7.3.1 Limit Order Book (LOB)

The primary order book structure is a **two-sided limit order book**:

```
═══════════════════════════════════════════════
         SELL SIDE (ASKS)
    Price    │  Qty   │  Orders  │  Priority
   ─────────┼────────┼──────────┼───────────
    100.05   │  500   │    3     │  Time
    100.04   │  200   │    1     │  Time
    100.03   │  800   │    5     │  Time  ← Best Ask
   ═══════════════════════════════════════════
    100.02   │ 1,200  │    4     │  Time  ← Best Bid
    100.01   │  600   │    2     │  Time
    100.00   │  300   │    1     │  Time
   ─────────┼────────┼──────────┼───────────
         BUY SIDE (BIDS)
═══════════════════════════════════════════════
```

**Data Structure Requirements**:
- O(1) access to best bid and best ask
- O(log N) order insertion by price level (or O(1) with pre-allocated price level array for instruments with known tick structure)
- O(1) order cancellation by order ID (via hash map lookup)
- O(1) access to order by order ID
- Memory pre-allocated to avoid allocation in the hot path
- Cache-friendly layout (consider struct-of-arrays vs array-of-structs)

### 7.3.2 Price Level Structure

Each price level maintains:
- Price value (integer representation to avoid floating-point)
- Total visible quantity at this level
- Total hidden quantity at this level (iceberg reserve)
- Number of orders
- Doubly-linked list of orders in priority sequence
- Pointer to next better/worse price level

### 7.3.3 Order Representation

Each order in the book maintains:
- Order ID (venue-assigned, globally unique, monotonically increasing)
- Client order ID (participant-assigned)
- Participant ID
- Account / sub-account
- Instrument ID
- Side (buy/sell)
- Order type
- Price (integer-encoded)
- Displayed quantity
- Hidden quantity (for iceberg)
- Minimum execution quantity (if applicable)
- Remaining quantity
- Time-in-force
- Expiry time (for GTD)
- Self-trade prevention instruction
- Entry timestamp (sequencer timestamp)
- Last modification timestamp
- Order flags (post-only, reduce-only, etc.)
- Priority sequence number (for deterministic ordering)

### 7.3.4 Integer Price Representation

**Requirement**: All prices must be represented as integers internally to avoid floating-point precision issues.

**Approach**: Define a price multiplier per instrument based on the minimum tick size.

Example:
- Instrument with tick size 0.01 → multiplier = 100 → price 100.05 stored as 10005
- Instrument with tick size 0.0001 → multiplier = 10000 → price 1.3456 stored as 13456

The multiplier is defined in the instrument reference data and applied at the gateway layer on ingress and egress.

## 7.4 Order Priority and Allocation

### 7.4.1 Price-Time Priority (FIFO)

**Algorithm**:
1. Orders are sorted by price (best first)
2. Within a price level, orders are sorted by entry time (earliest first)
3. When an aggressive order arrives, it matches against resting orders in price-time order
4. The first resting order at the best price fills first; if partially filled, it retains priority for the remainder

**Configuration Parameters**:
- `priority_model = PRICE_TIME`

**Edge Cases**:
- Order amendment that changes price loses time priority (new entry time assigned)
- Order amendment that reduces quantity retains time priority
- Order amendment that increases quantity: configurable — retain or lose priority

### 7.4.2 Pro-Rata Priority

**Algorithm**:
1. Orders are sorted by price (best first)
2. Within a price level, fills are allocated proportionally to each order's size
3. Rounding differences allocated by time priority (earliest first) or largest-remainder method

**Configuration Parameters**:
- `priority_model = PRO_RATA`
- `pro_rata_min_allocation` — minimum fill size to avoid dust fills
- `pro_rata_rounding_method = TIME | LARGEST_REMAINDER`

**Example**:
- Incoming buy for 100 lots matches sell price level with three orders: A=300, B=200, C=500 (total=1000)
- Allocation: A=30, B=20, C=50 (proportional)

### 7.4.3 Pro-Rata with Top-of-Queue Priority

**Algorithm**:
1. The first order at a price level (by time) receives a guaranteed percentage of each fill
2. The remainder is allocated pro-rata among all orders at that level (including the top order)

**Configuration Parameters**:
- `priority_model = PRO_RATA_TOP`
- `top_order_percentage` — guaranteed fill percentage for top-of-queue (e.g., 40%)

### 7.4.4 Parity Allocation

**Algorithm**:
1. Each order at a price level receives an equal share of the fill, regardless of size
2. Remainders allocated by time priority

**Configuration Parameters**:
- `priority_model = PARITY`

## 7.5 Auction Mechanisms

### 7.5.1 Opening Auction

**Purpose**: Establish a fair opening price through aggregated supply and demand before continuous trading begins.

**Process**:
1. **Collection Phase**: Orders accepted but not matched (instrument state: PRE_OPEN / AUCTION)
2. **Price Determination**: Calculate equilibrium price that maximizes executable volume
3. **Execution**: All matchable orders at equilibrium price execute simultaneously
4. **Transition**: Instrument transitions to CONTINUOUS trading

**Equilibrium Price Algorithm**:
1. At each possible price level, calculate executable volume (min of cumulative buy volume, cumulative sell volume)
2. Select price that maximizes executable volume
3. If multiple prices yield same volume, select price that minimizes imbalance
4. If still tied, select price closest to reference price
5. If still tied, select higher price (configurable)

**Publication During Collection**:
- Indicative equilibrium price
- Indicative executable volume
- Imbalance side and size
- Published at configurable interval (e.g., every second) or on every book change

**Random End Extension**:
- Auction end time has a random component (e.g., ±30 seconds) to prevent end-of-auction gaming
- Random seed is determined at session start for determinism

### 7.5.2 Closing Auction

**Purpose**: Establish the official closing price used for NAV calculations, index rebalancing, and settlement.

**Process**: Same as opening auction, with:
- Separate configuration for duration and extension rules
- Closing price may be used for off-book trade pricing
- Post-close phase may accept orders at closing price

### 7.5.3 Volatility Interruption Auction (Circuit Breaker)

**Purpose**: Pause continuous trading when price movement exceeds defined thresholds, allowing the market to stabilize through price discovery.

**Trigger Conditions**:
- **Static threshold**: Price moves > X% from reference price (e.g., last auction price, previous close)
- **Dynamic threshold**: Price moves > Y% from last traded price in a rolling window

**Process**:
1. Continuous trading halted for the instrument
2. Instrument enters VOLATILITY_AUCTION state
3. Collection phase begins (configurable duration, e.g., 5 minutes)
4. Equilibrium price calculated
5. Execution at equilibrium price
6. Resume continuous trading
7. If new volatility trigger during auction → extend auction

**Configuration Parameters**:
- `static_threshold_pct` — e.g., 10%
- `dynamic_threshold_pct` — e.g., 5%
- `dynamic_window_seconds` — e.g., 300
- `auction_duration_seconds` — e.g., 300
- `max_extensions` — e.g., 3
- `extension_duration_seconds` — e.g., 120

### 7.5.4 Intraday / Periodic Auction

**Purpose**: Concentrate liquidity at specific times during the trading day or run continuous periodic auctions (frequent batch auction model).

**Variants**:
- **Scheduled intraday auction**: Fixed times during the day (e.g., mid-day auction)
- **Periodic auction**: Rapid repeated auctions (e.g., every 100ms) as an alternative to continuous matching

**Configuration Parameters**:
- `auction_type = SCHEDULED | PERIODIC`
- `auction_interval_ms` — for periodic auctions
- `scheduled_times` — for scheduled auctions

### 7.5.5 Auction Fairness

- All orders in the auction are treated equally regardless of submission time (within the auction period)
- No information leakage about individual order contents during the auction
- Indicative price/volume publication is aggregate only
- Random end period prevents end-of-auction sniping

## 7.6 Self-Trade Prevention (STP)

### 7.6.1 Purpose
Prevent a participant from inadvertently trading against themselves, which may constitute wash trading and trigger regulatory scrutiny.

### 7.6.2 STP Modes

| Mode | Behavior | Use Case |
|------|----------|----------|
| `STP_CANCEL_NEWEST` | Cancel the incoming (aggressive) order | Default safe mode |
| `STP_CANCEL_OLDEST` | Cancel the resting (passive) order | Market maker refreshing quotes |
| `STP_CANCEL_BOTH` | Cancel both orders | Maximum prevention |
| `STP_DECREMENT` | Reduce both orders by the overlap quantity | Partial self-cross prevention |
| `STP_DISABLED` | No self-trade prevention | Where permitted and requested |

### 7.6.3 STP Scope
- Per participant ID (firm level)
- Per participant + sub-account (desk level)
- Per participant + trader ID (individual level)
- Configurable per instrument or globally

### 7.6.4 STP Identifier
- Each order carries an STP group identifier
- Self-trade detected when aggressive and passive orders share the same STP group
- STP group is assigned by the participant in the order message

## 7.7 Cancel-on-Disconnect (CoD)

### 7.7.1 Purpose
Automatically cancel a participant's resting orders when their connectivity session is lost, preventing stale quotes from being executed.

### 7.7.2 Behavior
- When a session heartbeat is missed and the session is declared disconnected, all orders associated with that session (or a configurable subset) are cancelled
- Cancellation is processed through the normal matching engine path (deterministic)
- A synthetic cancel message is generated per order and journaled

### 7.7.3 Configuration
- Enabled/disabled per session
- Configurable heartbeat interval and missed-heartbeat threshold
- Scope: all orders, or orders by instrument/market segment
- Grace period before cancellation (configurable, e.g., 0–5 seconds)
- Optionally retain GTC orders while cancelling day/session orders

### 7.7.4 Edge Cases
- Session reconnects during grace period → cancellation aborted
- Session disconnects during auction → orders cancelled or retained (configurable)
- Multiple sessions for same participant → only orders on disconnected session affected

## 7.8 Tick Size Regimes

### 7.8.1 Purpose
Define the minimum price increment for each instrument, ensuring orderly price formation and preventing sub-tick gaming.

### 7.8.2 Tick Size Table Structure

```
Instrument Group: LARGE_CAP_EQUITIES
┌──────────────────┬───────────┐
│ Price Range       │ Tick Size │
├──────────────────┼───────────┤
│ 0.00 – 0.4999    │ 0.0001    │
│ 0.50 – 0.9999    │ 0.0005    │
│ 1.00 – 1.9999    │ 0.001     │
│ 2.00 – 4.9999    │ 0.002     │
│ 5.00 – 9.9999    │ 0.005     │
│ 10.00 – 19.999   │ 0.01      │
│ 20.00 – 49.999   │ 0.02      │
│ 50.00 – 99.999   │ 0.05      │
│ 100.00 – 199.99  │ 0.10      │
│ 200.00 – 499.99  │ 0.20      │
│ 500.00 – 999.99  │ 0.50      │
│ 1000.00+          │ 1.00      │
└──────────────────┴───────────┘
```

### 7.8.3 Validation
- Orders at prices not aligned to the tick size are rejected
- Tick size table is per instrument or per instrument group
- Table can be updated by market operations (effective next session or immediately, configurable)

## 7.9 Lot Size and Quantity Precision

### 7.9.1 Lot Size
- Round lot: standard trading unit (e.g., 100 shares, 1 contract)
- Odd lot: below round lot (e.g., 1–99 shares)
- Mixed lot: combination (e.g., 150 shares = 1 round lot + 50 odd lot)

### 7.9.2 Treatment
- Configurable per instrument: odd lots allowed/rejected
- Odd lot orders may be matched separately or together with round lots
- Odd lot executions may not update last traded price / NBBO
- Board lot size for derivatives (typically 1 contract)
- Fractional quantity support for digital assets (configurable decimal precision)

## 7.10 Trade Bust / Cancel / Correction

### 7.10.1 Trade Bust
- Cancellation of a previously executed trade due to erroneous execution
- Requires authorized operator action
- Four-eyes approval for trade busts
- Time window for bust eligibility (e.g., within 30 minutes of execution)
- All busts journaled with full audit trail

### 7.10.2 Trade Price Correction
- Adjustment of execution price (e.g., to correct reference price error)
- Authorized operator action with four-eyes approval
- Original and corrected trade both retained in audit trail
- Downstream notifications to clearing and participants

### 7.10.3 Trade Cancel
- Bilateral agreement between counterparties to cancel a trade
- Requires both parties to confirm (or operator override)
- Clearing and settlement systems notified

## 7.11 Market Data Publication Semantics

### 7.11.1 Events Published
- New best bid/offer (top of book change)
- Depth update (any price level change)
- Trade execution (price, size, aggressor side)
- Auction indicative data (equilibrium price, volume, imbalance)
- Instrument state change
- Statistics (high, low, VWAP, turnover, trade count)

### 7.11.2 Ordering Guarantee
- Market data events published in the same order as the matching engine processes them
- Sequence numbers on all market data messages for gap detection
- No reordering or batching that would alter the perceived sequence of events

### 7.11.3 Snapshot + Incremental Model
- Full snapshot available on request or at session start
- Incremental updates after snapshot
- Client responsible for gap detection and snapshot recovery

## 7.12 Order ID and Execution ID Strategy

### 7.12.1 Order ID
- Venue-assigned, 64-bit unsigned integer
- Monotonically increasing within a partition
- Globally unique across the platform (partition prefix)
- Assigned at sequencer/gateway, before matching engine
- Format: `[partition_id:16][sequence:48]` — supports 2^48 orders per partition per lifetime

### 7.12.2 Execution ID
- Venue-assigned per execution (fill) event
- Monotonically increasing within a partition
- Format: `[partition_id:16][sequence:48]`
- One execution ID per fill (a trade may generate two execution reports — one per side)

### 7.12.3 Trade ID
- Unique identifier for a matched trade (two-sided)
- Links buyer execution ID and seller execution ID
- Used for clearing, settlement, and reporting
