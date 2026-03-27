# 14. Detailed Functional Requirements

## 14.1 Requirement ID Convention

Format: `FR-[AREA]-[NUMBER]`
- AREA: OMS (Order Management), MTH (Matching), AUC (Auction), RFQ (Request for Quote), RSK (Risk), MKD (Market Data), REF (Reference Data), PTC (Post-Trade), SUR (Surveillance), OPS (Operations), PAR (Participant), SEC (Security)

## 14.2 Order Management

| REQ ID | Requirement | Description | Acceptance Criteria | Priority |
|--------|-------------|-------------|---------------------|----------|
| FR-OMS-001 | New order submission | Accept new orders via all gateways with full field validation | Order acknowledged with venue-assigned OrderID within latency SLA | Mandatory |
| FR-OMS-002 | Order modification | Modify price and/or quantity of a resting order | Modified order retains OrderID; new price loses time priority; reduced qty retains priority | Mandatory |
| FR-OMS-003 | Order cancellation | Cancel a resting order by OrderID or ClientOrderID | Cancel confirmed; order removed from book; cancel journaled | Mandatory |
| FR-OMS-004 | Mass cancel by participant | Cancel all resting orders for a participant | All orders cancelled; confirmation per order; total count reported | Mandatory |
| FR-OMS-005 | Mass cancel by instrument | Cancel all resting orders for an instrument | All orders cancelled across all participants | Mandatory |
| FR-OMS-006 | Mass cancel by criteria | Cancel orders matching criteria (side, price range, order type) | Matched orders cancelled; non-matching retained | Highly Recommended |
| FR-OMS-007 | Order status inquiry | Query current status of an order by OrderID | Return current order state, filled qty, remaining qty | Mandatory |
| FR-OMS-008 | GTC order management | GTC orders persist across sessions up to configured max lifetime | Orders survive session end; auto-cancel at max lifetime | Mandatory |
| FR-OMS-009 | GTD order expiry | GTD orders auto-cancel at specified date/time | Order cancelled at expiry; cancel event published | Mandatory |
| FR-OMS-010 | Day order cleanup | All day orders cancelled at end of trading session | No day orders survive into next session | Mandatory |
| FR-OMS-011 | Stop order triggering | Stop orders activated when trigger price condition met | Stop converts to market/limit when last trade crosses trigger price | Mandatory |
| FR-OMS-012 | Iceberg replenishment | Hidden quantity replenished when displayed quantity fully filled | New display portion goes to back of price level queue | Mandatory |
| FR-OMS-013 | Pegged order repricing | Pegged orders automatically reprice on BBO change | Price updated; priority reset on each reprice (or configurable) | Highly Recommended |
| FR-OMS-014 | Post-only enforcement | Post-only orders rejected if they would immediately match | Reject with clear reason code; or slide to non-matching price | Highly Recommended |
| FR-OMS-015 | Order expiry notification | Participant notified when GTC/GTD orders expire or auto-cancel | Unsolicited cancel report sent via order-entry session and drop copy | Mandatory |
| FR-OMS-016 | Client order ID uniqueness | Reject duplicate client order IDs within a session | Rejection with duplicate order ID error code | Mandatory |
| FR-OMS-017 | Order quantity precision | Support configurable quantity decimal precision per instrument | Fractional quantities validated against instrument configuration | Mandatory |
| FR-OMS-018 | Order amendment — quantity increase priority | Configurable: quantity increase retains or loses time priority | Behavior matches venue rule configuration | Highly Recommended |

## 14.3 Matching

