#include "marketdata/depth_publisher.h"

#include <algorithm>
#include <chrono>
#include <map>

namespace marketdata {

DepthSnapshot DepthPublisher::build_snapshot(const engine::OrderBook& book, uint32_t max_levels) const {
    // Accumulate per-price totals from the book's raw orders.
    // Bids: descending price (std::greater), Asks: ascending price (std::less).
    std::map<engine::Price, DepthLevel, std::greater<engine::Price>> bid_levels;
    std::map<engine::Price, DepthLevel, std::less<engine::Price>> ask_levels;

    book.for_each_order([&](const engine::Order& order) {
        if (order.side == engine::Side::Buy) {
            auto& lvl = bid_levels[order.price];
            lvl.price = order.price;
            lvl.quantity += order.remaining_qty;
            lvl.order_count += 1;
        } else {
            auto& lvl = ask_levels[order.price];
            lvl.price = order.price;
            lvl.quantity += order.remaining_qty;
            lvl.order_count += 1;
        }
    });

    DepthSnapshot snap;
    snap.instrument_id = book.instrument_id();
    snap.sequence = sequence_;
    snap.timestamp = static_cast<engine::Timestamp>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch())
            .count());

    snap.bids.reserve(std::min(static_cast<uint32_t>(bid_levels.size()), max_levels));
    uint32_t count = 0;
    for (const auto& [price, lvl] : bid_levels) {
        if (count >= max_levels) break;
        snap.bids.push_back(lvl);
        ++count;
    }

    snap.asks.reserve(std::min(static_cast<uint32_t>(ask_levels.size()), max_levels));
    count = 0;
    for (const auto& [price, lvl] : ask_levels) {
        if (count >= max_levels) break;
        snap.asks.push_back(lvl);
        ++count;
    }

    return snap;
}

void DepthPublisher::on_depth_change(engine::InstrumentId instrument_id, engine::Side /*side*/,
                                      engine::Price price, engine::Quantity new_qty,
                                      uint32_t order_count) {
    DepthLevel lvl;
    lvl.price = price;
    lvl.quantity = new_qty;
    lvl.order_count = order_count;
    pending_updates_[instrument_id].push_back(lvl);
}

std::vector<DepthLevel> DepthPublisher::get_pending_updates(engine::InstrumentId instrument_id) {
    auto it = pending_updates_.find(instrument_id);
    if (it == pending_updates_.end()) {
        return {};
    }
    std::vector<DepthLevel> result = std::move(it->second);
    pending_updates_.erase(it);
    return result;
}

DepthSnapshot DepthPublisher::get_recovery_snapshot(engine::InstrumentId /*instrument_id*/,
                                                     const engine::OrderBook& book) const {
    // Recovery snapshot is identical to a regular snapshot — it provides
    // the full current state for a subscriber that missed prior updates.
    return build_snapshot(book);
}

}  // namespace marketdata
