#pragma once

#include "engine/order.h"
#include "engine/types.h"

#include <map>
#include <unordered_map>
#include <functional>
#include <utility>
#include <vector>

namespace engine {

/// A price level in the order book — contains a doubly-linked list of orders
/// at the same price, ordered by entry_sequence (time priority / FIFO).
struct PriceLevel {
    Price    price;
    Quantity total_qty;       // Sum of remaining_qty of all orders at this level
    uint32_t order_count;
    Order*   head;            // First order (highest priority)
    Order*   tail;            // Last order (lowest priority)

    PriceLevel() : price(0), total_qty(0), order_count(0), head(nullptr), tail(nullptr) {}
    explicit PriceLevel(Price p) : price(p), total_qty(0), order_count(0), head(nullptr), tail(nullptr) {}

    [[nodiscard]] bool is_empty() const noexcept { return order_count == 0; }

    /// Append order to the back of the queue (lowest priority).
    void append(Order* order) noexcept;

    /// Remove a specific order from this level (O(1) via intrusive pointers).
    void remove(Order* order) noexcept;
};


/// Callback for order book events. The engine's event-generation layer
/// implements this to emit journal entries and market-data updates.
struct OrderBookListener {
    virtual ~OrderBookListener() = default;

    virtual void on_order_inserted(const Order& order) = 0;
    virtual void on_order_cancelled(const Order& order, RejectCode reason) = 0;
    virtual void on_order_modified(const Order& order) = 0;
    virtual void on_trade(OrderId buy_order_id, OrderId sell_order_id,
                          Price fill_price, Quantity fill_qty,
                          Side aggressor_side) = 0;
};


/// A single-instrument order book using price-time (FIFO) priority.
///
/// Architecture alignment (frozen baseline):
///   - Owned by a single partition thread — no synchronization.
///   - Deterministic: given the same sequence of calls, produces the same state.
///   - No heap allocation after construction (orders provided by external pool).
///   - Integer prices only (no floating point).
class OrderBook {
public:
    explicit OrderBook(InstrumentId instrument_id);

    // ─── Core operations ────────────────────────────────────

    /// Insert a limit order into the book. Attempts to match first;
    /// any residual rests in the book. Returns true if order was
    /// fully filled (caller should release to pool), false if resting.
    bool insert_limit_order(Order* order);

    /// Cancel a resting order by OrderId. Returns true if found and cancelled.
    bool cancel_order(OrderId order_id);

    /// Modify a resting order's price and/or quantity.
    /// Price change loses time priority; quantity reduction retains it.
    bool modify_order(OrderId order_id, Price new_price, Quantity new_qty);

    /// Insert an order into the book without attempting to match.
    /// Used during PreOpen/auction collection phase.
    void insert_without_matching(Order* order);

    // ─── Queries ────────────────────────────────────────────

    [[nodiscard]] Price best_bid() const noexcept;
    [[nodiscard]] Price best_ask() const noexcept;
    [[nodiscard]] Quantity best_bid_qty() const noexcept;
    [[nodiscard]] Quantity best_ask_qty() const noexcept;
    [[nodiscard]] uint32_t order_count() const noexcept { return order_count_; }
    [[nodiscard]] InstrumentId instrument_id() const noexcept { return instrument_id_; }

    /// Look up an order by venue-assigned OrderId. Returns nullptr if not found.
    [[nodiscard]] Order* find_order(OrderId order_id) const noexcept;

    /// Return all resting order IDs.
    [[nodiscard]] std::vector<OrderId> all_order_ids() const {
        std::vector<OrderId> ids;
        ids.reserve(orders_by_id_.size());
        for (const auto& [id, _] : orders_by_id_) {
            ids.push_back(id);
        }
        return ids;
    }

    /// Return bid price levels as (price, total_qty) pairs, best first.
    [[nodiscard]] std::vector<std::pair<Price, Quantity>> bid_levels() const {
        std::vector<std::pair<Price, Quantity>> levels;
        for (const auto& [price, level] : bids_) {
            if (!level.is_empty()) {
                levels.emplace_back(price, level.total_qty);
            }
        }
        return levels;
    }

