# 30. Appendix: Glossary and Sample Workflows

## 30.1 Glossary

| Term | Definition |
|------|-----------|
| **Aggressive order** | An incoming order that immediately matches against resting orders (takes liquidity) |
| **ATS** | Alternative Trading System — US-regulated non-exchange trading venue |
| **Auction** | A trading mechanism that collects orders over a period and executes at a single equilibrium price |
| **BBO** | Best Bid and Offer — the highest bid and lowest ask prices in the order book |
| **Cancel-on-Disconnect (CoD)** | Automatic cancellation of a participant's resting orders when their connectivity session is lost |
| **CCP** | Central Counterparty — entity that interposes itself between buyer and seller to guarantee trade settlement |
| **Circuit breaker** | Mechanism that halts trading when price movement exceeds defined thresholds |
| **CLOB** | Central Limit Order Book — the primary order book structure matching buy and sell orders by price-time priority |
| **Colocation** | Physical hosting of participant servers in the same data center as the exchange for minimum latency |
| **Conflation** | Combining multiple market data updates into a single message to reduce bandwidth |
| **CSD** | Central Securities Depository — entity that holds securities and processes settlement |
| **Determinism** | Property that identical inputs always produce identical outputs |
| **DPDK** | Data Plane Development Kit — kernel bypass framework for high-speed packet processing |
| **Drop copy** | A read-only copy of all order and execution events for a participant |
| **Equilibrium price** | The auction price that maximizes executable volume |
| **Event sourcing** | Architecture pattern where all state changes are stored as a sequence of immutable events |
| **Execution report** | A message confirming an order action (new, fill, cancel, reject) |
| **Fat finger** | Erroneous order with obviously wrong price or quantity |
| **FIX** | Financial Information eXchange — industry-standard messaging protocol for trading |
| **FOK** | Fill-or-Kill — order that must be completely filled immediately or cancelled entirely |
| **FPGA** | Field-Programmable Gate Array — hardware for ultra-low-latency processing |
| **GTC** | Good-til-Cancel — order that remains active until explicitly cancelled |
| **GTD** | Good-til-Date — order that remains active until a specified expiry date |
| **Iceberg order** | Order with a visible (display) quantity and a hidden (reserve) quantity |
| **Implied matching** | Deriving synthetic prices from combinations of outright and spread instruments |
| **IOC** | Immediate-or-Cancel — order that executes immediately and cancels any unfilled residual |
| **io_uring** | Linux kernel interface for high-performance asynchronous I/O |
| **Journal** | Append-only log recording every input and output event for the matching engine |
| **Kill switch** | Emergency mechanism to cancel all orders and/or disable a participant |
| **Latency** | Time delay between an event and its effect (e.g., order submission to acknowledgment) |
| **LEI** | Legal Entity Identifier — 20-character code identifying legal entities in financial transactions |
| **LIS** | Large-in-Scale — orders above a defined size threshold qualifying for transparency waivers |
| **Maker** | A participant whose order rests in the book and provides liquidity |
| **Market order** | An order to execute at the best available price(s), sweeping through price levels. If residual quantity remains after exhausting liquidity, the venue policy applies: cancel residual (default) or convert to market-to-limit with protection collar. Unprotected resting at last fill price is not supported. |
| **Mass quote** | A single message updating bid and ask prices for multiple instruments simultaneously |
| **MiFID II** | Markets in Financial Instruments Directive II — EU regulatory framework for financial markets |
| **Midpoint** | The price exactly between the best bid and best ask |
| **MTF** | Multilateral Trading Facility — EU-regulated venue matching third-party orders |
| **Multicast** | Network protocol for efficiently sending data to multiple recipients simultaneously |
| **NBBO** | National Best Bid and Offer — the best bid and ask prices across all US venues |
| **NUMA** | Non-Uniform Memory Access — hardware architecture where memory access time depends on CPU-memory proximity |
| **OTF** | Organized Trading Facility — EU-regulated venue for non-equity instruments with operator discretion |
| **Partition** | A unit of work assignment in the matching engine. Each partition manages one or more order books (instruments) on a single dedicated thread. Instruments are sharded across partitions for horizontal scaling. There is no cross-partition matching. |
| **Passive order** | A resting order in the book that provides liquidity (is a maker) |
| **Pegged order** | An order whose price automatically adjusts to track a reference price (BBO, midpoint) |
| **Post-only** | An order that is only accepted if it would rest in the book (not immediately match) |
| **Price band** | A configurable range around a reference price; orders outside the band are rejected |
| **Price-time priority** | Matching algorithm where orders are prioritized by best price first, then earliest entry time |
| **Pro-rata** | Matching algorithm where fills at a price level are allocated proportionally to order size |
| **PTP** | Precision Time Protocol (IEEE 1588) — protocol for synchronizing clocks to nanosecond precision |
| **RFQ** | Request for Quote — a workflow where a buyer/seller requests prices from dealers |
| **RPO** | Recovery Point Objective — maximum acceptable data loss measured in time |
| **RTO** | Recovery Time Objective — maximum acceptable time to restore service after failure |
| **SBE** | Simple Binary Encoding — compact binary message format with FIX semantics |
| **Deployment Profile** | A named combination of infrastructure, networking, and configuration choices that determines achievable performance characteristics. The platform defines Profile A (ultra-low-latency colocation), Profile B (standard exchange-grade), and Profile C (hybrid/cloud-compatible). See §15. |
| **Foundation Release (FR-1)** | The first production-capable release of the platform, supporting the initial selected market scope with core order-book trading, basic risk controls, and primary connectivity gateways. Formerly referred to as MVP or MVR. |
| **Sequencer** | Component that assigns a partition-local monotonic sequence number and nanosecond timestamp to all inbound messages for a partition. Global event identity uses the composite `[partition_id]:[local_sequence]`. |
| **Snapshot** | A point-in-time capture of the matching engine's full state (all order books) |
| **STP** | Self-Trade Prevention — mechanism to prevent a participant from trading against themselves |
| **Taker** | A participant whose order immediately matches against resting orders (consumes liquidity) |
| **Tick size** | The minimum price increment for an instrument |
| **TIF** | Time-in-Force — instruction specifying how long an order remains active |
| **Volatility auction** | An auction triggered by a circuit breaker when price movement exceeds thresholds |
| **WAL** | Write-Ahead Log — a log where data is written before the operation is confirmed |
| **Warm restart** | Recovering engine state by loading a snapshot and replaying the journal |

