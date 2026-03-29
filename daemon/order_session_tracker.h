#pragma once

/// Tracks which FIX session owns each resting order.
///
/// Used to route fill notifications to the passive participant when a
/// trade occurs. Without this, only the aggressor receives ExecutionReports.
///
/// Lives in the daemon layer (not engine/core) because session routing
/// is a gateway concern, not a matching concern.

#include "engine/types.h"

#include <mutex>
#include <string>
#include <unordered_map>

/// Per-order metadata needed to build a passive-side fill notification.
struct OrderSessionInfo {
    engine::SessionId     session_id;
    engine::ParticipantId participant_id = 0;   // Owner of this order
    engine::Side          side;
    engine::InstrumentId  instrument_id;
    engine::Quantity      original_qty;
    engine::Quantity      cumulative_filled = 0;
    std::string           raw_client_order_id;  // For FIX response (tag 11)
};

/// Thread-safe order-to-session map.
///
/// Entries are added when an order is accepted (Inserted or partial-fill
/// with resting residual), and removed when the order is fully filled,
/// cancelled, or the session disconnects.
class OrderSessionTracker {
public:
    /// Register an order for passive-fill routing.
    void track(engine::OrderId order_id, const OrderSessionInfo& info) {
        std::lock_guard<std::mutex> lock(mutex_);
        orders_[order_id] = info;
    }

    /// Remove an order (fully filled, cancelled, disconnect).
    void untrack(engine::OrderId order_id) {
        std::lock_guard<std::mutex> lock(mutex_);
        orders_.erase(order_id);
    }

    /// Look up order info. Returns nullptr if not tracked.
    /// Caller must NOT hold the pointer across unlock boundaries —
    /// copy the data if you need it beyond the critical section.
    OrderSessionInfo* find(engine::OrderId order_id) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = orders_.find(order_id);
        return (it != orders_.end()) ? &it->second : nullptr;
    }

    /// Update cumulative filled quantity after a passive fill.
    /// Returns the new cumulative value, or 0 if the order is not tracked.
    engine::Quantity update_fill(engine::OrderId order_id, engine::Quantity fill_qty) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = orders_.find(order_id);
        if (it == orders_.end()) return 0;
        it->second.cumulative_filled += fill_qty;
        return it->second.cumulative_filled;
    }

    /// Snapshot info + update fill atomically. Returns false if not tracked.
    /// This avoids the find-then-update TOCTOU issue.
    bool snapshot_and_fill(engine::OrderId order_id, engine::Quantity fill_qty,
                           OrderSessionInfo& out) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = orders_.find(order_id);
        if (it == orders_.end()) return false;
        it->second.cumulative_filled += fill_qty;
        out = it->second;
        return true;
    }

    /// Number of tracked orders.
    size_t count() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return orders_.size();
    }

private:
    mutable std::mutex mutex_;
    std::unordered_map<engine::OrderId, OrderSessionInfo> orders_;
};
