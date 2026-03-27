# Engine Matching Algorithm — FR-1 Pseudocode

**Status**: Engineering draft — language-agnostic; FR-1 scope only
**Source**: §11.4–§11.5 (frozen baseline), §09 order-book design draft

---

## Ingress Assumptions

Messages arrive at the partition boundary already:
1. Parsed and validated by the gateway (field types, required fields)
2. Risk-checked by the pre-trade risk service (fat-finger, throttle — or stub for M1)
3. Sequenced: carrying a partition-local monotonic sequence number and PTP nanosecond timestamp
4. Journaled: input event durably written (Regulatory-Grade) or batched (Reduced-Latency)

The engine receives a sequenced, risk-cleared message and processes it deterministically.

---

## Main Processing Loop

```
function partition_loop(partition: Partition):
    while partition.state == RUNNING:
        msg = partition.input_queue.poll()          // busy-spin poll; no sleep
        if msg is null:
            continue

        // Deterministic processing — no external I/O, no wall-clock reads
        result = process_message(partition, msg)

        // Journal output events
        for event in result.events:
            partition.journal.write_output(event)   // append to output section

        // Publish to output bus (market data, drop copy, post-trade, surveillance)
        for event in result.events:
            partition.output_bus.publish(event)
```

---

## Message Dispatch

```
function process_message(partition, msg) -> ProcessingResult:
    match msg.type:
        NEW_ORDER       -> return handle_new_order(partition, msg)
        CANCEL_ORDER    -> return handle_cancel(partition, msg)
        MODIFY_ORDER    -> return handle_modify(partition, msg)
        MASS_CANCEL     -> return handle_mass_cancel(partition, msg)
        STATE_CHANGE    -> return handle_state_change(partition, msg)  // from control plane
        SESSION_DISCONNECT -> return handle_disconnect(partition, msg) // CoD synthetic cancels
        _               -> return reject(msg, UNKNOWN_MESSAGE_TYPE)
```

---

## New Order Flow

```
function handle_new_order(partition, msg) -> ProcessingResult:
    book = partition.get_book(msg.instrument_id)
    if book is null:
        return reject(msg, UNKNOWN_INSTRUMENT)

    // ── VALIDATION ──────────────────────────────
    if not book.state.accepts_new_orders():
        return reject(msg, INSTRUMENT_STATE_INVALID)

    if not is_valid_order_type(msg.order_type, book):
        return reject(msg, INVALID_ORDER_TYPE)

    if not is_valid_side(msg.side):
        return reject(msg, INVALID_SIDE)

    if msg.order_type == LIMIT:
        if not is_tick_aligned(msg.price, book.tick_size_table):
            return reject(msg, INVALID_TICK)

    if not is_lot_aligned(msg.quantity, book.lot_size_rule):
        return reject(msg, INVALID_LOT)

    if msg.quantity <= 0 or msg.quantity > book.lot_size_rule.max_quantity:
        return reject(msg, INVALID_QUANTITY)

    if msg.order_type == LIMIT:
        if not is_within_price_bands(msg.price, book):
            return reject(msg, PRICE_OUTSIDE_BANDS)

    // ── POST-ONLY CHECK (if flag set) ───────────
    if msg.flags.POST_ONLY and msg.order_type == LIMIT:
        if would_immediately_match(msg, book):
            return reject(msg, POST_ONLY_WOULD_MATCH)

    // ── ALLOCATE ORDER ──────────────────────────
    order = partition.order_pool.allocate()
    populate_order(order, msg, partition.sequence_number)

    // ── FOK PRE-CHECK ───────────────────────────
    if order.time_in_force == FOK:
        if not can_fill_entirely(order, book):
            partition.order_pool.release(order)
            return reject(msg, FOK_INSUFFICIENT_LIQUIDITY)

    // ── MATCH ───────────────────────────────────
    result = match_order(order, book)

    // ── HANDLE RESIDUAL ─────────────────────────
    if order.remaining_qty > 0:
        if order.time_in_force == IOC:
            result.add_event(OrderCancel, order, reason=IOC_RESIDUAL)
            partition.order_pool.release(order)

        elif order.order_type == MARKET:
            handle_market_residual(order, book, result)

        elif order.order_type == LIMIT:
            insert_into_book(order, book)
            result.add_event(OrderNew, order)
            // → MARKET DATA: depth update at order.price level

        else:
            // GTC/GTD/Day limit orders rest in book
            insert_into_book(order, book)
            result.add_event(OrderNew, order)

    elif order.remaining_qty == 0:
        // Fully filled — return to pool
        partition.order_pool.release(order)

    return result
```

