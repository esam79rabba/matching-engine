#pragma once

#include "engine/types.h"
#include "engine/order_book.h"
#include "engine/partition.h"

#include <vector>
#include <unordered_map>
#include <unordered_set>

namespace engine {

/// Configuration for cancel-on-disconnect behavior per session.
struct CoDConfig {
    bool     enabled = false;
    uint32_t grace_period_ms = 0;  // 0 = immediate cancel; >0 = delay (Phase 2+)
    // Scope: all session orders cancelled. GTC retention is Phase 2+.
};

/// Cancel-on-disconnect controller.
///
/// When a session disconnects, this generates synthetic cancel messages
/// for all resting orders belonging to that session. These cancels are
/// deterministic — they are sequenced through the normal partition
/// processing path and journaled like any other cancel.
///
/// Architecture:
///   - Tracks which session owns which orders (via order insertion events)
///   - On disconnect: generates PartitionMessage::Cancel for each order
///   - Caller (partition or gateway layer) feeds these into partition.process()
///   - NOT on the hot path during normal operation — only activates on disconnect
///
/// Generic — no asset-class-specific logic.
class CancelOnDisconnect {
public:
    /// Register a session's CoD configuration.
    void configure_session(SessionId session_id, const CoDConfig& config);

    /// Track an order as belonging to a session.
    /// Called when a new order is accepted and resting.
    void track_order(SessionId session_id, OrderId order_id);

    /// Remove tracking for an order (cancelled or filled).
    void untrack_order(OrderId order_id);

    /// Called when a session disconnects.
    /// Returns the list of OrderIds to cancel. Caller must generate
    /// cancel messages and feed them through the partition for
    /// deterministic journaling.
    ///
    /// Returns empty if CoD is not enabled for this session.
    [[nodiscard]]
    std::vector<OrderId> on_disconnect(SessionId session_id);

    /// Called when a session reconnects during grace period.
    /// Cancels the pending CoD action. (FR-1: no grace period, so this is a no-op.)
    void on_reconnect(SessionId session_id);

    /// Query: is CoD enabled for a session?
    [[nodiscard]] bool is_enabled(SessionId session_id) const;

    /// Query: how many orders tracked for a session?
    [[nodiscard]] uint32_t order_count(SessionId session_id) const;

private:
    std::unordered_map<SessionId, CoDConfig> configs_;
    std::unordered_map<SessionId, std::unordered_set<OrderId>> session_orders_;
    std::unordered_map<OrderId, SessionId> order_to_session_;
};

}  // namespace engine