## 30.2 Sample Workflow: Complete Order Lifecycle

### 30.2.1 Limit Buy Order — Immediate Partial Fill

```
Time    Actor           Action                          Engine State
─────── ─────────────── ─────────────────────────────── ──────────────────────
T+0µs   Broker A        Submit: Buy 500 @ 100.05        Order received at GW
T+2µs   FIX Gateway     Validate FIX message             Parsed, validated
T+5µs   Risk Service    Fat finger check PASS            Within limits
T+7µs   Sequencer       Assign seq#12345, ts=09:30:01.123456789
T+8µs   Journal         Write input event                Persisted
T+9µs   Engine          Check instrument state: CONTINUOUS  OK
T+10µs  Engine          Price band check: 100.05 within band  OK
T+11µs  Engine          Match vs. resting Sell 200 @ 100.05 (Broker B)
T+12µs  Engine          Fill: 200 @ 100.05 (Broker A buys from B)
T+13µs  Engine          Generate ExecID=98765, TradeID=55555
T+14µs  Engine          Remaining: 300 @ 100.05 — check more sells
T+15µs  Engine          No more sells at 100.05 — insert residual in book
T+16µs  Journal         Write output events (fill + insert)
T+17µs  Output Bus      Publish: ExecutionNew (buyer + seller), OrderInsert
T+19µs  Market Data     Publish: Trade(100.05, 200), DepthUpdate(bid 100.05 +300)
T+20µs  Drop Copy       Deliver fill to Broker A and Broker B
T+21µs  FIX Gateway     Send ExecutionReport(PartialFill) to Broker A
T+22µs  FIX Gateway     Send ExecutionReport(Fill) to Broker B
T+25µs  Post-Trade      Record trade; calculate fees; queue for clearing
```

### 30.2.2 Opening Auction

```
Time        State        Activity
──────────── ──────────── ──────────────────────────────────────────
07:00:00     PRE_OPEN     Session starts; orders accepted, no matching
07:00-07:50  PRE_OPEN     Orders accumulate in the book
                          Indicative equilibrium price published every 1 second
07:50:00     AUCTION      Auction phase begins (orders still accepted)
                          Indicative data published on every book change
07:59:45     AUCTION      Random end window opens (±30 seconds)
08:00:12     AUCTION      Random end time reached — auction closes

                          Book state at close:
                          Buys:  1000@100.10, 2000@100.05, 500@100.00
                          Sells: 800@100.00, 1500@100.05, 300@100.10

                          Equilibrium calculation:
                          @ 100.00: Buy=3500, Sell=800,  Exec=800
                          @ 100.05: Buy=3000, Sell=2300, Exec=2300  ← MAX
                          @ 100.10: Buy=1000, Sell=2600, Exec=1000

                          Equilibrium: 100.05, Volume: 2300

08:00:12     MATCH        All matchable orders execute at 100.05
                          Remaining orders transition to continuous book
08:00:12     CONTINUOUS   Trading begins
```

### 30.2.3 Cancel-on-Disconnect

