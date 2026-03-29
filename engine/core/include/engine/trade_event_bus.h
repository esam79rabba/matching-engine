#pragma once

#include "engine/types.h"

#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

namespace engine {

/// Event published for every trade execution.
/// External systems (clearing, settlement, billing, surveillance) subscribe to this.
struct TradeEvent {
    TradeId       trade_id{};
    InstrumentId  instrument_id{};
    OrderId       buy_order_id{};
    OrderId       sell_order_id{};
    ParticipantId buyer_id{};
    ParticipantId seller_id{};
    Price         price{};
    Quantity      quantity{};
    Side          aggressor_side{};
    Timestamp     timestamp{};
    // Enrichment (set by publisher)
    std::string   symbol;
    int64_t       buyer_fee = 0;
    int64_t       seller_fee = 0;
};

/// Callback interface for trade event subscribers.
using TradeEventCallback = std::function<void(const TradeEvent&)>;

/// Publish-subscribe bus for trade events.
/// Multiple subscribers can register — all receive every trade.
/// Used to connect: clearing, settlement, billing, surveillance, market data.
class TradeEventBus {
public:
    /// Subscribe to trade events. Returns a subscriber ID for unsubscribe.
    uint32_t subscribe(const std::string& name, TradeEventCallback callback);

    /// Unsubscribe by ID.
    void unsubscribe(uint32_t subscriber_id);

    /// Publish a trade event to all subscribers.
    void publish(const TradeEvent& event);

    /// Number of active subscribers.
    uint32_t subscriber_count() const;

    /// List subscriber names (for monitoring).
    std::vector<std::string> subscriber_names() const;

    /// Total events published since start.
    uint64_t events_published() const;

private:
    struct Subscriber {
        uint32_t id;
        std::string name;
        TradeEventCallback callback;
    };
    mutable std::mutex mutex_;
    std::vector<Subscriber> subscribers_;
    uint32_t next_id_ = 1;
    std::atomic<uint64_t> events_published_{0};
};

}  // namespace engine