| REQ ID | Requirement | Description | Acceptance Criteria | Priority |
|--------|-------------|-------------|---------------------|----------|
| FR-MTH-001 | Price-time matching | Match aggressive orders against resting liquidity in price-time priority | Best price matched first; within price, earliest order first | Mandatory |
| FR-MTH-002 | Pro-rata matching | Within a price level, allocate fills proportionally to order size | Allocation proportional to size; rounding by time priority | Mandatory |
| FR-MTH-003 | Deterministic replay | Replaying journal from any point produces identical output | Bit-identical output verified by replay test suite | Mandatory |
| FR-MTH-004 | Self-trade prevention | Prevent matching between orders with same STP group | STP mode applied correctly (cancel-newest/oldest/both/decrement) | Mandatory |
| FR-MTH-005 | Partial fill handling | Partially filled order remains in book with reduced quantity | Remaining quantity correct; priority maintained | Mandatory |
| FR-MTH-006 | IOC residual cancel | IOC order unfilled residual cancelled after matching attempt | No IOC order rests in book | Mandatory |
| FR-MTH-007 | FOK validation | FOK order only executes if entire quantity fillable | All-or-nothing; rejected if full qty not available | Mandatory |
| FR-MTH-008 | Market order sweep | Market orders sweep through all price levels until filled. When residual remains: (a) cancel residual (default), or (b) convert to market-to-limit with protection collar. Unprotected resting at last fill price is not supported. | Fills at multiple prices; residual policy per venue rulebook within supported set (see §11.4.3 REQ-OT-001) | Mandatory |
| FR-MTH-009 | Execution price | Passive (resting) order's price used for execution | Aggressive order may get price improvement | Mandatory |
| FR-MTH-010 | Fill generation | Each fill generates execution events for both sides | Buyer and seller each receive execution report | Mandatory |
| FR-MTH-011 | Trade ID assignment | Each matched trade assigned unique trade ID | Trade ID links buyer and seller execution IDs | Mandatory |
| FR-MTH-012 | Execution timestamp | Each fill timestamped with nanosecond precision | Timestamp from sequencer clock | Mandatory |

## 14.4 Auction

| REQ ID | Requirement | Description | Acceptance Criteria | Priority |
|--------|-------------|-------------|---------------------|----------|
| FR-AUC-001 | Opening auction | Execute opening auction at session start | Equilibrium price maximizes volume; all fills at single price | Mandatory |
| FR-AUC-002 | Closing auction | Execute closing auction at session end | Official closing price established; used for settlement/NAV | Mandatory |
| FR-AUC-003 | Volatility auction | Trigger auction on static/dynamic price threshold breach | Trading paused; auction runs; resumes continuous on completion | Mandatory |
| FR-AUC-004 | Intraday scheduled auction | Run auctions at configured intraday times | Auction executes at scheduled time with configured duration | Highly Recommended |
| FR-AUC-005 | Periodic auction | Run rapid repeated auctions as alternative to continuous | Configurable interval (e.g., 100ms); each batch executed as auction | Optional |
| FR-AUC-006 | Indicative price publication | Publish indicative equilibrium price during auction collection | Updated on every book change or at configured interval | Mandatory |
| FR-AUC-007 | Imbalance publication | Publish imbalance side and size during auction | Imbalance data included in auction market data | Mandatory |
| FR-AUC-008 | Random end period | Auction end has random component to prevent gaming | Random range configurable; seed set at session start for determinism | Mandatory |
| FR-AUC-009 | Auction extension | Extend auction if significant price change near end | Configurable extension triggers and durations | Highly Recommended |
| FR-AUC-010 | Surplus order handling | Orders not filled in auction: rest in continuous book or cancel per TIF | IOC/FOK cancelled; limit orders transition to continuous | Mandatory |
| FR-AUC-011 | Auction-only orders | Accept orders that only participate in auctions | Rejected during continuous; cancelled at auction→continuous transition | Mandatory |

## 14.5 Risk Controls