```
Time        Actor              Event
──────────── ────────────────── ──────────────────────────────────────
09:15:00     Market Maker X     Has 500 resting quotes across 50 instruments
09:15:01     Network            Market Maker X connection drops
09:15:01     Gateway            Detect heartbeat miss #1
09:15:02     Gateway            Detect heartbeat miss #2 (threshold=2)
09:15:02     Gateway            Declare session disconnected
09:15:02     Gateway            CoD grace period: 0 seconds (configured)
09:15:02     Gateway            Generate synthetic cancel for each resting order
09:15:02     Sequencer          Sequence 500 cancel messages
09:15:02     Engine             Process 500 cancels; remove from books
09:15:02     Market Data        500 depth updates published
09:15:02     Audit              500 cancel events journaled with reason=COD
09:15:05     Market Maker X     Reconnects; session re-established
09:15:05     Market Maker X     Receives cancel confirmations for all 500 orders
09:15:06     Market Maker X     Re-enters quotes via mass quote
```

### 30.2.4 Circuit Breaker / Volatility Auction

```
Time        Price     Event
──────────── ───────── ──────────────────────────────────────
10:00:00     100.00    Last trade; reference price = 100.00
10:00:01     100.02    Trade
10:00:02     100.05    Trade — within dynamic band (5%)
10:00:03     100.08    Trade — approaching dynamic threshold
10:00:04     105.50    Incoming aggressive buy would trade at 105.50
                       → 105.50 > 100.00 + 5% (105.00) → BREACH
                       → Dynamic threshold triggered
10:00:04              Engine halts continuous matching for instrument
                       Instrument state → VOLATILITY_AUCTION
                       Market data: InstrumentStatus(VOLATILITY_AUCTION)
10:00:04-10:05:04     Auction collection phase (5 minutes)
                       Orders accepted; indicative data published
10:05:04              Auction end (+ random ±30s)
10:05:22              Random end reached; equilibrium calculated
                       Equilibrium: 102.50, Volume: 5000
                       All matchable orders execute at 102.50
10:05:22              Instrument state → CONTINUOUS
                       New reference price = 102.50
                       Dynamic band resets around 102.50
```

## 30.3 Sample Configuration: Equity Instrument

```yaml
instrument:
  instrument_id: 12345
  symbol: "ACME"
  isin: "US0123456789"
  type: EQUITY
  currency: "USD"
  market_segment: "MAIN_BOARD"
  partition_id: 1

  pricing:
    tick_size_table: "US_EQUITY_STANDARD"
    price_precision: 4  # 4 decimal places
    price_multiplier: 10000  # internal integer = price × 10000

  quantity:
    lot_size: 1
    quantity_precision: 0  # whole shares
    max_order_qty: 10000000

  matching:
    priority_model: PRICE_TIME
    continuous_matching: true

  auctions:
    opening_auction: true
    closing_auction: true
    volatility_auction: true
    opening_duration_min_sec: 600
    opening_random_end_sec: 30
    closing_duration_sec: 300
    closing_random_end_sec: 30

  price_protection:
    static_band_pct: 10.0
    static_reference: PREVIOUS_CLOSE
    dynamic_band_pct: 5.0
    dynamic_reference: LAST_TRADE
    dynamic_window_sec: 300

  circuit_breaker:
    enabled: true
    auction_duration_sec: 300
    max_extensions: 3
    extension_duration_sec: 120

  session:
    profile: "US_EQUITY_REGULAR"
    timezone: "America/New_York"

  stp:
    default_mode: CANCEL_NEWEST
    scope: PARTICIPANT_AND_ACCOUNT

  cancel_on_disconnect:
    enabled: true
    grace_period_ms: 0
    scope: SESSION_ORDERS  # only orders from disconnected session
```

## 30.4 Sample Configuration: Jurisdiction Profile

```yaml
jurisdiction:
  id: "US_ATS"
  name: "United States - Alternative Trading System"
  version: "2026.1"

  clock_sync:
    required_accuracy_ms: 50  # SEC requirement: 50ms
    protocol: NTP  # PTP recommended but NTP acceptable for SEC
    evidence_retention_years: 6

  pre_trade_risk:
    fat_finger_mandatory: true
    kill_switch_mandatory: true
    message_rate_limit_mandatory: true
    credit_check_recommended: true

  transparency:
    pre_trade:
      enabled: true  # ATS-N requirements
      dark_pool_exempt: true  # If operating as dark ATS
    post_trade:
      sip_reporting: true
      delay_allowed: false

  trade_protection:
    reg_nms_compliance: true
    nbbo_reference_required: true
    trade_through_prevention: true
    sub_penny_rule: true  # Reg NMS Rule 612

  short_selling:
    reg_sho_compliance: true
    locate_requirement: true
    short_sale_price_test: true  # Circuit breaker short sale restriction

  reporting:
    cat_reporting: true  # Consolidated Audit Trail
    format: "CAT"
    deadline: "T+1_08:00"

  surveillance:
    finra_membership: true
    suspicious_activity_reporting: true

  retention:
    order_data_years: 6  # Rule 17a-4
    trade_data_years: 6
    communication_years: 3

  resilience:
    reg_sci_compliance: true  # Regulation SCI
    annual_sip_testing: true
    incident_notification_hours: 24
```

---

*End of Specification Package — Version 1.0.0-DRAFT*

*Document produced 2026-03-24. For questions, contact the specification authors listed in the document metadata.*
