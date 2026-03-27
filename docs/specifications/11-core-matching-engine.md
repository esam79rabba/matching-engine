# 11. Core Matching Engine Functions

## 11.1 Architecture Overview

The core matching engine is designed as a **deterministic, event-sourced, single-threaded-per-partition processing kernel**.

```
                     ┌─────────────┐
    Orders ─────────→│  SEQUENCER  │── Sequenced events ──→ Journal
    (from gateways)  └──────┬──────┘                        (WAL)
                            │
                            ▼
                     ┌─────────────┐
                     │  MATCHING   │── Execution events ──→ Output Bus
                     │   ENGINE    │                         │
                     │  PARTITION  │                         ├→ Market Data Publisher
                     │             │                         ├→ Drop Copy
                     └─────────────┘                         ├→ Risk Updates
                            │                                ├→ Post-Trade
                            │                                └→ Surveillance
                            ▼
                     ┌─────────────┐
                     │   BOOK(s)   │  (one or more instruments per partition)
                     └─────────────┘
```

### Key Architectural Decisions

| Decision | Choice | Rationale |
|----------|--------|-----------|
| Threading model | Single-threaded per partition | Guarantees determinism without locks; simplifies reasoning and testing |
| State management | In-memory with journal replay | Lowest latency; journal provides durability and recovery |
| Persistence | Append-only journal + periodic snapshots | Enables exact replay; snapshots reduce recovery time |
| Event sourcing | All state changes derived from journaled events | Auditability, replay, determinism proof |
| Partitioning | Instruments sharded across partitions | Horizontal scaling; independent failure domains |
| Time source | Sequencer-assigned timestamps (not wall clock in engine) | Determinism; sequencer is the single source of truth for ordering |

## 11.2 Sequencer

### 11.2.1 Purpose
The sequencer is the **single point of ordering** for all messages entering a partition. It assigns:
- A monotonically increasing **sequence number**
- A **nanosecond timestamp** (from hardware clock / PTP)

### 11.2.2 Requirements

| REQ ID | Requirement | Priority |
|--------|------------|----------|
| REQ-SEQ-001 | Assign a partition-local monotonically increasing sequence number to every inbound message for that partition. Global event identity is formed as the composite `[partition_id]:[local_sequence]`, which is unique across the platform without requiring a global sequencer. | Mandatory |
| REQ-SEQ-002 | Assign nanosecond-precision timestamp from PTP-synchronized hardware clock | Mandatory |
| REQ-SEQ-003 | Persist sequenced message to journal before forwarding to matching engine | Mandatory |
| REQ-SEQ-004 | Achieve low added latency for sequencing + journaling. Target: < 1 µs for Profile A (kernel bypass, NVMe); < 5 µs for Profile B; < 50 µs for Profile C. | Target (profile-dependent) |
| REQ-SEQ-005 | Support multiple input sources (gateways) with deterministic ordering | Mandatory |
| REQ-SEQ-006 | Detect and reject duplicate messages (idempotency key) | Mandatory |
| REQ-SEQ-007 | Support warm standby sequencer for failover | Mandatory |

### 11.2.3 Multi-Gateway Fairness
When multiple gateways feed into a single sequencer, the ordering policy must be fair:

| Policy | Description | Trade-off |
|--------|-------------|-----------|
| **Time-based** | Sequence by arrival timestamp | Fairest if clocks are synchronized; requires nanosecond PTP |
| **Round-robin** | Alternate between gateways | Fair in batch; may add latency to fast gateway |
| **Batched time** | Collect for N µs, sort by timestamp, sequence | Configurable fairness vs. latency |
| **Hardware sequencer** | FPGA or network switch assigns sequence | Most deterministic; highest infrastructure cost |

**Recommendation**: Time-based with PTP as default; hardware sequencer for ultra-low-latency deployments.

## 11.3 Journal / Write-Ahead Log

### 11.3.1 Purpose
The journal is the **single source of truth** for the matching engine. Every input message and every output event is recorded in the journal in sequence order.