| REQ ID | Requirement | Description | Acceptance Criteria | Priority |
|--------|-------------|-------------|---------------------|----------|
| FR-RSK-001 | Price band validation | Reject orders outside configured price bands | Static and dynamic bands enforced; configurable per instrument | Mandatory |
| FR-RSK-002 | Fat finger — quantity | Reject orders exceeding max quantity | Max per order; max per instrument per participant | Mandatory |
| FR-RSK-003 | Fat finger — notional | Reject orders exceeding max notional value | Notional = price × quantity; checked against limit | Mandatory |
| FR-RSK-004 | Message rate throttling | Limit message rate per session and per participant | Excess messages rejected with throttle error | Mandatory |
| FR-RSK-005 | Max open orders | Limit resting orders per participant per instrument | New orders rejected when limit reached | Mandatory |
| FR-RSK-006 | Credit limit | Check available credit before order acceptance | Orders rejected when credit exhausted; credit updated on fill/cancel | Highly Recommended |
| FR-RSK-007 | Position limit | Enforce position limits for derivatives | Orders rejected if they would breach position limit | Highly Recommended |
| FR-RSK-008 | Kill switch — participant | Cancel all orders and disable a participant immediately | All orders cancelled within 1 second; new orders rejected | Mandatory |
| FR-RSK-009 | Kill switch — instrument | Cancel all orders for an instrument (pre-halt) | All orders cancelled; instrument transitions to halt | Mandatory |
| FR-RSK-010 | Kill switch — market-wide | Emergency halt of all trading | All instruments halted; all orders cancelled | Mandatory |
| FR-RSK-011 | Cancel-on-disconnect | Cancel orders when participant session is lost | Orders cancelled per CoD configuration after heartbeat timeout | Mandatory |
| FR-RSK-012 | Circuit breaker trigger | Transition to volatility auction on price threshold breach | Static and dynamic thresholds monitored; auction triggered automatically | Mandatory |
| FR-RSK-013 | Short sell marking | Orders flagged as short; validation hooks for locate/borrow | Short-sell flag on order; reject if locate required and not provided | Highly Recommended |
| FR-RSK-014 | Risk limit real-time update | Update risk limits without restart or session disruption | New limits effective immediately for subsequent orders | Mandatory |

## 14.6 Market Data

| REQ ID | Requirement | Description | Acceptance Criteria | Priority |
|--------|-------------|-------------|---------------------|----------|
| FR-MKD-001 | Full depth publication | Publish all price levels for each instrument | Every book change generates depth update message | Mandatory |
| FR-MKD-002 | Top of book | Publish best bid/offer with aggregated quantities | Updated on every BBO change | Mandatory |
| FR-MKD-003 | Trade publication | Publish every executed trade | Price, size, timestamp, aggressor side published | Mandatory |
| FR-MKD-004 | Instrument status | Publish instrument state changes | State transition messages with reason | Mandatory |
| FR-MKD-005 | Snapshot service | Provide full book snapshot on request | Snapshot includes all resting orders (aggregated by level) with sequence number | Mandatory |
| FR-MKD-006 | Gap detection support | Sequential message numbering per channel | Monotonic sequence numbers; heartbeats for liveness | Mandatory |
| FR-MKD-007 | Multicast dissemination | Low-latency multicast for colocated participants | Configurable channel-to-instrument mapping | Mandatory |
| FR-MKD-008 | TCP/WebSocket dissemination | Market data for non-colocated participants | Configurable conflation; backpressure handling | Highly Recommended |
| FR-MKD-009 | Statistics publication | Periodic statistics (OHLC, VWAP, volume, trade count) | Updated at configurable interval (default 1 second) | Mandatory |
| FR-MKD-010 | Auction indicative data | Publish equilibrium price, volume, imbalance during auctions | Updated on every book change or at configured interval | Mandatory |

## 14.7 Reference Data