---

## Matching Loop (Price-Time FIFO)

```
function match_order(aggressive: Order, book: OrderBook) -> ProcessingResult:
    result = ProcessingResult.new()

    while aggressive.remaining_qty > 0:
        best_level = book.get_best_opposite_level(aggressive.side)

        if best_level is null:
            break                                   // no liquidity

        if not prices_cross(aggressive, best_level):
            break                                   // no price match

        // Walk the level in time priority (FIFO — front of linked list first)
        cursor = best_level.orders.head

        while cursor is not null and aggressive.remaining_qty > 0:
            passive = cursor
            cursor = cursor.next_in_level           // advance before potential removal

            // ── STP CHECK ───────────────────────
            stp_action = check_stp(aggressive, passive, book.stp_default_mode)

            if stp_action == CANCEL_AGGRESSIVE:
                result.add_event(OrderSTPCancel, aggressive, passive.order_id)
                aggressive.remaining_qty = 0        // stop matching
                partition.order_pool.release(aggressive)
                break

            elif stp_action == CANCEL_PASSIVE:
                remove_from_book(passive, book)
                result.add_event(OrderSTPCancel, passive, aggressive.order_id)
                partition.order_pool.release(passive)
                continue                            // try next passive order

            elif stp_action == CANCEL_BOTH:
                remove_from_book(passive, book)
                result.add_event(OrderSTPCancel, passive, aggressive.order_id)
                result.add_event(OrderSTPCancel, aggressive, passive.order_id)
                partition.order_pool.release(passive)
                aggressive.remaining_qty = 0
                partition.order_pool.release(aggressive)
                break

            // ── MINIMUM QUANTITY CHECK ───────────
            fill_qty = min(aggressive.remaining_qty, passive.remaining_qty)

            if aggressive.min_qty > 0 and fill_qty < aggressive.min_qty:
                continue                            // skip this passive; try next

            // ── GENERATE FILL ───────────────────
            fill_price = passive.price              // execution at passive's price

            trade_id = partition.next_trade_id()
            exec_id_buy = partition.next_exec_id()
            exec_id_sell = partition.next_exec_id()

            // Update quantities
            aggressive.remaining_qty -= fill_qty
            aggressive.cumulative_filled_qty += fill_qty
            passive.remaining_qty -= fill_qty
            passive.cumulative_filled_qty += fill_qty

            // Determine buyer/seller
            if aggressive.side == BUY:
                buyer_order = aggressive; seller_order = passive
                buyer_exec = exec_id_buy; seller_exec = exec_id_sell
            else:
                buyer_order = passive; seller_order = aggressive
                buyer_exec = exec_id_buy; seller_exec = exec_id_sell

            // ── EMIT EXECUTION EVENTS ───────────
            result.add_event(ExecutionNew, {
                trade_id, fill_price, fill_qty,
                buyer: { order_id: buyer_order.order_id, exec_id: buyer_exec,
                         participant_id: buyer_order.participant_id,
                         liquidity: buyer_order == passive ? MAKER : TAKER },
                seller: { order_id: seller_order.order_id, exec_id: seller_exec,
                          participant_id: seller_order.participant_id,
                          liquidity: seller_order == passive ? MAKER : TAKER },
                aggressor_side: aggressive.side
            })
            // → MARKET DATA: trade message (fill_price, fill_qty, aggressor_side)
            // → DROP COPY: execution reports to both participants
            // → POST-TRADE: trade capture + fee calculation
            // → SURVEILLANCE: execution event

            // Update book statistics
            book.last_trade_price = fill_price
            book.stats.update(fill_price, fill_qty)

            // ── PASSIVE ORDER CLEANUP ───────────
            if passive.remaining_qty == 0:
                remove_from_book(passive, book)
                partition.order_pool.release(passive)

            // [PLACEHOLDER: Iceberg replenishment — Phase 2]

        // end level walk

        if best_level.is_empty():
            book.remove_level(best_level)

    // end while liquidity exists

    return result
```

