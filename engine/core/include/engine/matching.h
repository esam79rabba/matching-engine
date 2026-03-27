#pragma once

#include "engine/order.h"
#include "engine/order_book.h"
#include "engine/order_pool.h"
#include "engine/instrument_state.h"
#include "engine/types.h"

namespace engine {

/// Instrument-level configuration injected from reference data.
/// This is the hook point where asset-class-specific parameters
/// are provided to the generic matching logic.
///
/// The MatchingEngine itself is asset-class-agnostic. All asset-specific
/// behavior (tick sizes, lot sizes, price bands, precision, settlement)
/// is expressed through this configuration struct, populated by the
/// reference data service at instrument load time.
struct InstrumentConfig {
    InstrumentId  instrument_id;
    Price         tick_size;             // Minimum price increment (integer-encoded)
    Quantity      lot_size;              // Minimum quantity increment (integer-encoded)
    Quantity      min_quantity;           // Minimum order quantity
    Quantity      max_quantity;           // Maximum order quantity
    Price         static_ref_price;      // Reference price for static band check
    double        static_band_pct;       // Static price band percentage (e.g., 20.0)
    double        dynamic_band_pct;      // Dynamic price band percentage (e.g., 10.0)
    ResidualPolicy residual_policy;      // Market-order residual handling
    uint32_t      collar_ticks;          // Protection collar width (if MarketToLimitCollar)
    STPMode       stp_mode;              // Self-trade prevention mode
    bool          qty_increase_loses_priority;  // Modify-up behavior
};


/// Result of processing a single message through the matching engine.
/// Events are collected here and then written to journal + output bus
/// by the partition's event-dispatch layer.
///
/// Generic — contains no asset-class-specific fields.
struct MatchResult {
    enum class Action : uint8_t {
        Inserted,           // Order resting in book
        Filled,             // Fully filled (order consumed)
        Cancelled,          // Cancelled (residual, IOC, STP, explicit)
        Rejected,           // Rejected at validation
        Modified,           // Modified in place
    };

    struct Fill {
        TradeId   trade_id;
        OrderId   buy_order_id;
        OrderId   sell_order_id;
        Price     fill_price;
        Quantity  fill_qty;
        Side      aggressor_side;
    };

    Action                action = Action::Rejected;
    RejectCode            reject_code = RejectCode::None;
    OrderId               assigned_order_id = 0;  // Venue-assigned OrderId for new orders
    std::vector<Fill>     fills;          // Trades generated (may be multiple for sweep)
    std::vector<OrderId>  cancelled_ids;  // Orders cancelled (STP, CoD, etc.)
    bool                  order_resting = false;
};


/// The matching engine for a single instrument.
///
/// Architecture notes (frozen baseline):
///   - Generic multi-asset core. The engine does not know whether it is
///     matching equities, futures, or digital assets.
///   - Asset-class-specific behavior is injected via InstrumentConfig.
///   - Single-threaded per partition (owned by Partition, not directly
///     by external callers).
///   - Deterministic: given the same inputs in the same sequence,
///     produces identical outputs.
class MatchingEngine {
public:
    MatchingEngine(const InstrumentConfig& config, OrderPool& pool);

    /// Process a new order. Validates, matches, handles residual.
    MatchResult process_new_order(Order* order);

    /// Process a cancel request.
    MatchResult process_cancel(OrderId order_id);

    /// Process a modify (cancel/replace) request.
    MatchResult process_modify(OrderId order_id, Price new_price, Quantity new_qty);

    /// Access the underlying order book (for queries, snapshot, etc.)
    [[nodiscard]] const OrderBook& book() const noexcept { return book_; }
    [[nodiscard]] OrderBook& book() noexcept { return book_; }

    /// Update instrument config at runtime (e.g., price band change).
    void update_config(const InstrumentConfig& new_config) noexcept { config_ = new_config; }

    // ─── State machine ──────────────────────────────────────

    /// Transition instrument to a new state. Returns true if valid.
    [[nodiscard]] bool transition_state(InstrumentState new_state);

    /// Current instrument state.
    [[nodiscard]] InstrumentState state() const noexcept { return state_machine_.state(); }

    /// Force state (for snapshot restore).
    void force_state(InstrumentState s) noexcept { state_machine_.force_state(s); }

private:
    InstrumentConfig        config_;
    OrderBook               book_;
    OrderPool&              pool_;
    InstrumentStateMachine  state_machine_;
    TradeId                 next_trade_id_ = 1;

    /// Internal listener that captures fills into a MatchResult during matching.
    struct FillCapture : public OrderBookListener {
        MatchResult* result = nullptr;
        TradeId*     trade_counter = nullptr;

        void on_order_inserted(const Order& /*order*/) override {}
        void on_order_cancelled(const Order& /*order*/, RejectCode /*reason*/) override {}
        void on_order_modified(const Order& /*order*/) override {}

        void on_trade(OrderId buy_order_id, OrderId sell_order_id,
                      Price fill_price, Quantity fill_qty,
                      Side aggressor_side) override {
            if (result && trade_counter) {
                MatchResult::Fill fill;
                fill.trade_id = (*trade_counter)++;
                fill.buy_order_id = buy_order_id;
                fill.sell_order_id = sell_order_id;
                fill.fill_price = fill_price;
                fill.fill_qty = fill_qty;
                fill.aggressor_side = aggressor_side;
                result->fills.push_back(fill);
            }
        }
    };
    FillCapture fill_capture_;

    // ─── Validation (generic — uses config for asset-specific params) ───

    [[nodiscard]] RejectCode validate_new_order(const Order& order) const noexcept;
    [[nodiscard]] bool is_tick_aligned(Price price) const noexcept;
    [[nodiscard]] bool is_lot_aligned(Quantity qty) const noexcept;
    [[nodiscard]] bool is_within_static_band(Price price) const noexcept;

    // ─── FOK pre-check ──────────────────────────────────────────────────

    /// Check if a FOK order can be fully filled without actually matching.
    /// Scans the opposite side of the book for sufficient liquidity at or
    /// better than the limit price.
    [[nodiscard]] bool can_fill_completely(Side side, Price price, Quantity qty) const;

    // ─── Residual handling (frozen policy) ──────────────────────────────

    void handle_market_residual(Order* order, MatchResult& result);
};

}  // namespace engine
