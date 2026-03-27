#pragma once

#include "engine/types.h"
#include "engine/partition.h"
#include "engine/session_scheduler.h"

#include <vector>

namespace engine {

/// Periodic GTD expiry checker — runs during continuous trading.
///
/// On each tick(), scans all resting GTD orders and cancels those
/// whose expiry_time has passed. Cancellations go through the
/// normal partition.process() path (deterministic, journaled).
///
/// Designed to be called from the control plane at a configurable
/// frequency (e.g., every 1 second). NOT on the hot path.
///
/// Generic — no asset-class-specific logic.
class GtdExpiryChecker {
public:
    explicit GtdExpiryChecker(ITimeSource& clock);

    /// Register a partition for periodic expiry scanning.
    void register_partition(Partition* partition);

    /// Tick: scan all registered partitions for expired GTD orders.
    /// Returns the total number of orders cancelled.
    uint32_t tick();

private:
    ITimeSource& clock_;
    std::vector<Partition*> partitions_;
};

}  // namespace engine
