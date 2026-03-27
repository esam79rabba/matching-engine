#pragma once

#include "engine/order.h"
#include "engine/order_book.h"
#include "engine/order_pool.h"
#include "engine/instrument_state.h"
#include "engine/stop_order_queue.h"
#include "engine/types.h"

#include <unordered_map>
#include <utility>
#include <vector>

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
    uint32_t      random_end_jitter_ms = 0;    // Random auction end jitter (0 = no jitter)
    MatchingAlgorithm matching_algo = MatchingAlgorithm::PriceTimeFIFO;
    Quantity      min_pro_rata_fill = 1;  // Minimum allocation in pro-rata (prevents dust fills)
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


/// Auction imbalance information for market data publication.
struct AuctionImbalance {
    Price    indicative_price;
    Side     imbalance_side;
    Quantity imbalance_qty;
    Quantity paired_qty;
};

/// Mass quote result for two-sided quoting.
struct MassQuoteResult {
    bool     accepted;
    OrderId  bid_order_id;
    OrderId  ask_order_id;
    RejectCode reject_code;
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

    /// Run opening auction: determine price that maximizes volume.
    /// Only callable during PreOpen → Continuous transition.
    /// Calculates the auction price, executes all matchable orders at that price,
    /// and transitions the instrument to Continuous trading.
    MatchResult run_opening_auction();

    /// Calculate the auction price that maximizes executable volume.
    /// Returns {price, volume} pair. Price=0 if no crossing interest.
    [[nodiscard]] std::pair<Price, Quantity> calculate_auction_price() const;

    /// Run closing auction: determine closing price that maximizes volume.
    /// Only callable during ClosingAuction → Closed transition.
    /// Same volume-maximizing algorithm as opening auction.
    MatchResult run_closing_auction();

    // ─── Volatility & Scheduled Auctions ────────────────────
    [[nodiscard]] bool trigger_volatility_auction();
    [[nodiscard]] bool trigger_scheduled_auction();
    MatchResult run_volatility_auction();
    [[nodiscard]] AuctionImbalance calculate_auction_imbalance() const;

    // ─── Pegged Orders ─────────────────────────────────────
    void reprice_pegged_orders();

    // ─── Trailing Stop Orders ─────────────────────────────
    void update_trailing_stops();

    // ─── MOC Queue Access ─────────────────────────────────
    [[nodiscard]] const std::vector<Order*>& moc_queue() const noexcept { return moc_queue_; }

    // ─── Mass Quote ──────────────────────────────────────────
    MassQuoteResult process_mass_quote(ParticipantId participant, OrderId bid_id, OrderId ask_id,
                                        Price bid_price, Quantity bid_qty,
                                        Price ask_price, Quantity ask_qty);
    void cancel_participant_quotes(ParticipantId participant);

    /// Access the underlying order book (for queries, snapshot, etc.)
    [[nodiscard]] const OrderBook& book() const noexcept { return book_; }
    [[nodiscard]] OrderBook& book() noexcept { return book_; }

    /// Update instrument config at runtime (e.g., price band change).
    void update_config(const InstrumentConfig& new_config) noexcept { config_ = new_config; }

    /// Last traded price for this instrument (0 if no trades yet).
    [[nodiscard]] Price last_traded_price() const noexcept { return last_traded_price_; }

    /// Access the stop order queue (for queries, cancel).
    [[nodiscard]] const StopOrderQueue& stop_queue() const noexcept { return stop_queue_; }
    [[nodiscard]] StopOrderQueue& stop_queue() noexcept { return stop_queue_; }

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
    Price                   last_traded_price_ = 0;  // Updated after every trade
    StopOrderQueue          stop_queue_;

    void check_stop_triggers(Price last_trade_price);
    MatchResult activate_stop(Order* stop_order);

    // ─── MOC/MOO queue ───────────────────────────────────
    std::vector<Order*> moc_queue_;   // MOC orders held during continuous trading
    [[nodiscard]] bool breaches_dynamic_band(Price price) const noexcept;
    [[nodiscard]] Price calculate_peg_price(const Order& order) const;

    // Mass quote tracking: participant → (bid_order_id, ask_order_id)
    std::unordered_map<ParticipantId, std::pair<OrderId, OrderId>> participant_quotes_;

    /// Internal listener that captures fills into a MatchResult during matching.
    struct FillCapture : public OrderBookListener {
        MatchResult* result = nullptr;
        TradeId*     trade_counter = nullptr;
        Price*       last_traded_price = nullptr;

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
            // Track last traded price
            if (last_traded_price) {
                *last_traded_price = fill_price;
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