### 11.3.2 Requirements

| REQ ID | Requirement | Priority |
|--------|------------|----------|
| REQ-JNL-001 | Append-only, immutable write of every sequenced input message | Mandatory |
| REQ-JNL-002 | Append-only, immutable write of every output event (fills, rejects, state changes) | Mandatory |
| REQ-JNL-003 | In Regulatory-Grade mode (default): journal write durable (fsync or equivalent) before message processing and before acknowledgment. In Reduced-Latency mode (opt-in): journal writes batched within a configurable window. Mode selection is deployment-configured. | Mandatory (mode is deployment-configured) |
| REQ-JNL-004 | Journal entries include sequence number, timestamp, message type, and full payload | Mandatory |
| REQ-JNL-005 | Journal supports sequential read for replay from any sequence number | Mandatory |
| REQ-JNL-006 | Journal retention minimum 90 days online, 7 years archived | Mandatory |
| REQ-JNL-007 | Journal write-to-stable-storage latency targets: < 2 µs for Regulatory-Grade synchronous mode on NVMe (Profile A); < 500 ns per entry amortized for Reduced-Latency batched mode. Assumes NVMe SSD with direct I/O. | Highly Recommended |
| REQ-JNL-008 | Journal integrity verifiable (checksums per entry and per segment) | Mandatory |
| REQ-JNL-009 | Journal supports segment rotation (e.g., hourly or by size) | Mandatory |
| REQ-JNL-010 | Journal replication to standby for DR | Mandatory |

### 11.3.3 Journal Entry Format (Conceptual)

```
┌────────────┬────────────┬──────────┬──────────┬──────────┬───────────┬──────────┐
│ Sequence # │ Timestamp  │ Msg Type │ Partition│ Payload  │ Payload   │ CRC32    │
│ (8 bytes)  │ (8 bytes)  │ (2 bytes)│ (2 bytes)│ Length   │ (variable)│ (4 bytes)│
│            │ nanos UTC  │          │          │ (4 bytes)│           │          │
└────────────┴────────────┴──────────┴──────────┴──────────┴───────────┴──────────┘
```

### 11.3.4 Storage Strategy
- **Hot storage**: NVMe SSD, memory-mapped, sequential write-optimized
- **Warm storage**: Compressed segments on high-capacity SSD (30–90 days)
- **Cold storage**: Archived to object storage (S3/GCS) for regulatory retention (7 years)

## 11.4 Order Processing Pipeline

### 11.4.1 Order Lifecycle

```
New Order Message
        │
        ▼
┌───────────────┐    ┌──────────────┐
│ Pre-validation │───→│   REJECT     │  (invalid fields, unknown instrument)
│ (gateway)      │    └──────────────┘
└───────┬───────┘
        │ Valid
        ▼
┌───────────────┐    ┌──────────────┐
│ Risk Checks   │───→│   REJECT     │  (fat finger, credit breach, etc.)
│ (risk service) │    └──────────────┘
└───────┬───────┘
        │ Passed
        ▼
┌───────────────┐
│ Sequencer     │── Assigns seq# + timestamp; writes to journal
└───────┬───────┘
        │
        ▼
┌───────────────┐
│ Matching      │
│ Engine        │
│               │
│ 1. State check│── REJECT if instrument not in valid state for new orders
│ 2. Price band │── REJECT if price outside bands
│ 3. Tick/lot   │── REJECT if not aligned to tick/lot
│ 4. STP check  │── Handle self-trade per STP mode
│ 5. Match      │── Execute against resting orders
│ 6. Book insert│── Rest residual in book (if applicable)
│ 7. Events     │── Generate fill, insert, reject events
└───────────────┘
        │
        ▼
  Output Events → Journal → Output Bus → Market Data, Drop Copy, Post-Trade
```

### 11.4.2 Order Validation Checks (In Matching Engine)