    /// Return ask price levels as (price, total_qty) pairs, best first.
    [[nodiscard]] std::vector<std::pair<Price, Quantity>> ask_levels() const {
        std::vector<std::pair<Price, Quantity>> levels;
        for (const auto& [price, level] : asks_) {
            if (!level.is_empty()) {
                levels.emplace_back(price, level.total_qty);
            }
        }
        return levels;
    }

    // ─── Configuration ──────────────────────────────────────

    void set_listener(OrderBookListener* listener) noexcept { listener_ = listener; }

    // ─── Placeholder hooks ──────────────────────────────────
    // These will be expanded as matching, STP, and market-order logic mature.

    /// Check self-trade prevention. Returns true if STP triggered (order should be skipped/cancelled).
    [[nodiscard]] bool check_stp(const Order& aggressive, const Order& passive) const noexcept;

    /// Set the STP mode for this order book (from InstrumentConfig).
    void set_stp_mode(STPMode mode) noexcept { stp_mode_ = mode; }
    [[nodiscard]] STPMode stp_mode() const noexcept { return stp_mode_; }

    /// Set matching algorithm (FIFO or Pro-Rata).
    void set_matching_algo(MatchingAlgorithm algo, Quantity min_fill = 1) noexcept {
        matching_algo_ = algo;
        min_pro_rata_fill_ = min_fill;
    }
    [[nodiscard]] MatchingAlgorithm matching_algo() const noexcept { return matching_algo_; }

    /// Collect STP-cancelled order IDs from the last matching operation.
    [[nodiscard]] const std::vector<OrderId>& stp_cancelled_ids() const noexcept { return stp_cancelled_ids_; }

    /// Iterate all resting orders in deterministic order.
    /// Traversal order: bids (best→worst price, FIFO within level),
    /// then asks (best→worst price, FIFO within level).
    /// This is the canonical order for snapshot serialization.
    template<typename Func>
    void for_each_order(Func&& func) const {
        // Bids: highest price first (best bid), then time priority within level
        for (const auto& [price, level] : bids_) {
            const Order* o = level.head;
            while (o != nullptr) {
                func(*o);
                o = o->next_in_level;
            }
        }
        // Asks: lowest price first (best ask), then time priority within level
        for (const auto& [price, level] : asks_) {
            const Order* o = level.head;
            while (o != nullptr) {
                func(*o);
                o = o->next_in_level;
            }
        }
    }

private:
    InstrumentId instrument_id_;

    // Buy side: sorted by price descending (highest first = best bid)
    std::map<Price, PriceLevel, std::greater<Price>> bids_;

    // Sell side: sorted by price ascending (lowest first = best ask)
    std::map<Price, PriceLevel, std::less<Price>> asks_;

    // O(1) lookup by OrderId
    std::unordered_map<OrderId, Order*> orders_by_id_;

    uint32_t order_count_ = 0;

    OrderBookListener* listener_ = nullptr;
    STPMode stp_mode_ = STPMode::CancelNewest;
    MatchingAlgorithm matching_algo_ = MatchingAlgorithm::PriceTimeFIFO;
    Quantity min_pro_rata_fill_ = 1;
    std::vector<OrderId> stp_cancelled_ids_;  // Cleared on each try_match

    // ─── Internal helpers ───────────────────────────────────

    /// Try to match an aggressive order against the opposite side.
    /// Fills are generated via listener_->on_trade(). Returns remaining qty.
    Quantity try_match(Order* aggressive);

    /// Insert order into the appropriate side at its price level.
    void insert_into_side(Order* order);

    /// Remove order from its price level and the lookup map.
    void remove_from_side(Order* order);

    /// Get or create a price level on the appropriate side.
    PriceLevel& get_or_create_level(Side side, Price price);
};

}  // namespace engine