| REQ ID | Requirement | Description | Acceptance Criteria | Priority |
|--------|-------------|-------------|---------------------|----------|
| FR-REF-001 | Instrument CRUD | Create, read, update instruments | Full instrument lifecycle support | Mandatory |
| FR-REF-002 | Instrument listing | Add instruments to trading (list) with all required parameters | Instrument tradeable after listing with correct tick/lot/session | Mandatory |
| FR-REF-003 | Instrument delisting | Remove instruments from trading | Orderly delisting: cancel orders, suspend, delist | Mandatory |
| FR-REF-004 | Tick size management | Configure tick size tables per instrument group | Price-dependent tick sizes; validated on every order | Mandatory |
| FR-REF-005 | Session scheduling | Define and manage trading calendars and session phases | Automatic state transitions per schedule | Mandatory |
| FR-REF-006 | Holiday calendar | Non-trading days per market/jurisdiction | Trading prevented on holidays; sessions skipped | Mandatory |
| FR-REF-007 | Corporate action support | Update reference prices, tick sizes, lot sizes for corporate actions | Ex-date adjustments applied before session open | Highly Recommended |
| FR-REF-008 | Participant management | CRUD for participants, accounts, entitlements | Full participant lifecycle | Mandatory |
| FR-REF-009 | Fee schedule management | Configure maker/taker fees, volume tiers, special programs | Fees applied correctly to executed trades | Mandatory |
| FR-REF-010 | Configuration versioning | All reference data changes versioned with effective date | Historical state retrievable; changes audited | Mandatory |

## 14.8 Post-Trade

| REQ ID | Requirement | Description | Acceptance Criteria | Priority |
|--------|-------------|-------------|---------------------|----------|
| FR-PTC-001 | Trade capture | Record all executed trades with full detail | Every execution captured with all fields; queryable | Mandatory |
| FR-PTC-002 | Fee calculation | Calculate fees per trade based on fee schedule | Correct maker/taker fee; volume tier applied | Mandatory |
| FR-PTC-003 | Trade confirmation | Confirm trade details to both counterparties | Confirmation delivered within 1 second of execution | Mandatory |
| FR-PTC-004 | Clearing submission | Submit trades to CCP for clearing | Trades submitted within SLA; acknowledgment tracked | Mandatory (Phase 2) |
| FR-PTC-005 | Allocation workflow | Split block trades into sub-allocations | Allocations sum to original; each allocation cleared separately | Highly Recommended |
| FR-PTC-006 | Give-up workflow | Transfer trade from executing to clearing broker | Both parties confirm; clearing updated | Highly Recommended |
| FR-PTC-007 | Trade bust | Cancel erroneous trade with four-eyes approval | Trade busted; counterparties notified; clearing reversed | Mandatory |
| FR-PTC-008 | Trade correction | Correct trade price with four-eyes approval | Original and corrected both in audit trail | Mandatory |
| FR-PTC-009 | Position calculation | Real-time position per participant per instrument | Position updated on every fill; queryable via API | Highly Recommended |
| FR-PTC-010 | End-of-day processing | Generate EOD trade files, settlement instructions, reports | All trades reconciled; files generated within 30 min of close | Mandatory |

## 14.9 Operations

| REQ ID | Requirement | Description | Acceptance Criteria | Priority |
|--------|-------------|-------------|---------------------|----------|
| FR-OPS-001 | Session start/stop | Operator can start and stop trading sessions | State transitions executed correctly; all participants notified | Mandatory |
| FR-OPS-002 | Instrument halt/resume | Operator can halt and resume individual instruments | Instrument state changes; orders handled per halt policy | Mandatory |
| FR-OPS-003 | Parameter hot-reload | Update venue parameters without system restart | New parameters effective for next order; no downtime | Mandatory |
| FR-OPS-004 | Four-eyes approval | Sensitive operations require second approver | Operation pending until approved; timeout for stale approvals | Mandatory |
| FR-OPS-005 | Replay viewer | Reconstruct order book state at any historical timestamp | Book state matches what was live at that time | Mandatory |
| FR-OPS-006 | Reconciliation tools | Compare internal state with external systems | Discrepancies identified and reported | Mandatory |
| FR-OPS-007 | Audit trail query | Search and export audit records by time, participant, instrument | Full text search; export to CSV/JSON | Mandatory |
| FR-OPS-008 | Operational dashboards | Real-time display of system health, trading activity, alerts | Dashboard updates < 5 seconds; configurable layouts | Mandatory |
| FR-OPS-009 | Alert management | Configure, view, and acknowledge operational alerts | Alert rules configurable; escalation chains defined | Mandatory |
| FR-OPS-010 | Runbook integration | Step-through procedures for operational scenarios | Runbooks linked to operational events; progress tracked | Highly Recommended |
