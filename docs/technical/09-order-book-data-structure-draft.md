# Order Book Data Structure — Engineering Draft

**Status**: Starter design — language-agnostic pseudocode; aligns with frozen baseline
**Source**: §7.3 Order Book Model, §11 Core Matching Engine

---

## Partition Ownership Model

```
Partition {
    partition_id: uint16
    instruments: Map<InstrumentId, OrderBook>     // 1..N books per partition
    sequence_number: uint64                         // partition-local monotonic
    config: PartitionConfig                         // tick tables, price bands, session profiles
    state: PartitionState                           // RUNNING, RECOVERING, STOPPED
}
```

- One thread owns one partition. No shared mutable state across partitions.
- Instruments assigned to partitions at startup via reference data. Reassignment requires maintenance window.
- High-activity instruments may be isolated to a dedicated partition (1 instrument = 1 partition).

## Order Book

```
OrderBook {
    instrument_id: uint32
    state: InstrumentState                          // PRE_OPEN, AUCTION, CONTINUOUS, HALT, etc.
    buy_side: PriceLevelTree                        // sorted best (highest) to worst
    sell_side: PriceLevelTree                       // sorted best (lowest) to worst
    orders_by_id: HashMap<uint64, Order*>           // O(1) lookup by OrderId
    stop_list: StopTriggerList                      // dormant stop/stop-limit orders
    stats: BookStatistics                           // high, low, last, volume, trade count

    // Configuration (from reference data)
    tick_size_table: TickSizeTable*
    lot_size_rule: LotSizeRule*
    static_price_band_pct: decimal
    dynamic_price_band_pct: decimal
    reference_price: int64                          // for price band calculations
    last_trade_price: int64                         // for dynamic bands
    circuit_breaker_enabled: boolean
    priority_model: PriorityModel                   // PRICE_TIME for FR-1
    stp_default_mode: STPMode
    market_order_residual_policy: ResidualPolicy    // CANCEL or MARKET_TO_LIMIT_WITH_COLLAR
    collar_ticks: uint32                            // protection collar width (if MTL policy)
}
```

## Price Level

```
PriceLevel {
    price: int64                                    // integer representation (price x multiplier)
    total_visible_qty: uint64
    total_hidden_qty: uint64                        // iceberg reserve (Phase 2)
    order_count: uint32
    orders: DoublyLinkedList<Order>                 // in priority sequence (time order for FIFO)
    next_better: PriceLevel*                        // intrusive linked list for level traversal
    next_worse: PriceLevel*
}
```

**Data structure choice for price levels**:
- **Option A**: Red-black tree of price levels — O(log N) insert/remove, O(1) best price
- **Option B**: Pre-allocated array indexed by price (if tick structure is bounded) — O(1) everything
- **Recommendation**: Option A for general case; Option B if instrument has known bounded price range

## Order Object

```
Order {
    order_id: uint64                                // venue-assigned (partition:sequence composite)
    client_order_id: uint64                         // participant-assigned
    participant_id: uint32
    account_id: uint32
    instrument_id: uint32
    side: Side                                      // BUY, SELL
    order_type: OrderType                           // MARKET, LIMIT, STOP, STOP_LIMIT
    time_in_force: TimeInForce                      // DAY, GTC, GTD, IOC, FOK
    price: int64                                    // integer-encoded limit price
    original_qty: uint64
    remaining_qty: uint64
    display_qty: uint64                             // iceberg display (= remaining_qty if not iceberg)
    hidden_qty: uint64                              // iceberg reserve
    cumulative_filled_qty: uint64
    stop_price: int64                               // trigger price for stop orders
    stp_group_id: uint32                            // self-trade prevention group
    flags: uint16                                   // POST_ONLY, REDUCE_ONLY, CANCEL_ON_DISCONNECT
    entry_sequence: uint64                          // sequencer sequence number (deterministic priority)
    entry_timestamp: uint64                         // nanosecond PTP timestamp
    expiry_time: uint64                             // GTD expiry (0 = not applicable)

    // Intrusive linked list pointers (for price-level queue)
    prev_in_level: Order*
    next_in_level: Order*
    price_level: PriceLevel*                        // back-pointer for O(1) removal
}
```

**Memory management**: All Order objects pre-allocated in a pool at startup. No allocation during matching.

## Order Pool

```
OrderPool {
    storage: PreAllocatedArray<Order>               // e.g., 1M orders pre-allocated
    free_list: Stack<Order*>                        // available slots
    active_count: uint32

    allocate() -> Order*                            // O(1), pop from free list
    release(order: Order*)                          // O(1), push to free list
}
```

## Cancel / Replace Handling

