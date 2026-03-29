#include "engine/trade_event_bus.h"

#include <algorithm>

namespace engine {

uint32_t TradeEventBus::subscribe(const std::string& name, TradeEventCallback callback) {
    std::lock_guard<std::mutex> lock(mutex_);
    uint32_t id = next_id_++;
    subscribers_.push_back({id, name, std::move(callback)});
    return id;
}

void TradeEventBus::unsubscribe(uint32_t subscriber_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    subscribers_.erase(
        std::remove_if(subscribers_.begin(), subscribers_.end(),
                        [subscriber_id](const Subscriber& s) { return s.id == subscriber_id; }),
        subscribers_.end());
}

void TradeEventBus::publish(const TradeEvent& event) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& sub : subscribers_) {
        sub.callback(event);
    }
    events_published_.fetch_add(1, std::memory_order_relaxed);
}

uint32_t TradeEventBus::subscriber_count() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return static_cast<uint32_t>(subscribers_.size());
}

std::vector<std::string> TradeEventBus::subscriber_names() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<std::string> names;
    names.reserve(subscribers_.size());
    for (const auto& sub : subscribers_) {
        names.push_back(sub.name);
    }
    return names;
}

uint64_t TradeEventBus::events_published() const {
    return events_published_.load(std::memory_order_relaxed);
}

}  // namespace engine
