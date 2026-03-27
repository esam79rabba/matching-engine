#pragma once

#include "engine/order.h"
#include "engine/types.h"

#include <map>
#include <unordered_map>
#include <vector>

namespace engine {

/// Holds pending stop and stop-limit orders indexed by trigger price.
///
/// Buy stops trigger when trade price >= stop_price (buy stop-loss).
/// Sell stops trigger when trade price <= stop_price (sell stop-loss).
///
/// Orders are stored in std::map<Price, vector<Order*>> for efficient
/// range-based triggering, plus an unordered_map for O(1) cancel lookup.
class StopOrderQueue {
public:
    /// Add a stop order to the queue.
    void add(Order* order);

    /// Find a stop order by ID. Returns nullptr if not found.
    [[nodiscard]] Order* find(OrderId id) const noexcept;

    /// Remove a stop order by ID. Returns true if found and removed.
    bool remove(OrderId id);

    /// Find all buy stops triggered when market price reaches the given level.
    /// Buy stops trigger when trade price >= stop_price.
    [[nodiscard]] std::vector<Order*> find_triggered_buys(Price market_price) const;

    /// Find all sell stops triggered when market price reaches the given level.
    /// Sell stops trigger when trade price <= stop_price.
    [[nodiscard]] std::vector<Order*> find_triggered_sells(Price market_price) const;

    /// Number of pending stop orders.
    [[nodiscard]] uint32_t count() const noexcept;

    /// Remove all pending stop orders.
    void clear();

private:
    // Buy stops: keyed by stop_price ascending (lower prices trigger first)
    std::map<Price, std::vector<Order*>> buy_stops_;

    // Sell stops: keyed by stop_price descending (higher prices trigger first)
    std::map<Price, std::vector<Order*>, std::greater<Price>> sell_stops_;

    // O(1) lookup for cancel
    std::unordered_map<OrderId, Order*> orders_by_id_;
};

}  // namespace engine
