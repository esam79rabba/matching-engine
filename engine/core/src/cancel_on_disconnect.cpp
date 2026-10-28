#include "engine/cancel_on_disconnect.h"

#include <algorithm>

namespace engine {

void CancelOnDisconnect::configure_session(SessionId session_id, const CoDConfig& config) {
    configs_[session_id] = config;
}

void CancelOnDisconnect::track_order(SessionId session_id, OrderId order_id) {
    session_orders_[session_id].insert(order_id);
    order_to_session_[order_id] = session_id;
}

void CancelOnDisconnect::untrack_order(OrderId order_id) {
    auto it = order_to_session_.find(order_id);
    if (it != order_to_session_.end()) {
        SessionId sid = it->second;
        auto sit = session_orders_.find(sid);
        if (sit != session_orders_.end()) {
            sit->second.erase(order_id);
        }
        order_to_session_.erase(it);
    }
}

std::vector<OrderId> CancelOnDisconnect::on_disconnect(SessionId session_id) {
    std::vector<OrderId> to_cancel;

    // Check if CoD is enabled
    auto cfg_it = configs_.find(session_id);
    if (cfg_it == configs_.end() || !cfg_it->second.enabled) {
        return to_cancel;  // CoD not enabled — orders remain
    }

    // FR-1: immediate cancel (grace_period_ms = 0).
    // Phase 2+: grace period timer would defer cancellation.

    auto orders_it = session_orders_.find(session_id);
    if (orders_it == session_orders_.end()) {
        return to_cancel;
    }

    // Collect all order IDs for this session.
    // Sort for deterministic cancellation order.
    to_cancel.reserve(orders_it->second.size());
    for (OrderId oid : orders_it->second) {
        to_cancel.push_back(oid);
    }
    std::sort(to_cancel.begin(), to_cancel.end());

    // Clear tracking — these orders will be cancelled by the caller
    // feeding synthetic cancels through partition.process().
    for (OrderId oid : to_cancel) {
        order_to_session_.erase(oid);
    }
    session_orders_.erase(orders_it);

    return to_cancel;
}

void CancelOnDisconnect::on_reconnect(SessionId /*session_id*/) {
    // FR-1: no grace period, so reconnect after disconnect has no
    // pending CoD to cancel. Orders are already cancelled or not.
    // Phase 2+: if grace period is active, cancel the pending timer.
}

bool CancelOnDisconnect::is_enabled(SessionId session_id) const {
    auto it = configs_.find(session_id);
    return it != configs_.end() && it->second.enabled;
}

uint32_t CancelOnDisconnect::order_count(SessionId session_id) const {
    auto it = session_orders_.find(session_id);
    return it != session_orders_.end() ? static_cast<uint32_t>(it->second.size()) : 0;
}

}  // namespace engine