| Check | Description | Action on Failure |
|-------|-------------|-------------------|
| Instrument exists | Instrument ID valid and known | Reject |
| Instrument state | Current state permits new orders | Reject |
| Order type permitted | Order type allowed in current state and for this instrument | Reject |
| Side valid | BUY or SELL | Reject |
| Price valid | Non-negative (or valid for instruments allowing negative); aligned to tick | Reject |
| Quantity valid | Positive; aligned to lot size; within instrument max | Reject |
| Price band check | Price within static and dynamic bands | Reject (or trigger auction) |
| TIF valid | Time-in-force valid for current state | Reject |
| GTD expiry valid | Expiry date in the future and within max | Reject |
| Account valid | Account/sub-account recognized for this participant | Reject |

### 11.4.3 Order Types — Detailed Behavior

#### REQ-OT-001: Market Order
- **Behavior**: Execute at best available price(s); sweep through price levels until fully filled or book exhausted
- **Residual handling** (configurable per venue rulebook):
  - **(a) Cancel residual** (default) — unfilled portion is immediately cancelled. An unsolicited cancel event is published.
  - **(b) Market-to-limit with protection collar** — residual converts to a limit order at the last fill price, subject to a configurable protection collar (e.g., ± N ticks or ± X% from reference price). If the collar would be breached, the residual is cancelled instead.
  - **(c) Venue-rulebook-defined policy** — custom handling via rule-parameter framework.
  - **Note**: Resting a market order residual at an unprotected last-fill price is explicitly **not supported** due to the risk of placing a resting order at a price that does not reflect fair value after a book sweep.
- **State restriction**: Not permitted in auction collection phase (configurable)
- **Risk**: Can result in poor fills in thin books; price band protection and residual collar mitigate

#### REQ-OT-002: Limit Order
- **Behavior**: Execute at limit price or better; rest residual in book at limit price
- **Priority**: Enters book at the back of the queue at its price level
- **State**: Permitted in all states that accept orders

#### REQ-OT-003: Stop Order
- **Behavior**: Dormant until trigger price is reached; then converts to market order
- **Trigger**: Last traded price crosses stop price (buy stop: trade ≥ stop; sell stop: trade ≤ stop)
- **Storage**: Stop orders stored in a separate trigger list, not in the order book
- **Risk**: Cascading stops can accelerate price movement

#### REQ-OT-004: Stop-Limit Order
- **Behavior**: Like stop, but converts to limit order (not market) when triggered
- **Parameters**: Stop price (trigger) and limit price (worst execution price)

#### REQ-OT-005: Pegged Order
- **Behavior**: Price automatically adjusts to track a reference price
- **Variants**:
  - Primary peg: tracks best bid (buy) or best ask (sell)
  - Midpoint peg: tracks midpoint of BBO
  - Market peg: tracks opposite side best (buy pegs to ask, sell pegs to bid)
- **Offset**: Optional offset from peg reference (e.g., midpoint - 1 tick)
- **Reprice frequency**: On every BBO change (or configurable)

#### REQ-OT-006: Iceberg / Reserve Order
- **Behavior**: Only a portion (display quantity) visible in book; hidden portion replenished when displayed fills
- **Parameters**: Total quantity, display quantity, optional variance (randomize displayed size ±X%)
- **Priority**: Replenished portion goes to back of queue at its price level
- **Market data**: Only display quantity published; total quantity hidden

#### REQ-OT-007: Post-Only Order
- **Behavior**: Only accepted if it would rest in the book (add liquidity); rejected if it would immediately match
- **Purpose**: Allows participants to earn maker rebates and avoid taker fees
- **Variant**: Slide — instead of reject, adjust price to one tick less aggressive to avoid matching

#### REQ-OT-008: Reduce-Only Order
- **Behavior**: Only reduces an existing position; rejected if it would create or increase a position
- **Dependency**: Requires position tracking (external risk service provides position data)
- **Use case**: Closing positions in derivatives/digital assets