---

## Market-Order Residual Policy (Frozen)

```
function handle_market_residual(order: Order, book: OrderBook, result: ProcessingResult):
    match book.market_order_residual_policy:

        CANCEL:
            result.add_event(OrderCancel, order, reason=MARKET_RESIDUAL_CANCEL)
            partition.order_pool.release(order)
            // → MARKET DATA: order cancel

        MARKET_TO_LIMIT_WITH_COLLAR:
            if order.cumulative_filled_qty == 0:
                // Never filled — no reference price for collar
                result.add_event(OrderCancel, order, reason=MARKET_NO_FILLS)
                partition.order_pool.release(order)
            else:
                tick = get_tick_size(book.last_trade_price, book.tick_size_table)
                if order.side == BUY:
                    collar_price = book.last_trade_price + (book.collar_ticks * tick)
                else:
                    collar_price = book.last_trade_price - (book.collar_ticks * tick)

                if is_within_price_bands(collar_price, book):
                    order.order_type = LIMIT
                    order.price = collar_price
                    insert_into_book(order, book)
                    result.add_event(OrderConvertedToLimit, order, collar_price)
                    // → MARKET DATA: new resting order at collar_price
                else:
                    result.add_event(OrderCancel, order, reason=COLLAR_OUTSIDE_BANDS)
                    partition.order_pool.release(order)

    // NOTE: Unprotected resting at last fill price is NOT implemented.
```

---

## Cancel Flow

```
function handle_cancel(partition, msg) -> ProcessingResult:
    order = partition.orders_by_id.get(msg.orig_order_id)
    if order is null:
        return cancel_reject(msg, UNKNOWN_ORDER)

    book = partition.get_book(order.instrument_id)
    remove_from_book(order, book)
    result = ProcessingResult.new()
    result.add_event(OrderCancel, order, reason=PARTICIPANT_REQUEST)
    // → MARKET DATA: depth update (remove qty at order.price)
    partition.order_pool.release(order)
    return result
```

---

## Modify (Cancel/Replace) Flow

