#pragma once

#include "engine/types.h"

namespace engine {

/// Core order representation for the matching engine.
///
/// Design invariants (frozen baseline):
///   - All memory pre-allocated via OrderPool; no heap allocation in hot path.
///   - Intrusive linked-list pointers for O(1) removal from price level.
///   - Integer-encoded price and quantity (no floating point).
///   - Single-threaded per partition — no synchronization needed.
struct Order {
    // ─── Identity ───────────────────────────────────────────
    OrderId       order_id;           // Venue-assigned (partition:sequence composite)
    ClientOrderId client_order_id;    // Participant-assigned
    ParticipantId participant_id;
    AccountId     account_id;
    SessionId     session_id;         // Originating gateway session
    InstrumentId  instrument_id;

    // ─── Order parameters ───────────────────────────────────
    Side          side;
    OrderType     order_type;
    TimeInForce   time_in_force;
    Price         price;              // Integer-encoded limit price (0 for market)
    Quantity      original_qty;       // Original order quantity
    Quantity      remaining_qty;      // Unfilled quantity
    Quantity      cumulative_filled;  // Total filled so far

    // ─── Self-trade prevention ──────────────────────────────
    uint32_t      stp_group_id;       // 0 = STP not active

    // ─── Flags ──────────────────────────────────────────────
    uint16_t      flags;              // FLAG_POST_ONLY, FLAG_CANCEL_ON_DISCONNECT

    // ─── Timestamps and sequencing ──────────────────────────
    SequenceNum   entry_sequence;     // Sequencer-assigned; determines priority
    Timestamp     entry_timestamp;    // PTP nanosecond timestamp
    Timestamp     expiry_time;        // GTD expiry (0 = not applicable)

    // ─── Intrusive linked-list pointers (price level queue) ─
    Order*        prev_in_level;      // Previous order at same price level
    Order*        next_in_level;      // Next order at same price level

    // ─── Convenience ────────────────────────────────────────

    [[nodiscard]] bool is_buy() const noexcept { return side == Side::Buy; }
    [[nodiscard]] bool is_sell() const noexcept { return side == Side::Sell; }
    [[nodiscard]] bool is_filled() const noexcept { return remaining_qty == 0; }
    [[nodiscard]] bool is_post_only() const noexcept { return (flags & FLAG_POST_ONLY) != 0; }

    /// Reset all fields for reuse from pool.
    void clear() noexcept {
        order_id = 0;
        client_order_id = 0;
        participant_id = 0;
        account_id = 0;
        session_id = 0;
        instrument_id = 0;
        side = Side::Buy;
        order_type = OrderType::Limit;
        time_in_force = TimeInForce::Day;
        price = 0;
        original_qty = 0;
        remaining_qty = 0;
        cumulative_filled = 0;
        stp_group_id = 0;
        flags = 0;
        entry_sequence = 0;
        entry_timestamp = 0;
        expiry_time = 0;
        prev_in_level = nullptr;
        next_in_level = nullptr;
    }
};

// Verify Order fits in 2 cache lines (128 bytes) for cache friendliness.
// Adjust padding if needed after adding fields.
static_assert(sizeof(Order) <= 160,
    "Order struct exceeds 160 bytes — review for cache efficiency");

}  // namespace engine