#### REQ-OT-009: IOC (Immediate-or-Cancel)
- **Behavior**: Execute as much as possible immediately; cancel any unfilled residual
- **No book insertion**: IOC orders never rest in the book

#### REQ-OT-010: FOK (Fill-or-Kill)
- **Behavior**: Execute the entire quantity immediately or cancel the entire order
- **Atomicity**: All-or-nothing; no partial fills

#### REQ-OT-011: GTC (Good-til-Cancel)
- **Behavior**: Remains in the book until explicitly cancelled or a maximum lifetime is reached
- **Max lifetime**: Configurable (e.g., 90 days)
- **Session handling**: Survives end-of-day; carries over to next session

#### REQ-OT-012: GTD (Good-til-Date)
- **Behavior**: Remains in book until specified date/time, then automatically cancelled
- **Validation**: Expiry must be in the future and within configured maximum

#### REQ-OT-013: Day Order
- **Behavior**: Valid for the current trading session only; cancelled at end of session

#### REQ-OT-014: Auction-Only Order
- **Behavior**: Only participates in auctions; cancelled or rejected if submitted during continuous trading
- **Use case**: Opening/closing price participation

#### REQ-OT-015: Mass Quote
- **Behavior**: A single message containing multiple two-sided quotes (bid + ask) for multiple instruments
- **Efficiency**: Reduces message count for market makers quoting many instruments
- **Replacement**: Each new mass quote replaces previous quotes for the same instruments from the same session
- **Cancel**: Mass cancel of all quotes by session

## 11.5 Matching Algorithm

### 11.5.1 Continuous Matching

```
function match(aggressiveOrder, book):
    while aggressiveOrder.remainingQty > 0:
        bestLevel = book.getBestOppositeLevel(aggressiveOrder.side)

        if bestLevel is null:
            break  // no liquidity

        if not pricesCross(aggressiveOrder, bestLevel):
            break  // no price match

        for restingOrder in bestLevel.orders():  // in priority order
            if aggressiveOrder.remainingQty == 0:
                break

            if isSelfTrade(aggressiveOrder, restingOrder):
                handleSTP(aggressiveOrder, restingOrder)
                continue

            fillQty = min(aggressiveOrder.remainingQty, restingOrder.remainingQty)
            fillPrice = restingOrder.price  // passive order's price

            if aggressiveOrder.minQty > 0 and fillQty < aggressiveOrder.minQty:
                continue  // minimum quantity not met

            generateExecution(aggressiveOrder, restingOrder, fillQty, fillPrice)

            aggressiveOrder.remainingQty -= fillQty
            restingOrder.remainingQty -= fillQty

            if restingOrder.remainingQty == 0:
                removeFromBook(restingOrder)
            elif restingOrder.isIceberg and restingOrder.displayQty == 0:
                replenishIceberg(restingOrder)  // goes to back of queue

        if bestLevel.isEmpty():
            removeLevel(bestLevel)

    // Handle residual
    if aggressiveOrder.remainingQty > 0:
        if aggressiveOrder.TIF == IOC:
            cancelResidual(aggressiveOrder)
        elif aggressiveOrder.TIF == FOK:
            // FOK should have been checked before matching started
            unreachable()
        elif aggressiveOrder.type == MARKET:
            handleMarketOrderResidual(aggressiveOrder)  // configurable
        else:
            insertIntoBook(aggressiveOrder)  // rest in book
```

### 11.5.2 FOK Pre-Check

```
function canFillFOK(order, book):
    availableQty = 0
    for level in book.oppositeLevels(order.side):
        if not pricesCross(order, level):
            break
        for restingOrder in level.orders():
            if not isSelfTrade(order, restingOrder):
                availableQty += restingOrder.remainingQty
                if availableQty >= order.quantity:
                    return true
    return false
```

## 11.6 State Checkpointing and Recovery

### 11.6.1 Snapshot Strategy

