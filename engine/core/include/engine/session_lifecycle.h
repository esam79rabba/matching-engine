#pragma once

#include "engine/types.h"
#include "engine/partition.h"

#include <vector>

namespace engine {

/// Session lifecycle controller — handles order cleanup on state transitions.
///
/// When an instrument transitions to CLOSED:
///   - All Day orders are cancelled (deterministic, journaled)
///   - GTC orders survive (remain in book for next session)
///   - GTD orders checked for expiry (cancelled if expired)
///
/// Cancellations go through partition.process() for determinism and journaling.
///
/// Generic — no asset-class-specific logic.
class SessionLifecycle {
public:

    /// Called when an instrument transitions to CLOSED.
    /// Cancels Day orders, checks GTD expiry. GTC orders remain.
    /// Returns number of orders cancelled.
    static uint32_t on_session_close(Partition& partition, InstrumentId instrument_id,
                                      Timestamp current_time);

    /// Cancel all Day orders for an instrument across the book.
    /// Returns cancelled order IDs (caller feeds through partition.process()).
    static std::vector<OrderId> collect_day_orders(const Partition& partition,
                                                     InstrumentId instrument_id);

    /// Collect GTD orders that have expired (expiry_time <= current_time).
    static std::vector<OrderId> collect_expired_gtd_orders(const Partition& partition,
                                                             InstrumentId instrument_id,
                                                             Timestamp current_time);
};

}  // namespace engine