**Cancel**:
1. Look up order by OrderId in `orders_by_id` — O(1)
2. Remove from price-level linked list — O(1) via intrusive pointers
3. Update price level aggregates (total_qty, order_count)
4. If price level empty, remove from tree — O(log N)
5. Return Order to pool
6. Generate OrderCancel event

**Modify (price change)**:
1. Cancel existing order at old price level (steps 1–5 above)
2. Re-insert at new price level with new entry_sequence (loses time priority)
3. Generate OrderModify event

**Modify (quantity reduce only)**:
1. Look up order — O(1)
2. Reduce remaining_qty in place — O(1)
3. Update price level aggregate — O(1)
4. Retain time priority (entry_sequence unchanged)
5. Generate OrderModify event

**Modify (quantity increase)**:
- Configurable: retain or lose priority (venue rule parameter)
- If loses priority: cancel + re-insert (same as price change)

## Self-Trade Prevention Hook

```
function check_stp(aggressive: Order, passive: Order) -> STPAction:
    if aggressive.stp_group_id == 0 or passive.stp_group_id == 0:
        return NO_STP                               // STP not active for this order

    if aggressive.stp_group_id != passive.stp_group_id:
        return NO_STP                               // different groups, no self-trade

    // Same STP group — apply venue policy
    match book.stp_default_mode:
        CANCEL_NEWEST  -> return CANCEL_AGGRESSIVE
        CANCEL_OLDEST  -> return CANCEL_PASSIVE
        CANCEL_BOTH    -> return CANCEL_BOTH
        DECREMENT      -> return DECREMENT_BOTH     // Phase 2
```

**Hook point**: Called inside the matching loop before each potential fill. If STP triggers, the specified order(s) are cancelled and matching continues with the next candidate.

## Market-Order Residual Policy Handling

```
function handle_market_residual(order: Order, book: OrderBook):
    match book.market_order_residual_policy:
        CANCEL:
            cancel_order(order)
            emit(OrderCancel, reason=MARKET_RESIDUAL_CANCEL)

        MARKET_TO_LIMIT_WITH_COLLAR:
            if order.cumulative_filled_qty == 0:
                // Never filled — cancel (no reference price for collar)
                cancel_order(order)
                emit(OrderCancel, reason=MARKET_NO_FILLS)
            else:
                collar_price = last_fill_price +/- (book.collar_ticks * tick_size)
                if price_within_bands(collar_price, book):
                    convert_to_limit(order, collar_price)
                    insert_into_book(order)
                    emit(OrderConvertedToLimit, collar_price)
                else:
                    cancel_order(order)
                    emit(OrderCancel, reason=COLLAR_BREACH)
```

**Frozen policy**: Unprotected resting at last fill price without a collar is NOT implemented.

## Snapshot / Replay Interaction

**Snapshot capture**:
```
function capture_snapshot(partition: Partition) -> Snapshot:
    snapshot.partition_id = partition.partition_id
    snapshot.sequence_number = partition.sequence_number     // current seq#
    snapshot.timestamp = now()
    for each book in partition.instruments:
        snapshot.books.append(serialize_book(book))          // all orders, all levels
    snapshot.checksum = compute_checksum(snapshot)
    return snapshot
```

**Replay from snapshot**:
```
function restore_from_snapshot(partition: Partition, snapshot: Snapshot):
    partition.sequence_number = snapshot.sequence_number
    for each book_data in snapshot.books:
        restore_book(partition, book_data)

function replay_journal(partition: Partition, journal: Journal, from_seq: uint64):
    for each entry in journal.read_from(from_seq):
        process_message(partition, entry)                    // deterministic — same output
    // After replay: partition state is identical to pre-failure state
```

**Determinism guarantee**: Replay produces bit-identical state because the partition is single-threaded and all inputs are deterministically sequenced.

## Memory Layout Considerations

- **Cache-friendly**: Orders at the same price level should be in contiguous or near-contiguous memory when possible
- **Avoid pointer chasing**: Consider struct-of-arrays for hot fields (price, qty, side) if profiling shows cache misses
- **Pre-allocate everything**: Order pool, price-level pool, event buffers — zero allocation in hot path
- **NUMA-local**: All partition memory allocated on the same NUMA node as the processing CPU core
- **Hugepages**: Use 2MB or 1GB hugepages to reduce TLB misses

## Complexity Summary

| Operation | Complexity | Notes |
|-----------|:----------:|-------|
| Insert order | O(log N) or O(1) | Tree insert or array index |
| Cancel order by ID | O(1) | HashMap lookup + linked list remove |
| Match at best price | O(1) per fill | Walk best level's linked list |
| Find best bid/ask | O(1) | Cached at tree root or head pointer |
| Modify price | O(log N) | Cancel + re-insert |
| Modify quantity (reduce) | O(1) | In-place update |
| Snapshot | O(N) | Serialize all active orders |
| Replay single event | O(log N) | Same as insert |
