#include "engine/stop_order_queue.h"

#include <algorithm>

namespace engine {

void StopOrderQueue::add(Order* order) {
    orders_by_id_[order->order_id] = order;

    if (order->side == Side::Buy) {
        buy_stops_[order->stop_price].push_back(order);
    } else {
        sell_stops_[order->stop_price].push_back(order);
    }
}

Order* StopOrderQueue::find(OrderId id) const noexcept {
    auto it = orders_by_id_.find(id);
    return (it != orders_by_id_.end()) ? it->second : nullptr;
}

bool StopOrderQueue::remove(OrderId id) {
    auto it = orders_by_id_.find(id);
    if (it == orders_by_id_.end()) return false;

    Order* order = it->second;
    orders_by_id_.erase(it);

    if (order->side == Side::Buy) {
        auto map_it = buy_stops_.find(order->stop_price);
        if (map_it != buy_stops_.end()) {
            auto& vec = map_it->second;
            vec.erase(std::remove(vec.begin(), vec.end(), order), vec.end());
            if (vec.empty()) buy_stops_.erase(map_it);
        }
    } else {
        auto map_it = sell_stops_.find(order->stop_price);
        if (map_it != sell_stops_.end()) {
            auto& vec = map_it->second;
            vec.erase(std::remove(vec.begin(), vec.end(), order), vec.end());
            if (vec.empty()) sell_stops_.erase(map_it);
        }
    }

    return true;
}

std::vector<Order*> StopOrderQueue::find_triggered_buys(Price market_price) const {
    // Buy stops trigger when trade price >= stop_price.
    // buy_stops_ is sorted ascending by stop_price.
    // Collect all entries where stop_price <= market_price.
    std::vector<Order*> triggered;
    for (const auto& [price, orders] : buy_stops_) {
        if (price > market_price) break;  // ascending order, no more triggers
        for (Order* o : orders) {
            triggered.push_back(o);
        }
    }
    return triggered;
}

std::vector<Order*> StopOrderQueue::find_triggered_sells(Price market_price) const {
    // Sell stops trigger when trade price <= stop_price.
    // sell_stops_ is sorted descending by stop_price.
    // Collect all entries where stop_price >= market_price.
    std::vector<Order*> triggered;
    for (const auto& [price, orders] : sell_stops_) {
        if (price < market_price) break;  // descending order, no more triggers
        for (Order* o : orders) {
            triggered.push_back(o);
        }
    }
    return triggered;
}

uint32_t StopOrderQueue::count() const noexcept {
    return static_cast<uint32_t>(orders_by_id_.size());
}

void StopOrderQueue::clear() {
    buy_stops_.clear();
    sell_stops_.clear();
    orders_by_id_.clear();
}

}  // namespace engine