| REQ ID | Requirement | Priority |
|--------|------------|----------|
| REQ-REC-001 | Periodic state snapshot capturing all order books, pending orders, and instrument states | Mandatory |
| REQ-REC-002 | Snapshot frequency configurable (e.g., every 60 seconds, every N events, or on-demand) | Mandatory |
| REQ-REC-003 | Snapshot includes the journal sequence number at the point of capture | Mandatory |
| REQ-REC-004 | Snapshot written atomically (no partial snapshots) | Mandatory |
| REQ-REC-005 | Warm restart: load latest snapshot + replay journal from snapshot sequence number | Mandatory |
| REQ-REC-006 | Warm restart time < 60 seconds for typical state (100K resting orders) | Mandatory |
| REQ-REC-007 | Snapshot format is versioned for forward/backward compatibility | Highly Recommended |

### 11.6.2 Recovery Modes

| Mode | Description | RTO | Data Loss |
|------|-------------|-----|-----------|
| **Warm restart** | Same host; load snapshot + replay journal | < 60 seconds | Zero (journal intact) |
| **Hot standby failover** | Standby continuously replays journal; promotes on primary failure | < 5 seconds | Zero (real-time replication) |
| **Cold restart** | Full rebuild from journal (no snapshot) | Minutes to hours | Zero (full journal available) |
| **DR failover** | Switch to DR site with replicated journal | < 5 minutes | Near-zero (replication lag) |
| **Point-in-time reconstruction** | Replay journal to any historical point | Variable | Not applicable (forensic use) |

## 11.7 Partition Model

### 11.7.1 Partitioning Strategy

Instruments are assigned to partitions. Each partition:
- Runs on a dedicated CPU core (pinned)
- Has its own journal
- Has its own sequencer (or a shared sequencer with partition routing)
- Is an independent failure domain
- Can be independently recovered

### 11.7.2 Partition Sizing Guidelines

| Parameter | Guideline |
|-----------|-----------|
| Instruments per partition | 100–1,000 (depending on activity level) |
| Max active orders per partition | 1,000,000 |
| Target throughput per partition | 1,000,000 messages/second |
| Memory per partition | Pre-allocated, typically 1–4 GB |

### 11.7.3 Cross-Partition Considerations
- No cross-partition matching (instruments belong to exactly one partition)
- Cross-partition operations (e.g., cancel-all for a participant) are fan-out messages to each partition
- Global sequence numbers are partition-prefixed (no global sequencer bottleneck)
- Market data combines output from all partitions

## 11.8 Clocking and Timestamping

### 11.8.1 Requirements

| REQ ID | Requirement | Priority |
|--------|------------|----------|
| REQ-CLK-001 | All timestamps in nanoseconds since Unix epoch (UTC) | Mandatory |
| REQ-CLK-002 | Hardware NIC timestamping for gateway ingress/egress | Mandatory |
| REQ-CLK-003 | PTP (IEEE 1588) synchronization across all hosts | Mandatory |
| REQ-CLK-004 | PTP accuracy target: < 1 µs to grandmaster clock | Mandatory |
| REQ-CLK-005 | Grandmaster clock disciplined by GPS or atomic reference | Mandatory |
| REQ-CLK-006 | Clock sync monitoring with alerting on drift > 1 µs | Mandatory |
| REQ-CLK-007 | All journal entries carry hardware timestamp | Mandatory |
| REQ-CLK-008 | Regulatory timestamp (per MiFID II RTS 25): ≤ 100 µs accuracy to UTC for order events | Mandatory (EU venues) |

### 11.8.2 Timestamp Types

| Timestamp | Source | Purpose |
|-----------|--------|---------|
| **Gateway receive** | NIC hardware timestamp | Regulatory; fairness measurement |
| **Sequencer assign** | Sequencer hardware clock | Ordering; deterministic time |
| **Engine process** | Engine loop counter (deterministic) | Internal profiling |
| **Gateway send** | NIC hardware timestamp | Latency measurement |
| **Exchange time** | Sequencer timestamp | Business/regulatory timestamp of record |