```
function handle_modify(partition, msg) -> ProcessingResult:
    order = partition.orders_by_id.get(msg.orig_order_id)
    if order is null:
        return cancel_reject(msg, UNKNOWN_ORDER)

    book = partition.get_book(order.instrument_id)

    // ── PRICE CHANGE ────────────────────────────
    if msg.new_price != 0 and msg.new_price != order.price:
        if not is_tick_aligned(msg.new_price, book.tick_size_table):
            return cancel_reject(msg, INVALID_TICK)
        if not is_within_price_bands(msg.new_price, book):
            return cancel_reject(msg, PRICE_OUTSIDE_BANDS)

        // Remove from old level, re-insert at new level (loses time priority)
        remove_from_book(order, book)
        order.price = msg.new_price
        order.entry_sequence = partition.sequence_number  // new priority
        // Attempt to match at new price before inserting
        result = match_order(order, book)
        if order.remaining_qty > 0:
            insert_into_book(order, book)
        result.add_event(OrderModify, order)
        return result

    // ── QUANTITY CHANGE ─────────────────────────
    if msg.new_quantity != 0 and msg.new_quantity != order.remaining_qty:
        if msg.new_quantity < order.remaining_qty:
            // Reduce — retains time priority
            old_qty = order.remaining_qty
            order.remaining_qty = msg.new_quantity
            update_level_aggregates(order.price_level, old_qty - msg.new_quantity)
            result = ProcessingResult.new()
            result.add_event(OrderModify, order)
            // → MARKET DATA: depth update
            return result
        else:
            // Increase — configurable: retain or lose priority
            // [Venue rule parameter: qty_increase_loses_priority]
            if book.qty_increase_loses_priority:
                remove_from_book(order, book)
                order.remaining_qty = msg.new_quantity
                order.entry_sequence = partition.sequence_number
                insert_into_book(order, book)
            else:
                order.remaining_qty = msg.new_quantity
                update_level_aggregates(order.price_level, msg.new_quantity - order.remaining_qty)
            result = ProcessingResult.new()
            result.add_event(OrderModify, order)
            return result

    return cancel_reject(msg, NO_CHANGE_REQUESTED)
```

---

## Helper Functions

```
function prices_cross(aggressive: Order, level: PriceLevel) -> boolean:
    if aggressive.side == BUY:
        if aggressive.order_type == MARKET: return true
        return aggressive.price >= level.price
    else:
        if aggressive.order_type == MARKET: return true
        return aggressive.price <= level.price

function would_immediately_match(msg, book) -> boolean:
    best = book.get_best_opposite_level(msg.side)
    if best is null: return false
    return prices_cross_at_price(msg.side, msg.price, best.price)

function can_fill_entirely(order, book) -> boolean:
    available = 0
    for level in book.opposite_levels(order.side):
        if not prices_cross(order, level): break
        for passive in level.orders:
            if not is_self_trade(order, passive):
                available += passive.remaining_qty
                if available >= order.quantity: return true
    return false

function check_stp(aggressive, passive, mode) -> STPAction:
    if aggressive.stp_group_id == 0 or passive.stp_group_id == 0:
        return NO_ACTION
    if aggressive.stp_group_id != passive.stp_group_id:
        return NO_ACTION
    match mode:
        CANCEL_NEWEST  -> return CANCEL_AGGRESSIVE
        CANCEL_OLDEST  -> return CANCEL_PASSIVE
        CANCEL_BOTH    -> return CANCEL_BOTH
        // DECREMENT: Phase 2

function insert_into_book(order, book):
    level = book.get_or_create_level(order.side, order.price)
    level.orders.append(order)          // back of queue (FIFO)
    level.total_visible_qty += order.remaining_qty
    level.order_count += 1
    book.orders_by_id.insert(order.order_id, order)
    order.price_level = level

function remove_from_book(order, book):
    level = order.price_level
    level.orders.remove(order)          // O(1) via intrusive pointers
    level.total_visible_qty -= order.remaining_qty
    level.order_count -= 1
    book.orders_by_id.remove(order.order_id)
    if level.is_empty(): book.remove_level(level)
```

---

## Event Emission Points Summary

| Point in Flow | Events Emitted | Consumers |
|---------------|---------------|-----------|
| Order accepted + inserted | OrderNew (100) | MD, DC, SUR |
| Fill generated | ExecutionNew (200) | MD, DC, PT, SUR |
| Order cancelled | OrderCancel (102/103/104) | MD, DC, SUR |
| Order modified | OrderModify (101) | MD, DC, SUR |
| Order rejected | OrderReject (105) | DC, SUR |
| STP cancellation | OrderSTPCancel (107) | DC, SUR |
| Market residual converted | OrderConvertedToLimit | MD, DC, SUR |
| Instrument state change | InstrumentStateChange (300) | MD, SUR |

MD=Market Data, DC=Drop Copy, PT=Post-Trade, SUR=Surveillance
